// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "ModelRunTime.h"
#include "SharedStateSet.h" // for SharedShaderName, SharedParameterBox, etc
#include "../RenderCore/Assets/ModelScaffold.h"
#include "../RenderCore/Assets/ModelScaffoldInternal.h"
#include "../Utility/Streams/SerializationUtils.h"
#include "../Utility/PtrUtils.h"
#include "../Utility/IteratorUtils.h"

namespace RenderCore { class IResource; class ConstantBufferView; class MiniInputElementDesc; }
namespace RenderCore { namespace Techniques { class DeferredShaderResource; }}

namespace FixedFunctionModel 
{
    class SkinningBindingBox;

////////////////////////////////////////////////////////////////////////////////////////////
    //      r e n d e r e r         //

    namespace ModelConstruction { class BuffersUnderConstruction; class ParamBoxDescriptions; }

    class PendingGeoUpload
    {
    public:
        unsigned _sourceFileOffset, _size;
        unsigned _supplementIndex;
        unsigned _bufferDestination;
    };

    class ModelRenderer::Pimpl
    {
    public:
        typedef unsigned UnifiedBufferOffset;
        static const auto MaxVertexStreams = 3u;

        class Mesh
        {
        public:
            unsigned    _id;

                // indices
            UnifiedBufferOffset     _ibOffset;
            RenderCore::Format		_indexFormat;

                // vertices
            UnifiedBufferOffset _vbOffsets[MaxVertexStreams];
            unsigned    _vertexStrides[MaxVertexStreams];
            unsigned    _vertexStreamCount;

                // geo params & technique interface
                // each mesh within the model can have a different vertex input interface
                //  (and so needs a different technique interface)
            SharedParameterBox          _geoParamBox;
            SharedTechniqueInterface    _techniqueInterface;

            #if defined(_DEBUG)
                unsigned _vbSize, _ibSize;  // used for metrics
            #endif
        };

        std::vector<::Assets::PtrToMarkerPtr<RenderCore::Techniques::DeferredShaderResource>> _boundTextures;
        size_t  _texturesPerMaterial;

        ///////////////////////////////////////////////////////////////////////////////
            // "resources" or state information attached on a per-draw call basis
        class DrawCallResources
        {
        public:
            SharedTechniqueConfig _shaderName;
            SharedParameterBox  _geoParamBox;
            SharedParameterBox  _materialParamBox;

            unsigned        _textureSet;
            unsigned        _constantBuffer;
            SharedRenderStateSet _renderStateSet;

            DelayStep       _delayStep;
            RenderCore::Assets::MaterialGuid    _materialBindingGuid;

            DrawCallResources();
            DrawCallResources(
                SharedTechniqueConfig shaderName,
                SharedParameterBox geoParamBox, SharedParameterBox matParamBox,
                unsigned textureSet, unsigned constantBuffer,
                SharedRenderStateSet renderStateSet, DelayStep delayStep, RenderCore::Assets::MaterialGuid materialBindingIndex);
        };
        std::vector<DrawCallResources>   _drawCallRes;

        ///////////////////////////////////////////////////////////////////////////////
        std::shared_ptr<RenderCore::IResource>	_vertexBuffer;
        std::shared_ptr<RenderCore::IResource>	_indexBuffer;
        std::vector<Mesh>			_meshes;
        std::vector<RenderCore::Metal::ConstantBuffer>  _constantBuffers;

        ///////////////////////////////////////////////////////////////////////////////
        typedef std::pair<unsigned, RenderCore::Assets::DrawCallDesc> MeshAndDrawCall;
        std::vector<MeshAndDrawCall>    _drawCalls;

        const RenderCore::Assets::ModelScaffold*    _scaffold;
        unsigned                _levelOfDetail;

        ///////////////////////////////////////////////////////////////////////////////
        std::vector<PendingGeoUpload>  _vbUploads;
        std::vector<PendingGeoUpload>  _ibUploads;

        ///////////////////////////////////////////////////////////////////////////////
        #if defined(_DEBUG)
            unsigned _vbSize, _ibSize;
            std::vector<::Assets::rstring> _boundTextureNames;
            std::vector<std::pair<SharedParameterBox,std::string>> _paramBoxDesc;
        #endif

        ///////////////////////////////////////////////////////////////////////////////
        Pimpl() : _scaffold(nullptr), _levelOfDetail(~unsigned(0x0)) {}
        ~Pimpl() {}

        SharedStateSet::BoundVariation BeginVariation(
            const ModelRendererContext&, const SharedStateSet&, unsigned, SharedTechniqueInterface) const;

        SharedTechniqueInterface BeginGeoCall(
            const ModelRendererContext& context,
            RenderCore::Metal::ConstantBuffer&  localTransformBuffer,
            const MeshToModel&      transforms,
            const Float4x4&         modelToWorld,
            unsigned                geoCallIndex) const;

		void ApplyBoundUnforms(
			const ModelRendererContext&     context,
			RenderCore::Metal::BoundUniforms&           boundUniforms,
			unsigned                        resourcesIndex,
			unsigned                        constantsIndex,
			RenderCore::ConstantBufferView				cbvs[2]);

		void ApplyBoundInputLayout(
            const ModelRendererContext&		context,
			RenderCore::Metal::BoundInputLayout&		boundInputLayout,
            unsigned						geoCallIndex) const;

