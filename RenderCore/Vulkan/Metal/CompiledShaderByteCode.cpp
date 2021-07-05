// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Shader.h"
#include "../IDeviceVulkan.h"

namespace RenderCore { namespace Metal_DX11
{
	std::shared_ptr<ILowLevelCompiler> CreateVulkanPrecompiler();
	std::shared_ptr<ILowLevelCompiler> CreateHLSLToSPIRVCompiler();
}}

#if defined(ENABLE_HLSLCC)

#include "DeviceContext.h"
#include "ShaderReflection.h"		// (for metrics string)
#include "IncludeVulkan.h"
#include "../../UniformsStream.h"
#include "../../ShaderService.h"
#include "../../ShaderLangUtil.h"
#include "../../../Assets/AssetServices.h"
#include "../../../Assets/AssetUtils.h"
#include "../../../Utility/StringUtils.h"
#include "../../../Utility/StringFormat.h"
#include "../../../Utility/Conversion.h"

#include <sstream>

// HLSL cross compiler includes --
#define EXCLUDE_PSTDINT
#include "../../../Foreign/HLSLCC/include/hlslcc.hpp"

#define HAS_SPIRV_HEADERS
#if defined(HAS_SPIRV_HEADERS)

// Vulkan SDK includes -- 
#pragma push_macro("new")
#undef new
#pragma warning(disable:4458)		// declaration of 'loc' hides class member
#undef _ENFORCE_MATCHING_ALLOCATORS
#define _ENFORCE_MATCHING_ALLOCATORS 0
#undef strdup
#include "glslang/Public/ShaderLang.h"
#include "glslang/SPIRV/GlslangToSpv.h"
#pragma pop_macro("new")

#endif

namespace RenderCore { namespace Metal_Vulkan
{
	using ::Assets::ResChar;

#if defined(HAS_SPIRV_HEADERS)

///////////////////////////////////////////////////////////////////////////////////////////////////

