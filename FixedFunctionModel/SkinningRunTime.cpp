// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ModelRendererInternal.h"
#include "SharedStateSet.h"
#include "../RenderCore/Assets/SkeletonScaffoldInternal.h"
#include "../RenderCore/Assets/ModelImmutableData.h"
#include "../RenderCore/Assets/RawAnimationCurve.h"
#include "../RenderCore/Techniques/DeferredShaderResource.h"
#include "../RenderCore/RenderUtils.h"
#include "../RenderCore/Types.h"
#include "../RenderCore/Format.h"
#include "../RenderCore/BufferView.h"

#include "../RenderCore/IDevice.h"
#include "../RenderCore/Metal/Shader.h"
#include "../RenderCore/Metal/InputLayout.h"
#include "../RenderCore/Metal/DeviceContext.h"
#include "../RenderCore/Metal/TextureView.h"
#include "../RenderCore/Metal/Resource.h"
#include "../RenderCore/Metal/ObjectFactory.h"

#include "../RenderCore/Techniques/Techniques.h"
#include "../RenderCore/Techniques/TechniqueUtils.h"
#include "../RenderCore/Techniques/ParsingContext.h"
#include "../RenderCore/Techniques/CommonResources.h"
#include "../Assets/Assets.h"
#include "../ConsoleRig/ResourceBox.h"
#include "../ConsoleRig/Console.h"

#if GFXAPI_TARGET == GFXAPI_DX11
	#include "../RenderCore/DX11/Metal/DX11Utils.h"
	#include "../RenderCore/DX11/Metal/IncludeDX11.h"
    #include "../RenderCore/DX11/IDeviceDX11.h"
#endif

#pragma warning(disable:4127)       // conditional expression is constant
#pragma warning(disable:4505)		// unreferenced local function has been removed

namespace FixedFunctionModel
{
	using namespace RenderCore;

	unsigned ModelRenderer::PimplWithSkinning::BuildPostSkinInputAssembly(
		InputElementDesc dst[], unsigned dstCount,
		const RenderCore::Assets::BoundSkinnedGeometry& scaffoldGeo)
	{
		assert(0);
		return 0;
	}

	auto ModelRenderer::PimplWithSkinning::BuildAnimBinding(
		const RenderCore::Assets::ModelCommandStream::GeoCall& geoInst,
		const RenderCore::Assets::BoundSkinnedGeometry& geo,
		SharedStateSet& sharedStateSet,
		const uint64 textureBindPoints[], unsigned textureBindPointsCnt) -> SkinnedMeshAnimBinding
	{
		assert(0);
		return {};
	}

	void ModelRenderer::PimplWithSkinning::BuildSkinnedBuffer(
		Metal::DeviceContext&       context,
		const SkinnedMesh&          mesh,
		const SkinnedMeshAnimBinding& preparedAnimBinding,
		const Float4x4              transformationMachineResult[],
		const RenderCore::Assets::SkeletonBinding&      skeletonBinding,
		IResource&					outputResult,
		unsigned                    outputOffset) const
	{
		assert(0);
	}

	static void DeletePreparedAnimation(PreparedAnimation* ptr) { delete ptr; }

	auto ModelRenderer::CreatePreparedAnimation() const -> std::unique_ptr<PreparedAnimation, void(*)(PreparedAnimation*)>
	{
		assert(0);
		auto result = std::unique_ptr<PreparedAnimation, void(*)(PreparedAnimation*)>(
			nullptr, &DeletePreparedAnimation);
		return result;
	}

	void ModelRenderer::PrepareAnimation(
		IThreadContext& context, PreparedAnimation& result,
		const RenderCore::Assets::SkeletonBinding& skeletonBinding) const
	{
		assert(0);
	}

	bool ModelRenderer::CanDoPrepareAnimation(IThreadContext& context)
	{
		return false;
	}
}

#if 0

namespace RenderCore { namespace Assets
{
    class HashedInputAssemblies
    {
    public:
        class Desc
        {
        public:
            uint64 _hash;
            Desc(uint64 hash) : _hash(hash) {}
        };

        std::vector<InputElementDesc> _elements;
        HashedInputAssemblies(const Desc& ) {}
    };

    class SkinningBindingBox
    {
    public:
        struct BindingType { enum Enum { tbuffer, cbuffer }; };
        class Desc
        {
        public:
            BindingType::Enum _bindingType;
            uint64 _iaHash;
            unsigned _vertexStride;
            Desc(BindingType::Enum bindingType, uint64 iaHash, unsigned vertexStride) : _bindingType(bindingType), _iaHash(iaHash), _vertexStride(vertexStride) {}
        };

        SkinningBindingBox(const Desc& desc);

        Metal::GeometryShader       _geometryShader;
        Metal::BoundUniforms        _boundUniforms;
        Metal::BoundInputLayout     _boundInputLayout;
        BindingType::Enum           _bindingType;

        Metal::VertexShader       _skinningVertexShaderP4;
        Metal::VertexShader       _skinningVertexShaderP2;
        Metal::VertexShader       _skinningVertexShaderP1;
        Metal::VertexShader       _skinningVertexShaderP0;

        const ::Assets::DependencyValidation& GetDependencyValidation() const   { return _validationCallback; }
        ::Assets::DependencyValidation _validationCallback;
    };

