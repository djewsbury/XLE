// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "RawFileAsset.h"
#include "IFileSystem.h"

namespace Assets
{
	RawFileAsset::RawFileAsset(StringSection<> fname)
	: _fname(fname.AsString())
	{
		FileSnapshot snapshot;
		_data = MainFileSystem::TryLoadFileAsMemoryBlock_TolerateSharingErrors(fname, &_dataSize, &snapshot);
		_fileState = {fname.AsString(), snapshot};
		_depVal = GetDepValSys().Make(_fileState);
	}

	RawFileAsset::~RawFileAsset()
	{}
}

