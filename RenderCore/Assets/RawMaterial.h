// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "ShaderPatchCollection.h"
#include "../StateDesc.h"
#if !defined(__CLR_VER)
    #include "../../Assets/AssetMixins.h"
#endif
#include "../../Utility/ParameterBox.h"
#include "../../Utility/MemoryUtils.h"
#include <memory>
#include <vector>
#include <string>

namespace Assets { class DependencyValidation; class DirectorySearchRules; }
namespace Formatters { class TextOutputFormatter; }

namespace RenderCore { namespace Assets
{
    #pragma pack(push)
	#pragma pack(1)

    /// <summary>Render state settings</summary>
    /// These settings are used to select the low-level graphics API render 
    /// state while rendering using this material.
    ///
    /// There are only a few low-level states that are practical & meaningful
    /// to set this way. Often we get fighting between different parts of the
    /// engine wanting to control render states. For example, a graphics effect
    /// may want to select the back face culling mode -- but the material may
    /// have a setting for that as well. So who wins? The material or the 
    /// graphics effect? The answer changes from situation to situation.
    ///
    /// These are difficult problems! To try to avoid, we should make sure that
    /// the material only has settings for the minimal set of states it really 
    /// needs (and free everything else up for higher level stuff)
    ///
    /// RasterizerDesc:
    /// -----------------------------------------------------------------------------
    ///     double-sided culling enable/disable
    ///         Winding direction and CULL_FRONT/CULL_BACK don't really belong here.
    ///         Winding direction should be a property of the geometry and any
    ///         transforms applied to it. And we should only need to select CULL_FRONT
    ///         for special graphics techniques -- they can do it another way
    ///
    ///     depth bias
    ///         Sometimes it's handy to apply some bias at a material level. But it
    ///         should blend somehow with depth bias applied as part of the shadow 
    ///         rendering pass.
    ///
    ///     fill mode
    ///         it's rare to want to change the fill mode. But it feels like it should
    ///         be a material setting (though, I guess it could alternatively be
    ///         attached to the geometry).
    ///
    /// BlendDesc:
    /// -----------------------------------------------------------------------------
    ///     blend mode settings
    ///         This is mostly meaningful during forward rendering operations. But it
    ///         may be handy for deferred decals to select a blend mode at a material
    ///         based level. 
    ///
    ///         There may be some cases where we want to apply different blend mode 
    ///         settings in deferred and forward rendering. That suggests having 2
    ///         separate states -- one for deferred, one for forward.
    ///         We don't really want to use the low-level states in the deferred case,
    ///         because it may depend on the structure of the gbuffer (which is defined
    ///         elsewhere)
    ///
    ///         The blend mode might depend on the texture, as well. If the texture is
    ///         premultiplied alpha, it might end up with a different blend mode than
    ///         when using a non-premultiplied alpha texture.
    ///
    ///         The alpha channel blend settings (and IndependentBlendEnable setting)
    ///         are not exposed.
    ///
    ///     write mask
    ///         It's rare to want to change the write mask -- but it can be an interesting
    ///         trick. It doesn't hurt much to have some behaviour for it here.
    ///
    /// Other possibilities
    /// -----------------------------------------------------------------------------
    ///     stencil write states & stencil test states
    ///         there may be some cases where we want the material to define how we 
    ///         read and write the stencil buffer. Mostly some higher level state will
    ///         control this, but the material may want to have some effect..?
    ///
    /// Also note that alpha test is handled in a different way. We use shader behaviour
    /// (not a render state) to enable/disable 
    class RenderStateSet
    {
    public:
        enum class BlendType : unsigned
        {
            Basic, DeferredDecal, Ordered
        };
        unsigned    _doubleSided : 1;
        unsigned    _smoothLines : 1;
        unsigned    _writeMask : 4;
        BlendType   _blendType : 2;

            //  These "blend" values may not be completely portable across all platforms
            //  (either because blend modes aren't supported, or because we need to
            //  change the meaning of the values)
        Blend		_forwardBlendSrc : 5;
        Blend		_forwardBlendDst : 5;
        BlendOp		_forwardBlendOp  : 5;

        struct Flag
        {
            enum Enum {
                DoubleSided = 1<<0, SmoothLines = 1<<1, WriteMask = 1<<2,
                BlendType = 1<<3, ForwardBlend = 1<<4, DepthBias = 1<<5,
                LineWeight = 1<<6
            };
            typedef unsigned BitField;
        };
        Flag::BitField  _flag : 7;
        unsigned        _padding : 2;   // 8 + 15 + 32 + 7 = 62 bits... pad to 64 bits

        union
        {
            int             _depthBias;     // do we need all of the bits for this?
            float           _lineWeight;
        };

        RenderStateSet& SetDoubleSided(bool);
        RenderStateSet& SetSmoothLines(bool);
        RenderStateSet& SetWriteMask(unsigned);
        RenderStateSet& SetBlendType(BlendType);
        RenderStateSet& SetForwardBlend(Blend src, Blend dst, BlendOp op = BlendOp::Add);
        RenderStateSet& SetDepthBias(int);
        RenderStateSet& SetLineWeight(float);

        uint64 GetHash() const;
        RenderStateSet();
    };

    #pragma pack(pop)

	RenderStateSet Merge(RenderStateSet underride, RenderStateSet override);

