// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DrawablesWriter.h"
#include "VisualisationGeo.h"
#include "../../RenderCore/Techniques/TechniqueUtils.h"
#include "../../RenderCore/Techniques/PipelineAccelerator.h"
#include "../../RenderCore/Techniques/ParsingContext.h"
#include "../../RenderCore/IDevice.h"
#include "../../RenderCore/UniformsStream.h"
#include "../../Math/Matrix.h"
#include "../../Math/Transformations.h"
#include "../../Math/ProjectionMath.h"
#include <memory>

namespace ToolsRig
{
	static std::shared_ptr<RenderCore::IResource> CreateVB(RenderCore::IDevice& device, IteratorRange<const void*> data)
	{
		using namespace RenderCore;
		return device.CreateResource(
			CreateDesc(
				BindFlag::VertexBuffer, 0, GPUAccess::Read,
				LinearBufferDesc::Create((unsigned)data.size()),
				"vertex-buffer"),
			SubResourceInitData { data });
	}

	static std::pair<std::shared_ptr<RenderCore::Techniques::DrawableGeo>, size_t> CreateSphereGeo(RenderCore::IDevice& device)
	{
		auto sphereGeo = ToolsRig::BuildGeodesicSphere();
		auto sphereVb = CreateVB(device, sphereGeo);
		auto geo = std::make_shared<RenderCore::Techniques::DrawableGeo>();
		geo->_vertexStreams[0]._resource = sphereVb;
		geo->_vertexStreamCount = 1;
		return {geo, sphereGeo.size()};
	}

	static std::pair<std::shared_ptr<RenderCore::Techniques::DrawableGeo>, size_t> CreateCubeGeo(RenderCore::IDevice& device)
	{
		auto cubeGeo = ToolsRig::BuildCube();
		auto cubeVb = CreateVB(device, cubeGeo);
		auto geo = std::make_shared<RenderCore::Techniques::DrawableGeo>();
		geo->_vertexStreams[0]._resource = cubeVb;
		geo->_vertexStreamCount = 1;
		return {geo, cubeGeo.size()};
	}

	static std::pair<std::shared_ptr<RenderCore::Techniques::DrawableGeo>, size_t> CreateTriangleBasePyramidGeo(RenderCore::IDevice& device)
	{
		auto pyramidGeo = ToolsRig::BuildTriangleBasePyramid();
		auto pyramidVb = CreateVB(device, pyramidGeo);
		auto geo = std::make_shared<RenderCore::Techniques::DrawableGeo>();
		geo->_vertexStreams[0]._resource = pyramidVb;
		geo->_vertexStreamCount = 1;
		return {geo, pyramidGeo.size()};
	}

	class DrawablesWriterCommon : public IDrawablesWriter
	{
	public:
		std::shared_ptr<RenderCore::Techniques::PipelineAccelerator> _pipelineAccelerator;
		std::shared_ptr<RenderCore::Techniques::DescriptorSetAccelerator> _descriptorSetAccelerator;
		std::shared_ptr<RenderCore::UniformsStreamInterface> _usi;

		void WriteDrawable(
			RenderCore::Techniques::DrawablesPacket& pkt, 
			std::shared_ptr<RenderCore::Techniques::DrawableGeo> geo,
			size_t vertexCount,
			const Float4x4& localToWorld)
		{
			struct CustomDrawable : public RenderCore::Techniques::Drawable { unsigned _vertexCount; Float4x4 _localToWorld; };
			auto* drawables = pkt._drawables.Allocate<CustomDrawable>(1);
			drawables[0]._pipeline = _pipelineAccelerator;
			drawables[0]._descriptorSet = _descriptorSetAccelerator;
			drawables[0]._geo = std::move(geo);
			drawables[0]._vertexCount = vertexCount;
			drawables[0]._looseUniformsInterface = _usi;
			drawables[0]._localToWorld = localToWorld;
			drawables[0]._drawFn = [](RenderCore::Techniques::ParsingContext& parsingContext, const RenderCore::Techniques::ExecuteDrawableContext& drawFnContext, const RenderCore::Techniques::Drawable& drawable)
				{
					auto localTransform = RenderCore::Techniques::MakeLocalTransform(((CustomDrawable&)drawable)._localToWorld, ExtractTranslation(parsingContext.GetProjectionDesc()._cameraToWorld));
					drawFnContext.ApplyLooseUniforms(RenderCore::ImmediateDataStream(localTransform));
					drawFnContext.Draw(((CustomDrawable&)drawable)._vertexCount);
				};
		}

