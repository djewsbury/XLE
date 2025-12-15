// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "LightingEngineDisplay.h"
#include "../../RenderCore/LightingEngine/LightingEngine.h"
#include "../../RenderCore/LightingEngine/LightingDelegateUtil.h"
#include "../../RenderCore/Techniques/ParsingContext.h"
#include "../../RenderCore/Techniques/TechniqueUtils.h"
#include "../../RenderCore/Techniques/ManualDrawables.h"
#include "../../RenderCore/Techniques/DescriptorSetAccelerator.h"
#include "../../RenderCore/Techniques/PipelineAccelerator.h"
#include "../../RenderCore/Techniques/ImmediateDrawables.h"
#include "../../RenderCore/Assets/ScaffoldCmdStream.h"
#include "../../RenderCore/Assets/RawMaterial.h"
#include "../../RenderCore/Assets/ShaderPatchCollection.h"
#include "../../RenderCore/Assets/MaterialCompiler.h"
#include "../../RenderCore/Assets/CompiledMaterialSet.h"
#include "../../RenderOverlays/DebuggingDisplay.h"
#include "../../RenderOverlays/ShapesRendering.h"
#include "../../RenderOverlays/DrawText.h"
#include "../../RenderOverlays/LayoutEngine.h"
#include "../../RenderOverlays/IOverlayContext.h"
#include "../../RenderOverlays/OverlayPrimitives.h"
#include "../../Assets/Marker.h"
#include "../../Assets/CompoundAsset.h"
#include "../../Math/Transformations.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/StringFormat.h"
#include "../../Utility/StreamUtils.h"
#include "../../Utility/MemoryUtils.h"
#include <chrono>
#include <future>

using namespace Utility::Literals;

namespace PlatformRig { namespace Overlays
{

	class DescriptorSetConstructorHelper
	{
	public:
		IteratorRange<RenderCore::Assets::ScaffoldCmdIterator> _matMachine;
		std::shared_ptr<RenderCore::Assets::CompiledMaterialSet> _matScaffold;
		std::shared_ptr<RenderCore::Assets::ShaderPatchCollection> _patchCollection;
		std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout> _matDescSet;
		RenderCore::Techniques::MatMachineDecompositionHelper _matMachineDecomposed;

		DescriptorSetConstructorHelper(RenderCore::Assets::RawMaterial&& rawMat);
		DescriptorSetConstructorHelper(RenderCore::Assets::RawMaterial&& rawMat, std::shared_ptr<RenderCore::Assets::ShaderPatchCollection> shaderPatches, std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout> matDescSet);
	};

	DescriptorSetConstructorHelper::DescriptorSetConstructorHelper(RenderCore::Assets::RawMaterial&& rawMat)
	{
		auto matScaffoldConstr = std::make_shared<RenderCore::Assets::MaterialSetConstruction>();
		std::string baseMaterials[] { "main" };
		matScaffoldConstr->SetBaseMaterials(baseMaterials);
		matScaffoldConstr->AddOverride("main", std::move(rawMat));

		std::promise<std::shared_ptr<RenderCore::Assets::CompiledMaterialSet>> promisedMatScaffold;
		auto futureMatScaffold = promisedMatScaffold.get_future();
		RenderCore::Assets::ConstructMaterialSet(std::move(promisedMatScaffold), std::move(matScaffoldConstr));

		YieldToPool(futureMatScaffold);
		_matScaffold = futureMatScaffold.get();

		using namespace Utility::Literals;
		_matMachine = _matScaffold->GetMaterialMachine("main"_h);

		_matMachineDecomposed = RenderCore::Techniques::DecomposeMaterialMachine(_matMachine);
		if (_matMachineDecomposed._shaderPatchCollection != ~0u)
			_patchCollection = _matScaffold->GetShaderPatchCollection(_matMachineDecomposed._shaderPatchCollection);
	}

