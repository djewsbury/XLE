// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "ShaderPatchCollection.h"
#include "../StateDesc.h"
#include "../../Assets/AssetsCore.h"
#include "../../Assets/AssetUtils.h"
#include "../../Utility/ParameterBox.h"
#include "../../Utility/MemoryUtils.h"
#include <memory>
#include <vector>
#include <string>

namespace Assets 
{ 
    class DependencyValidation; class DirectorySearchRules; 
	class DependentFileState;
}
namespace Utility { class OutputStreamFormatter; }

namespace RenderCore { namespace Assets
{
    using MaterialGuid = uint64_t;
    
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
        unsigned    _wireframe : 1;
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
                DoubleSided = 1<<0, Wireframe = 1<<1, WriteMask = 1<<2, 
                BlendType = 1<<3, ForwardBlend = 1<<4, DepthBias = 1<<5 
            };
            typedef unsigned BitField;
        };
        Flag::BitField  _flag : 6;
        unsigned        _padding : 3;   // 8 + 15 + 32 + 5 = 60 bits... pad to 64 bits

        int             _depthBias;     // do we need all of the bits for this?

        uint64 GetHash() const;
        RenderStateSet();
    };

    #pragma pack(pop)

	RenderStateSet Merge(RenderStateSet underride, RenderStateSet override);

    /// <summary>Pre-resolved material settings</summary>
    /// Materials are a hierachical set of properties. Each RawMaterial
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
        ParameterBox	_resourceBindings;
        ParameterBox	_matParamBox;
        RenderStateSet	_stateSet;
        ParameterBox	_constants;
        
		ShaderPatchCollection _patchCollection;

        std::vector<std::string> _inherit;

		void					    MergeInto(RawMaterial& dest) const; 
		std::vector<std::string>	ResolveInherited(const ::Assets::DirectorySearchRules& searchRules) const;

		const ::Assets::DependencyValidation&	GetDependencyValidation() const { return _depVal; }
		const ::Assets::DirectorySearchRules&	GetDirectorySearchRules() const { return _searchRules; }

        void SerializeMethod(OutputStreamFormatter& formatter) const;
        
        RawMaterial();
        RawMaterial(
            InputStreamFormatter<utf8>& formatter, 
            const ::Assets::DirectorySearchRules&,
			const ::Assets::DependencyValidation& depVal);
        ~RawMaterial();

		static void ConstructToFuture(
			::Assets::FuturePtr<RawMaterial>&,
			StringSection<::Assets::ResChar> initializer);

    private:
        ::Assets::DependencyValidation _depVal;
		::Assets::DirectorySearchRules _searchRules;
    };

	class RawMatConfigurations
    {
    public:
        std::vector<std::basic_string<utf8>> _configurations;

		RawMatConfigurations(
			const ::Assets::Blob& locator,
			const ::Assets::DependencyValidation& depVal,
			StringSection<::Assets::ResChar> requestParameters);

        static const auto CompileProcessType = ConstHash64<'RawM', 'at'>::Value;

        auto GetDependencyValidation() const -> const ::Assets::DependencyValidation& { return _validationCallback; }
    protected:
        ::Assets::DependencyValidation _validationCallback;
    };

    class ResolvedMaterial
    {
    public:
        ParameterBox	_resourceBindings;
        ParameterBox	_matParamBox;
        RenderStateSet	_stateSet;
        ParameterBox	_constants;
        
		ShaderPatchCollection _patchCollection;
        std::vector<::Assets::DependentFileState> _depFileStates;

		const ::Assets::DependencyValidation&	GetDependencyValidation() const { return _depVal; }

        ResolvedMaterial();
        ~ResolvedMaterial();

        static void ConstructToFuture(
			::Assets::FuturePtr<ResolvedMaterial>&,
			StringSection<> initializer);
    private:
        ::Assets::DependencyValidation _depVal;
    };

    void ResolveMaterialFilename(
        ::Assets::ResChar resolvedFile[], unsigned resolvedFileCount,
        const ::Assets::DirectorySearchRules& searchRules, StringSection<char> baseMatName);

	MaterialGuid MakeMaterialGuid(StringSection<utf8> name);

    inline RenderStateSet::RenderStateSet()
    {
        _doubleSided = false;
        _wireframe = false;
        _writeMask = 0xf;
        _blendType = BlendType::Basic;
        _depthBias = 0;
        _flag = 0;
        
        _forwardBlendSrc = Blend(0); // Metal::Blend::One;
        _forwardBlendDst = Blend(0); // Metal::Blend::Zero;
        _forwardBlendOp = BlendOp(0); // Metal::BlendOp::NoBlending;

		_padding = 0;
    }
    
    inline uint64 RenderStateSet::GetHash() const
    {
        static_assert(sizeof(*this) == sizeof(uint64), "expecting StateSet to be 64 bits long");
        return *(const uint64*)this;
    }

}}