    ///////////////////////////////////////////////////////////////////////////////
        //   B U I L D I N G   A N D   I N I T I A L I Z A T I O N   //
    ///////////////////////////////////////////////////////////////////////////////

        static auto BuildMesh(
            const RenderCore::Assets::ModelCommandStream::GeoCall& geoInst,
            const RenderCore::Assets::RawGeometry& geo,
            IteratorRange<RenderCore::Assets::VertexData**> supplements,
            ModelConstruction::BuffersUnderConstruction& workingBuffers,
            SharedStateSet& sharedStateSet,
            const uint64 textureBindPoints[], unsigned textureBindPointsCnt,
            ModelConstruction::ParamBoxDescriptions& paramBoxDesc,
            bool normalFromSkinning = false) -> Mesh;
    };

    class ModelRenderer::PimplWithSkinning : public Pimpl
    {
    public:
        class SkinnedMesh : public Mesh
        {
        public:
                //  Vertex data is separated into several "streams". 
                //  These match the low level api streams.
                //  The base class "Mesh" members contain the static geometry elements.

            struct VertexStreams { enum Enum { AnimatedGeo, SkeletonBinding, Max }; };
            UnifiedBufferOffset         _extraVbOffset[VertexStreams::Max];
            unsigned                    _extraVbStride[VertexStreams::Max];
            unsigned                    _vertexCount[VertexStreams::Max];
            SharedTechniqueInterface    _skinnedTechniqueInterface;
        };

        class SkinnedMeshAnimBinding
        {
        public:
            uint64      _iaAnimationHash;
            const RenderCore::Assets::BoundSkinnedGeometry* _scaffold;

            SharedTechniqueInterface    _techniqueInterface;
            unsigned                    _vertexStride;
        };

        std::vector<SkinnedMesh>            _skinnedMeshes;
        std::vector<SkinnedMeshAnimBinding> _skinnedBindings;
        std::vector<MeshAndDrawCall>        _skinnedDrawCalls;

        SharedTechniqueInterface BeginSkinCall(
            const ModelRendererContext&     context,
            RenderCore::Metal::ConstantBuffer&  localTransformBuffer,
            const MeshToModel&      transforms,
            const Float4x4&         modelToWorld,
            unsigned                geoCallIndex,
            PreparedAnimation*      preparedAnimation) const;

    ///////////////////////////////////////////////////////////////////////////////
        //   B U I L D I N G   A N D   I N I T I A L I Z A T I O N   //
    ///////////////////////////////////////////////////////////////////////////////

        void BuildSkinnedBuffer(
            RenderCore::Metal::DeviceContext&   context,
            const SkinnedMesh&      mesh,
            const SkinnedMeshAnimBinding& preparedAnimBinding, 
            const Float4x4          transformationMachineResult[],
            const RenderCore::Assets::SkeletonBinding&  skeletonBinding,
            RenderCore::IResource&				outputResult,
            unsigned                outputOffset) const;

        static auto BuildMesh(
            const RenderCore::Assets::ModelCommandStream::GeoCall& geoInst,
            const RenderCore::Assets::BoundSkinnedGeometry& geo,
            IteratorRange<RenderCore::Assets::VertexData**> supplements,
            ModelConstruction::BuffersUnderConstruction& workingBuffers,
            SharedStateSet& sharedStateSet,
            const uint64 textureBindPoints[], unsigned textureBindPointsCnt,
            ModelConstruction::ParamBoxDescriptions& paramBoxDesc) -> SkinnedMesh;

        static auto BuildAnimBinding(
            const RenderCore::Assets::ModelCommandStream::GeoCall& geoInst,
            const RenderCore::Assets::BoundSkinnedGeometry& geo,
            SharedStateSet& sharedStateSet,
            const uint64 textureBindPoints[], unsigned textureBindPointsCnt) -> SkinnedMeshAnimBinding;

        static void InitialiseSkinningVertexAssembly(
            uint64 inputAssemblyHash,
            const RenderCore::Assets::BoundSkinnedGeometry& scaffoldGeo);

        static unsigned BuildPostSkinInputAssembly(
            RenderCore::InputElementDesc dst[],
            unsigned dstCount,
            const RenderCore::Assets::BoundSkinnedGeometry& scaffoldGeo);

        ///////////////////////////////////////////////////////////////////////////////
            //   S K I N N I N G   S E T U P   //
        ///////////////////////////////////////////////////////////////////////////////

        void StartBuildingSkinning(RenderCore::Metal::DeviceContext& context, SkinningBindingBox& bindingBox) const;
        void EndBuildingSkinning(RenderCore::Metal::DeviceContext& context) const;
    };

    class PreparedAnimation
    {
    public:
        std::unique_ptr<Float4x4[]> _finalMatrices;
        unsigned                    _finalMatrixCount;
        std::shared_ptr<RenderCore::IResource>	_skinningBuffer;
        std::vector<unsigned>       _vbOffsets;

        PreparedAnimation();
        PreparedAnimation(PreparedAnimation&&) never_throws;
        PreparedAnimation& operator=(PreparedAnimation&&) never_throws;
        PreparedAnimation(const PreparedAnimation&) = delete;
        PreparedAnimation& operator=(const PreparedAnimation&) = delete;
    };

}

