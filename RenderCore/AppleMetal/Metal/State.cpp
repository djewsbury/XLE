// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "State.h"
#include "DeviceContext.h"
#include "ObjectFactory.h"

#include <Metal/MTLSampler.h>
#include <Metal/MTLRenderCommandEncoder.h>

namespace RenderCore { namespace Metal_AppleMetal
{

    static MTLSamplerAddressMode AsMTLenum(AddressMode mode)
    {
        switch (mode) {
            case AddressMode::Wrap: return MTLSamplerAddressModeRepeat;
            case AddressMode::Mirror: return MTLSamplerAddressModeMirrorRepeat;
            case AddressMode::Clamp: return MTLSamplerAddressModeClampToEdge;
#if TARGET_OS_OSX
            case AddressMode::Border: return MTLSamplerAddressModeClampToBorderColor;
#endif
            default:
                assert(0);
                return MTLSamplerAddressModeRepeat;
        }
    }

    static MTLCompareFunction AsMTLenum(CompareOp comparison)
    {
        switch (comparison) {
            case CompareOp::Less:           return MTLCompareFunctionLess;
            case CompareOp::Equal:          return MTLCompareFunctionEqual;
            case CompareOp::LessEqual:      return MTLCompareFunctionLessEqual;
            case CompareOp::Greater:        return MTLCompareFunctionGreater;
            case CompareOp::NotEqual:       return MTLCompareFunctionNotEqual;
            case CompareOp::GreaterEqual:   return MTLCompareFunctionGreaterEqual;
            case CompareOp::Always:         return MTLCompareFunctionAlways;
            default:
            case CompareOp::Never:          return MTLCompareFunctionNever;
        }
    }

    SamplerState::SamplerState(
        ObjectFactory& factory, const SamplerDesc& desc)
    {
        _enableMipmaps = !(desc._flags & SamplerDescFlags::DisableMipmaps);

        OCPtr<MTLSamplerDescriptor> underlyingDesc = moveptr([[MTLSamplerDescriptor alloc] init]);

        underlyingDesc.get().rAddressMode = AsMTLenum(AddressMode::Clamp);
        underlyingDesc.get().sAddressMode = AsMTLenum(desc._addressU);
        underlyingDesc.get().tAddressMode = AsMTLenum(desc._addressV);

        MTLSamplerMinMagFilter minFilter;
        MTLSamplerMinMagFilter magFilter;
        MTLSamplerMipFilter mipFilter;

        switch (desc._filter) {
            case FilterMode::Bilinear:
            case FilterMode::ComparisonBilinear:
                minFilter = MTLSamplerMinMagFilterLinear;
                magFilter = MTLSamplerMinMagFilterLinear;
                mipFilter = MTLSamplerMipFilterNearest;
                break;

            case FilterMode::Trilinear:
            case FilterMode::Anisotropic:
                minFilter = MTLSamplerMinMagFilterLinear;
                magFilter = MTLSamplerMinMagFilterLinear;
                mipFilter = MTLSamplerMipFilterLinear;
                break;

            default:
            case FilterMode::Point:
                minFilter = MTLSamplerMinMagFilterNearest;
                magFilter = MTLSamplerMinMagFilterNearest;
                mipFilter = MTLSamplerMipFilterNearest;
                break;
        }

        underlyingDesc.get().minFilter = minFilter;
        underlyingDesc.get().magFilter = magFilter;
        underlyingDesc.get().mipFilter = mipFilter;

        underlyingDesc.get().compareFunction = AsMTLenum(desc._comparison);
        if (desc._filter != FilterMode::ComparisonBilinear) {
            underlyingDesc.get().compareFunction = MTLCompareFunctionNever;
        }

        if (!(factory.GetFeatureSet() & FeatureSet::Flags::SamplerComparisonFn)) {
            // Not all Metal feature sets allow you to define a framework-side sampler comparison function for a MTLSamplerState object.
            // All feature sets support shader-side sampler comparison functions, as described in the Metal Shading Language Guide.
            underlyingDesc.get().compareFunction = MTLCompareFunctionNever;
        }

        _underlyingSamplerMipmaps = factory.CreateSamplerState(underlyingDesc);

        underlyingDesc.get().mipFilter = MTLSamplerMipFilterNotMipmapped;
        _underlyingSamplerNoMipmaps = factory.CreateSamplerState(underlyingDesc);
    }

    SamplerState::SamplerState()
    {
        _enableMipmaps = false;
        _underlyingSamplerNoMipmaps = _underlyingSamplerMipmaps = GetObjectFactory().StandInSamplerState();
    }

    void SamplerState::Apply(GraphicsEncoder& encoder, unsigned samplerIndex, ShaderStage stage) const never_throws
    {
        id<MTLSamplerState> mtlSamplerState = nil;
        if (_enableMipmaps) {
            mtlSamplerState = _underlyingSamplerMipmaps.get();
        } else {
            mtlSamplerState = _underlyingSamplerNoMipmaps.get();
        }
        assert(mtlSamplerState);

        id<MTLRenderCommandEncoder> underlyingEncoder = encoder.GetUnderlying();
        if (stage == ShaderStage::Vertex) {
            [underlyingEncoder setVertexSamplerState:mtlSamplerState atIndex:(NSUInteger)samplerIndex];
        } else if (stage == ShaderStage::Pixel) {
            [underlyingEncoder setFragmentSamplerState:mtlSamplerState atIndex:(NSUInteger)samplerIndex];
        }
    }

}}