    SkinningBindingBox::SkinningBindingBox(const Desc& desc)
    {
        auto& ai = ConsoleRig::FindCachedBox<HashedInputAssemblies>(HashedInputAssemblies::Desc(desc._iaHash));
        const auto& skinningInputLayout = ai._elements;

        std::vector<InputElementDesc> skinningOutputLayout;
        for (auto i=skinningInputLayout.cbegin(); i!=skinningInputLayout.cend(); ++i) {
            if (i->_inputSlot == 0) skinningOutputLayout.push_back(*i);
        }

            ///////////////////////////////////////////////

        const char* skinningVertexShaderSourceP4    = (desc._bindingType==BindingType::cbuffer) ? (SKINNING_VERTEX_HLSL ":P4:" VS_DefShaderModel) : (SKINNING_VIA_TBUFFER ":P4:" VS_DefShaderModel);
        const char* skinningVertexShaderSourceP2    = (desc._bindingType==BindingType::cbuffer) ? (SKINNING_VERTEX_HLSL ":P2:" VS_DefShaderModel) : (SKINNING_VIA_TBUFFER ":P2:" VS_DefShaderModel);
        const char* skinningVertexShaderSourceP1    = (desc._bindingType==BindingType::cbuffer) ? (SKINNING_VERTEX_HLSL ":P1:" VS_DefShaderModel) : (SKINNING_VIA_TBUFFER ":P1:" VS_DefShaderModel);
        const char* skinningVertexShaderSourceP0    = (desc._bindingType==BindingType::cbuffer) ? (SKINNING_VERTEX_HLSL ":P0:" VS_DefShaderModel) : (SKINNING_VIA_TBUFFER ":P0:" VS_DefShaderModel);
        const char* geometryShaderSourceP           = SKINNING_GEO_HLSL ":P:" GS_DefShaderModel;

        const char* skinningVertexShaderSourcePN4   = (desc._bindingType==BindingType::cbuffer) ? (SKINNING_VERTEX_HLSL ":PN4:" VS_DefShaderModel) : (SKINNING_VIA_TBUFFER ":PN4:" VS_DefShaderModel);
        const char* skinningVertexShaderSourcePN2   = (desc._bindingType==BindingType::cbuffer) ? (SKINNING_VERTEX_HLSL ":PN2:" VS_DefShaderModel) : (SKINNING_VIA_TBUFFER ":PN2:" VS_DefShaderModel);
        const char* skinningVertexShaderSourcePN1   = (desc._bindingType==BindingType::cbuffer) ? (SKINNING_VERTEX_HLSL ":PN1:" VS_DefShaderModel) : (SKINNING_VIA_TBUFFER ":PN1:" VS_DefShaderModel);
        const char* skinningVertexShaderSourcePN0   = (desc._bindingType==BindingType::cbuffer) ? (SKINNING_VERTEX_HLSL ":PN0:" VS_DefShaderModel) : (SKINNING_VIA_TBUFFER ":PN0:" VS_DefShaderModel);
        const char* geometryShaderSourcePN          = SKINNING_GEO_HLSL ":PN:" GS_DefShaderModel;

        const bool hasNormals = !!HasElement(MakeIteratorRange(skinningOutputLayout), "NORMAL");

            //  outputs from skinning are always float3's currently. So, we can get the vertex stride
            //  just from the outputs count
        unsigned outputVertexStride = unsigned(skinningOutputLayout.size() * 3 * sizeof(float));

        using namespace Metal;
        _geometryShader = GeometryShader(
            hasNormals ? geometryShaderSourcePN : geometryShaderSourceP, 
            GeometryShader::StreamOutputInitializers(
                AsPointer(skinningOutputLayout.begin()), unsigned(skinningOutputLayout.size()),
                &outputVertexStride, 1));

        auto& vsByteCodeP4 = ::Assets::Legacy::GetAssetDep<CompiledShaderByteCode>(hasNormals ? skinningVertexShaderSourcePN4 : skinningVertexShaderSourceP4);
        auto& vsByteCodeP2 = ::Assets::Legacy::GetAssetDep<CompiledShaderByteCode>(hasNormals ? skinningVertexShaderSourcePN2 : skinningVertexShaderSourceP2);
        auto& vsByteCodeP1 = ::Assets::Legacy::GetAssetDep<CompiledShaderByteCode>(hasNormals ? skinningVertexShaderSourcePN1 : skinningVertexShaderSourceP1);
        auto& vsByteCodeP0 = ::Assets::Legacy::GetAssetDep<CompiledShaderByteCode>(hasNormals ? skinningVertexShaderSourcePN0 : skinningVertexShaderSourceP0);

        _skinningVertexShaderP4 = Metal::VertexShader(vsByteCodeP4);
        _skinningVertexShaderP2 = Metal::VertexShader(vsByteCodeP2);
        _skinningVertexShaderP1 = Metal::VertexShader(vsByteCodeP1);
        _skinningVertexShaderP0 = Metal::VertexShader(vsByteCodeP0);

        BoundUniforms boundUniforms(vsByteCodeP4);
        const auto jointTransformsHash = Hash64("JointTransforms");
        if (desc._bindingType == BindingType::cbuffer) {
            boundUniforms.BindConstantBuffer(jointTransformsHash, 0, 1);
        } else {
            boundUniforms.BindShaderResource(jointTransformsHash, 0, 1);
        }

        _boundInputLayout = BoundInputLayout(
            MakeIteratorRange(skinningInputLayout),
            vsByteCodeP4);

        /////////////////////////////////////////////////////////////////////////////

        auto validationCallback = std::make_shared<::Assets::DependencyValidation>();
        validationCallback.RegisterDependency(vsByteCodeP4.GetDependencyValidation());
        validationCallback.RegisterDependency(vsByteCodeP2.GetDependencyValidation());
        validationCallback.RegisterDependency(vsByteCodeP1.GetDependencyValidation());
        validationCallback.RegisterDependency(vsByteCodeP0.GetDependencyValidation());

        _validationCallback = std::move(validationCallback);
        _boundUniforms = std::move(boundUniforms);
    }

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    void ModelRenderer::PimplWithSkinning::StartBuildingSkinning(
        Metal::DeviceContext& context, SkinningBindingBox& bindingBox) const
    {
        #if GFXAPI_TARGET == GFXAPI_DX11        // platformtemp
            context.Bind(bindingBox._geometryShader);
        #endif

        context.Unbind<Metal::PixelShader>();
        context.Bind(Topology::PointList);
    }

