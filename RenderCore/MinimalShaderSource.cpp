// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "MinimalShaderSource.h"
#include "../Assets/IArtifact.h"
#include "../Assets/IFileSystem.h"
#include "../Assets/ICompileOperation.h"
#include "../Assets/AssetUtils.h"
#include "../Assets/IntermediateCompilers.h"
#include "../Assets/InitializerPack.h"
#include "../ConsoleRig/GlobalServices.h"		// for ConsoleRig::GetLibVersionDesc()
#include "../OSServices/AttachableLibrary.h"
#include "../Utility/IteratorUtils.h"
#include "../Utility/StringFormat.h"
#include "../Utility/Streams/PathUtils.h"
#include "../Utility/ArithmeticUtils.h"

namespace RenderCore
{
	static const auto ChunkType_Log = ConstHash64<'Log'>::Value;
	static const auto ChunkType_Metrics = ConstHash64<'Metr', 'ics'>::Value;
	static const auto ChunkType_CompiledShaderByteCode = ConstHash64<'Shdr', 'Byte', 'Code'>::Value;

	class MinimalShaderSource::Pimpl
	{
	public:
		std::shared_ptr<ILowLevelCompiler> _compiler;
		std::shared_ptr<ISourceCodePreprocessor> _preprocessor;
	};

	static std::string AppendSystemDefines(StringSection<> definesTable, const ILowLevelCompiler::ResId& resId)
	{
		const char* shaderModelDefine = nullptr;
		switch (resId._shaderModel[0]) {
		case 'V':
		case 'v':
			shaderModelDefine = "VS=1";
			break;
		case 'P':
		case 'p':
			shaderModelDefine = "PS=1";
			break;
		case 'G':
		case 'g':
			shaderModelDefine = "GS=1";
			break;
		case 'D':
		case 'd':
			shaderModelDefine = "DS=1";
			break;
		case 'H':
		case 'h':
			shaderModelDefine = "HS=1";
			break;
		case 'C':
		case 'c':
			shaderModelDefine = "CS=1";
			break;
		default:
			break;
		}
		if (shaderModelDefine) {
			if (!definesTable.IsEmpty()) {
				std::string result;
				result.reserve(definesTable.size() + 1 + std::strlen(shaderModelDefine));
				result.insert(result.end(), definesTable.begin(), definesTable.end());
				result.push_back(';');
				result.insert(result.end(), shaderModelDefine, XlStringEnd(shaderModelDefine));
				return result;
			} else
				return shaderModelDefine;
		}
		return definesTable.AsString();
	}

	auto MinimalShaderSource::Compile(
		StringSection<> shaderInMemory,
		const ILowLevelCompiler::ResId& resId,
		StringSection<::Assets::ResChar> definesTable) const -> ShaderByteCodeBlob
	{
		ShaderByteCodeBlob result;
		bool success = false;
		TRY
		{
			auto processedDefinesTable = AppendSystemDefines(definesTable, resId);
			if (_pimpl->_preprocessor) {
				auto preprocessedOutput = _pimpl->_preprocessor->RunPreprocessor(
					shaderInMemory, processedDefinesTable,
					::Assets::DefaultDirectorySearchRules(resId._filename));
				if (preprocessedOutput._processedSource.empty())
					Throw(std::runtime_error("Preprocessed output is empty"));

				result._deps = std::move(preprocessedOutput._dependencies);

				success = _pimpl->_compiler->DoLowLevelCompile(
					result._payload, result._errors, result._deps,
					preprocessedOutput._processedSource.data(), preprocessedOutput._processedSource.size(), resId,
					processedDefinesTable,
					MakeIteratorRange(preprocessedOutput._lineMarkers));

			} else {
				success = _pimpl->_compiler->DoLowLevelCompile(
					result._payload, result._errors, result._deps,
					shaderInMemory.begin(), shaderInMemory.size(), resId, 
					processedDefinesTable);
			}
		}
			// embue any exceptions with the dependency validation
		CATCH(const ::Assets::Exceptions::ConstructionError& e)
		{
			result._errors = ::Assets::AsBlob(e.what());
		}
		CATCH(const std::exception& e)
		{
			result._errors = ::Assets::AsBlob(e.what());
		}
		CATCH_END

		(void)success;

		return result;
	}