	static TBuiltInResource CreateTBuiltInResource()
	{
		TBuiltInResource result;
		result.maxLights = 32;
		result.maxClipPlanes = 6;
		result.maxTextureUnits = 32;
		result.maxTextureCoords = 32;
		result.maxVertexAttribs = 64;
		result.maxVertexUniformComponents = 4096;
		result.maxVaryingFloats = 64;
		result.maxVertexTextureImageUnits = 32;
		result.maxCombinedTextureImageUnits = 80;
		result.maxTextureImageUnits = 32;
		result.maxFragmentUniformComponents = 4096;
		result.maxDrawBuffers = 32;
		result.maxVertexUniformVectors = 128;
		result.maxVaryingVectors = 8;
		result.maxFragmentUniformVectors = 16;
		result.maxVertexOutputVectors = 16;
		result.maxFragmentInputVectors = 15;
		result.minProgramTexelOffset = -8;
		result.maxProgramTexelOffset = 7;
		result.maxClipDistances = 8;
		result.maxComputeWorkGroupCountX = 65535;
		result.maxComputeWorkGroupCountY = 65535;
		result.maxComputeWorkGroupCountZ = 65535;
		result.maxComputeWorkGroupSizeX = 1024;
		result.maxComputeWorkGroupSizeY = 1024;
		result.maxComputeWorkGroupSizeZ = 64;
		result.maxComputeUniformComponents = 1024;
		result.maxComputeTextureImageUnits = 16;
		result.maxComputeImageUniforms = 8;
		result.maxComputeAtomicCounters = 8;
		result.maxComputeAtomicCounterBuffers = 1;
		result.maxVaryingComponents = 60;
		result.maxVertexOutputComponents = 64;
		result.maxGeometryInputComponents = 64;
		result.maxGeometryOutputComponents = 128;
		result.maxFragmentInputComponents = 128;
		result.maxImageUnits = 8;
		result.maxCombinedImageUnitsAndFragmentOutputs = 8;
		result.maxCombinedShaderOutputResources = 8;
		result.maxImageSamples = 0;
		result.maxVertexImageUniforms = 0;
		result.maxTessControlImageUniforms = 0;
		result.maxTessEvaluationImageUniforms = 0;
		result.maxGeometryImageUniforms = 0;
		result.maxFragmentImageUniforms = 8;
		result.maxCombinedImageUniforms = 8;
		result.maxGeometryTextureImageUnits = 16;
		result.maxGeometryOutputVertices = 256;
		result.maxGeometryTotalOutputComponents = 1024;
		result.maxGeometryUniformComponents = 1024;
		result.maxGeometryVaryingComponents = 64;
		result.maxTessControlInputComponents = 128;
		result.maxTessControlOutputComponents = 128;
		result.maxTessControlTextureImageUnits = 16;
		result.maxTessControlUniformComponents = 1024;
		result.maxTessControlTotalOutputComponents = 4096;
		result.maxTessEvaluationInputComponents = 128;
		result.maxTessEvaluationOutputComponents = 128;
		result.maxTessEvaluationTextureImageUnits = 16;
		result.maxTessEvaluationUniformComponents = 1024;
		result.maxTessPatchComponents = 120;
		result.maxPatchVertices = 32;
		result.maxTessGenLevel = 64;
		result.maxViewports = 16;
		result.maxVertexAtomicCounters = 0;
		result.maxTessControlAtomicCounters = 0;
		result.maxTessEvaluationAtomicCounters = 0;
		result.maxGeometryAtomicCounters = 0;
		result.maxFragmentAtomicCounters = 8;
		result.maxCombinedAtomicCounters = 8;
		result.maxAtomicCounterBindings = 1;
		result.maxVertexAtomicCounterBuffers = 0;
		result.maxTessControlAtomicCounterBuffers = 0;
		result.maxTessEvaluationAtomicCounterBuffers = 0;
		result.maxGeometryAtomicCounterBuffers = 0;
		result.maxFragmentAtomicCounterBuffers = 1;
		result.maxCombinedAtomicCounterBuffers = 1;
		result.maxAtomicCounterBufferSize = 16384;
		result.maxTransformFeedbackBuffers = 4;
		result.maxTransformFeedbackInterleavedComponents = 64;
		result.maxCullDistances = 8;
		result.maxCombinedClipAndCullDistances = 8;
		result.maxSamples = 4;
		result.limits.nonInductiveForLoops = 1;
		result.limits.whileLoops = 1;
		result.limits.doWhileLoops = 1;
		result.limits.generalUniformIndexing = 1;
		result.limits.generalAttributeMatrixVectorIndexing = 1;
		result.limits.generalVaryingIndexing = 1;
		result.limits.generalSamplerIndexing = 1;
		result.limits.generalVariableIndexing = 1;
		result.limits.generalConstantMatrixVectorIndexing = 1;
		return result;
	}

	#if 0
		static EShLanguage AsEShLanguage(const VkShaderStageFlagBits shader_type) 
		{
			switch (shader_type) {
			case VK_SHADER_STAGE_VERTEX_BIT:                    return EShLangVertex;
			case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT:      return EShLangTessControl;
			case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT:   return EShLangTessEvaluation;
			case VK_SHADER_STAGE_GEOMETRY_BIT:                  return EShLangGeometry;
			case VK_SHADER_STAGE_FRAGMENT_BIT:                  return EShLangFragment;
			case VK_SHADER_STAGE_COMPUTE_BIT:                   return EShLangCompute;
			default:                                            return EShLangVertex;
			}
		}
	#endif

	static EShLanguage GLSLShaderTypeToEShLanguage(unsigned glslShaderType)
	{
		switch (glslShaderType)
		{
		case 0x8B31: // GL_VERTEX_SHADER_ARB;
			return EShLangVertex;
		default:
		case 0x8B30: // GL_FRAGMENT_SHADER_ARB
			return EShLangFragment;
		case 0x8DD9: // GL_GEOMETRY_SHADER;
			return EShLangGeometry;
		case 0x8E87: // GL_TESS_EVALUATION_SHADER;
			return EShLangTessEvaluation;
		case 0x8E88: // GL_TESS_CONTROL_SHADER;
			return EShLangTessControl;
		case 0x91B9: // GL_COMPUTE_SHADER;
			return EShLangCompute;
		}
	}