    void ModelRenderer::PimplWithSkinning::EndBuildingSkinning(Metal::DeviceContext& context) const
    {
		#if GFXAPI_TARGET == GFXAPI_DX11
			context.GetUnderlying()->SOSetTargets(0, nullptr, nullptr);
		#endif
        context.Unbind<Metal::GeometryShader>();
    }

    static void SetSkinningShader(Metal::DeviceContext& context, SkinningBindingBox& bindingBox, unsigned materialIndexValue)
    {
        #if GFXAPI_TARGET == GFXAPI_DX11        // platformtemp
            if (materialIndexValue == 4)         context.Bind(bindingBox._skinningVertexShaderP4);
            else if (materialIndexValue == 2)    context.Bind(bindingBox._skinningVertexShaderP2);
            else if (materialIndexValue == 1)    context.Bind(bindingBox._skinningVertexShaderP1);
            else {
                assert(materialIndexValue == 0);
                context.Bind(bindingBox._skinningVertexShaderP0);
            }
        #endif
    }

    static void WriteJointTransforms(   Float3x4 destination[], size_t destinationCount,
                                        const BoundSkinnedGeometry& controller,
                                        const Float4x4              transformationMachineResult[],
                                        const SkeletonBinding&      skeletonBinding)
    {
        for (unsigned c=0; c<std::min(controller._jointMatrixCount, destinationCount); ++c) {
            auto jointMatrixIndex = controller._jointMatrices[c];
            auto transMachineOutput = skeletonBinding.ModelJointToMachineOutput(jointMatrixIndex);
            if (transMachineOutput != ~unsigned(0x0)) {
                Float4x4 inverseBindByBindShapeMatrix = controller._inverseBindByBindShapeMatrices[c];
                Float4x4 finalMatrix = 
                    Combine(    inverseBindByBindShapeMatrix, 
                                transformationMachineResult[transMachineOutput]);
                destination[c] = Truncate(finalMatrix);
            } else {
                destination[c] = Identity<Float3x4>();
            }
        }
    }

        //
        //      Simple method for managing tbuffer textures...
        //      Keep multiple textures for each frame. Use a single
        //      texture for each
        //
        //      Also consider using a single large circular buffer
        //      and using the discard/no-overwrite pattern
        //
    class TBufferTemporaryTexture
    {
    public:
        RenderCore::IResourcePtr		_resource;
        RenderCore::IResourcePtr		_stagingResource;
        Metal::ShaderResourceView       _view;
        size_t                          _size;
        unsigned                        _lastAllocatedFrame;
        unsigned                        _shaderOffsetValue;

        TBufferTemporaryTexture();
        TBufferTemporaryTexture(const void* sourceData, size_t bufferSize, bool dynamicResource=false);
        TBufferTemporaryTexture(const TBufferTemporaryTexture& cloneFrom);
        TBufferTemporaryTexture(TBufferTemporaryTexture&& moveFrom);
        TBufferTemporaryTexture& operator=(const TBufferTemporaryTexture& cloneFrom);
        TBufferTemporaryTexture& operator=(TBufferTemporaryTexture&& moveFrom);
    };

    static unsigned globalCircularBuffer_Size = 200 * sizeof(Float3x4);
    static unsigned globalCircularBuffer_WritingPosition = 0;
    TBufferTemporaryTexture globalCircularBuffer;

    static TBufferTemporaryTexture AllocateExistingTBufferTemporaryTexture(size_t bufferSize);
    static TBufferTemporaryTexture AllocateNewTBufferTemporaryTexture(const void* bufferData, size_t bufferSize);
    static void PushTBufferTemporaryTexture(Metal::DeviceContext* context, TBufferTemporaryTexture& tex);

    void ModelRenderer::PimplWithSkinning::InitialiseSkinningVertexAssembly(
        uint64 inputAssemblyHash,
        const BoundSkinnedGeometry& scaffoldGeo)
    {
        auto& iaBox = ConsoleRig::FindCachedBox<HashedInputAssemblies>(HashedInputAssemblies::Desc(inputAssemblyHash));
        if (iaBox._elements.empty()) {
                //  This hashed input assembly will contain both the full input assembly 
                //  for preparing skinning (with the animated elements in slot 0, and 
                //  the skeleton binding info in slot 1)
            InputElementDesc inputDesc[12];
            unsigned vertexElementCount = BuildLowLevelInputAssembly(
                inputDesc, dimof(inputDesc),
                scaffoldGeo._animatedVertexElements._ia._elements);

            vertexElementCount += BuildLowLevelInputAssembly(
                &inputDesc[vertexElementCount], dimof(inputDesc) - vertexElementCount,
                scaffoldGeo._skeletonBinding._ia._elements, 1);

            iaBox._elements.insert(iaBox._elements.begin(), inputDesc, &inputDesc[vertexElementCount]);
        }
    }

    static void ApplyConversionFromStreamOutput(VertexElement dst[], const VertexElement src[], unsigned count)
    {
        using namespace Metal;
        for (unsigned c=0; c<count; ++c) {
            XlCopyMemory(&dst[c], &src[c], sizeof(VertexElement));
            dst[c]._alignedByteOffset = ~unsigned(0x0);   // have to reset all of the offsets (because elements might change size)

                // change 16 bit precision formats into 32 bit
                //  (also change 4 dimensional vectors into 3 dimension vectors. Since there
                //  isn't a 4D float16 format, most of the time the 4D format is intended to
                //  be a 3D format. This will break if the format is intended to truly be
                //  4D).
            if (    GetComponentType(dst[c]._nativeFormat) == FormatComponentType::Float
                &&  GetComponentPrecision(dst[c]._nativeFormat) == 16) {

                auto components = GetComponents(dst[c]._nativeFormat);
                if (components == FormatComponents::RGBAlpha) {
                    components = FormatComponents::RGB;
                }
                auto recastFormat = FindFormat(
                    FormatCompressionType::None, 
                    components, FormatComponentType::Float,
                    32);
                if (recastFormat != Format::Unknown) {
                    dst[c]._nativeFormat = recastFormat;
                }
            }
        }
    }

