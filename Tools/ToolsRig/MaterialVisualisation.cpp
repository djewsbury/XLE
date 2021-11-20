// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "MaterialVisualisation.h"
#include "VisualisationUtils.h"		// for IVisContent
#include "VisualisationGeo.h"

#include "../ShaderParser/ShaderPatcher.h"

#include "../../SceneEngine/IScene.h"

#include "../../RenderCore/Techniques/CommonResources.h"
#include "../../RenderCore/Techniques/ParsingContext.h"
#include "../../RenderCore/Techniques/DrawableDelegates.h"
#include "../../RenderCore/Techniques/Techniques.h"
#include "../../RenderCore/Techniques/TechniqueDelegates.h"
#include "../../RenderCore/Techniques/CompiledShaderPatchCollection.h"
#include "../../RenderCore/Techniques/CommonUtils.h"
#include "../../RenderCore/Techniques/PipelineAccelerator.h"
#include "../../RenderCore/Techniques/DescriptorSetAccelerator.h"
#include "../../RenderCore/Techniques/Drawables.h"
#include "../../RenderCore/Metal/Shader.h" // (for CreateLowLevelShaderCompiler)
#include "../../RenderCore/Assets/Services.h"
#include "../../RenderCore/Assets/ShaderPatchCollection.h"
#include "../../RenderCore/Assets/MaterialScaffold.h"
#include "../../RenderCore/Assets/PredefinedDescriptorSetLayout.h"
#include "../../RenderCore/MinimalShaderSource.h"

#include "../../RenderCore/UniformsStream.h"
#include "../../RenderCore/BufferView.h"
#include "../../RenderCore/ShaderService.h"

#include "../../Math/Transformations.h"
#include "../../Assets/Assets.h"
#include "../../Assets/Continuation.h"
#include "../../ConsoleRig/ResourceBox.h"
#include "../../ConsoleRig/GlobalServices.h"
#include "../../Utility/Streams/PathUtils.h"
#include "../../Utility/Threading/Mutex.h"
#include "../../Utility/Threading/ThreadingUtils.h"
#include "../../Utility/Threading/CompletionThreadPool.h"

namespace RenderCore { namespace Techniques
{
	const RenderCore::Assets::PredefinedDescriptorSetLayout& GetFallbackMaterialDescriptorSetLayout();
}}

namespace ToolsRig
{
    using namespace RenderCore;

///////////////////////////////////////////////////////////////////////////////////////////////////

    class CachedVisGeo
    {
    public:
        IResourcePtr _cubeBuffer;
        IResourcePtr _sphereBuffer;
        unsigned _cubeVCount;
        unsigned _sphereVCount;

        CachedVisGeo(IDevice&);
    };

    CachedVisGeo::CachedVisGeo(IDevice& device)
    {
        auto sphereGeometry = BuildGeodesicSphere();
        _sphereBuffer = RenderCore::Techniques::CreateStaticVertexBuffer(device, MakeIteratorRange(sphereGeometry));
        _sphereVCount = unsigned(sphereGeometry.size());
        auto cubeGeometry = BuildCube();
        _cubeBuffer = RenderCore::Techniques::CreateStaticVertexBuffer(device, MakeIteratorRange(cubeGeometry));
        _cubeVCount = unsigned(cubeGeometry.size());
    }

	class MaterialSceneParserDrawable : public Techniques::Drawable
	{
	public:
		unsigned	_vertexCount;

		static void DrawFn(
			Techniques::ParsingContext& parserContext,
			const Techniques::ExecuteDrawableContext& drawFnContext,
			const MaterialSceneParserDrawable& drawable)
		{
			if (drawFnContext.GetBoundLooseImmediateDatas() != 0) {
				auto transformPkt = 
					Techniques::MakeLocalTransformPacket(
						Identity<Float4x4>(), 
						ExtractTranslation(parserContext.GetProjectionDesc()._cameraToWorld));
				IteratorRange<const void*> pkts[] = { MakeIteratorRange(transformPkt) };
				UniformsStream uniforms;
				uniforms._immediateData = MakeIteratorRange(pkts);
				drawFnContext.ApplyLooseUniforms(uniforms);
			}

			assert(!drawable._geo->_ib);
			drawFnContext.Draw(drawable._vertexCount);
		}
	};