	static EShLanguage EShLanguageFromShaderModel(StringSection<> shaderModel)
	{
		if (shaderModel.IsEmpty())
			return EShLangVertex;

		switch (tolower(shaderModel[0])) {
		default:
        case 'v': return EShLangVertex;
        case 'p': return EShLangFragment;
        case 'g': return EShLangGeometry;
        case 'h': assert(0); return EShLangTessEvaluation;
        case 'd': assert(0); return EShLangTessControl;
        case 'c': return EShLangCompute;
        }
	}

	static void AppendErrors(
		::Assets::Blob& errors,
		glslang::TShader& shader,
		StringSection<> glslSource,
		StringSection<> definesTable)
	{
		auto infoLog = shader.getInfoLog();
		auto infoDebugLog = shader.getInfoDebugLog();

		std::stringstream result;
		result << "--- Info log ---" << std::endl;
		result << infoLog << std::endl;
		result << "--- Info debug log ---" << std::endl;
		result << infoDebugLog << std::endl;
		result << "--- GLSL source ---" << std::endl;
		result << glslSource << std::endl;
		result << "--- Defines table ---" << std::endl;
		result << definesTable << std::endl;

		auto str = result.str();
		if (!errors) errors = std::make_shared<std::vector<uint8>>();
		errors->insert(
			errors->end(),
			AsPointer(str.begin()), AsPointer(str.end()));
	}

	static std::string MakeDefinesPreamble(StringSection<> definesTable)
	{
		std::stringstream definesPreamble;
        {
            auto p = definesTable.begin();
            while (p != definesTable.end()) {
                while (p != definesTable.end() && std::isspace(*p)) ++p;

                auto definition = std::find(p, definesTable.end(), '=');
                auto defineEnd = std::find(p, definesTable.end(), ';');

                auto endOfName = std::min(defineEnd, definition);
                while ((endOfName-1) > p && std::isspace(*(endOfName-1))) ++endOfName;

                if (definition < defineEnd) {
                    auto e = definition+1;
                    while (e < defineEnd && std::isspace(*e)) ++e;
                    definesPreamble << "#define " << MakeStringSection(p, endOfName).AsString() << " " << MakeStringSection(e, defineEnd).AsString() << "\n";
                } else {
                    definesPreamble << "#define " << MakeStringSection(p, endOfName).AsString() << "\n";
                }

                p = (defineEnd == definesTable.end()) ? defineEnd : (defineEnd+1);
            }
        }
		return definesPreamble.str();
	}

	static bool GLSLtoSPV(
		/*out*/ ::Assets::Blob& payload,
		/*out*/ ::Assets::Blob& errors,
		EShLanguage shaderType,
		StringSection<> glslSource,
		StringSection<> definesTable,
		StringSection<> identifier,
		const ResChar shaderModel[]) 
	{
		// Enable SPIR-V and Vulkan rules when parsing GLSL
		EShMessages messages = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);
		messages = (EShMessages)(messages|EShMsgAST);

		glslang::TShader shader(shaderType);

		auto definesPreamble = MakeDefinesPreamble(definesTable);
		shader.setPreamble(definesPreamble.c_str());

		const char *shaderStrings[1] { glslSource.begin() };
		int shaderStringLengths[1] { (int)glslSource.size() };
		shader.setStringsWithLengths(shaderStrings, shaderStringLengths, dimof(shaderStrings));

		auto builtInLimits = CreateTBuiltInResource();
		if (!shader.parse(&builtInLimits, 100, false, messages)) {
			AppendErrors(errors, shader, glslSource, definesTable);
			return false;
		}

		glslang::TProgram program;
		program.addShader(&shader);

		if (!program.link(messages)) {
			AppendErrors(errors, shader, glslSource, definesTable);
			return false;
		}

		// Awkwardly, GlslangToSpv only writes to a vector of "unsigned". 
		// But we're expecting to return a vector of "uint8". There's no way
		// to move the block from one vector type to another...!
		glslang::SpvOptions options;
		options.generateDebugInfo = false;
		options.disableOptimizer = false;
		options.optimizeSize= false;
		#if defined(_DEBUG)
			options.generateDebugInfo = true;
			options.disableOptimizer = true;
		#endif
		std::vector<unsigned> spirv;
		glslang::GlslangToSpv(*program.getIntermediate(shaderType), spirv, &options);

