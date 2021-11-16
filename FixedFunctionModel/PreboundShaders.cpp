// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "PreboundShaders.h"
#include "../RenderCore/Types.h"
#include "../RenderCore/Techniques/ParsingContext.h"
#include "../RenderCore/Metal/InputLayout.h"
#include "../RenderCore/Metal/Shader.h"
#include "../RenderCore/Metal/DeviceContext.h"
#include "../RenderCore/Assets/Services.h"
#include "../Assets/Assets.h"
#include "../Utility/Streams/PathUtils.h"

namespace FixedFunctionModel
{
	using namespace RenderCore;

        ///////////////////////   T E C H N I Q U E   I N T E R F A C E   ///////////////////////////

    class TechniquePrebindingInterface::Pimpl
    {
    public:
        std::vector<InputElementDesc>			_vertexInputLayout;
        std::vector<UniformsStreamInterface>    _uniformStreams;

        uint64_t _hashValue;
        Pimpl() : _hashValue(0) {}

        void        UpdateHashValue();
    };

    void        TechniquePrebindingInterface::Pimpl::UpdateHashValue()
    {
		_hashValue = 0;
		for (const auto& i : _uniformStreams)
			_hashValue = HashCombine(i.GetHash(), _hashValue);

        unsigned index = 0;
        for (auto i=_vertexInputLayout.cbegin(); i!=_vertexInputLayout.cend(); ++i, ++index) {
            struct PartialDesc
            {
                unsigned        _semanticIndex;
                Format			_nativeFormat;
                unsigned        _inputSlot;
                unsigned        _alignedByteOffset;
                InputDataRate	_inputSlotClass;
                unsigned        _instanceDataStepRate;
            } partialDesc;

            partialDesc._semanticIndex          = i->_semanticIndex;
            partialDesc._nativeFormat           = i->_nativeFormat;
            partialDesc._inputSlot              = i->_inputSlot;
            partialDesc._alignedByteOffset      = i->_alignedByteOffset;        // could get a better hash if we convert this to a true offset, here (if it's the default offset marker)
            partialDesc._inputSlotClass         = i->_inputSlotClass;
            partialDesc._instanceDataStepRate   = i->_instanceDataStepRate;

            auto elementHash = 
                Hash64(&partialDesc, PtrAdd(&partialDesc, sizeof(partialDesc)))
                ^ Hash64(i->_semanticName);
            _hashValue ^= (elementHash<<(2*index));     // index is a slight problem here. two different interfaces with the same elements, but in reversed order would come to the same hash. We can shift it slightly to true to avoid this
        }
    }

    void    TechniquePrebindingInterface::BindUniformsStream(unsigned streamIndex, const UniformsStreamInterface& interf)
    {
		if (_pimpl->_uniformStreams.size() <= streamIndex)
			_pimpl->_uniformStreams.resize(streamIndex + 1);
		_pimpl->_uniformStreams[streamIndex] = interf;
		_pimpl->UpdateHashValue();
    }

	void     TechniquePrebindingInterface::BindGlobalUniforms()
    {
		BindUniformsStream(0, RenderCore::Techniques::TechniqueContext::GetGlobalUniformsStreamInterface());
    }

    uint64_t  TechniquePrebindingInterface::GetHashValue() const
    {
        return _pimpl->_hashValue;
    }

    TechniquePrebindingInterface::TechniquePrebindingInterface() 
    {
        _pimpl = std::make_unique<TechniquePrebindingInterface::Pimpl>();
    }

    TechniquePrebindingInterface::TechniquePrebindingInterface(IteratorRange<const InputElementDesc*> vertexInputLayout)
    {
        _pimpl = std::make_unique<TechniquePrebindingInterface::Pimpl>();
        _pimpl->_vertexInputLayout.insert(
            _pimpl->_vertexInputLayout.begin(),
            vertexInputLayout.begin(), vertexInputLayout.end());
        _pimpl->UpdateHashValue();
    }

    TechniquePrebindingInterface::~TechniquePrebindingInterface()
    {}

    TechniquePrebindingInterface::TechniquePrebindingInterface(TechniquePrebindingInterface&& moveFrom) never_throws
    {
        _pimpl = std::move(moveFrom._pimpl);
    }