    class MaterialVisualizationScene : public SceneEngine::IScene, public IVisContent, public IPatchCollectionVisualizationScene
    {
    public:
        virtual void ExecuteScene(
            RenderCore::IThreadContext& threadContext,
			const SceneEngine::ExecuteSceneContext& executeContext) const override
        {
			if (executeContext._batchFilter != RenderCore::Techniques::BatchFilter::General) return;

			auto usi = std::make_shared<UniformsStreamInterface>();
			usi->BindImmediateData(0, Techniques::ObjectCB::LocalTransform);

			auto pipeline = _pipelineFuture->Actualize();

            auto geoType = _settings._geometryType;
            if (geoType == MaterialVisSettings::GeometryType::Plane2D) {

                const Internal::Vertex3D    vertices[] = 
                {
                    { Float3(-1.f, -1.f, 0.f),  Float3(0.f, 0.f, 1.f), Float2(0.f, 1.f), Float4(1.f, 0.f, 0.f, 1.f) },
                    { Float3( 1.f, -1.f, 0.f),  Float3(0.f, 0.f, 1.f), Float2(1.f, 1.f), Float4(1.f, 0.f, 0.f, 1.f) },
                    { Float3(-1.f,  1.f, 0.f),  Float3(0.f, 0.f, 1.f), Float2(0.f, 0.f), Float4(1.f, 0.f, 0.f, 1.f) },
					{ Float3(-1.f,  1.f, 0.f),  Float3(0.f, 0.f, 1.f), Float2(0.f, 0.f), Float4(1.f, 0.f, 0.f, 1.f) },
					{ Float3( 1.f, -1.f, 0.f),  Float3(0.f, 0.f, 1.f), Float2(1.f, 1.f), Float4(1.f, 0.f, 0.f, 1.f) },
                    { Float3( 1.f,  1.f, 0.f),  Float3(0.f, 0.f, 1.f), Float2(1.f, 0.f), Float4(1.f, 0.f, 0.f, 1.f) }
                };

				auto space = executeContext._destinationPkt->AllocateStorage(Techniques::DrawablesPacket::Storage::VB, sizeof(vertices));
				std::memcpy(space._data.begin(), vertices, sizeof(vertices));

				auto& drawable = *executeContext._destinationPkt->_drawables.Allocate<MaterialSceneParserDrawable>();
				if (pipeline->_descriptorSet) {
					auto* t = pipeline->_descriptorSet->TryActualize();
					drawable._descriptorSet = t ? *t : nullptr;
				}
				drawable._pipeline = pipeline->_pipelineAccelerator;
				drawable._geo = std::make_shared<Techniques::DrawableGeo>();
				drawable._geo->_vertexStreams[0]._vbOffset = space._startOffset;
				drawable._geo->_vertexStreams[0]._type = Techniques::DrawableGeo::StreamType::PacketStorage;
				drawable._geo->_vertexStreamCount = 1;
				drawable._drawFn = (Techniques::ExecuteDrawableFn*)&MaterialSceneParserDrawable::DrawFn;
				drawable._vertexCount = (unsigned)dimof(vertices);
				drawable._looseUniformsInterface = usi;

            } else {

                unsigned count = 0;
				RenderCore::IResourcePtr vb;
                if (geoType == MaterialVisSettings::GeometryType::Sphere) {
					vb = _visGeo._sphereBuffer;
                    count = _visGeo._sphereVCount;
                } else if (geoType == MaterialVisSettings::GeometryType::Cube) {
					vb = _visGeo._cubeBuffer;
                    count = _visGeo._cubeVCount;
                } else return;

				auto& drawable = *executeContext._destinationPkt->_drawables.Allocate<MaterialSceneParserDrawable>();
				if (pipeline->_descriptorSet) {
					auto* t = pipeline->_descriptorSet->TryActualize();
					drawable._descriptorSet = t ? *t : nullptr;
				}
				drawable._pipeline = pipeline->_pipelineAccelerator;
				drawable._geo = std::make_shared<Techniques::DrawableGeo>();
				drawable._geo->_vertexStreams[0]._resource = vb;
				drawable._geo->_vertexStreamCount = 1;
				drawable._drawFn = (Techniques::ExecuteDrawableFn*)&MaterialSceneParserDrawable::DrawFn;
				drawable._vertexCount = count;
				drawable._looseUniformsInterface = usi;

            }
        }

