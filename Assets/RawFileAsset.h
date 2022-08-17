// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "AssetsCore.h"
#include "AssetUtils.h"
#include "DepVal.h"
#include "IFileSystem.h"
#include "../Utility/IteratorUtils.h"
#include <string>

namespace Assets
{
	class RawFileAsset
	{
	public:
		IteratorRange<const void*> GetData() const { return MakeIteratorRange(_data.get(), PtrAdd(_data.get(), _dataSize)); }
		const std::string& GetFileName() const { return _fname; }
		const DependencyValidation& GetDependencyValidation() const { return _depVal; }
		const DependentFileState& GetFileState() const { return _fileState; }

		RawFileAsset(StringSection<> fname);
		~RawFileAsset();
	private:
		DependencyValidation _depVal;
		DependentFileState _fileState;
		std::unique_ptr<uint8_t[]> _data;
		size_t _dataSize;
		std::string _fname;
	};
}