    unsigned ModelRenderer::PimplWithSkinning::BuildPostSkinInputAssembly(
        InputElementDesc dst[], unsigned dstCount,
        const BoundSkinnedGeometry& scaffoldGeo)
    {
        // build the native input assembly that should be used when using
        // prepared animation

        VertexElement convertedElements[16];
        unsigned convertedCount = 
            std::min( dstCount, 
                (unsigned)std::min(dimof(convertedElements), 
                    scaffoldGeo._animatedVertexElements._ia._elements.size()));
        ApplyConversionFromStreamOutput(
            convertedElements, 
            AsPointer(scaffoldGeo._animatedVertexElements._ia._elements.cbegin()), convertedCount);

        unsigned eleCount = BuildLowLevelInputAssembly(
            dst, dstCount, convertedElements, convertedCount);

            // (add the unanimated part)
        eleCount += BuildLowLevelInputAssembly(
            &dst[eleCount], dstCount - eleCount, 
            scaffoldGeo._vb._ia._elements, 1);

        return eleCount;
    }

    auto ModelRenderer::PimplWithSkinning::BuildAnimBinding(
        const ModelCommandStream::GeoCall& geoInst,
        const BoundSkinnedGeometry& geo,
        SharedStateSet& sharedStateSet,
        const uint64 textureBindPoints[], unsigned textureBindPointsCnt) -> SkinnedMeshAnimBinding
    {
            //  Build technique interfaces and binding information for the prepared
            //  animation case. This is extra information attached to the skinned mesh
            //  object that is used when we render the mesh using prepared animation

        SkinnedMeshAnimBinding result;
        result._iaAnimationHash = 0;
        result._scaffold = &geo;
        result._techniqueInterface = ~unsigned(0x0);
        result._vertexStride = 0;

        InputElementDesc inputDescForRender[12];
        auto vertexElementForRenderCount = 
            PimplWithSkinning::BuildPostSkinInputAssembly(
                inputDescForRender, dimof(inputDescForRender), geo);

        result._techniqueInterface = 
            sharedStateSet.InsertTechniqueInterface(
                inputDescForRender, vertexElementForRenderCount, 
                textureBindPoints, textureBindPointsCnt);

        result._vertexStride = 
            CalculateVertexStrideForSlot(MakeIteratorRange(inputDescForRender, &inputDescForRender[vertexElementForRenderCount]), 0u);

        result._iaAnimationHash = geo._animatedVertexElements._ia.BuildHash() ^ geo._skeletonBinding._ia.BuildHash();
        InitialiseSkinningVertexAssembly(result._iaAnimationHash, geo);

        return result;
    }