		void WriteDrawable(
			RenderCore::Techniques::DrawablesPacket& pkt, 
			std::shared_ptr<RenderCore::Techniques::DrawableGeo> geo,
			size_t vertexCount,
			const Float4x4& localToWorld,
			const std::shared_ptr<IExtendedDrawablesWriter::CustomDrawDelegate>& customDrawDelegate,
			uint64_t viewMask = ~0ull)
		{
			struct CustomDrawable2 : public RenderCore::Techniques::Drawable 
			{ 
				Float4x4 _localToWorld;
				std::shared_ptr<IExtendedDrawablesWriter::CustomDrawDelegate> _customDrawDelegate;
				uint64_t _viewMask;
				unsigned _vertexCount; 
			};
			auto* drawables = pkt._drawables.Allocate<CustomDrawable2>(1);
			drawables[0]._pipeline = _pipelineAccelerator;
			drawables[0]._descriptorSet = _descriptorSetAccelerator;
			drawables[0]._geo = std::move(geo);
			drawables[0]._vertexCount = vertexCount;
			drawables[0]._looseUniformsInterface = _usi;
			drawables[0]._localToWorld = localToWorld;
			drawables[0]._customDrawDelegate = customDrawDelegate;
			drawables[0]._viewMask = viewMask;
			drawables[0]._drawFn = [](RenderCore::Techniques::ParsingContext& parsingContext, const RenderCore::Techniques::ExecuteDrawableContext& drawFnContext, const RenderCore::Techniques::Drawable& drawable)
				{
					((CustomDrawable2&)drawable)._customDrawDelegate->OnDraw(
						parsingContext, drawFnContext, drawable,
						((CustomDrawable2&)drawable)._vertexCount, ((CustomDrawable2&)drawable)._localToWorld, ((CustomDrawable2&)drawable)._viewMask);
				};
		}

		DrawablesWriterCommon(RenderCore::IDevice& device, RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAcceleratorPool)
		{
			_usi = std::make_shared<RenderCore::UniformsStreamInterface>();
			_usi->BindImmediateData(0, Hash64("LocalTransform"));

			_pipelineAccelerator = pipelineAcceleratorPool.CreatePipelineAccelerator(
				nullptr,
				ParameterBox {},
				ToolsRig::Vertex3D_InputLayout,
				RenderCore::Topology::TriangleList,
				RenderCore::Assets::RenderStateSet{});

			_descriptorSetAccelerator = pipelineAcceleratorPool.CreateDescriptorSetAccelerator(
				nullptr,
				{}, {}, {});
		}
	};

	class SphereDrawableWriter : public DrawablesWriterCommon
	{
	public:
		std::shared_ptr<RenderCore::Techniques::DrawableGeo> _geo;
		size_t _vertexCount;

		void WriteDrawables(RenderCore::Techniques::DrawablesPacket& pkt)
		{
			WriteDrawable(pkt, _geo, _vertexCount, Identity<Float4x4>());
		}

		SphereDrawableWriter(RenderCore::IDevice& device, RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAcceleratorPool)
		: DrawablesWriterCommon(device, pipelineAcceleratorPool)
		{
			std::tie(_geo, _vertexCount) = CreateSphereGeo(device);
		}
	};

	std::shared_ptr<IDrawablesWriter> CreateSphereDrawablesWriter(RenderCore::IDevice& device, RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAcceleratorPool)
	{
		return std::make_shared<SphereDrawableWriter>(device, pipelineAcceleratorPool);
	}

