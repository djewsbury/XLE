// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ShaderAnalysis.h"
#include "AutomaticSelectorFiltering.h"
#include "../RenderCore/MinimalShaderSource.h"
#include "../Assets/IFileSystem.h"
#include "../Assets/IntermediatesStore.h"		// (for GetDependentFileState)
#include "../Assets/AssetUtils.h"
#include "../OSServices/Log.h"
#include "../Utility/Streams/StreamFormatter.h"
#include "../Utility/Streams/PreprocessorInterpreter.h"
#include "../Utility/StringFormat.h"
#include <sstream>

namespace ShaderSourceParser
{

	template<typename CharType> 
        static bool WhitespaceChar(CharType c)  // (excluding new line)
    {
        return c==' ' || c=='\t' || c==0x0B || c==0x0C || c==0x85 || c==0xA0 || c==0x0;
    }

	static StringSection<> ParseIncludeDirective(StringSection<> src)
	{
		auto i = src.begin();
		while (i != src.end() && WhitespaceChar(*i)) ++i;
		if (i != src.end() && (*i == '"' || *i == '<')) {
			char terminator = (*i == '"') ? '"' : '>';
			++i;
			auto resultStart = i;
			while (i != src.end() && *i != terminator) ++i;
			if (i == src.end()) return {};		// no terminator

			return MakeStringSection(resultStart, i);
		}

		return {};		// didn't understand
	}

	struct LineDetails
	{
		enum class LineType 
		{
			Normal,
			EndsInBlockComment, 
			EndsInTrailingLineComment
		};

		StringSection<> _line;
		LineType _type;

		const char* _nextLineBegin = nullptr;
		const char* _firstNonCommentNonWhitespaceChar = nullptr;
	};

	static LineDetails ParseLineDetails(StringSection<> src, LineDetails::LineType prevLineType)
	{
		// Note -- there's a bit of overlap between this behaviour and what we do in the "ConditionalProcessingTokenizer" class
		// in PredefinedCBLayout.cpp. It would be nice if we could combine the parsing together somehow
		bool inBlockComment = prevLineType == LineDetails::LineType::EndsInBlockComment;
		bool inLineComment = prevLineType == LineDetails::LineType::EndsInTrailingLineComment;
		auto i = src.begin();
		auto firstNonCommentNonWhitespaceChar = src.end();
		for (;;) {
			if (i == src.end() || *i == '\n' || *i == '\r') {
				// Hit the end of the line; return our result
				// Note that even when we're inside of a block comment we will return here
				LineDetails result;
				result._line = MakeStringSection(src.begin(), i);
				if (inBlockComment) {
					result._type = LineDetails::LineType::EndsInBlockComment;
				} else if (inLineComment) {
					if ((i - 1) >= src.begin() && *(i-1) == '\\') {
						result._type = LineDetails::LineType::EndsInTrailingLineComment;
					} else
						result._type = LineDetails::LineType::Normal;	// line comment ends here, so we don't need to care about it for future lines
				} else {
					result._type = LineDetails::LineType::Normal;
				}
				result._firstNonCommentNonWhitespaceChar = std::min(i, firstNonCommentNonWhitespaceChar);

				// i currently points to the end of this line; we should
				// advance to the start of the next line
				if (i!=src.end()) {
					if (*i == '\n') {
						++i;
					} else if (*i == '\r') {
						++i;
						if (i < src.end() && *i == '\n')
							++i;
					}
				}
				result._nextLineBegin = i;
				return result;
			}

			if (inBlockComment) {
				if (*i == '*') {
					if (i+1 < src.end() && *(i+1) == '/') {
						inBlockComment = false;
						i+=2;
						continue;
					}
				}
			} else if (!inLineComment) {
				// We cannot start a block comment inside of a line comment, so only
				// do this check when where not in a line comment
				if (*i == '/') {
					if (i+1 < src.end() && *(i+1) == '*') {
						inBlockComment = true;
						i+=2;
						continue;
					} else if (i+1 < src.end() && *(i+1) == '/') {
						inLineComment = true;
						i+=2;
						continue;
					}
				}

				// otherwise, this is real text, let's record it
				if (!WhitespaceChar(*i) && firstNonCommentNonWhitespaceChar == src.end()) {
					firstNonCommentNonWhitespaceChar = i;
				}
			}

			++i;
		}
	}

	static bool HasNonWhiteSpace(StringSection<> input)
	{
		for (auto i:input) if (!WhitespaceChar(i) && i != '\n' && i != '\r') return true;
		return false;
	}