	auto MinimalShaderSource::CompileFromFile(
		const ILowLevelCompiler::ResId& resId, 
		StringSection<::Assets::ResChar> definesTable) const
		-> ShaderByteCodeBlob
	{
		::Assets::FileSnapshot snapshot;
		size_t fileSize = 0;
		auto fileData = ::Assets::MainFileSystem::TryLoadFileAsMemoryBlock(resId._filename, &fileSize, &snapshot);
		if (fileData.get() && fileSize) {
			auto result = Compile({(const char*)fileData.get(), (const char*)fileData.get() + fileSize}, resId, definesTable);
			result._deps.push_back(::Assets::DependentFileState{resId._filename, snapshot});
			return result;
		} else {
			ShaderByteCodeBlob result;
			result._errors = ::Assets::AsBlob(std::string{"Empty or missing shader file: "} + resId._filename);
			result._deps.push_back(::Assets::DependentFileState{resId._filename, snapshot});
			return result;
		}
	}
			
	auto MinimalShaderSource::CompileFromMemory(
		StringSection<char> shaderInMemory, StringSection<char> entryPoint, 
		StringSection<char> shaderModel, StringSection<::Assets::ResChar> definesTable) const
		-> ShaderByteCodeBlob
	{
		return Compile(
			shaderInMemory,
			ILowLevelCompiler::ResId("", entryPoint, shaderModel),		// use an empty string for the filename here, beacuse otherwhile it tends to confuse the DX11 compiler (when generating error messages, it will treat the string as a filename from the current directory)
			definesTable);
	}

	ILowLevelCompiler::ResId MinimalShaderSource::MakeResId(
        StringSection<> initializer) const
	{
		StringSection<> filename, entryPoint, shaderModel;
        auto splitter = MakeFileNameSplitter(initializer);

        filename = splitter.AllExceptParameters();

        if (splitter.Parameters().IsEmpty()) {
            entryPoint = "main";
        } else {
            auto startShaderModel = XlFindChar(splitter.Parameters().begin(), ':');
            if (!startShaderModel) {
				entryPoint = splitter.Parameters();
            } else {
				entryPoint = {splitter.Parameters().begin(), startShaderModel};
				shaderModel = {startShaderModel+1, splitter.Parameters().end()};
            }
        }

		ILowLevelCompiler::ResId shaderId { filename, entryPoint, shaderModel };

		if (!shaderId._shaderModel[0])
            XlCopyString(shaderId._shaderModel, PS_DefShaderModel);

            //  we have to do the "AdaptShaderModel" shader model here to convert
            //  the default shader model string (etc, "vs_*) to a resolved shader model
            //  this is because we want the archive name to be correct
        _pimpl->_compiler->AdaptResId(shaderId);

        return shaderId;
	}

	std::string MinimalShaderSource::GenerateMetrics(
        IteratorRange<const void*> byteCodeBlob) const
	{
		return _pimpl->_compiler->MakeShaderMetricsString(byteCodeBlob.begin(), byteCodeBlob.size());
	}

	MinimalShaderSource::MinimalShaderSource(
		const std::shared_ptr<ILowLevelCompiler>& compiler, 
		const std::shared_ptr<ISourceCodePreprocessor>& preprocessor)
	{
		_pimpl = std::make_unique<Pimpl>();
		_pimpl->_compiler = compiler;
		_pimpl->_preprocessor = preprocessor;
	}
	MinimalShaderSource::~MinimalShaderSource() {}

	class ShaderCompileOperation : public ::Assets::ICompileOperation
	{
	public:
		virtual std::vector<TargetDesc> GetTargets() const override
		{
			return {
				TargetDesc { ChunkType_CompiledShaderByteCode, "main" }
			};
		}
		
		virtual std::vector<SerializedArtifact> SerializeTarget(unsigned idx) override
		{
			std::vector<SerializedArtifact> result;
			if (_byteCode._payload)
				result.push_back({
					ChunkType_CompiledShaderByteCode, 0, "main",
					_byteCode._payload});
			if (_byteCode._errors)
				result.push_back({
					ChunkType_Log, 0, "log",
					_byteCode._errors});
			if (_metrics)
				result.push_back({
					ChunkType_Metrics, 0, "metrics",
					_metrics});
			return result;
		}