    /// <summary>Pre-resolved material settings</summary>
    /// Materials are a hierarchical set of properties. Each RawMaterial
    /// object can inherit from sub RawMaterials -- and it can either
    /// inherit or override the properties in those sub RawMaterials.
    ///
    /// RawMaterials are intended to be used in tools (for preprocessing 
    /// and material authoring). ResolvedMaterial is the run-time representation.
    ///
    /// During preprocessing, RawMaterials should be resolved down to a 
    /// ResolvedMaterial object (using the Resolve() method). 
    class RawMaterial
    {
    public:
        ParameterBox	_resources;
        ParameterBox	_selectors;
        ParameterBox	_uniforms;
        RenderStateSet	_stateSet;
        std::vector<std::pair<std::string, SamplerDesc>> _samplers;

		ShaderPatchCollection _patchCollection;

        template<typename Value>
            void BindResource(StringSection<>, const Value&);
        template<typename Value>
            void SetSelector(StringSection<>, const Value&);
        template<typename Value>
            void SetUniform(StringSection<>, const Value&);
        void BindSampler(const std::string&, const SamplerDesc&);

		void MergeInWithFilenameResolve(const RawMaterial&, const ::Assets::DirectorySearchRules&);

        void SerializeMethod(Formatters::TextOutputFormatter& formatter) const;
        bool TryDeserializeKey(Formatters::TextInputFormatter<utf8>&, StringSection<>);

        uint64_t CalculateHash(uint64_t seed = DefaultSeed64) const;

        RawMaterial();
        explicit RawMaterial(Formatters::TextInputFormatter<utf8>& formatter);
        ~RawMaterial();
    };

    class ModelCompilationConfiguration;

#if !defined(__CLR_VER)
    using ContextImbuedRawMaterialPtr = ::Assets::ContextImbuedAsset<std::shared_ptr<RawMaterial>>;
    using ContextImbuedRawMaterial = ::Assets::ContextImbuedAsset<RawMaterial>;

    void AutoConstructToPromiseOverride(
        std::promise<ContextImbuedRawMaterialPtr>&& promise,
        StringSection<> initializer);

    void AutoConstructToPromiseOverride(
        std::promise<ContextImbuedRawMaterial>&& promise,
        StringSection<> initializer);

    void AutoConstructToPromiseOverride(
        std::promise<ContextImbuedRawMaterialPtr>&& promise,
        StringSection<> initializer, std::shared_ptr<ModelCompilationConfiguration> cfg);

    void AutoConstructToPromiseOverride(
        std::promise<ContextImbuedRawMaterial>&& promise,
        StringSection<> initializer, std::shared_ptr<ModelCompilationConfiguration> cfg);

    std::shared_future<::Assets::ResolvedAssetMixin<RawMaterial>> GetResolvedMaterialFuture(StringSection<>);

	class RawMaterialSet
    {
    public:
        using Entry = std::tuple<RawMaterial, ::Assets::InheritList>;
        std::vector<std::pair<std::string, Entry>> _materials;

        void AddMaterial(std::string s, RawMaterial&& mat) { _materials.emplace_back(std::move(s), std::make_tuple(std::move(mat), ::Assets::InheritList{})); }
        void AddMaterial(std::string s, RawMaterial&& mat, ::Assets::InheritList&& inherit) { _materials.emplace_back(std::move(s), std::make_tuple(std::move(mat), std::move(inherit))); }

		RawMaterialSet(Formatters::TextInputFormatter<char>& fmttr);
        RawMaterialSet() = default;
    };

    constexpr auto GetCompileProcessType(RawMaterialSet*) { return ConstHash64Legacy<'RawM', 'at'>::Value; }

        // todo -- avoid the need for this!
    constexpr auto GetCompileProcessType(::Assets::ContextImbuedAsset<std::shared_ptr<RawMaterialSet>>*) { return ConstHash64Legacy<'RawM', 'at'>::Value; }

    void SerializationOperator(Formatters::TextOutputFormatter&, const RawMaterialSet&);
    void SerializationOperator(Formatters::TextOutputFormatter&, const std::tuple<RawMaterial, ::Assets::InheritList>&);
#endif

    template<typename Value> void RawMaterial::BindResource(StringSection<> name, const Value& value) { _resources.SetParameter(name, value); }
    template<typename Value> void RawMaterial::SetSelector(StringSection<> name, const Value& value) { _selectors.SetParameter(name, value); }
    template<typename Value> void RawMaterial::SetUniform(StringSection<> name, const Value& value) { _uniforms.SetParameter(name, value); }

    inline RenderStateSet::RenderStateSet()
    {
        _doubleSided = false;
        _smoothLines = false;
        _writeMask = 0xf;
        _blendType = BlendType::Basic;
        _depthBias = 0;
        _flag = 0;
        
        _forwardBlendSrc = Blend(0); // Metal::Blend::One;
        _forwardBlendDst = Blend(0); // Metal::Blend::Zero;
        _forwardBlendOp = BlendOp(0); // Metal::BlendOp::NoBlending;

		_padding = 0;
    }
    
    static_assert(sizeof(RenderStateSet) == sizeof(uint64_t), "expecting StateSet to be 64 bits long");
    inline uint64 RenderStateSet::GetHash() const
    {
        return *(const uint64_t*)this;
    }

}}