		void ExecuteScene(
			RenderCore::IThreadContext& threadContext,
			const SceneEngine::ExecuteSceneContext& executeContext,
			IteratorRange<const RenderCore::Techniques::ProjectionDesc*> multiViews) const override
		{
			assert(0);
		}

		std::pair<Float3, Float3> GetBoundingBox() const override { return { Float3{-1.0f, 1.0f, 1.0f}, Float3{1.0f, 1.0f, 1.0f} }; }
		DrawCallDetails GetDrawCallDetails(unsigned drawCallIndex, uint64_t materialGuid) const override { return { {}, {} }; }
		std::shared_ptr<RenderCore::Techniques::ICustomDrawDelegate> SetCustomDrawDelegate(const std::shared_ptr<RenderCore::Techniques::ICustomDrawDelegate>& delegate) override { return nullptr; }
		void RenderSkeleton(
			RenderOverlays::IOverlayContext& overlayContext, 
			RenderCore::Techniques::ParsingContext& parserContext, 
			bool drawBoneNames) const override {}
		void BindAnimationState(const std::shared_ptr<VisAnimationState>& animState) override {}
		bool HasActiveAnimation() const override { return false; }

		void SetPatchCollection(const PatchCollectionFuture& patchCollectionFuture) override
		{
			assert(0);
#if 0
			auto mat = _material;
			if (!mat)
				mat = std::make_shared<RenderCore::Assets::MaterialScaffoldMaterial>();

			std::weak_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> weakPipelineAcceleratorPool = _pipelineAcceleratorPool;

			_pipelineFuture = std::make_shared<::Assets::MarkerPtr<PendingPipeline>>("MaterialVisualizationScene pipeline");
			::Assets::WhenAll(patchCollectionFuture).ThenConstructToPromise(
				_pipelineFuture->AdoptPromise(),
				[weakPipelineAcceleratorPool, mat](const std::shared_ptr<RenderCore::Techniques::CompiledShaderPatchCollection>& patchCollection) {

					auto strongPipelineAcceleratorPool = weakPipelineAcceleratorPool.lock();
					if (!strongPipelineAcceleratorPool)
						throw std::runtime_error("Aborting because pipeline accelerator pool has been destroyed");

					auto pendingPipeline = std::make_shared<PendingPipeline>();
					pendingPipeline->_depVal = ::Assets::GetDepValSys().Make();

					std::tie(pendingPipeline->_pipelineAccelerator, pendingPipeline->_descriptorSet) = RenderCore::Techniques::CreatePipelineAccelerator(
						*strongPipelineAcceleratorPool,
						patchCollection,
						*mat,
						Vertex3D_InputLayout,
						Topology::TriangleList);

					if (pendingPipeline->_descriptorSet)
						pendingPipeline->_depVal.RegisterDependency(pendingPipeline->_descriptorSet->GetDependencyValidation());

					return pendingPipeline;
				});
#endif
		}

		MaterialVisualizationScene(
			const MaterialVisSettings& settings,
			const std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool>& pipelineAcceleratorPool,
			const std::shared_ptr<RenderCore::Assets::MaterialScaffoldMaterial>& material)
        : _settings(settings)
		, _visGeo(*pipelineAcceleratorPool->GetDevice())
		{
			_pipelineAcceleratorPool = pipelineAcceleratorPool;
			_material = material;
		}

		const ::Assets::DependencyValidation& GetDependencyValidation() const 
		{
			if (_pipelineFuture)
				_pipelineFuture->GetDependencyValidation();
			return _depVal; 
		}

    protected:
        MaterialVisSettings		_settings;

		struct PendingPipeline
		{
			::Assets::DependencyValidation		_depVal;
			std::shared_ptr<RenderCore::Assets::MaterialScaffoldMaterial> _material;
			std::shared_ptr<RenderCore::Techniques::PipelineAccelerator> _pipelineAccelerator;
			::Assets::PtrToMarkerPtr<RenderCore::Techniques::DescriptorSetAccelerator> _descriptorSet;
			const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }
		};
		::Assets::PtrToMarkerPtr<PendingPipeline> _pipelineFuture;