		auto spirvBlockSize = spirv.size() * sizeof(unsigned);
		payload = std::make_shared<std::vector<uint8>>(spirvBlockSize + sizeof(ShaderService::ShaderHeader));

		*(ShaderService::ShaderHeader*)AsPointer(payload->begin())
			= ShaderService::ShaderHeader { identifier, shaderModel, "main", false };

		// std::stringstream disassem;
		// spv::Disassemble(disassem, spirv);
		// auto d = disassem.str();

		std::memcpy(
			PtrAdd(AsPointer(payload->begin()), sizeof(ShaderService::ShaderHeader)),
			AsPointer(spirv.begin()), spirvBlockSize);

		return true;
	}

	static ResourceGroup ResourceTypeToResourceGroup(ResourceType eType)
	{
		switch(eType)
		{
		case RTYPE_CBUFFER:
			return RGROUP_CBUFFER;

		case RTYPE_SAMPLER:
			return RGROUP_SAMPLER;

		case RTYPE_TEXTURE:
		case RTYPE_BYTEADDRESS:
		case RTYPE_STRUCTURED:
			return RGROUP_TEXTURE;

		case RTYPE_UAV_RWTYPED:
		case RTYPE_UAV_RWSTRUCTURED:
		case RTYPE_UAV_RWBYTEADDRESS:
		case RTYPE_UAV_APPEND_STRUCTURED:
		case RTYPE_UAV_CONSUME_STRUCTURED:
		case RTYPE_UAV_RWSTRUCTURED_WITH_COUNTER:
			return RGROUP_UAV;

		case RTYPE_TBUFFER:
			assert(0); // Need to find out which group this belongs to
			return RGROUP_TEXTURE;

		default:
			break;
		}

		assert(0);
		return RGROUP_CBUFFER;
	}

	static LegacyRegisterBindingDesc::RegisterType AsBindingType(ResourceBinding* srcResBinding, ::ConstantBuffer* srcCBBinding)
	{
		if (!srcResBinding)
			return srcCBBinding ? LegacyRegisterBindingDesc::RegisterType::ConstantBuffer : LegacyRegisterBindingDesc::RegisterType::Unknown;

		auto group = ResourceTypeToResourceGroup(srcResBinding->eType);
		switch (group) {
		case RGROUP_CBUFFER:    return LegacyRegisterBindingDesc::RegisterType::ConstantBuffer;
		case RGROUP_TEXTURE:    return LegacyRegisterBindingDesc::RegisterType::ShaderResource;
		case RGROUP_SAMPLER:    return LegacyRegisterBindingDesc::RegisterType::Sampler;
		case RGROUP_UAV:        return LegacyRegisterBindingDesc::RegisterType::UnorderedAccess;
		default:
			break;
		}
		return LegacyRegisterBindingDesc::RegisterType::Unknown;
	}

	static LegacyRegisterBindingDesc::RegisterQualifier AsRegisterQualifier(ResourceBinding* srcResBinding)
	{
		if (!srcResBinding)
			return LegacyRegisterBindingDesc::RegisterQualifier::None;
		if (    srcResBinding->eType == RTYPE_UAV_RWSTRUCTURED
			||  srcResBinding->eType == RTYPE_UAV_RWBYTEADDRESS
			||  srcResBinding->eType == RTYPE_UAV_APPEND_STRUCTURED
			||  srcResBinding->eType == RTYPE_STRUCTURED
			||  srcResBinding->eType == RTYPE_BYTEADDRESS
			||  srcResBinding->eType == RTYPE_UAV_CONSUME_STRUCTURED
			||  srcResBinding->eType == RTYPE_UAV_RWSTRUCTURED_WITH_COUNTER)
			return LegacyRegisterBindingDesc::RegisterQualifier::Buffer;
		if (    srcResBinding->eType == RTYPE_TEXTURE)
			return LegacyRegisterBindingDesc::RegisterQualifier::Texture;
		return LegacyRegisterBindingDesc::RegisterQualifier::None;
	}

	struct EvaluateBindingData
	{
		const VulkanCompilerConfiguration* _compilerCfg;
		std::vector<std::pair<uint64_t, unsigned>> _soOffsets;

		EvaluateBindingData(const VulkanCompilerConfiguration& cfg, StringSection<> defines);
	};

