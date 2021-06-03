// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../RenderCore/Techniques/ShaderVariationSet.h"
#include "../Utility/ParameterBox.h"

namespace RenderCore { class InputElementDesc; class VertexBufferView; class UniformsStream; }
namespace RenderCore { namespace Assets { class PredefinedCBLayout; }}

namespace FixedFunctionModel
{
		//
        //  <summary>Vertex, constants and resources interface for a technique<summary>
        //
        //  Defines an input interface for a technique. Normally a client may only define
        //  a few "TechniquePrebindingInterface" objects, and reuse them with many techniques.
        //
        //  The interface consists of:
        //      * vertex input layout
        //          - input vertex format (position of elements in the vertex buffer)
        //      * constant buffers
        //          - constant buffer binding locations
        //      * shader resources
        //          - shader resources binding locations
        //
        //  The constants and shader resources bindings define how the "BoundUniforms" object in the ResolvedShader
        //  is created. For example, if you bind a constant buffer to slot 0, then you can set it by passing the constant
        //  buffer in slot 0 to ResolvedShader::_boundUniforms->Apply(...).
        //
        //  When calling BindConstantBuffer or BindShaderResource, the first parameter
        //  is a hash value of the name in the shader. For example, if the shader has:
        //      Texture2D<float> Depths;
        //
        //  Then you can pass:
        //      BindShaderResource(Hash64("Depth"), interfaceSlotIndex);
        //
    class TechniquePrebindingInterface
    {
    public:
		uint64_t	GetHashValue() const;

        void		BindUniformsStream(unsigned streamIndex, const RenderCore::UniformsStreamInterface& interf);
		void		BindGlobalUniforms();

		TechniquePrebindingInterface(IteratorRange<const RenderCore::InputElementDesc*> vertexInputLayout);

        TechniquePrebindingInterface();        
        TechniquePrebindingInterface(TechniquePrebindingInterface&& moveFrom) never_throws;
        TechniquePrebindingInterface&operator=(TechniquePrebindingInterface&& moveFrom) never_throws;
        ~TechniquePrebindingInterface();

    private:
        class Pimpl;
        std::unique_ptr<Pimpl> _pimpl;

        friend class BoundShaderVariationSet; // makes internal structure easier
    };

	/// <summary>Like TechniqueShaderVariationSet, but also caches objects generated from a TechniquePrebindingInterface</summary>
	class BoundShaderVariationSet : public RenderCore::Techniques::TechniqueShaderVariationSet
    {
	public:
		class ResolvedShader
		{
		public:
			uint64_t						_variationHash = 0;
			RenderCore::Metal::ShaderProgram*           _shaderProgram = nullptr;
			RenderCore::Metal::BoundUniforms*           _boundUniforms = nullptr;
			RenderCore::Metal::BoundInputLayout*        _boundLayout = nullptr;

			void Apply(
				RenderCore::Metal::DeviceContext& devContext,
				RenderCore::Techniques::ParsingContext& parserContext,
				const std::initializer_list<RenderCore::VertexBufferView>& vbs) const;

			void ApplyUniforms(
				RenderCore::Metal::DeviceContext& context,
				unsigned streamIdx,
				const RenderCore::UniformsStream& stream) const;
		};

		ResolvedShader FindVariation(
			int techniqueIndex, 
			const ParameterBox* shaderSelectors[RenderCore::Techniques::SelectorStages::Max], 
			const TechniquePrebindingInterface& techniqueInterface) const;

		BoundShaderVariationSet(const std::shared_ptr<RenderCore::Techniques::Technique>& technique);
		~BoundShaderVariationSet();

		static void ConstructToFuture(
			::Assets::FuturePtr<BoundShaderVariationSet>& future,
			StringSection<::Assets::ResChar> techniqueName);

	protected:
		class Entry
		{
		public:
			class BoundShader
			{
			public:
				std::shared_ptr<RenderCore::Metal::ShaderProgram>		_shaderProgram;
				std::unique_ptr<RenderCore::Metal::BoundUniforms>		_boundUniforms;
				std::unique_ptr<RenderCore::Metal::BoundInputLayout>	_boundLayout;
			};
			mutable std::vector<std::pair<uint64_t, BoundShader>> _boundShaders;
			
			static BoundShader MakeBoundShader(
				const std::shared_ptr<RenderCore::Metal::ShaderProgram>& shader, 
				const RenderCore::Techniques::TechniqueEntry& techEntry,
				const ParameterBox* shaderSelectors[RenderCore::Techniques::SelectorStages::Max],
				const TechniquePrebindingInterface& techniqueInterface);
			static ResolvedShader AsResolvedShader(uint64_t hash, const BoundShader&);
		};
        Entry	_entries[size_t(RenderCore::Techniques::TechniqueIndex::Max)];
	};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	/// <summary>Utility class for selecting a shader variation matching a given interface</summary>
	class SimpleShaderVariationManager
    {
    public:
        ParameterBox _materialParameters;
        ParameterBox _geometryParameters;
        TechniquePrebindingInterface _techniqueInterface;

        class Variation
        {
        public:
			BoundShaderVariationSet::ResolvedShader      _shader;
            const RenderCore::Assets::PredefinedCBLayout* _cbLayout;
        };

        Variation FindVariation(
			RenderCore::Techniques::ParsingContext& parsingContext,
            unsigned techniqueIndex,
            StringSection<> techniqueConfig) const;

        const RenderCore::Assets::PredefinedCBLayout& GetCBLayout(StringSection<> techniqueConfig);

        SimpleShaderVariationManager(
            IteratorRange<const RenderCore::InputElementDesc*> inputLayout,
            const std::initializer_list<uint64_t>& objectCBs,
            const ParameterBox& materialParameters);
        SimpleShaderVariationManager();
        SimpleShaderVariationManager(SimpleShaderVariationManager&& moveFrom) never_throws = default;
        SimpleShaderVariationManager& operator=(SimpleShaderVariationManager&& moveFrom) never_throws = default;
        ~SimpleShaderVariationManager();
    };

    ParameterBox TechParams_SetGeo(IteratorRange<const RenderCore::InputElementDesc*> inputLayout);
}