	class ShapeStackDrawableWriter : public DrawablesWriterCommon
	{
	public:
		std::shared_ptr<RenderCore::Techniques::DrawableGeo> _sphereGeo, _cubeGeo;
		size_t _sphereVertexCount, _cubeVertexCount;

		void WriteDrawables(RenderCore::Techniques::DrawablesPacket& pkt)
		{
			WriteDrawable(
				pkt,
				_sphereGeo, _sphereVertexCount, 
				AsFloat4x4(Float3{0.f, 1.0f + std::sqrt(8.0f)/2.0f, 0.f}));

			Float4x4 transform = Identity<Float4x4>();
			Combine_IntoLHS(transform, RotationY{gPI / 4.0f});
			Combine_IntoLHS(transform, RotationZ{gPI / 4.0f});
			WriteDrawable(
				pkt,
				_cubeGeo, _cubeVertexCount, 
				transform);

			WriteDrawable(
				pkt,
				_cubeGeo, _cubeVertexCount, 
				AsFloat4x4(Float3{0.f, -1.0f - std::sqrt(8.0f)/2.0f, 0.f}));
		}

		ShapeStackDrawableWriter(RenderCore::IDevice& device, RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAcceleratorPool)
		: DrawablesWriterCommon(device, pipelineAcceleratorPool)
		{
			std::tie(_sphereGeo, _sphereVertexCount) = CreateSphereGeo(device);
			std::tie(_cubeGeo, _cubeVertexCount) = CreateCubeGeo(device);
		}
	};

	std::shared_ptr<IDrawablesWriter> CreateShapeStackDrawableWriter(RenderCore::IDevice& device, RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAcceleratorPool)
	{
		return std::make_shared<ShapeStackDrawableWriter>(device, pipelineAcceleratorPool);
	}

	class StonehengeDrawableWriter : public DrawablesWriterCommon
	{
	public:
		std::shared_ptr<RenderCore::Techniques::DrawableGeo> _geo;
		size_t _vertexCount;

		void WriteDrawables(RenderCore::Techniques::DrawablesPacket& pkt)
		{
			const unsigned stoneCount = 32;
			
			const float radius = 1.0f;
			const float circumference = 2.0f * gPI * radius;
			const float stoneWidth = circumference / 2.0f / stoneCount;
			for (unsigned c=0; c<stoneCount; ++c) {
				float theta = 2.f * gPI * c / stoneCount;

				Float4x4 transform = Identity<Float4x4>();
				Combine_IntoLHS(transform, ArbitraryScale{Float3{stoneWidth / 2.0f * 0.2f, 1.0f, stoneWidth / 2.0f}});
				Combine_IntoLHS(transform, RotationY{gPI / 2.0f + theta});
				Combine_IntoLHS(transform, Float3{std::sin(theta), 0.f, std::cos(theta)});

				WriteDrawable(
					pkt,
					_geo, _vertexCount, 
					transform);
			}

			Float4x4 baseTransform = Identity<Float4x4>();
			Combine_IntoLHS(baseTransform, ArbitraryScale{Float3{2.0f, 0.125f, 2.0f}});
			Combine_IntoLHS(baseTransform, Float3{0.f, -0.125f, 0.f});

			static unsigned count = 0;
			if (count < 2) {
				Combine_IntoLHS(baseTransform, ArbitraryScale{Float3{1e-3f, 1e-3f, 1e-3f}});
			}
			++count;

			WriteDrawable(
				pkt,
				_geo, _vertexCount, 
				baseTransform);
		}

		StonehengeDrawableWriter(RenderCore::IDevice& device, RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAcceleratorPool)
		: DrawablesWriterCommon(device, pipelineAcceleratorPool)
		{
			std::tie(_geo, _vertexCount) = CreateCubeGeo(device);
		}
	};

	std::shared_ptr<IDrawablesWriter> CreateStonehengeDrawableWriter(RenderCore::IDevice& device, RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAcceleratorPool)
	{
		return std::make_shared<StonehengeDrawableWriter>(device, pipelineAcceleratorPool);
	}