	EvaluateBindingData::EvaluateBindingData(const VulkanCompilerConfiguration& cfg, StringSection<> defines)
	: _compilerCfg(&cfg)
	{
		auto starter = MakeStringSection("SO_OFFSETS=");
		auto offsets = XlFindString(defines, starter);
		if (offsets && offsets != defines.end()) {
			offsets += starter.size();
			auto i = offsets;
			while (i!=defines.end()) {
				auto i2 = i+1;
				while (*i2 != ',' && i2!=defines.end()) i2++;
				if (i2 == defines.end()) break;
				auto i3 = i2+1;
				while (*i3 != ',' && i3!=defines.end()) i3++;

				auto h = Conversion::Convert<uint64>(MakeStringSection(i, i2));
				auto o = Conversion::Convert<unsigned>(MakeStringSection(i2+1, i3));
				_soOffsets.push_back({h, o});

				i = i3;
				if (i == defines.end()) break;
				++i;
			}
		}
		std::sort(_soOffsets.begin(), _soOffsets.end(), CompareFirst<uint64_t, unsigned>());
	}

	uint32_t __cdecl EvaluateBinding(
		void* rawUserData,
		GLSLResourceBinding* dstBinding, 
		ResourceBinding* srcResBinding,
		::ConstantBuffer* srcCBBinding,
		char* semantic,
		uint32_t bindPoint, uint32_t shaderStage)
	{
		auto* userData = (const EvaluateBindingData*)rawUserData;
		if (semantic) {
			auto semanticRange = MakeStringSection(semantic);
			auto indexStart = semanticRange.end();
			while (indexStart != semanticRange.begin() && *(indexStart-1) >= '0' && *(indexStart-1) <= '9') --indexStart;
			auto hash = Hash64(MakeStringSection(semanticRange.begin(), indexStart)) + XlAtoI32(indexStart);

			auto i = LowerBound(userData->_soOffsets, hash);
			if (i!=userData->_soOffsets.end() && i->first == hash) {
				dstBinding->_locationIndex = 0;
				dstBinding->_bindingIndex = i->second;
				dstBinding->_setIndex = ~0u;
				dstBinding->_flags = GLSL_BINDING_TYPE_TRANSFORMFEEDBACK;
				return 1;
			}
		}

		// Attempt to find this binding in our root signature, and return the binding
		// index and set associated with it!
		auto type = AsBindingType(srcResBinding, srcCBBinding);
		if (type == LegacyRegisterBindingDesc::RegisterType::Unknown) return 0;

		char* name = nullptr;
		if (srcResBinding) name = srcResBinding->Name;
		else if (srcCBBinding) name = srcCBBinding->Name;

		// First, check to see if it has been assigned as push constants
		if (type == LegacyRegisterBindingDesc::RegisterType::ConstantBuffer && name) {
			const auto& pushConstantRanges = userData->_compilerCfg->_pushConstants;
			for (unsigned rangeIndex=0; rangeIndex<(unsigned)pushConstantRanges.size(); ++rangeIndex) {
				if (XlEqString(pushConstantRanges[rangeIndex]._name, name)) {
					assert(srcCBBinding);
					assert(srcCBBinding->ui32TotalSizeInBytes == pushConstantRanges[rangeIndex]._cbSize);		// If you hit this, it means there's a mismatch in the amount of PushConstants assigned and the size of this buffer
					dstBinding->_locationIndex = ~0u;
					dstBinding->_bindingIndex = ~0u;
					dstBinding->_setIndex = ~0u;
					dstBinding->_flags = GLSL_BINDING_TYPE_PUSHCONSTANTS;
					return 1;
				}
			}
		}

		auto qualifier = AsRegisterQualifier(srcResBinding);
		for (const auto&e:userData->_compilerCfg->_legacyBindings.GetEntries(type, qualifier))
			if (e._begin <= bindPoint && bindPoint < e._end) {
				// found it!
				dstBinding->_locationIndex = ~0u;
				dstBinding->_bindingIndex = e._targetBegin + bindPoint - e._begin;
				dstBinding->_setIndex = e._targetDescriptorSetIdx;
				dstBinding->_flags = 0;
				return 1;
			}

		return 0;
	}

#endif

		////////////////////////////////////////////////////////////

	static unsigned s_glslLangRefCount = 0;

