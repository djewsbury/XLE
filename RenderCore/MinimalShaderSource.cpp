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
	static const auto ChunkType_Log = ConstHash64Legacy<'Log'>::Value;
	static const auto ChunkType_Metrics = ConstHash64Legacy<'Metr', 'ics'>::Value;
	static const auto ChunkType_CompiledShaderByteCode = ConstHash64Legacy<'Shdr', 'Byte', 'Code'>::Value;

	static std::string AppendSystemDefines(StringSection<> definesTable, const ILowLevelCompiler::ResId& resId)
	{
		const char* additionalDefines[2];
		unsigned additionalDefineCount = 0;
		switch (resId._shaderModel[0]) {
		case 'V':
		case 'v':
			additionalDefines[additionalDefineCount++] = "VS=1";
			break;
		case 'P':
		case 'p':
			additionalDefines[additionalDefineCount++] = "PS=1";
			break;
		case 'G':
		case 'g':
			additionalDefines[additionalDefineCount++] = "GS=1";
			break;
		case 'D':
		case 'd':
			additionalDefines[additionalDefineCount++] = "DS=1";
			break;
		case 'H':
		case 'h':
			additionalDefines[additionalDefineCount++] = "HS=1";
			break;
		case 'C':
		case 'c':
			additionalDefines[additionalDefineCount++] = "CS=1";
			break;
		default:
			break;
		}

		#if defined(_DEBUG)
			additionalDefines[additionalDefineCount++] = "_DEBUG=1";
		#endif

		if (additionalDefineCount) {
			size_t size = definesTable.size();
			bool pendingComma = size != 0;
			for (const auto* s:MakeIteratorRange(additionalDefines, additionalDefines+additionalDefineCount)) {
				if (pendingComma) size += 1;
				pendingComma = true;
				size += std::strlen(s);
			}
			std::string result;
			result.reserve(size);
			result.insert(result.end(), definesTable.begin(), definesTable.end());
			pendingComma = !definesTable.IsEmpty();
			for (const auto* s:MakeIteratorRange(additionalDefines, additionalDefines+additionalDefineCount)) {
				if (pendingComma) result.push_back(';');
				pendingComma = true;
				result.insert(result.end(), s, XlStringEnd(s));
			}
			return result;
		}
		return definesTable.AsString();
	}

	class MinimalShaderSource : public IShaderSource
	{
	public:
		ShaderByteCodeBlob CompileFromFile(
			const ILowLevelCompiler::ResId& resId,
			StringSection<> definesTable) const override
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
			
		ShaderByteCodeBlob CompileFromMemory(
			StringSection<> shaderInMemory, StringSection<> entryPoint, 
			StringSection<> shaderModel, StringSection<> definesTable) const override
		{
			return Compile(
				shaderInMemory,
				ILowLevelCompiler::ResId("", entryPoint, shaderModel),		// use an empty string for the filename here, beacuse otherwhile it tends to confuse the DX11 compiler (when generating error messages, it will treat the string as a filename from the current directory)
				definesTable);
		}

		ILowLevelCompiler::ResId MakeResId(
            StringSection<> initializer) const override
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
			_compiler->AdaptResId(shaderId);

			return shaderId;
		}

		std::string GenerateMetrics(
        	IteratorRange<const void*> byteCodeBlob) const override
		{
			return _compiler->MakeShaderMetricsString(byteCodeBlob.begin(), byteCodeBlob.size());
		}

		ILowLevelCompiler::CompilerCapability::BitField GetCompilerCapabilities() const override
		{
			return _compiler->GetCapabilities();
		}

		MinimalShaderSource(
			std::shared_ptr<ILowLevelCompiler> compiler,
			std::shared_ptr<ISourceCodePreprocessor> preprocessor)
		{
			_compiler = std::move(compiler);
			_preprocessor = std::move(preprocessor);
		}
		~MinimalShaderSource() = default;

	protected:
		std::shared_ptr<ILowLevelCompiler> _compiler;
		std::shared_ptr<ISourceCodePreprocessor> _preprocessor;

		ShaderByteCodeBlob Compile(
			StringSection<> shaderInMemory,
			const ILowLevelCompiler::ResId& resId,
			StringSection<::Assets::ResChar> definesTable) const
		{
			ShaderByteCodeBlob result;
			bool success = false;
			TRY
			{
				auto processedDefinesTable = AppendSystemDefines(definesTable, resId);
				if (_preprocessor) {
					auto preprocessedOutput = _preprocessor->RunPreprocessor(
						shaderInMemory, processedDefinesTable,
						::Assets::DefaultDirectorySearchRules(resId._filename));
					if (preprocessedOutput._processedSource.empty())
						Throw(std::runtime_error("Preprocessed output is empty"));

					result._deps = std::move(preprocessedOutput._dependencies);

					success = _compiler->DoLowLevelCompile(
						result._payload, result._errors, result._deps,
						preprocessedOutput._processedSource.data(), preprocessedOutput._processedSource.size(), resId,
						processedDefinesTable,
						MakeIteratorRange(preprocessedOutput._lineMarkers));

				} else {
					success = _compiler->DoLowLevelCompile(
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
	};

	std::shared_ptr<IShaderSource> CreateMinimalShaderSource(
		std::shared_ptr<ILowLevelCompiler> compiler,
		std::shared_ptr<ISourceCodePreprocessor> preprocessor)
	{
		return std::make_shared<MinimalShaderSource>(std::move(compiler), std::move(preprocessor));
	}

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

				auto compilerCapabilities = shaderSource->GetCompilerCapabilities();

				StringMeld<MaxPath> archiveName;
				StringMeld<MaxPath> descriptiveName;
				bool compressedFN = true;
				if (compressedFN) {
					// shader model & extension already considered in entry id; we just need to look at the directory and filename here
					archiveName << splitFN.File() << "-" << std::hex << HashFilenameAndPath(splitFN.DriveAndPath());
					descriptiveName << res._filename << ":" << res._entryPoint << "[" << definesTable << "]" << res._shaderModel << "-" << res._compilationFlags << "-" << compilerCapabilities;
				} else {
					archiveName << res._filename;
					descriptiveName << res._entryPoint << "[" << definesTable << "]" << res._shaderModel << "-" << res._compilationFlags << "-" << compilerCapabilities;
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

