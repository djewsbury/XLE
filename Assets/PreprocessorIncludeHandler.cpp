// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "PreprocessorIncludeHandler.h"
#include "DepVal.h"
#include "IFileSystem.h"
#include <stdexcept>

namespace Assets
{
	auto PreprocessorIncludeHandler::OpenFile(
		StringSection<> requestString,
		StringSection<> fileIncludedFrom) -> Result
	{
		Result result;
		if (!fileIncludedFrom.IsEmpty()) {
			char resolvedFileT[MaxPath];
			::Assets::DefaultDirectorySearchRules(fileIncludedFrom).ResolveFile(resolvedFileT, requestString);
			result._filename = resolvedFileT;
		} else {
			result._filename = requestString.AsString();
		}

		::Assets::FileSnapshot snapshot;	
		result._fileContents = ::Assets::MainFileSystem::TryLoadFileAsMemoryBlock_TolerateSharingErrors(result._filename, &result._fileContentsSize, &snapshot);
		_depFileStates.insert(DependentFileState{result._filename, snapshot});
		if (!result._fileContentsSize) {
			if (!fileIncludedFrom.IsEmpty())
				Throw(std::runtime_error("Missing or empty file when loading: " + result._filename + " (included from: " + fileIncludedFrom.AsString() + ")"));
			Throw(std::runtime_error("Missing or empty file when loading: " + result._filename));
		}
		return result;
	}

	DependencyValidation PreprocessorIncludeHandler::MakeDependencyValidation() const
	{
		if (_depFileStates.empty()) return {};
		std::vector<DependentFileState> temp { _depFileStates.begin(), _depFileStates.end() };
		return GetDepValSys().Make(temp);
	}

	PreprocessorIncludeHandler::PreprocessorIncludeHandler() {}
	PreprocessorIncludeHandler::~PreprocessorIncludeHandler() {}
}