	class GLSLToSPIRVCompiler : public ILowLevelCompiler
	{
	public:
		virtual void AdaptShaderModel(
			ResChar destination[], 
			const size_t destinationCount,
			StringSection<ResChar> source) const override;

		virtual bool DoLowLevelCompile(
			/*out*/ Payload& payload,
			/*out*/ Payload& errors,
			/*out*/ std::vector<::Assets::DependentFileState>& dependencies,
			const void* sourceCode, size_t sourceCodeLength,
			const ResId& shaderPath,
			StringSection<::Assets::ResChar> definesTable,
			IteratorRange<const SourceLineMarker*> sourceLineMarkers) const override;

		virtual std::string MakeShaderMetricsString(
			const void* byteCode, size_t byteCodeSize) const override;

		virtual ShaderLanguage GetShaderLanguage() const override;

		GLSLToSPIRVCompiler(
			const VulkanCompilerConfiguration& cfg);
		~GLSLToSPIRVCompiler();

	protected:
		VulkanCompilerConfiguration _cfg;

		bool DoLowLevelCompileInternal(
			/*out*/ Payload& payload,
			/*out*/ Payload& errors,
			/*out*/ std::vector<::Assets::DependentFileState>& dependencies,
			const void* sourceCode, size_t sourceCodeLength,
			const ResId& shaderPath,
			StringSection<::Assets::ResChar> identifier,
			StringSection<::Assets::ResChar> definesTable,
			IteratorRange<const SourceLineMarker*> sourceLineMarkers) const;
	};

	void GLSLToSPIRVCompiler::AdaptShaderModel(
		ResChar destination[], 
		const size_t destinationCount,
		StringSection<ResChar> inputShaderModel) const
	{
		if (destination != inputShaderModel.begin()) 
			XlCopyString(destination, destinationCount, inputShaderModel);
	}

	bool GLSLToSPIRVCompiler::DoLowLevelCompile(
		/*out*/ ::Assets::Blob& payload,
		/*out*/ ::Assets::Blob& errors,
		/*out*/ std::vector<::Assets::DependentFileState>& dependencies,
		const void* sourceCode, size_t sourceCodeLength,
		const ILowLevelCompiler::ResId& shaderPath,
		StringSection<::Assets::ResChar> definesTable,
		IteratorRange<const ILowLevelCompiler::SourceLineMarker*> sourceLineMarkers) const
	{
		StringMeld<dimof(ShaderService::ShaderHeader::_identifier)> identifier;
		identifier << shaderPath._filename << "-" << shaderPath._entryPoint << "[" << definesTable << "]";
		return DoLowLevelCompileInternal(payload, errors, dependencies, sourceCode, sourceCodeLength, shaderPath, identifier.AsStringSection(), definesTable, sourceLineMarkers);
	}
		
	bool GLSLToSPIRVCompiler::DoLowLevelCompileInternal(
		/*out*/ ::Assets::Blob& payload,
		/*out*/ ::Assets::Blob& errors,
		/*out*/ std::vector<::Assets::DependentFileState>& dependencies,
		const void* sourceCode, size_t sourceCodeLength,
		const ILowLevelCompiler::ResId& shaderPath,
		StringSection<::Assets::ResChar> identifier,
		StringSection<::Assets::ResChar> definesTable,
		IteratorRange<const ILowLevelCompiler::SourceLineMarker*> sourceLineMarkers) const
	{
#if defined(HAS_SPIRV_HEADERS)
		return GLSLtoSPV(
			payload, errors, 
			EShLanguageFromShaderModel(shaderPath._shaderModel),
			MakeStringSection((const char*)sourceCode, (const char*)PtrAdd(sourceCode, sourceCodeLength)),
			definesTable,
			identifier, shaderPath._shaderModel);
#else
		return false;
#endif
	}

	std::string GLSLToSPIRVCompiler::MakeShaderMetricsString(const void* data, size_t dataSize) const
	{
		if (dataSize > sizeof(ShaderService::ShaderHeader)) {
			std::stringstream str;
			str << SPIRVReflection({PtrAdd(data, sizeof(ShaderService::ShaderHeader)), PtrAdd(data, dataSize - sizeof(ShaderService::ShaderHeader))});
			return str.str();
		} else {
			return "<<error: buffer is too small>>";
		}
	}