	RenderCore::SourceCodeWithRemapping ExpandIncludes(
		StringSection<> src,
		const std::string& srcName,
		const ::Assets::DirectorySearchRules& searchRules)
	{
		// Find the #include statements in the input and expand them out
		// Note; some limitations:
		//		1) not handling #pragma once -- every include gets included every time
		//		2) expanding out includes even when they are within a failed "#if" preprocessor statement
		//		3) some combinations of comments and preprocessor statements will be handled incorrectly. Comments are mostly
		//			handled, but there are some less common cases (such as a block/line comment immediately after the "#include" that
		//			can throw it off

		unsigned sourceLineIndex = 0;		// (note zero based index for lines -- ie, line at the top of the file is line number 0)

		unsigned workingBlockStartLineIndex = 0;
		unsigned workingBlockLineCount = 0;
		StringSection<> workingBlock;

		std::stringstream outputStream;
		unsigned outputStreamLineCount = 0;
		RenderCore::SourceCodeWithRemapping result;

		LineDetails::LineType prevLineType = LineDetails::LineType::Normal;
		auto i = src.begin();
		while (i != src.end()) {
			auto lineDetails = ParseLineDetails(MakeStringSection(i, src.end()), prevLineType);

			// Check if the first non-whitespace, non-commented character on the line
			// is a hash -- if so, there's a preprocessor directive to consider
			bool appendLineToWorkingBlock = true;
			{
				auto i2 = lineDetails._firstNonCommentNonWhitespaceChar;
				if (i2 != lineDetails._line.end() && *i2 == '#') {
					++i2;
					// Note -- comments not supporting in these upcoming scans
					while (i2 != lineDetails._line.end() && WhitespaceChar(*i2)) ++i2;
					auto i3 = i2;
					while (i3 != lineDetails._line.end() && !WhitespaceChar(*i3) && *i3 != '"' && *i3 != '<') ++i3;

					// It is a valid preprocessor statement; check if it's an include
					if (XlEqStringI(MakeStringSection(i2, i3), "include")) {
						auto includeFile = ParseIncludeDirective(MakeStringSection(i3, lineDetails._line.end()));
						if (includeFile.IsEmpty())
							Throw(::Utility::FormatException("Could not interpret #include directive", StreamLocation{unsigned(i3-lineDetails._line.begin()), sourceLineIndex}));

						// Commit the working block to our output stream
						// We must also include the text that becomes before the #include on this line -- because it
						// could close a block comment that began on a previous line
						bool requiresPrefix = HasNonWhiteSpace(MakeStringSection(lineDetails._line._start, lineDetails._firstNonCommentNonWhitespaceChar));
						if (!workingBlock.IsEmpty() || requiresPrefix) {
							result._lineMarkers.push_back(
								RenderCore::ILowLevelCompiler::SourceLineMarker {
									srcName,
									(!workingBlock.IsEmpty()) ? workingBlockStartLineIndex : sourceLineIndex,		// (second case is for when there is only the prefix, and no working block
									outputStreamLineCount
								});

							outputStream << workingBlock;
							outputStreamLineCount += workingBlockLineCount;

							if (requiresPrefix) {
								if (!workingBlock.IsEmpty()) {
									outputStream << MakeStringSection(workingBlock.end(), lineDetails._firstNonCommentNonWhitespaceChar) << std::endl;
								} else {
									outputStream << MakeStringSection(lineDetails._line.begin(), lineDetails._firstNonCommentNonWhitespaceChar) << std::endl;
								}
								++outputStreamLineCount;
							}
						}

						workingBlockLineCount = 0;
						workingBlock = {};
						appendLineToWorkingBlock = false;		// (don't append this line, because we've just used it as an include */

						// We must expand out this include...
						char resolvedFN[MaxPath];
						searchRules.ResolveFile(resolvedFN, includeFile);
						auto subFile = ::Assets::MainFileSystem::OpenFileInterface(resolvedFN, "rb");
						subFile->Seek(0, OSServices::FileSeekAnchor::End);
						auto subFileLength = subFile->TellP();
						subFile->Seek(0);

						std::string buffer;
						buffer.resize(subFileLength);
						subFile->Read(AsPointer(buffer.begin()), subFileLength);

						auto expanded = ExpandIncludes(
							MakeStringSection(buffer),
							resolvedFN,
							::Assets::DefaultDirectorySearchRules(resolvedFN));

						if (!expanded._processedSource.empty()) {
							for (const auto&l:expanded._lineMarkers)
								result._lineMarkers.push_back(
									RenderCore::ILowLevelCompiler::SourceLineMarker {
										l._sourceName,
										l._sourceLine,
										outputStreamLineCount + l._processedSourceLine
									});

							outputStream << expanded._processedSource;
							outputStreamLineCount += expanded._processedSourceLineCount;

							// Some expanded files might not end in a new line; so append one if needed
							if (!expanded._processedSource.empty() && *(expanded._processedSource.end()-1) != '\n' && *(expanded._processedSource.end()-1) != '\r') {
								outputStream << std::endl;
								outputStreamLineCount ++;
							}
						}

						// todo -- search for duplicates in the dependencies
						result._dependencies.insert(
							result._dependencies.end(),
							expanded._dependencies.begin(), expanded._dependencies.end());
						result._dependencies.push_back(::Assets::IntermediatesStore::GetDependentFileState(resolvedFN));
					}
				}
			}

			// either append to, or construct the working block -- 
			if (appendLineToWorkingBlock) {
				if (!workingBlock.IsEmpty()) {
					workingBlock._end = lineDetails._nextLineBegin;
				} else {
					workingBlockStartLineIndex = sourceLineIndex;
					workingBlock = MakeStringSection(lineDetails._line.begin(), lineDetails._nextLineBegin);
				}
				// This condition is to avoid an issue counting lines if there is no newline at the end of the
				// file. The line count is actually the number of newlines in the text -- so if the final
				// line doesn't end in a new line, we don't count it. In the extreme case, for a file with
				// no new lines at all, we actually return a line count of 0
				if (lineDetails._line.end() != lineDetails._nextLineBegin)
					++workingBlockLineCount;
			}
			
			++sourceLineIndex;
			prevLineType = lineDetails._type;
			i = lineDetails._nextLineBegin;
		}

		// Append the last fragment from the working block -- 
		if (!workingBlock.IsEmpty()) {
			result._lineMarkers.push_back(
				RenderCore::ILowLevelCompiler::SourceLineMarker {
					srcName,
					workingBlockStartLineIndex,
					outputStreamLineCount
				});
			outputStream << workingBlock;
			outputStreamLineCount += workingBlockLineCount;
		}

		result._processedSource = outputStream.str();
		result._processedSourceLineCount = outputStreamLineCount;

		return result;
	}