		::Assets::DependencyValidation				_depVal;

		CachedVisGeo _visGeo;

		std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> _pipelineAcceleratorPool;
		std::shared_ptr<RenderCore::Assets::MaterialScaffoldMaterial> _material;
    };

	std::shared_ptr<SceneEngine::IScene> MakeScene(
		const std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool>& pipelineAcceleratorPool,
		const MaterialVisSettings& visObject,
		const std::shared_ptr<RenderCore::Assets::MaterialScaffoldMaterial>& material)
	{
		return std::make_shared<MaterialVisualizationScene>(visObject, pipelineAcceleratorPool, material);
	}

	/*::Assets::PtrToMarkerPtr<SceneEngine::IScene> ConvertToFuture(const std::shared_ptr<SceneEngine::IScene>& scene)
	{
		// HACK -- we have to use MaterialVisualizationScene as in intermediate type, 
		// because AssetFuture requires GetDependencyValidation()
		auto result = std::make_shared<::Assets::MarkerPtr<MaterialVisualizationScene>>("SceneFuture");
		if (dynamic_cast<::Assets::IAsyncMarker*>(scene.get())) {
			result->SetPollingFunction(
				[scene](::Assets::MarkerPtr<MaterialVisualizationScene>& thatFuture) {
					auto marker = std::dynamic_pointer_cast<MaterialVisualizationScene>(scene);
					auto state = marker->GetAssetState();
					if (state == ::Assets::AssetState::Pending)
						return true;

					if (state == ::Assets::AssetState::Ready) {
						thatFuture.SetAsset(std::shared_ptr<MaterialVisualizationScene>{marker}, nullptr);
					} else {
						thatFuture.SetInvalidAsset(nullptr, nullptr);
					}
					return false;
				});
		} else {
			result->SetAsset(std::dynamic_pointer_cast<MaterialVisualizationScene>(scene), nullptr);
		}
		return std::reinterpret_pointer_cast<::Assets::MarkerPtr<SceneEngine::IScene>>(result);
	}*/

///////////////////////////////////////////////////////////////////////////////////////////////////