	ShaderLanguage GLSLToSPIRVCompiler::GetShaderLanguage() const
	{
		return ShaderLanguage::GLSL;
	}
	
	GLSLToSPIRVCompiler::GLSLToSPIRVCompiler(const VulkanCompilerConfiguration& cfg) 
	: _cfg(cfg)
	{
		#if defined(HAS_SPIRV_HEADERS)
			bool initResult = glslang::InitializeProcess();
			if (!initResult)
				Throw(::Exceptions::BasicLabel("Failed while initializing glsl to spirv shader compiler"));
			++s_glslLangRefCount;
		#endif
	}

	GLSLToSPIRVCompiler::~GLSLToSPIRVCompiler()
	{
		#if defined(HAS_SPIRV_HEADERS)
			--s_glslLangRefCount;
			if (!s_glslLangRefCount)
				glslang::FinalizeProcess();
		#endif
	}

		////////////////////////////////////////////////////////////

	class HLSLCCToSPIRVCompiler : public GLSLToSPIRVCompiler
	{
	public:
		virtual void AdaptShaderModel(
			ResChar destination[], 
			const size_t destinationCount,
			StringSection<ResChar> source) const override;

		virtual bool DoLowLevelCompile(
			/*out*/ Payload& payload,
			/*out*/ Payload& errors,
			/*out*/ std::vector<::Assets::DependentFileState>& dependencies,
			const void* sourceCode, size_t sourceCodeLength,
			const ResId& shaderPath,
			StringSection<::Assets::ResChar> definesTable,
			IteratorRange<const SourceLineMarker*> sourceLineMarkers) const override;

		virtual ShaderLanguage GetShaderLanguage() const override;

		HLSLCCToSPIRVCompiler(
			std::shared_ptr<ILowLevelCompiler> hlslCompiler, 
			const VulkanCompilerConfiguration& cfg);
		~HLSLCCToSPIRVCompiler();

	private:
		std::shared_ptr<ILowLevelCompiler>				_hlslCompiler;
	};

	void HLSLCCToSPIRVCompiler::AdaptShaderModel(
		ResChar destination[], 
		const size_t destinationCount,
		StringSection<ResChar> inputShaderModel) const
	{
		// _hlslCompiler->AdaptShaderModel(destination, destinationCount, inputShaderModel);

		if (inputShaderModel[0] != '\0') {
			size_t length = inputShaderModel.size();

			//  Some shaders end with vs_*, gs_*, etc..
			//  Change this to the default shader model version. We should use
			//  version 5.0 in this case. We haven't initialized a D3D device (so we
			//  don't have to care about compatibility with any physical device).
			//  As long as the installed shader compile dll supports 5.0, then we
			//  should be ok.
			if (inputShaderModel[length-1] == '*') {
				const char* bestShaderModel = "5_0";
				if (destination != inputShaderModel.begin()) 
					XlCopyString(destination, destinationCount, inputShaderModel);
				destination[std::min(length-1, destinationCount-1)] = '\0';
				XlCatString(destination, destinationCount, bestShaderModel);
				return;
			}
		}

		if (destination != inputShaderModel.begin()) 
			XlCopyString(destination, destinationCount, inputShaderModel);
	}