    TechniquePrebindingInterface&TechniquePrebindingInterface::operator=(TechniquePrebindingInterface&& moveFrom) never_throws
    {
        _pimpl = std::move(moveFrom._pimpl);
        return *this;
    }

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	BoundShaderVariationSet::ResolvedShader BoundShaderVariationSet::Entry::AsResolvedShader(uint64_t hash, const BoundShader& shader)
	{
		return ResolvedShader {
			hash,
			shader._shaderProgram.get(),
			shader._boundUniforms.get(),
			shader._boundLayout.get() };
	}

	auto BoundShaderVariationSet::Entry::MakeBoundShader(	
		const std::shared_ptr<Metal::ShaderProgram>& shader, 
		const RenderCore::Techniques::TechniqueEntry& techEntry,
		const ParameterBox* globalState[SelectorStages::Max],
		const TechniquePrebindingInterface& techniqueInterface) -> BoundShader
    {
		BoundShader result;
		result._shaderProgram = shader;
		// auto shaderProgramFuture = GetShaderVariation(techEntry, globalState);
		// shaderProgramFuture->StallWhilePending();
		// resolvedShader._shaderProgram = shaderProgramFuture->Actualize();

		UniformsStreamInterface streamInterfaces[4];
		for (unsigned c=0; c<std::min(dimof(streamInterfaces), techniqueInterface._pimpl->_uniformStreams.size()); ++c)
			streamInterfaces[c] = techniqueInterface._pimpl->_uniformStreams[c];

		result._boundUniforms = std::make_unique<Metal::BoundUniforms>(
			std::ref(*shader),
			RenderCore::Metal::PipelineLayoutConfig{},
			streamInterfaces[0],
			streamInterfaces[1],
			streamInterfaces[2],
			streamInterfaces[3]);

        result._boundLayout = std::make_unique<Metal::BoundInputLayout>(
			MakeIteratorRange(techniqueInterface._pimpl->_vertexInputLayout), std::ref(*shader));
		return result;
    }

	auto BoundShaderVariationSet::FindVariation(  
		int techniqueIndex, 
        const ParameterBox* shaderSelectors[RenderCore::Techniques::SelectorStages::Max],
        const TechniquePrebindingInterface& techniqueInterface) const -> ResolvedShader
    {
		const auto& techEntry = _technique->GetEntry(techniqueIndex);
		RenderCore::Techniques::ShaderVariationFactory_Basic factory(techEntry);
        auto& base = _variationSet.FindVariation(techEntry._selectorFiltering, shaderSelectors, factory);

		auto& boundShaders = _entries[techniqueIndex]._boundShaders;
		auto i = LowerBound(boundShaders, base._variationHash);
		if (i == boundShaders.end() || i->first != base._variationHash) {
			auto actual = base._shaderFuture->TryActualize();
			if (!actual)	// invalid or still pending
				return {};

			auto shr = Entry::MakeBoundShader(*actual, techEntry, shaderSelectors, techniqueInterface);
			i = boundShaders.insert(i, std::make_pair(base._variationHash, std::move(shr)));
		}
		return Entry::AsResolvedShader(i->first, i->second);
    }

	void BoundShaderVariationSet::ResolvedShader::Apply(
        Metal::DeviceContext& devContext,
        RenderCore::Techniques::ParsingContext& parserContext,
		const std::initializer_list<VertexBufferView>& vbs) const
    {
		_boundUniforms->Apply(devContext, 0, parserContext.GetGlobalUniformsStream());
		_boundLayout->Apply(devContext, MakeIteratorRange(vbs.begin(), vbs.end()));
        _shaderProgram->Apply(devContext);
    }

	void BoundShaderVariationSet::ResolvedShader::ApplyUniforms(
		Metal::DeviceContext& devContext,
		unsigned streamIdx,
		const UniformsStream& stream) const
	{
		_boundUniforms->Apply(devContext, streamIdx, stream);
	}

	BoundShaderVariationSet::BoundShaderVariationSet(const std::shared_ptr<RenderCore::Techniques::Technique>& technique)
	: TechniqueShaderVariationSet(technique)
	{
	}
	BoundShaderVariationSet::~BoundShaderVariationSet() {}