#if 0
	std::unique_ptr<RenderCore::Techniques::CompiledShaderPatchCollection> MakeCompiledShaderPatchCollection(
		const std::shared_ptr<GraphLanguage::INodeGraphProvider>& provider,
		const RenderCore::Techniques::DescriptorSetLayoutAndBinding& materialDescSetLayout,
		const std::shared_ptr<MessageRelay>& logMessages)
	{
		ShaderSourceParser::InstantiationRequest instRequest;
		instRequest._customProvider = provider;
		ShaderSourceParser::GenerateFunctionOptions generateOptions;
		generateOptions._shaderLanguage = RenderCore::Techniques::GetDefaultShaderLanguage();
		return std::make_unique<RenderCore::Techniques::CompiledShaderPatchCollection>(
			ShaderSourceParser::InstantiateShader(
				MakeIteratorRange(&instRequest, &instRequest+1),
				generateOptions),
			materialDescSetLayout);
	}

	class PatchAnalysisHelper
	{
	public:
		::Assets::PtrToMarkerPtr<CompiledShaderByteCode> MakeByteCodeFuture(
			ShaderStage stage, StringSection<> patchCollectionCode, StringSection<> techniqueCode, StringSection<> entryPoint, StringSection<> definesTable)
		{
			char profileStr[] = "?s_*";
			switch (stage) {
			case ShaderStage::Vertex: profileStr[0] = 'v'; break;
			case ShaderStage::Geometry: profileStr[0] = 'g'; break;
			case ShaderStage::Pixel: profileStr[0] = 'p'; break;
			case ShaderStage::Domain: profileStr[0] = 'd'; break;
			case ShaderStage::Hull: profileStr[0] = 'h'; break;
			case ShaderStage::Compute: profileStr[0] = 'c'; break;
			}

			std::stringstream str;
			str << patchCollectionCode << std::endl << techniqueCode << std::endl;

			auto artifactFuture = _shaderSource->CompileFromMemory(
				str.str(), entryPoint,
				profileStr, definesTable);

			auto result = std::make_shared<::Assets::MarkerPtr<CompiledShaderByteCode>>();
			::Assets::AutoConstructToPromise(result->AdoptPromise(), artifactFuture);
			return result;
		}

		::Assets::PtrToMarkerPtr<Metal::ShaderProgram> MakeShaderVariation(const std::shared_ptr<RenderCore::ICompiledPipelineLayout>& pipelineLayout, StringSection<> defines)
		{
			using namespace RenderCore::Techniques;
			auto patchCollection = RenderCore::Techniques::AssembleShader(
				*_patchCollection, _patchExpansions, 
				defines);

			auto vsCode = MakeByteCodeFuture(ShaderStage::Vertex, patchCollection._processedSource, _vsTechniqueCode, "vs_main", defines);
			auto psCode = MakeByteCodeFuture(ShaderStage::Pixel, patchCollection._processedSource, _psTechniqueCode, "ps_main", defines);
			return CreateShaderProgramFromByteCode(pipelineLayout, vsCode, psCode, "ShaderPatchFactory");
		}

		const RenderCore::Techniques::CompiledShaderPatchCollection* _patchCollection;
		IteratorRange<const uint64_t*> _patchExpansions;
		std::shared_ptr<RenderCore::IShaderSource> _shaderSource;
		std::string _vsTechniqueCode;
		std::string _psTechniqueCode;

		static std::string InstantiatePreviewStructure(
			const RenderCore::Techniques::CompiledShaderPatchCollection& mainInstantiation,
			const ShaderSourceParser::PreviewOptions& previewOptions)
		{
			assert(mainInstantiation.GetInterface().GetPatches().size() == 1);	// only tested with a single entry point
			auto structureForPreview = GenerateStructureForPreview(
				"preview_graph", *mainInstantiation.GetInterface().GetPatches()[0]._signature, previewOptions);
			return "#include \"xleres/System/Prefix.h\"\n" + structureForPreview;
		}
	};

	class ShaderPatchAnalysisDelegate : public RenderCore::Techniques::ITechniqueDelegate
	{
	public:
		::Assets::PtrToMarkerPtr<RenderCore::Metal::ShaderProgram> ResolveVariation(
			const RenderCore::Techniques::CompiledShaderPatchCollection& shaderPatches,
			IteratorRange<const ParameterBox**> selectors)
		{
			using namespace RenderCore::Techniques;

			ShaderSourceParser::ManualSelectorFiltering previewStructureFiltering;
			previewStructureFiltering._relevanceMap["GEO_PRETRANSFORMED"] = "1";
			auto filteredDefines = MakeFilteredDefinesTable(selectors, previewStructureFiltering, shaderPatches.GetInterface().GetSelectorRelevance());

			auto structureForPreview = PatchAnalysisHelper::InstantiatePreviewStructure(shaderPatches, _previewOptions);
			
			PatchAnalysisHelper factory;
			factory._patchCollection = &shaderPatches;
			factory._vsTechniqueCode = structureForPreview;
			factory._psTechniqueCode = structureForPreview;
			factory._shaderSource = _shaderSource;
			auto shader = factory.MakeShaderVariation(filteredDefines);
			shader->StallWhilePending();	// hack -- we have to stall here, or every shader we generate will be permanently pending
			return shader;
		}

		ResolvedTechnique Resolve(
			const std::shared_ptr<RenderCore::Techniques::CompiledShaderPatchCollection>& shaderPatches,
			IteratorRange<const ParameterBox**> selectors,
			const RenderCore::Assets::RenderStateSet& stateSet) override
		{
			using namespace RenderCore::Techniques;

			ResolvedTechnique result;
			result._shaderProgram = ResolveVariation(*shaderPatches, selectors);
			result._rasterization = BuildDefaultRastizerDesc(stateSet);

			#pragma warning(disable:4127) // conditional expression is constant
				// disable blending to avoid problem when rendering single component stuff 
                //  (ie, nodes that output "float", not "float4")
			const bool forceOpaqueBlend = true;
			if (!forceOpaqueBlend && (stateSet._flag & RenderCore::Assets::RenderStateSet::Flag::ForwardBlend)) {
                result._blend = AttachmentBlendDesc {
					stateSet._forwardBlendOp != BlendOp::NoBlending,
					stateSet._forwardBlendSrc, stateSet._forwardBlendDst, stateSet._forwardBlendOp };
            } else {
                result._blend = Techniques::CommonResources()._abOpaque;
            }
			return result;
		}

		ShaderPatchAnalysisDelegate(const ShaderSourceParser::PreviewOptions& previewOptions)
		: _previewOptions(previewOptions)
		{
			_shaderSource = std::make_shared<RenderCore::MinimalShaderSource>(
				RenderCore::Metal::CreateLowLevelShaderCompiler(RenderCore::Assets::Services::GetDevice()),
				RenderCore::MinimalShaderSource::Flags::CompileInBackground);
		}
	private:
		std::shared_ptr<RenderCore::IShaderSource> _shaderSource;
		ShaderSourceParser::PreviewOptions _previewOptions;
	};