		virtual ::Assets::DependencyValidation GetDependencyValidation() const override
		{
			return _depVal;
		}

		ShaderCompileOperation(
			IShaderSource& shaderSource,
			const ILowLevelCompiler::ResId& resId,
			StringSection<> definesTable)
		: _byteCode { shaderSource.CompileFromFile(resId, definesTable) }
		{
			const bool writeMetrics = true;
			if (writeMetrics && _byteCode._payload && !_byteCode._payload->empty()) {
				auto metrics = shaderSource.GenerateMetrics(MakeIteratorRange(*_byteCode._payload));
				_metrics = ::Assets::AsBlob(metrics);
			}
			_depVal = ::Assets::GetDepValSys().Make(_byteCode._deps);
		}
		
		~ShaderCompileOperation()
		{
		}

		IShaderSource::ShaderByteCodeBlob _byteCode;
		::Assets::DependencyValidation _depVal;
		::Assets::Blob _metrics;
	};

	::Assets::CompilerRegistration RegisterShaderCompiler(
		const std::shared_ptr<IShaderSource>& shaderSource,
		::Assets::IIntermediateCompilers& intermediateCompilers,
		ILowLevelCompiler::CompilationFlags::BitField universalComplationFlags)
	{
		::Assets::CompilerRegistration result{
			intermediateCompilers,
			"shader-compiler",
			"shader-compiler",
			ConsoleRig::GetLibVersionDesc(),
			{},
			[shaderSource, universalComplationFlags](const ::Assets::InitializerPack& initializers) {
				std::string definesTable;
				if (initializers.GetCount() > 1)
					definesTable = initializers.GetInitializer<std::string>(1);
				auto res = shaderSource->MakeResId(initializers.GetInitializer<std::string>(0));
				res._compilationFlags |= universalComplationFlags;
				return std::make_shared<ShaderCompileOperation>(
					*shaderSource,
					res,
					definesTable
				);
			},
			[shaderSource, universalComplationFlags](::Assets::ArtifactTargetCode targetCode, const ::Assets::InitializerPack& initializers) {
				auto res = shaderSource->MakeResId(initializers.GetInitializer<std::string>(0));
				res._compilationFlags |= universalComplationFlags;
				std::string definesTable;
				if (initializers.GetCount() > 1)
					definesTable = initializers.GetInitializer<std::string>(1);

				// we don't encode the targetCode, because we assume it's always the same
				assert(targetCode == CompiledShaderByteCode::CompileProcessType);
				auto splitFN = MakeFileNameSplitter(res._filename);
				auto entryId = Hash64(res._entryPoint, Hash64(definesTable, Hash64(res._shaderModel, Hash64(splitFN.Extension()))));
				assert(res._compilationFlags < 64);
				entryId = rotr64(entryId, res._compilationFlags);

				StringMeld<MaxPath> archiveName;
				StringMeld<MaxPath> descriptiveName;
				bool compressedFN = true;
				if (compressedFN) {
					// shader model & extension already considered in entry id; we just need to look at the directory and filename here
					archiveName << splitFN.File() << "-" << std::hex << HashFilenameAndPath(splitFN.DriveAndPath());
					descriptiveName << res._filename << ":" << res._entryPoint << "[" << definesTable << "]" << res._shaderModel << "-" << res._compilationFlags;
				} else {
					archiveName << res._filename;
					descriptiveName << res._entryPoint << "[" << definesTable << "]" << res._shaderModel << "-" << res._compilationFlags;
				}
				return ::Assets::IIntermediateCompilers::SplitArchiveName { archiveName.AsString(), entryId, descriptiveName.AsString() };
			}
		};

		uint64_t outputAssetTypes[] = { CompiledShaderByteCode::CompileProcessType };
		intermediateCompilers.AssociateRequest(
			result.RegistrationId(),
			MakeIteratorRange(outputAssetTypes));
		return result;
	}

	ISourceCodePreprocessor::~ISourceCodePreprocessor() {}

}