	class FlatPlaneDrawableWriter : public DrawablesWriterCommon
	{
	public:
		std::shared_ptr<RenderCore::Techniques::DrawableGeo> _geo;
		size_t _vertexCount;

		void WriteDrawables(RenderCore::Techniques::DrawablesPacket& pkt)
		{
			ScaleRotationTranslationM srt {
				Float3 { 1000.f, 1.0f, 1000.f },
				Identity<Float3x3>(),
				Float3 { 0.f, -1.0f, 0.f }
			};
			WriteDrawable(
				pkt,
				_geo, _vertexCount, 
				AsFloat4x4(srt));
		}

		FlatPlaneDrawableWriter(RenderCore::IDevice& device, RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAcceleratorPool)
		: DrawablesWriterCommon(device, pipelineAcceleratorPool)
		{
			std::tie(_geo, _vertexCount) = CreateCubeGeo(device);
		}
	};

	std::shared_ptr<IDrawablesWriter> CreateFlatPlaneDrawableWriter(RenderCore::IDevice& device, RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAcceleratorPool)
	{
		return std::make_shared<FlatPlaneDrawableWriter>(device, pipelineAcceleratorPool);
	}

	class SharpContactDrawableWriter : public DrawablesWriterCommon
	{
	public:
		std::shared_ptr<RenderCore::Techniques::DrawableGeo> _sphereGeo, _pyramidGeo;
		size_t _sphereVertexCount, _pyramidVertexCount;

		void WriteDrawables(RenderCore::Techniques::DrawablesPacket& pkt)
		{
			WriteDrawable(pkt, _sphereGeo, _sphereVertexCount, AsFloat4x4(Float3{0.f, 0.0f, 1.f}));
			auto pyramidTransform = MakeObjectToWorld(Float3{0.f, 0.0f, 1.f}, Float3{1.f, 0.f, 0.f}, Float3{0.f, 0.f, -1.f});
			Combine_IntoLHS(pyramidTransform, RotationZ(-gPI/4.0f));
			
			// squeeze in local X & Z to a finer point 
			Combine_IntoRHS(ArbitraryScale(Float3{0.1f, 1.0f, 0.1f}), pyramidTransform);
			
			// Rotate a small amount around X (towards the camera), around the center of the sphere (which is at Float3{0.f, 0.0f, 1.f})
			Combine_IntoLHS(pyramidTransform, Float3{0.f, 0.0f, -1.f});
			Combine_IntoLHS(pyramidTransform, RotationX(gPI*3.0f/16.0f));
			Combine_IntoLHS(pyramidTransform, Float3{0.f, 0.0f, 1.f});

			WriteDrawable(pkt, _pyramidGeo, _pyramidVertexCount, pyramidTransform);
		}

		SharpContactDrawableWriter(RenderCore::IDevice& device, RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAcceleratorPool)
		: DrawablesWriterCommon(device, pipelineAcceleratorPool)
		{
			std::tie(_sphereGeo, _sphereVertexCount) = CreateSphereGeo(device);
			std::tie(_pyramidGeo, _pyramidVertexCount) = CreateTriangleBasePyramidGeo(device);
		}
	};

	std::shared_ptr<IDrawablesWriter> CreateSharpContactDrawableWriter(RenderCore::IDevice& device, RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAcceleratorPool)
	{
		return std::make_shared<SharpContactDrawableWriter>(device, pipelineAcceleratorPool);
	}

	static std::pair<Float3, Float3> CreateSphereBoundingBox(const Float3& position, float radius)
	{
		assert(radius > 0);
		return {	Float3(position) - Float3(radius, radius, radius),
					Float3(position) + Float3(radius, radius, radius) };
	}

	static std::pair<Float3, Float3> CreateRotateableCubeBoundingBox(const Float3& position, float scale)
	{
		const float sqrt3 = std::sqrt(3.f);
		return CreateSphereBoundingBox(position, scale*sqrt3);
	}