#endif

	std::unique_ptr<RenderCore::Techniques::ITechniqueDelegate> MakeShaderPatchAnalysisDelegate(
		const ShaderSourceParser::PreviewOptions& previewOptions)
	{
		assert(0);		// broken in technique delegate refactor
		Throw(std::runtime_error("Unimplemented"));
		// return std::make_unique<ShaderPatchAnalysisDelegate>(previewOptions);
	}

#if 0
	std::unique_ptr<RenderCore::Techniques::CompiledShaderPatchCollection> MakeCompiledShaderPatchCollection(
		const GraphLanguage::NodeGraph& nodeGraph,
		const GraphLanguage::NodeGraphSignature& nodeGraphSignature,
		uint32_t previewNodeId,
		const std::shared_ptr<GraphLanguage::INodeGraphProvider>& subProvider)
	{
		ShaderSourceParser::InstantiationRequest instantiationReq {};
		ShaderSourceParser::GenerateFunctionOptions generateOptions;
		if (previewNodeId != ~0u)
			generateOptions._generateDanglingOutputs = previewNodeId;
		generateOptions._generateDanglingInputs = true;
		generateOptions._shaderLanguage = RenderCore::Techniques::GetDefaultShaderLanguage();

		auto mainInstantiation = ShaderSourceParser::InstantiateShader(
			GraphLanguage::INodeGraphProvider::NodeGraph { "preview_graph", nodeGraph, nodeGraphSignature, subProvider },
			false,
			instantiationReq,
			generateOptions);

		return std::make_unique<RenderCore::Techniques::CompiledShaderPatchCollection>(mainInstantiation);
	}

	template<typename Function, typename... Args>
		Utility::Threading::ContinuationFuture<std::invoke_result_t<std::decay_t<Function>, std::decay_t<Args>...>>
			ContinuationAsync(Function&& function, Args&&... args)
	{
		using PromiseResult = std::invoke_result_t<std::decay_t<Function>, std::decay_t<Args>...>;
		
		// We have to wrap up the uncopyable objects in a packet, because
		// there's a forced copy of the functor when we convert to a std::function<void>()
		struct Packet
		{
			Function _function;
			Utility::Threading::ContinuationPromise<PromiseResult> _promise;
		};
		auto pkt = std::make_shared<Packet>(Packet{std::move(function)});
		auto result = pkt->_promise.get_future();
		ConsoleRig::GlobalServices::GetInstance().GetLongTaskThreadPool().Enqueue(
			[pkt](Args&&... args) mutable -> void {
				try {
					pkt->_promise.set_value(pkt->_function(std::forward<Args>(args)...));
				} catch (...) {
					try {
						pkt->_promise.set_exception(std::current_exception());
					} catch(...) {}
				}
			},
			std::forward<Args>(args)...);

		return result;
	}

	template<typename Function, typename... Args>
		void AsyncConstructToPromise(
			std::promise<
				std::decay_t<std::invoke_result_t<std::decay_t<Function>, std::decay_t<Args>...>>
			>&& promise,
			Function&& function, Args&&... args)
	{
		ConsoleRig::GlobalServices::GetInstance().GetLongTaskThreadPool().Enqueue(
			[function=std::move(function), promise=std::move(promise)](Args&&... args) mutable -> void {
				TRY {
					auto object = function(std::forward<Args>(args)...);
					future->set_value(std::move(object));
				} CATCH (...) {
					promise.set_exception(std::current_exception());
				} CATCH_END
			},
			std::forward<Args>(args)...);
	}

	PatchCollectionFuture MakeCompiledShaderPatchCollectionAsync(
		GraphLanguage::NodeGraph&& nodeGraph,
		GraphLanguage::NodeGraphSignature&& nodeGraphSignature,
		uint32_t previewNodeId,
		const std::shared_ptr<GraphLanguage::INodeGraphProvider>& subProvider)
	{
		auto future = std::make_shared<::Assets::MarkerPtr<RenderCore::Techniques::CompiledShaderPatchCollection>>("MakeCompiledShaderPatchCollectionAsync");
		AsyncConstructToPromise(
			future->AdoptPromise(),
			[nodeGraph{std::move(nodeGraph)}, nodeGraphSignature{std::move(nodeGraphSignature)}, previewNodeId, subProvider]() {
				return MakeCompiledShaderPatchCollection(nodeGraph, nodeGraphSignature, previewNodeId, subProvider);
			});

		return future;
	}

	const PatchCollectionFuture& DeferredCompiledShaderPatchCollection::GetFuture()
	{
		return *_future;
	}

	DeferredCompiledShaderPatchCollection::DeferredCompiledShaderPatchCollection(
		GraphLanguage::NodeGraph&& nodeGraph,
		GraphLanguage::NodeGraphSignature&& nodeGraphSignature,
		uint32_t previewNodeId,
		const std::shared_ptr<GraphLanguage::INodeGraphProvider>& subProvider)
	{
		_future = std::make_unique<PatchCollectionFuture>(
			MakeCompiledShaderPatchCollectionAsync(
				std::move(nodeGraph),
				std::move(nodeGraphSignature),
				previewNodeId,
				subProvider));
	}
	DeferredCompiledShaderPatchCollection::~DeferredCompiledShaderPatchCollection() {}