    void ModelRenderer::PimplWithSkinning::BuildSkinnedBuffer(  
                Metal::DeviceContext&       context,
                const SkinnedMesh&          mesh,
                const SkinnedMeshAnimBinding& preparedAnimBinding, 
                const Float4x4              transformationMachineResult[],
                const SkeletonBinding&      skeletonBinding,
                IResource&					outputResult,
                unsigned                    outputOffset) const
    {
#if GFXAPI_TARGET == GFXAPI_DX11
        using namespace Metal;
        const auto bindingType =
            Tweakable("SkeletonUpload_ViaTBuffer", false)
                ? SkinningBindingBox::BindingType::tbuffer
                : SkinningBindingBox::BindingType::cbuffer;

            // fill in the "HashedInputAssemblies" box if necessary
        const auto& scaffold = *preparedAnimBinding._scaffold;
        auto& bindingBox = ConsoleRig::FindCachedBoxDep2<SkinningBindingBox>(
            bindingType, preparedAnimBinding._iaAnimationHash, mesh._extraVbStride[SkinnedMesh::VertexStreams::AnimatedGeo]);

            ///////////////////////////////////////////////

        #if defined(_DEBUG) ///////////////////////////////////////////////////////////////////
                    //
                    //  we get warning messages from D3D11 if the device debug flag is on, 
                    //  and the buffer size is not right
                    //
            const size_t packetCount = (bindingType==SkinningBindingBox::BindingType::cbuffer)?200:scaffold._jointMatrixCount;     
        #else /////////////////////////////////////////////////////////////////////////////////
            const size_t packetCount = scaffold._jointMatrixCount;
        #endif ////////////////////////////////////////////////////////////////////////////////

        ConstantBufferPacket constantBufferPackets[1];

        TBufferTemporaryTexture tbufferTexture;
        if (bindingType == SkinningBindingBox::BindingType::cbuffer) {

            constantBufferPackets[0] = RenderCore::MakeSharedPktSize(sizeof(Float3x4) * packetCount);
            Float3x4* jointTransforms = (Float3x4*)constantBufferPackets[0].begin();
            WriteJointTransforms(jointTransforms, packetCount, scaffold, transformationMachineResult, skeletonBinding);

        } else {

                //
                //      Write via tbuffer. We need to allocate a texture,
                //      write in the new transform data, and bind it to
                //      the pipeline.
                //
                //      The way in which we create and upload to the texture
                //      may have some effect on this... We probably want to avoid 
                //      copying via the command buffer -- and instead just write
                //      to reserved GPU space.
                //

            const bool useCircularBuffer = true;
            if (useCircularBuffer) {

                    //
                    //  Use a single buffer with many discard maps... This
                    //  seems to be producing more efficient GPU side results!
                    //
                if (!globalCircularBuffer._size) {
                    globalCircularBuffer = TBufferTemporaryTexture(nullptr, globalCircularBuffer_Size, true);
                }

                unsigned currentWritingPosition = globalCircularBuffer_WritingPosition;
                D3D11_MAP mapType = D3D11_MAP_WRITE_DISCARD; // D3D11_MAP_WRITE_NO_OVERWRITE;
                globalCircularBuffer_WritingPosition += unsigned(packetCount * sizeof(Float3x4));
                if (1) { // globalCircularBuffer_WritingPosition > globalCircularBuffer_Size) {
                        // Wrap around moment... discard and start writing from top
                    currentWritingPosition = 0;
                    mapType = D3D11_MAP_WRITE_DISCARD;
                    globalCircularBuffer_WritingPosition = unsigned(packetCount * sizeof(Float3x4));
                }

                    //
                    //      Note we always have to map the entire buffer here... But most of the time
                    //      we're only writing to a small subset of the buffer. The means extra redundant
                    //      dirty data will be copied to the GPU... Is there any way to do this better?
                    //      We can't use NO_OVERWRITE maps here, because that is only allows for
                    //      index and vertex buffers.
                    //
                D3D11_MAPPED_SUBRESOURCE mapping;
                HRESULT hresult = context.GetUnderlying()->Map((ID3D::Resource*)globalCircularBuffer._resource.get(), 0, mapType, 0, &mapping);
                if (SUCCEEDED(hresult) && mapping.pData) {
                    WriteJointTransforms(
                        (Float3x4*)PtrAdd(mapping.pData, currentWritingPosition), packetCount, 
                        scaffold, transformationMachineResult, skeletonBinding);
                    context.GetUnderlying()->Unmap((ID3D::Resource*)globalCircularBuffer._resource.get(), 0);
                }

                tbufferTexture = globalCircularBuffer;
                tbufferTexture._shaderOffsetValue = currentWritingPosition / sizeof(Float3x4);

                // unsigned* shaderOffsetValue = (unsigned*)AsPointer(constantBufferPackets[0]->begin());
                // *shaderOffsetValue = tbufferTexture._shaderOffsetValue;

            } else {
                tbufferTexture = AllocateExistingTBufferTemporaryTexture(packetCount * sizeof(Float3x4));
                if (tbufferTexture._stagingResource) {

                    D3D11_MAPPED_SUBRESOURCE mapping;
                        //
                        //      Also consider "no-overwrite" map access. This can only be used for
                        //      vertex buffers, however... which means binding a vertex buffer to a tbuffer
                        //      shader input.
                        //
                    HRESULT hresult = context.GetUnderlying()->Map((ID3D::Resource*)tbufferTexture._stagingResource.get(), 0, D3D11_MAP_WRITE, 0, &mapping);
                    if (SUCCEEDED(hresult) && mapping.pData) {
                        WriteJointTransforms((Float3x4*)mapping.pData, packetCount, scaffold, transformationMachineResult, skeletonBinding);
                        context.GetUnderlying()->Unmap((ID3D::Resource*)tbufferTexture._stagingResource.get(), 0);
                    }
                    PushTBufferTemporaryTexture(&context, tbufferTexture);

                } else {

                    auto buffer = std::make_unique<Float3x4[]>(packetCount);
                    WriteJointTransforms(buffer.get(), packetCount, scaffold, transformationMachineResult, skeletonBinding);
                    tbufferTexture = AllocateNewTBufferTemporaryTexture(buffer.get(), packetCount * sizeof(Float3x4));

                }
            }

        }

            ///////////////////////////////////////////////

		auto* d3dRes = (Metal_DX11::Resource*)outputResult.QueryInterface(typeid(Metal_DX11::Resource).hash_code());
		assert(d3dRes);

        auto* soOutputBuffer = Metal_DX11::QueryInterfaceCast<ID3D::Buffer>(d3dRes->_underlying).get();
        context.GetUnderlying()->SOSetTargets(1, &soOutputBuffer, &outputOffset);
        {
            const Metal::ShaderResourceView* temp[] = { tbufferTexture._view.GetUnderlying() ? &tbufferTexture._view : nullptr };
            bindingBox._boundUniforms.Apply(
                context, 
                Metal::UniformsStream(),
                Metal::UniformsStream(constantBufferPackets, nullptr, dimof(constantBufferPackets), temp, 1));
        }
        StartBuildingSkinning(context, bindingBox);

            // bind the mesh streams -- we need animated geometry and skeleton binding
        auto animGeo = SkinnedMesh::VertexStreams::AnimatedGeo;
        auto skelBind = SkinnedMesh::VertexStreams::SkeletonBinding;

		VertexBufferView vbs[] = {
			{ _vertexBuffer.get(), mesh._extraVbOffset[animGeo] },		// _extraVbStride[animGeo]
			{ _vertexBuffer.get(), mesh._extraVbOffset[skelBind] }		// _extraVbStride[skelBind]
		};
		bindingBox._boundInputLayout.Apply(context, MakeIteratorRange(vbs));


            //
            //      (   vertex shader signatures are all the same -- so we can keep the 
            //          same input layout and constant buffer assignments   )
            //

        for (unsigned di=0; di<scaffold._preskinningDrawCallCount; ++di) {
            auto& d = scaffold._preskinningDrawCalls[di];
            SetSkinningShader(context, bindingBox, d._subMaterialIndex);
            context.Draw(d._indexCount, d._firstVertex);
        }
#endif
    }

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    static void DeletePreparedAnimation(PreparedAnimation* ptr) { delete ptr; }

    auto ModelRenderer::CreatePreparedAnimation() const -> std::unique_ptr<PreparedAnimation, void(*)(PreparedAnimation*)>
    {
            //  We need to allocate a vertex buffer that can contain the animated vertex data
            //  for this whole mesh. 
        unsigned vbSize = 0;
        std::vector<unsigned> offsets;
        offsets.reserve(_pimpl->_skinnedMeshes.size());

        auto b=_pimpl->_skinnedBindings.cbegin();
        for (auto m=_pimpl->_skinnedMeshes.cbegin(); m!=_pimpl->_skinnedMeshes.cend(); ++m, ++b) {
            offsets.push_back(vbSize);

            const auto stream = PimplWithSkinning::SkinnedMesh::VertexStreams::AnimatedGeo;
            unsigned size =     // (size post conversion might not be the same as the input data)
                m->_vertexCount[stream] * b->_vertexStride;
            vbSize += size;
        }

        auto result = std::unique_ptr<PreparedAnimation, void(*)(PreparedAnimation*)>(
            new PreparedAnimation, &DeletePreparedAnimation);
        result->_skinningBuffer = Metal::CreateResource(
			Metal::GetObjectFactory(),
			CreateDesc(
				BindFlag::VertexBuffer | BindFlag::StreamOutput, 0, GPUAccess::Read|GPUAccess::Write,
				LinearBufferDesc::Create(vbSize),
				"SkinningBuffer"));
        result->_vbOffsets = std::move(offsets);
        return result;
    }

