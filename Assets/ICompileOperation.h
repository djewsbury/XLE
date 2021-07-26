// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "AssetsCore.h"
#include "../Utility/IteratorUtils.h"
#include <vector>
#include <memory>

namespace Assets
{
	class DependentFileState;
	class InitializerPack;
	using ArtifactTargetCode = uint64_t;

	class ICompileOperation
	{
	public:
		struct TargetDesc
		{
			ArtifactTargetCode	_targetCode;
			const char*			_name;
		};
		struct SerializedArtifact
		{
			uint64_t		_chunkTypeCode;
			unsigned		_version;
			std::string		_name;
			::Assets::Blob	_data;
		};
		virtual std::vector<TargetDesc>			GetTargets() const = 0;
		virtual std::vector<SerializedArtifact>	SerializeTarget(unsigned idx) = 0;
		virtual std::vector<DependentFileState> GetDependencies() const = 0;

		virtual ~ICompileOperation();
	};

	using CreateCompileOperationFn = std::shared_ptr<ICompileOperation>(const InitializerPack&);
}