	void ManualSelectorFiltering::GenerateHash()
	{
		_hash = HashCombine(_setValues.GetHash(), _setValues.GetParameterNamesHash());
		for (const auto&r:_relevanceMap)
			_hash = HashCombine(HashCombine(Hash64(r.first), Hash64(r.second)), _hash);
	}

	ParameterBox FilterSelectors(
		IteratorRange<const ParameterBox* const*> selectors,
		const std::unordered_map<std::string, std::string>& manualRevelanceMap,
		IteratorRange<const SelectorFilteringRules**> automaticFiltering)
	{
		assert(!selectors.empty());
		ParameterBox filteredBox = **selectors.begin();
		for (auto i=selectors.begin()+1; i!=selectors.end(); ++i)
			filteredBox.MergeIn(**i);

		if (filteredBox.GetCount() == 0)
			return filteredBox;

		// We have to do the evaluation one at a time, updating the filteredBox as we go,
		// because of mutual relationships between selectors. For example, in construction
		// 		#if !defined(SEL_0) || !defined(SEL_1)
		// if both SEL_0 and SEL_1 are defined, we have to be careful not to consider both
		// irrelevant

		for (ptrdiff_t c=filteredBox.GetCount()-1; c>=0;) {
			auto evaluating = filteredBox.at(c);
			bool relevant = false;

			// If we're listed in the technique filtering relevance map, then we treat that as an override
			// of the automatic filtering
			auto relevanceI = manualRevelanceMap.find(evaluating->Name().AsString());
			if (relevanceI != manualRevelanceMap.end()) {
				// Set a key called "value" to the new value we want to set
				ParameterBox pBoxValue;
				pBoxValue.SetParameter("value", evaluating->RawValue(), evaluating->Type());
				const ParameterBox* boxes[] = { &pBoxValue, &filteredBox };
				relevant = EvaluatePreprocessorExpression(relevanceI->second, MakeIteratorRange(boxes));
			} else {
				const ParameterBox* env[] = { &filteredBox };
				for (const auto*f:automaticFiltering)
					if ((relevant = f->IsRelevant(evaluating->Name(), evaluating->ValueAsString(), MakeIteratorRange(env))))
						break;
			}

			if (!relevant)
				filteredBox.RemoveParameter(evaluating->HashName());
			--c;	// dec idx in either case
		}

		return filteredBox;
	}

	ParameterBox FilterSelectors(
		const ParameterBox& selectors,
		const ManualSelectorFiltering& manualFiltering,
		const SelectorFilteringRules& automaticFiltering)
	{
		const ParameterBox* boxes[] = { &manualFiltering._setValues, &selectors };
		const SelectorFilteringRules* filtering[] = { &automaticFiltering };
		return FilterSelectors(MakeIteratorRange(boxes), manualFiltering._relevanceMap, MakeIteratorRange(filtering));
	}
}