    void ModelRenderer::PrepareAnimation(
        IThreadContext& context, PreparedAnimation& result, 
        const SkeletonBinding& skeletonBinding) const
    {
        auto metalContext = RenderCore::Metal::DeviceContext::Get(context);
        for (size_t i=0; i<_pimpl->_skinnedMeshes.size(); ++i) {
            _pimpl->BuildSkinnedBuffer(
                *metalContext, 
                _pimpl->_skinnedMeshes[i], 
                _pimpl->_skinnedBindings[i],
                result._finalMatrices.get(), skeletonBinding, 
                *result._skinningBuffer, result._vbOffsets[i]);
        }

        _pimpl->EndBuildingSkinning(*metalContext);
    }

	#if GFXAPI_TARGET == GFXAPI_DX11
		static intrusive_ptr<ID3D::Device> ExtractDevice(RenderCore::Metal::DeviceContext* context)
		{
			ID3D::Device* tempPtr;
			context->GetUnderlying()->GetDevice(&tempPtr);
			return moveptr(tempPtr);
		}
	#endif

    bool ModelRenderer::CanDoPrepareAnimation(IThreadContext& context)
    {
		#if GFXAPI_TARGET == GFXAPI_DX11
            auto* contextD3D = (IThreadContextDX11*)context.QueryInterface(typeid(IThreadContextDX11).hash_code());
            if (contextD3D) {
			    auto featureLevel = contextD3D->GetUnderlyingDevice()->GetFeatureLevel();
			    return (featureLevel >= D3D_FEATURE_LEVEL_10_0);
            }
		#endif
        return false;
    }

    PreparedAnimation::PreparedAnimation() 
    : _finalMatrixCount(0)
    {
    }

    PreparedAnimation::PreparedAnimation(PreparedAnimation&& moveFrom) never_throws
    : _finalMatrices(std::move(moveFrom._finalMatrices))
    , _finalMatrixCount(moveFrom._finalMatrixCount)
    , _skinningBuffer(std::move(moveFrom._skinningBuffer))
    , _vbOffsets(std::move(moveFrom._vbOffsets))
    {}

    PreparedAnimation& PreparedAnimation::operator=(PreparedAnimation&& moveFrom) never_throws
    {
        _finalMatrices = std::move(moveFrom._finalMatrices);
        _finalMatrixCount = moveFrom._finalMatrixCount;
        _skinningBuffer = std::move(moveFrom._skinningBuffer);
        _vbOffsets = std::move(moveFrom._vbOffsets);
        return *this;
    }

    MeshToModel::MeshToModel(
        const PreparedAnimation& preparedAnim,
        const SkeletonBinding* binding)
    : MeshToModel(preparedAnim._finalMatrices.get(), preparedAnim._finalMatrixCount, binding)
    {
    }

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    class SkinPrepareMachine::Pimpl
    {
    public:
        std::unique_ptr<AnimationSetBinding> _animationSetBinding;
        std::unique_ptr<SkeletonBinding> _skeletonBinding;
        const AnimationSetScaffold* _animationSetScaffold;
        const SkeletonMachine* _transMachine;
    };

    void SkinPrepareMachine::PrepareAnimation(
        IThreadContext& context, 
        PreparedAnimation& state,
        const AnimationState& animState) const
    {
        auto& skeleton = *_pimpl->_transMachine;
            
        state._finalMatrixCount = skeleton.GetOutputMatrixCount();
        state._finalMatrices = std::make_unique<Float4x4[]>(state._finalMatrixCount);
        if (_pimpl->_animationSetScaffold && !Tweakable("AnimBasePose", false)) {
            auto& animSet = _pimpl->_animationSetScaffold->ImmutableData();
            auto params = animSet._animationSet.BuildTransformationParameterSet(
                animState, 
                skeleton, *_pimpl->_animationSetBinding, 
                animSet._curves, animSet._curvesCount);
            
            skeleton.GenerateOutputTransforms(state._finalMatrices.get(), state._finalMatrixCount, &params);
        } else {
            skeleton.GenerateOutputTransforms(state._finalMatrices.get(), state._finalMatrixCount, &skeleton.GetDefaultParameters());
        }
    }

    const SkeletonBinding& SkinPrepareMachine::GetSkeletonBinding() const
    {
        return *_pimpl->_skeletonBinding;
    }

    unsigned SkinPrepareMachine::GetSkeletonOutputCount() const
    {
        return _pimpl->_transMachine->GetOutputMatrixCount();
    }

    SkinPrepareMachine::SkinPrepareMachine(const ModelScaffold& skinScaffold, const AnimationSetScaffold& animationScaffold, const SkeletonScaffold& skeletonScaffold)
    {
        auto pimpl = std::make_unique<Pimpl>();
        pimpl->_animationSetBinding = std::make_unique<AnimationSetBinding>(
            animationScaffold.ImmutableData()._animationSet.GetOutputInterface(), 
            skeletonScaffold.GetTransformationMachine().GetInputInterface());
        pimpl->_skeletonBinding = std::make_unique<SkeletonBinding>(
            skeletonScaffold.GetTransformationMachine().GetOutputInterface(), 
            skinScaffold.CommandStream().GetInputInterface());
        pimpl->_animationSetScaffold = &animationScaffold;
        pimpl->_transMachine = &skeletonScaffold.GetTransformationMachine();
        _pimpl = std::move(pimpl);
    }

