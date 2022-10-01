// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "MergedAnimationSetCompiler.h"
#include "ModelScaffold.h"
#include "RawAnimationCurve.h"
#include "../GeoProc/NascentCommandStream.h"
#include "../GeoProc/NascentObjectsSerialize.h"
#include "../../Assets/Assets.h"
#include "../../Assets/BlockSerializer.h"
#include "../../Assets/Assets.h"
#include "../../OSServices/AttachableLibrary.h"
#include "../../Utility/Streams/PathUtils.h"
#include "../../Core/Prefix.h"
#include <regex>

namespace RenderCore { namespace Assets
{
///////////////////////////////////////////////////////////////////////////////////////////////////
	static const uint64_t Type_AnimationSet = ConstHash64<'Anim', 'Set'>::Value;
	static const auto ChunkType_Log = ConstHash64<'Log'>::Value;

	static void MergeInAsManyAnimations(
		GeoProc::NascentAnimationSet& dst,
		const AnimationImmutableData& src,
		const std::string& namePrefix)
	{
		auto curveOffset = dst.GetCurves().size();
		for (auto& c:src._animationSet.GetCurves())
			dst.AddCurve(RawAnimationCurve{c});

		auto outputInterface = src._animationSet.GetOutputInterface();

		for (auto& animation:src._animationSet.GetAnimations()) {
			auto name = src._animationSet.FindAnimation(animation.first)->_stringName.AsString();
			if (src._animationSet.GetAnimations().size() == 1) {
				name = namePrefix;
			} else
				name = namePrefix + name;
			auto srcAnim = animation.second;
			VLA(GeoProc::NascentAnimationSet::BlockSpan, blockSpans, srcAnim._endBlock-srcAnim._startBlock);
			for (unsigned b=0; b<(srcAnim._endBlock-srcAnim._startBlock); ++b)
				blockSpans[b] = { src._animationSet.GetAnimationBlocks()[srcAnim._startBlock+b]._beginFrame, src._animationSet.GetAnimationBlocks()[srcAnim._startBlock+b]._endFrame };
			auto newBlocks = dst.AddAnimation(name, MakeIteratorRange(blockSpans, &blockSpans[srcAnim._endBlock-srcAnim._startBlock]), srcAnim._framesPerSecond);

			for (unsigned b=0; b<(srcAnim._endBlock-srcAnim._startBlock); ++b) {
				auto& srcBlock = src._animationSet.GetAnimationBlocks()[srcAnim._startBlock+b];

				for (unsigned idx=srcBlock._beginConstantDriver; idx != srcBlock._endConstantDriver; ++idx) {
					auto& constantDriver=src._animationSet.GetConstantDrivers()[idx];
					newBlocks[b].AddConstantDriver(
						outputInterface[constantDriver._parameterIndex]._name,
						outputInterface[constantDriver._parameterIndex]._component,
						outputInterface[constantDriver._parameterIndex]._samplerType,
						PtrAdd(src._animationSet.GetConstantData().begin(), constantDriver._dataOffset),
						BitsPerPixel(constantDriver._format) / 8,
						constantDriver._format);
				}

				for (unsigned idx=srcBlock._beginDriver; idx != srcBlock._endDriver; ++idx) {
					auto& animDriver=src._animationSet.GetAnimationDrivers()[idx];
					newBlocks[b].AddAnimationDriver(
						outputInterface[animDriver._parameterIndex]._name,
						outputInterface[animDriver._parameterIndex]._component,
						outputInterface[animDriver._parameterIndex]._samplerType,
						animDriver._curveIndex + curveOffset, animDriver._interpolationType);
				}
			}
		}
	}

	static ::Assets::SimpleCompilerResult MergedAnimSetCompileOperation(const ::Assets::InitializerPack& initializers)
	{
		auto baseFolderSrc = initializers.GetInitializer<std::string>(0);
		auto splitPath = MakeSplitPath(baseFolderSrc);
		if (splitPath.GetSectionCount() < 2 || !XlEqString(splitPath.GetSection(splitPath.GetSectionCount()-1), "*"))
			Throw(std::runtime_error("Expecting merged anim set request to end with '/*'"));
		auto baseFolder = MakeStringSection((const char*)AsPointer(baseFolderSrc.begin()), splitPath.GetSection(splitPath.GetSectionCount()-2).end()).AsString();

		auto walk = ::Assets::MainFileSystem::BeginWalk(baseFolder);
		std::vector<std::string> files;
		std::regex fileMatcher{R"(.*\.(([hH][kK][xX])|([dD][aA][eE])))"};
		for (auto w=walk.begin_files(); w!=walk.end_files(); ++w) {
			auto f = w.Desc()._mountedName;
			if (std::regex_match(f.begin(), f.end(), fileMatcher))
				files.push_back(f);
		}

		std::stringstream log;
		std::vector<::Assets::DependencyValidationMarker> depVals;

		// merge all of the source files into a single output animation set
		RenderCore::Assets::GeoProc::NascentAnimationSet animSet;
		for (const auto& f:files) {
			TRY {
				auto part = ::Assets::ActualizeAssetPtr<AnimationSetScaffold>(f);
				depVals.push_back(part->GetDependencyValidation());
				MergeInAsManyAnimations(animSet, part->ImmutableData(), MakeFileNameSplitter(f).File().AsString());
			} CATCH(const ::Assets::Exceptions::ExceptionWithDepVal& e) {
				depVals.push_back(e.GetDependencyValidation());
				log << "Failed to include animation (" << MakeFileNameSplitter(f).File() << ") in animation set because of exception: (" << e.what() << ")" << std::endl;
			} CATCH(const std::exception& e) {
				log << "Failed to include animation (" << MakeFileNameSplitter(f).File() << ") in animation set because of exception: (" << e.what() << ")" << std::endl;
			} CATCH_END
		}

		std::string finalName = splitPath.GetSection(splitPath.GetSectionCount()-2).AsString();
		auto serializedArtifacts = GeoProc::SerializeAnimationsToChunks(finalName, animSet);

		auto logStr = log.str();
		if (!logStr.empty())
			serializedArtifacts.push_back({ ChunkType_Log, 0, "log", ::Assets::AsBlob(std::move(logStr)) });

		return {
			std::move(serializedArtifacts),
			Type_AnimationSet,
			::Assets::GetDepValSys().MakeOrReuse(depVals)
		};
	}

	::Assets::CompilerRegistration RegisterMergedAnimationSetCompiler(
		::Assets::IIntermediateCompilers& intermediateCompilers)
	{
		auto result = ::Assets::RegisterSimpleCompiler(intermediateCompilers, "merged-animset-compiler", "merged-animset-compiler", MergedAnimSetCompileOperation);
		uint64_t outputAssetTypes[] = { Type_AnimationSet };
		intermediateCompilers.AssociateRequest(
			result.RegistrationId(),
			MakeIteratorRange(outputAssetTypes),
			R"(.*[\\/]\*)");
		return result;
	}

///////////////////////////////////////////////////////////////////////////////////////////////////
	
}}