	class ShapeWorldDrawableWriter : public DrawablesWriterCommon, public IExtendedDrawablesWriter
	{
	public:
		std::shared_ptr<RenderCore::Techniques::DrawableGeo> _sphereGeo, _pyramidGeo, _cubeGeo;
		size_t _sphereVertexCount, _pyramidVertexCount, _cubeVertexCount;

		void WriteDrawables(RenderCore::Techniques::DrawablesPacket& pkt)
		{
			for (const auto& transform:_cubes) WriteDrawable(pkt, _cubeGeo, _cubeVertexCount, transform);
			for (const auto& transform:_spheres) WriteDrawable(pkt, _sphereGeo, _sphereVertexCount, transform);
			for (const auto& transform:_pyramid) WriteDrawable(pkt, _pyramidGeo, _pyramidVertexCount, transform);
		}

		void WriteDrawables(RenderCore::Techniques::DrawablesPacket& pkt, const std::shared_ptr<CustomDrawDelegate>& customDraw)
		{
			for (const auto& transform:_cubes) WriteDrawable(pkt, _cubeGeo, _cubeVertexCount, transform, customDraw);
			for (const auto& transform:_spheres) WriteDrawable(pkt, _sphereGeo, _sphereVertexCount, transform, customDraw);
			for (const auto& transform:_pyramid) WriteDrawable(pkt, _pyramidGeo, _pyramidVertexCount, transform, customDraw);
		}

		void WriteDrawables(
			RenderCore::Techniques::DrawablesPacket& pkt,
			CullingDelegate& cullingDelegate,
			uint64_t viewMask,
			const std::shared_ptr<CustomDrawDelegate>& customDraw)
		{
			auto bb=_cubeBoundingBoxes.begin();
			for (const auto& transform:_cubes) {
				uint64_t withinViewMask = 0, boundaryViewMask = 0;
				cullingDelegate.TestAABB(boundaryViewMask, withinViewMask, viewMask, bb->first, bb->second);
				boundaryViewMask |= withinViewMask;
				if (boundaryViewMask)
					WriteDrawable(pkt, _cubeGeo, _cubeVertexCount, transform, customDraw, boundaryViewMask);
					// WriteDrawable(pkt, _sphereGeo, _sphereVertexCount, transform, customDraw, boundaryViewMask);
				++bb;
			}
			bb=_sphereBoundingBoxes.begin();
			for (const auto& transform:_spheres) {
				uint64_t withinViewMask = 0, boundaryViewMask = 0;
				cullingDelegate.TestAABB(boundaryViewMask, withinViewMask, viewMask, bb->first, bb->second);
				boundaryViewMask |= withinViewMask;
				if (boundaryViewMask)
					WriteDrawable(pkt, _sphereGeo, _sphereVertexCount, transform, customDraw, boundaryViewMask);
					// WriteDrawable(pkt, _cubeGeo, _cubeVertexCount, transform, customDraw, boundaryViewMask);
				++bb;
			}
			bb=_pyramidBoundingBoxes.begin();
			for (const auto& transform:_pyramid) {
				uint64_t withinViewMask = 0, boundaryViewMask = 0;
				cullingDelegate.TestAABB(boundaryViewMask, withinViewMask, viewMask, bb->first, bb->second);
				boundaryViewMask |= withinViewMask;
				if (boundaryViewMask)
					WriteDrawable(pkt, _pyramidGeo, _pyramidVertexCount, transform, customDraw, boundaryViewMask);
					// WriteDrawable(pkt, _sphereGeo, _sphereVertexCount, transform, customDraw, boundaryViewMask);
				++bb;
			}
		}