    SkinPrepareMachine::SkinPrepareMachine(const ModelScaffold& skinScaffold, const SkeletonMachine& transMachine)
    {
        auto pimpl = std::make_unique<Pimpl>();
        pimpl->_skeletonBinding = std::make_unique<SkeletonBinding>(
            transMachine.GetOutputInterface(), 
            skinScaffold.CommandStream().GetInputInterface());
        pimpl->_animationSetScaffold = nullptr;
        pimpl->_transMachine = &transMachine;
        _pimpl = std::move(pimpl);
    }

    SkinPrepareMachine::~SkinPrepareMachine()
    {}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    class Vertex_PC
    {
    public:
        Float3      _position;
        unsigned    _color;
    };

    static void RenderSkeleton_DebugIterator(const Float4x4& parent, const Float4x4& child, const void* userData)
    {
        Vertex_PC start;   start._position   = ExtractTranslation(parent);   start._color = 0xffff7f7f;
        Vertex_PC end;       end._position   = ExtractTranslation(child);      end._color = 0xffffffff;
        ((std::vector<Vertex_PC>*)userData)->push_back(start);
        ((std::vector<Vertex_PC>*)userData)->push_back(end);
    }

    void    SkinPrepareMachine::RenderSkeleton( 
        IThreadContext& context, 
        Techniques::ParsingContext& parserContext, 
        const AnimationState& animState, const Float4x4& localToWorld)
    {
        using namespace RenderCore::Metal;

        InputElementDesc vertexInputLayout[] = {
            InputElementDesc( "POSITION",   0, Format::R32G32B32_FLOAT ),
            InputElementDesc( "COLOR",      0, Format::R8G8B8A8_UNORM )
        };

            //      Setup basic state --- blah, blah, blah..., 

        ConstantBufferPacket constantBufferPackets[1];
        constantBufferPackets[0] = Techniques::MakeLocalTransformPacket(
            localToWorld, 
            ExtractTranslation(parserContext.GetProjectionDesc()._cameraToWorld));

        auto metalContext = Metal::DeviceContext::Get(context);

        const auto& shaderProgram = ::Assets::Legacy::GetAsset<ShaderProgram>(  
            ILLUM_FORWARD_VERTEX_HLSL ":main:" VS_DefShaderModel, 
            ILLUM_FORWARD_PIXEL_HLSL ":main", "GEO_HAS_COLOR=1");
        BoundInputLayout boundVertexInputLayout(MakeIteratorRange(vertexInputLayout), shaderProgram);
        metalContext->Bind(shaderProgram);

        BoundUniforms boundLayout(shaderProgram);
        static const auto HashLocalTransform = Hash64("LocalTransform");
        boundLayout.BindConstantBuffer(HashLocalTransform, 0, 1);
        Techniques::TechniqueContext::BindGlobalUniforms(boundLayout);
        boundLayout.Apply(*metalContext, 
            parserContext.GetGlobalUniformsStream(),
            UniformsStream(constantBufferPackets, nullptr, dimof(constantBufferPackets)));

            //      Construct the lines for while iterating through the transformation machine

        std::vector<Vertex_PC> workingVertices;
        auto& skeleton = *_pimpl->_transMachine;
        auto temp = std::make_unique<Float4x4[]>(skeleton.GetOutputMatrixCount());
        if (_pimpl->_animationSetScaffold) {
            auto& animSet = _pimpl->_animationSetScaffold->ImmutableData();
            auto params = animSet._animationSet.BuildTransformationParameterSet(
                    animState, skeleton, *_pimpl->_animationSetBinding.get(), 
                    animSet._curves, animSet._curvesCount);
            using namespace std::placeholders;
            skeleton.GenerateOutputTransforms(
                temp.get(), skeleton.GetOutputMatrixCount(),
                &params, 
                std::bind(&RenderSkeleton_DebugIterator, _1, _2, &workingVertices));
        } else {
            using namespace std::placeholders;
            skeleton.GenerateOutputTransforms(
                temp.get(), skeleton.GetOutputMatrixCount(), &skeleton.GetDefaultParameters(),
                std::bind(&RenderSkeleton_DebugIterator, _1, _2, &workingVertices));
        }

        auto vertexBuffer = MakeVertexBuffer(GetObjectFactory(), MakeIteratorRange(workingVertices));
		VertexBufferView vbs[] = { {&vertexBuffer} };		// sizeof(Vertex_PC)
		boundVertexInputLayout.Apply(*metalContext, MakeIteratorRange(vbs));
        metalContext->Bind(Techniques::CommonResources()._dssDisable);
        metalContext->Bind(Techniques::CommonResources()._blendOpaque);
        metalContext->Bind(Topology::LineList);
        metalContext->Draw((unsigned)workingVertices.size());
    }

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    static unsigned currentFrameIndex = 0;
    void IncreaseTBufferTemporaryTextureFrameIndex() { ++currentFrameIndex; }       // should be more or less synced with Present()