	DescriptorSetConstructorHelper::DescriptorSetConstructorHelper(RenderCore::Assets::RawMaterial&& rawMat, std::shared_ptr<RenderCore::Assets::ShaderPatchCollection> shaderPatches, std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout> matDescSet)
	: _patchCollection(std::move(shaderPatches)), _matDescSet(std::move(matDescSet))
	{
		auto matScaffoldConstr = std::make_shared<RenderCore::Assets::MaterialSetConstruction>();
		std::string baseMaterials[] { "main" };
		matScaffoldConstr->SetBaseMaterials(baseMaterials);
		matScaffoldConstr->AddOverride("main", std::move(rawMat));

		std::promise<std::shared_ptr<RenderCore::Assets::CompiledMaterialSet>> promisedMatScaffold;
		auto futureMatScaffold = promisedMatScaffold.get_future();
		RenderCore::Assets::ConstructMaterialSet(std::move(promisedMatScaffold), std::move(matScaffoldConstr));

		YieldToPool(futureMatScaffold);
		_matScaffold = futureMatScaffold.get();

		using namespace Utility::Literals;
		_matMachine = _matScaffold->GetMaterialMachine("main"_h);

		_matMachineDecomposed = RenderCore::Techniques::DecomposeMaterialMachine(_matMachine);
	}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class ShadowProbesDisplay : public RenderOverlays::DebuggingDisplay::IWidget
	{
	public:
		ShadowProbesDisplay(std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> overlayAccelerators, std::shared_ptr<RenderCore::LightingEngine::CompiledLightingTechnique> technique);
		~ShadowProbesDisplay();
	protected:
		void    Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState) override;
		ProcessInputResult    ProcessInput(InterfaceState& interfaceState, const OSServices::InputSnapshot& input) override;
		
		std::shared_ptr<RenderCore::LightingEngine::CompiledLightingTechnique> _technique;
		::Assets::PtrToMarkerPtr<RenderOverlays::Font> _headingFont;

		struct Resources
		{
			std::shared_ptr<RenderCore::Techniques::PipelineAccelerator> _depthMapVisPipeline;
			std::shared_ptr<RenderCore::Techniques::DescriptorSetAccelerator> _depthMapVisDS;
		};
		std::shared_future<Resources> _futureResources;
	};

	static void DrawCircle(RenderOverlays::IOverlayContext& context, Float3 position, Float3 camRight, Float3 camUp, const RenderOverlays::ColorB col)
	{
		const float outerRadius = 0.2f;
		const float innerRadius = 0.18f;
		const unsigned segmentCount = 32;
		Float3 vertices[segmentCount*6];
		for (unsigned q=0; q<segmentCount; ++q) {
			float t = q / float(segmentCount-1) * 2.f * gPI;
			float t2 = (q+1) / float(segmentCount-1) * 2.f * gPI;
			float s, c, s2, c2; std::tie(s, c) = XlSinCos(t); std::tie(s2, c2) = XlSinCos(t2);
			vertices[q*6+0] = position + outerRadius * s * camUp + outerRadius * c * camRight;
			vertices[q*6+1] = position + outerRadius * s2 * camUp + outerRadius * c2 * camRight;
			vertices[q*6+2] = position + innerRadius * s * camUp + innerRadius * c * camRight;

			vertices[q*6+3] = position + innerRadius * s * camUp + innerRadius * c * camRight;
			vertices[q*6+4] = position + outerRadius * s2 * camUp + outerRadius * c2 * camRight;
			vertices[q*6+5] = position + innerRadius * s2 * camUp + innerRadius * c2 * camRight;
		}
		context.DrawTriangles(RenderOverlays::ProjectionMode::P3D, vertices, dimof(vertices), col);
	}

	static const RenderCore::UniformsStreamInterface s_usiCubeMapVis = RenderCore::UniformsStreamInterface{}.BindResourceView(0, "CubeMap"_h);

