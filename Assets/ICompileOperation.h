// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "IntermediateCompilers.h"
#include "AssetsCore.h"
#include <vector>
#include <memory>

namespace Assets
{
	struct DependentFileState;
	class InitializerPack;
	using ArtifactTargetCode = uint64_t;

	struct SerializedArtifact
	{
		uint64_t		_chunkTypeCode;
		unsigned		_version;
		std::string		_name;
		::Assets::Blob	_data;

		SerializedArtifact() = default;
		SerializedArtifact(
			uint64_t chunkTypeCode, unsigned version,
			std::string name, ::Assets::Blob data) 
		: _chunkTypeCode(chunkTypeCode), _version(version), _name(std::move(name)), _data(std::move(data)) {}
	};

	struct SerializedTarget
	{
		std::vector<SerializedArtifact> _artifacts;
		::Assets::DependencyValidation _depVal;
	};

	class ICompileOperation
	{
	public:
		struct TargetDesc
		{
			ArtifactTargetCode	_targetCode;
			const char*			_name;
		};
		virtual std::vector<TargetDesc> GetTargets() const = 0;
		virtual SerializedTarget SerializeTarget(unsigned idx) = 0;
		virtual ::Assets::DependencyValidation GetDependencyValidation() const = 0;	// note that serialize targets can return additional dep vals

		virtual ~ICompileOperation();
	};

	using CreateCompileOperationFn = std::shared_ptr<ICompileOperation>(const InitializerPack&);

	struct SimpleCompilerResult : public SerializedTarget
	{
		uint64_t _targetCode;
	};
	using SimpleCompilerSig = ::Assets::SimpleCompilerResult(const ::Assets::InitializerPack&);

	CompilerRegistration RegisterSimpleCompiler(
		IIntermediateCompilers& compilers,
		const std::string& name,
		const std::string& shortName,
		std::function<SimpleCompilerSig>&& fn,
		IIntermediateCompilers::ArchiveNameDelegate&& archiveNameDelegate = {});
}