    TBufferTemporaryTexture::TBufferTemporaryTexture() { _size = 0; }
    TBufferTemporaryTexture::TBufferTemporaryTexture(const void* sourceData, size_t bufferSize, bool dynamicResource)
    {
        _lastAllocatedFrame = currentFrameIndex;
        _size = bufferSize;
        _shaderOffsetValue = 0;

		#if GFXAPI_TARGET == GFXAPI_DX11
			using namespace Metal;
			D3D11_BUFFER_DESC bufferDesc;
			bufferDesc.ByteWidth = (UINT)bufferSize;
			bufferDesc.Usage = dynamicResource?D3D11_USAGE_DYNAMIC:D3D11_USAGE_DEFAULT;
			bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			bufferDesc.CPUAccessFlags = dynamicResource?D3D11_CPU_ACCESS_WRITE:0;
			bufferDesc.MiscFlags = 0;
			bufferDesc.StructureByteStride = 0;

			auto& objFactory = GetObjectFactory();

			D3D11_SUBRESOURCE_DATA subData;
			subData.pSysMem = sourceData;
			subData.SysMemPitch = subData.SysMemSlicePitch = (UINT)bufferSize;
			_resource = AsResourcePtr(objFactory.CreateBuffer(&bufferDesc, sourceData?(&subData):nullptr).get());

			bufferDesc.Usage = D3D11_USAGE_STAGING;
			bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			bufferDesc.BindFlags = 0;
			_stagingResource = AsResourcePtr(objFactory.CreateBuffer(&bufferDesc).get());

			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
			srvDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER ;
			srvDesc.Buffer.ElementOffset = 0;
			srvDesc.Buffer.ElementWidth = (UINT)(bufferSize / (4*sizeof(float)));
			auto srv = objFactory.CreateShaderResourceView(AsID3DResource(*_resource), &srvDesc);
			_view = ShaderResourceView(srv.get());
		#endif
    }

    TBufferTemporaryTexture::TBufferTemporaryTexture(const TBufferTemporaryTexture& cloneFrom)
    :       _resource(cloneFrom._resource)
    ,       _stagingResource(cloneFrom._stagingResource)
    ,       _view(cloneFrom._view)
    ,       _size(cloneFrom._size)
    ,       _shaderOffsetValue(cloneFrom._shaderOffsetValue)
    ,       _lastAllocatedFrame(cloneFrom._lastAllocatedFrame)
    {}

    TBufferTemporaryTexture::TBufferTemporaryTexture(TBufferTemporaryTexture&& moveFrom)
    :       _resource(std::move(moveFrom._resource))
    ,       _stagingResource(std::move(moveFrom._stagingResource))
    ,       _view(std::move(moveFrom._view))
    ,       _size(moveFrom._size)
    ,       _shaderOffsetValue(moveFrom._shaderOffsetValue)
    ,       _lastAllocatedFrame(moveFrom._lastAllocatedFrame)
    {}

    TBufferTemporaryTexture& TBufferTemporaryTexture::operator=(const TBufferTemporaryTexture& cloneFrom)
    {
        _resource = cloneFrom._resource;
        _stagingResource = cloneFrom._stagingResource;
        _view = cloneFrom._view;
        _size = cloneFrom._size;
        _shaderOffsetValue = cloneFrom._shaderOffsetValue;
        _lastAllocatedFrame = cloneFrom._lastAllocatedFrame;
        return *this;
    }

    TBufferTemporaryTexture& TBufferTemporaryTexture::operator=(TBufferTemporaryTexture&& moveFrom)
    {
        _resource = std::move(moveFrom._resource);
        _stagingResource = std::move(moveFrom._stagingResource);
        _view = std::move(moveFrom._view);
        _size = std::move(moveFrom._size);
        _shaderOffsetValue = std::move(moveFrom._shaderOffsetValue);
        _lastAllocatedFrame = std::move(moveFrom._lastAllocatedFrame);
        return *this;
    }

    static const unsigned FrameBufferCount = 4;
    static std::vector<TBufferTemporaryTexture> textures[FrameBufferCount];

    static TBufferTemporaryTexture AllocateExistingTBufferTemporaryTexture(size_t bufferSize)
    {
        auto& thisFrameTextures = textures[currentFrameIndex%FrameBufferCount];
        for (auto i=thisFrameTextures.begin(); i!=thisFrameTextures.end(); ++i) {
            if (i->_size == bufferSize && i->_lastAllocatedFrame < currentFrameIndex) {
                i->_lastAllocatedFrame = currentFrameIndex;
                return *i;
            }
        }

        return TBufferTemporaryTexture();
    }

    static TBufferTemporaryTexture AllocateNewTBufferTemporaryTexture(const void* bufferData, size_t bufferSize)
    {
        auto& thisFrameTextures = textures[currentFrameIndex%FrameBufferCount];
        thisFrameTextures.push_back(std::move(TBufferTemporaryTexture(bufferData, bufferSize)));
        return thisFrameTextures[thisFrameTextures.size()-1];
    }

    static void PushTBufferTemporaryTexture(Metal::DeviceContext* context, TBufferTemporaryTexture& tex)
    {
        Metal::Copy(*context, Metal::AsID3DResource(*tex._resource), Metal::AsID3DResource(*tex._stagingResource));
    }
}}

#else

namespace FixedFunctionModel
{
	MeshToModel::MeshToModel(const PreparedAnimation& preparedAnim, const RenderCore::Assets::SkeletonBinding* binding)
	{
	}

	class SkinPrepareMachine::Pimpl
    {
    public:
    };

	void SkinPrepareMachine::PrepareAnimation(  
        IThreadContext& context, 
        PreparedAnimation& state,
        const RenderCore::Assets::AnimationState& animState) const
	{}

	const RenderCore::Assets::SkeletonBinding& SkinPrepareMachine::GetSkeletonBinding() const 
	{
		return *(const RenderCore::Assets::SkeletonBinding*)nullptr;
	}

	unsigned SkinPrepareMachine::GetSkeletonOutputCount() const 
	{
		return 0;
	}

    void SkinPrepareMachine::RenderSkeleton(
        IThreadContext& context, 
        Techniques::ParsingContext& parserContext, 
        const RenderCore::Assets::AnimationState& animState, const Float4x4& localToWorld)
	{
	}
        
	SkinPrepareMachine::SkinPrepareMachine(const RenderCore::Assets::ModelScaffold&, const RenderCore::Assets::AnimationSetScaffold&, const RenderCore::Assets::SkeletonScaffold&) {}
	SkinPrepareMachine::SkinPrepareMachine(const RenderCore::Assets::ModelScaffold& skinScaffold, const RenderCore::Assets::SkeletonMachine& skeletonScaffold) {}
    SkinPrepareMachine::~SkinPrepareMachine() {}
}

#endif