	void BoundShaderVariationSet::ConstructToFuture(
		::Assets::MarkerPtr<BoundShaderVariationSet>& future,
		StringSection<::Assets::ResChar> modelScaffoldName)
	{
		auto scaffoldFuture = ::Assets::MakeAsset<RenderCore::Techniques::Technique>(modelScaffoldName);

		future.SetPollingFunction(
			[scaffoldFuture](::Assets::MarkerPtr<BoundShaderVariationSet>& thatFuture) -> bool {

			std::shared_ptr<RenderCore::Techniques::Technique> scaffoldActual; ::Assets::DependencyValidation scaffoldDepVal; ::Assets::Blob scaffoldLog;
			auto scaffoldState = scaffoldFuture->CheckStatusBkgrnd(scaffoldActual, scaffoldDepVal, scaffoldLog);

			if (!scaffoldActual) {
				if (scaffoldState == ::Assets::AssetState::Invalid) {
					thatFuture.SetInvalidAsset(scaffoldDepVal, scaffoldLog);
					return false;
				}
				return true;
			}

			auto newModel = std::make_shared<BoundShaderVariationSet>(scaffoldActual);
			thatFuture.SetAsset(std::move(newModel), {});
			return false;
		});
	}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	static TechniquePrebindingInterface MakeTechInterface(
		IteratorRange<const InputElementDesc*> inputLayout,
        const std::initializer_list<uint64_t>& objectCBs)
    {
		UniformsStreamInterface interf;
		unsigned index = 0;
		for (auto o : objectCBs)
			interf.BindConstantBuffer(index++, { o });

        TechniquePrebindingInterface techniqueInterface(inputLayout);
        techniqueInterface.BindGlobalUniforms();
		techniqueInterface.BindUniformsStream(1, interf);
        return std::move(techniqueInterface);
    }

    ParameterBox TechParams_SetGeo(IteratorRange<const InputElementDesc*> inputLayout)
    {
        ParameterBox result;
		Techniques::SetGeoSelectors(result, inputLayout);
        return result;
    }

    SimpleShaderVariationManager::SimpleShaderVariationManager(
        IteratorRange<const InputElementDesc*> inputLayout,
        const std::initializer_list<uint64_t>& objectCBs,
        const ParameterBox& materialParameters)
    : _materialParameters(std::move(materialParameters))
    , _techniqueInterface(MakeTechInterface(inputLayout, objectCBs))
    , _geometryParameters(TechParams_SetGeo(inputLayout))
    {}

    SimpleShaderVariationManager::SimpleShaderVariationManager() {}
    SimpleShaderVariationManager::~SimpleShaderVariationManager() {}

    auto SimpleShaderVariationManager::FindVariation(
        Techniques::ParsingContext& parsingContext,
        unsigned techniqueIndex, StringSection<> techniqueConfig) const -> Variation
    {
        const ParameterBox* state[] = {
            &_geometryParameters, 
            &parsingContext.GetTechniqueContext()._globalEnvironmentState,
            &parsingContext.GetSubframeShaderSelectors(), 
            &_materialParameters
        };
		
		const auto& searchDirs = RenderCore::Assets::Services::GetTechniqueConfigDirs();

		assert(XlEqStringI(MakeFileNameSplitter(techniqueConfig).Extension(), "tech"));
		::Assets::ResChar resName[MaxPath];
		searchDirs.ResolveFile(resName, dimof(resName), techniqueConfig);

        auto& techConfig = ::Assets::Legacy::GetAssetDep<BoundShaderVariationSet>(MakeStringSection(resName));

        Variation result;
        result._cbLayout = nullptr;
        result._shader = techConfig.FindVariation(techniqueIndex, state, _techniqueInterface);
        result._cbLayout = &techConfig.GetTechnique().TechniqueCBLayout();
        return result;
    }

    const RenderCore::Assets::PredefinedCBLayout& SimpleShaderVariationManager::GetCBLayout(StringSection<> techniqueConfigName)
    {
        auto& techConfig = ::Assets::Legacy::GetAssetDep<Techniques::TechniqueShaderVariationSet>(techniqueConfigName);
        return techConfig.GetTechnique().TechniqueCBLayout();
    }

}