#else
	const PatchCollectionFuture& DeferredCompiledShaderPatchCollection::GetFuture()
	{
		assert(0);
		static PatchCollectionFuture dummy;
		return dummy;
	}

	DeferredCompiledShaderPatchCollection::DeferredCompiledShaderPatchCollection(
		GraphLanguage::NodeGraph&& nodeGraph,
		GraphLanguage::NodeGraphSignature&& nodeGraphSignature,
		uint32_t previewNodeId,
		const std::shared_ptr<GraphLanguage::INodeGraphProvider>& subProvider)
	{}
	DeferredCompiledShaderPatchCollection::~DeferredCompiledShaderPatchCollection() {}

#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class MessageRelay::Pimpl
	{
	public:
		std::vector<std::string> _messages;
		Threading::RecursiveMutex _lock;

		std::vector<std::pair<unsigned, std::shared_ptr<OnChangeCallback>>> _callbacks;
		unsigned _nextCallbackId = 1;
	};

	std::string MessageRelay::GetMessages() const
	{
		ScopedLock(_pimpl->_lock);
		size_t length = 0;
		for (const auto&m:_pimpl->_messages)
			length += m.size();
		std::string result;
		result.reserve(length);
		for (const auto&m:_pimpl->_messages)
			result.insert(result.end(), m.begin(), m.end());
		return result;
	}

	unsigned MessageRelay::AddCallback(const std::shared_ptr<OnChangeCallback>& callback)
	{
		ScopedLock(_pimpl->_lock);
		_pimpl->_callbacks.push_back(std::make_pair(_pimpl->_nextCallbackId, callback));
		return _pimpl->_nextCallbackId++;
	}

	void MessageRelay::RemoveCallback(unsigned id)
	{
		ScopedLock(_pimpl->_lock);
		auto i = std::find_if(
			_pimpl->_callbacks.begin(), _pimpl->_callbacks.end(),
			[id](const std::pair<unsigned, std::shared_ptr<OnChangeCallback>>& p) { return p.first == id; } );
		_pimpl->_callbacks.erase(i);
	}

	void MessageRelay::AddMessage(const std::string& msg)
	{
		ScopedLock(_pimpl->_lock);
		_pimpl->_messages.push_back(msg);
		for (const auto&cb:_pimpl->_callbacks)
			cb.second->OnChange();
	}

	MessageRelay::MessageRelay()
	{
		_pimpl = std::make_unique<Pimpl>();
	}

	MessageRelay::~MessageRelay()
	{}

}