		void WriteDrawables(
			RenderCore::Techniques::DrawablesPacket& pkt,
			const Float4x4& cullingVolume)
		{
			auto bb=_cubeBoundingBoxes.begin();
			for (const auto& transform:_cubes) {
				if (!CullAABB(cullingVolume, bb->first, bb->second, RenderCore::Techniques::GetDefaultClipSpaceType())) 
					WriteDrawable(pkt, _cubeGeo, _cubeVertexCount, transform);
				++bb;
			}

			bb=_sphereBoundingBoxes.begin();
			for (const auto& transform:_spheres) {
				if (!CullAABB(cullingVolume, bb->first, bb->second, RenderCore::Techniques::GetDefaultClipSpaceType())) 
					WriteDrawable(pkt, _sphereGeo, _sphereVertexCount, transform);
				++bb;
			}

			bb=_pyramidBoundingBoxes.begin();
			for (const auto& transform:_pyramid) {
				if (!CullAABB(cullingVolume, bb->first, bb->second, RenderCore::Techniques::GetDefaultClipSpaceType())) 
					WriteDrawable(pkt, _pyramidGeo, _pyramidVertexCount, transform);
				++bb;
			}
		}

		ShapeWorldDrawableWriter(
			RenderCore::IDevice& device, RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAcceleratorPool,
			const Float2& worldMins, const Float2& worldMaxs)
		: DrawablesWriterCommon(device, pipelineAcceleratorPool)
		{
			std::tie(_sphereGeo, _sphereVertexCount) = CreateSphereGeo(device);
			std::tie(_pyramidGeo, _pyramidVertexCount) = CreateTriangleBasePyramidGeo(device);
			std::tie(_cubeGeo, _cubeVertexCount) = CreateCubeGeo(device);

			// //// //// //// //// //// //// //
			std::mt19937_64 rng(0);
			unsigned objects = 256;
			while (objects--) {
				float scale = std::pow(2.0f, std::uniform_real_distribution<float>(-2.0f, 2.0f)(rng));
				Float3 position {
					std::uniform_real_distribution<float>(worldMins[0], worldMaxs[0])(rng),
					1.0f * scale,
					std::uniform_real_distribution<float>(worldMins[1], worldMaxs[1])(rng)};
				float yRotation = std::uniform_real_distribution<float>(-gPI, gPI)(rng);
				auto transform = AsFloat4x4(UniformScaleYRotTranslation{scale, yRotation, position});

				auto type = std::uniform_int_distribution<>(0, 2)(rng);
				if (type == 0) {
					_cubes.push_back(transform);
					_cubeBoundingBoxes.push_back(CreateRotateableCubeBoundingBox(position, scale));
				} else if (type == 1) {
					_spheres.push_back(transform);
					_sphereBoundingBoxes.push_back(CreateSphereBoundingBox(position, scale));
				} else {
					_pyramid.push_back(transform);
					_pyramidBoundingBoxes.push_back(CreateRotateableCubeBoundingBox(position, scale));
				}
			}

			// //// //// //// //// //// //// //
			auto baseTransform = AsFloat4x4(Float3{1.0f, -2.0f, 1.0f});
			auto baseScale = ArbitraryScale{Float3{(worldMaxs[0] - worldMins[0]) / 2.0f, 0.01f, (worldMaxs[1] - worldMins[1]) / 2.0f}};
			Combine_IntoLHS(baseTransform, baseScale);
			_cubes.push_back(baseTransform);
			_cubeBoundingBoxes.push_back({
				ExtractTranslation(baseTransform) - baseScale._scale,
				ExtractTranslation(baseTransform) + baseScale._scale
			});
		}

		std::vector<Float4x4> _cubes;
		std::vector<Float4x4> _spheres;
		std::vector<Float4x4> _pyramid;
		std::vector<std::pair<Float3, Float3>> _cubeBoundingBoxes;
		std::vector<std::pair<Float3, Float3>> _sphereBoundingBoxes;
		std::vector<std::pair<Float3, Float3>> _pyramidBoundingBoxes;
	};

	std::shared_ptr<IDrawablesWriter> CreateShapeWorldDrawableWriter(
		RenderCore::IDevice& device, RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAcceleratorPool,
		const Float2& worldMins, const Float2& worldMaxs)
	{
		return std::make_shared<ShapeWorldDrawableWriter>(device, pipelineAcceleratorPool, worldMins, worldMaxs);
	}
    
}