	void    ShadowProbesDisplay::Render(IOverlayContext& context, Layout& layout, Interactables& interactables, InterfaceState& interfaceState)
	{
		using namespace RenderOverlays;
		using namespace RenderCore;
		const unsigned lineHeight = 20;
		const auto titleBkground = RenderOverlays::ColorB { 51, 51, 51 };

		{
			layout.SetDirection(Layout::Direction::Column);
			auto titleRect = layout.Allocate(30);
			FillRectangle(context, titleRect, titleBkground);
			titleRect._topLeft[0] += 8;
			auto* font = _headingFont->TryActualize();
			if (font)
				DrawText()
					.Font(**font)
					.Color({ 191, 123, 0 })
					.Alignment(RenderOverlays::TextAlignment::Left)
					.Flags(RenderOverlays::DrawTextFlags::Shadow)
					.Draw(context, titleRect, "Shadow Probes");
		}

		LightingEngine::Internal::IDynamicShadowProbeSchedulerMetrics* metricsInterface = nullptr;
		if (auto* lightScene = RenderCore::LightingEngine::TryGetLightScene(*_technique))
			metricsInterface = (LightingEngine::Internal::IDynamicShadowProbeSchedulerMetrics*)lightScene->QueryInterface(TypeHashCode<LightingEngine::Internal::IDynamicShadowProbeSchedulerMetrics>);

		if (metricsInterface) {
			auto metrics = metricsInterface->GetMetrics();

			// write some key metrics
			{
				char buffer[256];
				DrawText().FormatAndDraw(context, layout.Allocate(lineHeight), StringMeldInPlace(buffer) << "Probe table allocation: " << Utility::ByteCount{metrics._probeTableSizeBytes});
				DrawText().FormatAndDraw(context, layout.Allocate(lineHeight), StringMeldInPlace(buffer) << "Faces used: " << metrics._probeTableFaceUsed << " / " << metrics._probeTableFaceReserved);
				DrawText().FormatAndDraw(context, layout.Allocate(lineHeight), StringMeldInPlace(buffer) << "Active light count: " << metrics._activeLights.size());
				DrawText().FormatAndDraw(context, layout.Allocate(lineHeight), StringMeldInPlace(buffer) << "Active light clusters: " << metrics._clusters.size());
				DrawText().FormatAndDraw(context, layout.Allocate(lineHeight), StringMeldInPlace(buffer) << "Ave cluster count: " << metrics._activeLights.size()/float(metrics._clusters.size()));
			}

			// Draw indicators for the lights and clusters
			if (auto* parsingContext = context.GetService<Techniques::ParsingContext>()) {
				const auto& camToWorld = parsingContext->GetProjectionDesc()._cameraToWorld;
				auto camRight = ExtractRight_Cam(camToWorld), camUp = ExtractUp_Cam(camToWorld);
				for (auto& l:metrics._activeLights)
					DrawCircle(context, l._position, camRight, camUp, ColorB{200, 200, 200});		// rgb(200, 200, 200)

				// Recalculate the cluster boundaries and draw bounding boxes
				for (auto& cluster:metrics._clusters) {
					Float3 clusterMins { std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
					Float3 clusterMaxs { std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest() };
					auto clusterToWorld = MakeObjectToWorld(cluster._mainAxis, Float3{0,0,1}, Float3{0,0,0});

					for (auto& a:metrics._activeLights)
						if (a._clusterIndex == &cluster-metrics._clusters.data()) {
							// sphere rules
							auto p = TransformPointByOrthonormalInverse(clusterToWorld, a._position);
							clusterMins[0] = std::min(clusterMins[0], p[0] - a._radius);
							clusterMins[1] = std::min(clusterMins[1], p[1] - a._radius);
							clusterMins[2] = std::min(clusterMins[2], p[2] - a._radius);
							clusterMaxs[0] = std::max(clusterMaxs[0], p[0] + a._radius);
							clusterMaxs[1] = std::max(clusterMaxs[1], p[1] + a._radius);
							clusterMaxs[2] = std::max(clusterMaxs[2], p[2] + a._radius);
						}

					std::vector<Float3> corners {
						{clusterMins[0], clusterMins[1], clusterMins[2]},
						{clusterMins[0], clusterMaxs[1], clusterMins[2]},
						{clusterMaxs[0], clusterMins[1], clusterMins[2]},
						{clusterMaxs[0], clusterMaxs[1], clusterMins[2]},
						{clusterMins[0], clusterMins[1], clusterMaxs[2]},
						{clusterMins[0], clusterMaxs[1], clusterMaxs[2]},
						{clusterMaxs[0], clusterMins[1], clusterMaxs[2]},
						{clusterMaxs[0], clusterMaxs[1], clusterMaxs[2]}
					};
					for (auto& c:corners) c = TransformPoint(clusterToWorld, c);

					struct Edge { unsigned f0, f1, v0, v1; };
					Edge frustumEdges[] {
						{ 0, 4, 2, 0 },        // front & top
						{ 0, 2, 0, 1 },        // front & x=-1
						{ 0, 5, 1, 3 },        // front & bottom
						{ 0, 3, 3, 2 },        // front & x=1

						{ 1, 4, 4, 6 },        // back & top
						{ 1, 3, 6, 7 },        // back & x=1
						{ 1, 5, 7, 5 },        // back & bottom
						{ 1, 2, 5, 4 },        // back & x=1

						{ 2, 4, 0, 4 },        // x=-1 & top
						{ 2, 5, 5, 1 },        // x=-1 & bottom

						{ 3, 4, 6, 2 },        // x=1 & top
						{ 3, 5, 3, 7 }         // x=1 & bottom
					};

					Float3 lines[dimof(frustumEdges)*2];
					for (unsigned c=0; c<dimof(frustumEdges); ++c) {
						lines[c*2+0] = corners[frustumEdges[c].v0];
						lines[c*2+1] = corners[frustumEdges[c].v1];
					}
					context.DrawLines(ProjectionMode::P3D, lines, dimof(lines), ColorB{215, 100, 100}, 3.f);  // rgba(215, 100, 100, 1)
				}
			}

			if (_futureResources.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
				auto& resources = _futureResources.get();

				auto rect = layout.AllocateFullWidth(700);
				auto idealWidth = rect.Height() * 4 / 3;
				if (rect.Width() > idealWidth) rect = { rect._topLeft[0] + (rect.Width()-idealWidth)/2, rect._topLeft[1], rect._topLeft[0] + (rect.Width()+idealWidth)/2, rect._bottomRight[1] };
				RenderCore::Techniques::RetainedUniformsStream uniforms; uniforms._resourceViews.emplace_back(metricsInterface->GetCubeMapSRV(0));
				auto vertices = context.GetImmediateDrawables().QueueDraw(
					6, sizeof(Vertex_PT), *resources._depthMapVisPipeline, *resources._depthMapVisDS, &s_usiCubeMapVis, std::move(uniforms)).Cast<Vertex_PT*>();
				vertices[0] = Vertex_PT{ AsPixelCoords(rect._topLeft[0], rect._topLeft[1]), Float2{0.f, 0.f} };
				vertices[1] = Vertex_PT{ AsPixelCoords(rect._topLeft[0], rect._bottomRight[1]), Float2{0.f, 1.f} };
				vertices[2] = Vertex_PT{ AsPixelCoords(rect._bottomRight[0], rect._topLeft[1]), Float2{1.f, 0.f} };

				vertices[3] = Vertex_PT{ AsPixelCoords(rect._bottomRight[0], rect._topLeft[1]), Float2{1.f, 0.f} };
				vertices[4] = Vertex_PT{ AsPixelCoords(rect._topLeft[0], rect._bottomRight[1]), Float2{0.f, 1.f} };
				vertices[5] = Vertex_PT{ AsPixelCoords(rect._bottomRight[0], rect._bottomRight[1]), Float2{1.f, 1.f} };
			}
			
		} else {
			DrawText().FormatAndDraw(context, layout.Allocate(lineHeight), "No metrics interface for dynamic shadow probes");
		}
	}

	auto    ShadowProbesDisplay::ProcessInput(InterfaceState& interfaceState, const InputSnapshot& input) -> ProcessInputResult
	{
		return ProcessInputResult::Passthrough;
	}

	ShadowProbesDisplay::ShadowProbesDisplay(
		std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> overlayAccelerators,
		std::shared_ptr<RenderCore::LightingEngine::CompiledLightingTechnique> technique)
	: _technique(std::move(technique))
	{
		_headingFont = RenderOverlays::MakeFont("DosisExtraBold", 20);

		std::promise<Resources> promisedResources;
		_futureResources = promisedResources.get_future();

		auto util = std::make_shared<AssetsNew::CompoundAssetUtil>();
		::AssetsNew::ContextAndIdentifier indexer { "xleres/RenderOverlays/DepthMapVis.hlsl:cubeMapVis" };
		auto inputAssembly = RenderOverlays::Vertex_PT::s_inputElements2D;
		auto topology = RenderCore::Topology::TriangleList;

		auto futureMaterial = util->GetFuture<RenderCore::Assets::RawMaterial>("RawMaterial"_h, indexer);
		auto futureShaderPatches = util->GetFuture<std::shared_ptr<RenderCore::Assets::ShaderPatchCollection>>("ShaderPatchCollection"_h, indexer);
		::Assets::WhenAll(std::move(futureMaterial), std::move(futureShaderPatches)).ThenConstructToPromise(
			std::move(promisedResources),
			[pa=std::move(overlayAccelerators), topology, ia=std::vector<RenderCore::MiniInputElementDesc>(inputAssembly.begin(), inputAssembly.end()), util](const auto& material, const auto& shaderPatches) {
				Resources result;
				RenderCore::Assets::RawMaterial rawMat = std::get<0>(std::move(material));
				DescriptorSetConstructorHelper descSetHelper { std::move(rawMat) };
				result._depthMapVisDS = pa->CreateDescriptorSetAccelerator(nullptr, descSetHelper._patchCollection, descSetHelper._matDescSet, descSetHelper._matMachine, descSetHelper._matScaffold, "depth-map-vis");
				result._depthMapVisPipeline = pa->CreatePipelineAccelerator(shaderPatches, descSetHelper._matDescSet, std::move(descSetHelper._matMachineDecomposed._matSelectors), ia, topology, descSetHelper._matMachineDecomposed._stateSet);
				return result;
			});
	}

	ShadowProbesDisplay::~ShadowProbesDisplay()
	{
	}

	std::shared_ptr<RenderOverlays::DebuggingDisplay::IWidget> CreateShadowProbesDisplay(std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> overlayAccelerators, std::shared_ptr<RenderCore::LightingEngine::CompiledLightingTechnique> technique)
	{
		return std::make_shared<ShadowProbesDisplay>(std::move(overlayAccelerators), std::move(technique));
	}

}}