	bool HLSLCCToSPIRVCompiler::DoLowLevelCompile(
		/*out*/ ::Assets::Blob& payload,
		/*out*/ ::Assets::Blob& errors,
		/*out*/ std::vector<::Assets::DependentFileState>& dependencies,
		const void* sourceCode, size_t sourceCodeLength,
		const ILowLevelCompiler::ResId& shaderPath,
		StringSection<::Assets::ResChar> definesTable,
		IteratorRange<const ILowLevelCompiler::SourceLineMarker*> sourceLineMarkers) const
	{
#if defined(HAS_SPIRV_HEADERS)
		// So, this is a complex process for converting from HLSL source code into SPIR-V.
		// In time, hopefully there will be better solutions for this problem.
		// But, let's go HLSL -> HLSL bytecode -> GLSL -> SPIR-V

		// First, attempt to compile the HLSL code into byte code (using D3D compilers)
		::Assets::Blob hlslBytecode;
		bool hlslGood = _hlslCompiler->DoLowLevelCompile(
			hlslBytecode, errors, dependencies,
			sourceCode, sourceCodeLength, shaderPath, definesTable);
		if (!hlslGood) return false;

		dependencies.insert(dependencies.end(), _cfg._additionalDependencies.begin(), _cfg._additionalDependencies.end());

		// Second, HLSL bytecode -> GLSL source
		// We're going to be using James-Jones' cross compiler: 
		//      https://github.com/James-Jones/HLSLCrossCompiler
		GlExtensions ext;
		ext.ARB_explicit_attrib_location = 0;
		ext.ARB_explicit_uniform_location = 0;
		ext.ARB_shading_language_420pack = 1;
		ext.GL_KHR_vulkan_glsl = 1;
		GLSLCrossDependencyData depData = {};
		GLSLShader glslShader;
		unsigned hlslccFlags = HLSLCC_FLAG_UNIFORM_BUFFER_OBJECT | HLSLCC_FLAG_INOUT_SEMANTIC_NAMES;
		if (tolower(shaderPath._shaderModel[0]) == 'g')
			hlslccFlags &= ~HLSLCC_FLAG_INOUT_SEMANTIC_NAMES;

		auto* bytecodeStart = (const char*)PtrAdd(AsPointer(hlslBytecode->begin()), sizeof(ShaderService::ShaderHeader));
		EvaluateBindingData bd { _cfg, definesTable };
		auto translateResult = TranslateHLSLFromMem(
			bytecodeStart,
			hlslccFlags,
			LANG_440, &ext, &depData, 
			&EvaluateBinding, &bd,
			&glslShader);
		if (!translateResult) return false;
	
		// Third, GLSL source -> glslang::TShader -> SPIR-V bytecode
		assert(GLSLShaderTypeToEShLanguage(glslShader.shaderType) == EShLanguageFromShaderModel(shaderPath._shaderModel));
		StringMeld<dimof(ShaderService::ShaderHeader::_identifier)> identifier;
		identifier << shaderPath._filename << "-" << shaderPath._entryPoint << "[" << definesTable << "]";
		return GLSLToSPIRVCompiler::DoLowLevelCompileInternal(
			payload, errors, 
			dependencies,
			glslShader.sourceCode, std::strlen(glslShader.sourceCode),
			shaderPath, identifier.AsStringSection(), {}, sourceLineMarkers);
#else
		return false;
#endif
	}

	ShaderLanguage HLSLCCToSPIRVCompiler::GetShaderLanguage() const
	{
		return ShaderLanguage::HLSL;
	}

	HLSLCCToSPIRVCompiler::HLSLCCToSPIRVCompiler(
		std::shared_ptr<ILowLevelCompiler> hlslCompiler, 
		const VulkanCompilerConfiguration& cfg) 
	: GLSLToSPIRVCompiler(cfg)
	, _hlslCompiler(std::move(hlslCompiler))
	{
	}

	HLSLCCToSPIRVCompiler::~HLSLCCToSPIRVCompiler()
	{
	}

#endif


namespace RenderCore { namespace Metal_Vulkan
{
	std::shared_ptr<ILowLevelCompiler> CreateLowLevelShaderCompiler(
		IDevice& device, const VulkanCompilerConfiguration& cfg)
	{
		auto* vulkanDevice = (IDeviceVulkan*)device.QueryInterface(typeid(IDeviceVulkan).hash_code());
		if (!vulkanDevice) return nullptr;

		#if defined(ENABLE_GLSLLANG)
			if (cfg._shaderMode == VulkanShaderMode::HLSLCrossCompiled) {
				auto hlslCompiler = Metal_DX11::CreateVulkanPrecompiler();
				return std::make_shared<HLSLCCToSPIRVCompiler>(hlslCompiler, cfg);
			} else if (cfg._shaderMode == VulkanShaderMode::GLSLToSPIRV) {
				return std::make_shared<GLSLToSPIRVCompiler>(cfg);
			} else
		#endif 
		if (cfg._shaderMode == VulkanShaderMode::HLSLToSPIRV) {
			return Metal_DX11::CreateHLSLToSPIRVCompiler();
		} else {
			assert(0);
			return nullptr;
		}
	}

}}

