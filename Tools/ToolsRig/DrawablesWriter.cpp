// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DrawablesWriter.h"
#include "VisualisationGeo.h"
#include "../../RenderCore/Techniques/TechniqueUtils.h"
#include "../../RenderCore/Techniques/PipelineAccelerator.h"
#include "../../RenderCore/Techniques/ParsingContext.h"
#include "../../RenderCore/Assets/RawMaterial.h"
#include "../../RenderCore/Assets/ScaffoldCmdStream.h"
#include "../../RenderCore/IDevice.h"
#include "../../RenderCore/UniformsStream.h"
#include "../../Math/Matrix.h"
#include "../../Math/Transformations.h"
#include "../../Math/ProjectionMath.h"
#include "../../Utility/ArithmeticUtils.h"
#include <memory>

namespace ToolsRig
{
	static std::shared_ptr<RenderCore::IResource> CreateVB(RenderCore::IDevice& device, IteratorRange<const void*> data)
	{
		using namespace RenderCore;
		return device.CreateResource(
			CreateDesc(
				BindFlag::VertexBuffer,
				LinearBufferDesc::Create((unsigned)data.size()),
				"vertex-buffer"),
			SubResourceInitData { data });
	}

	static std::pair<std::shared_ptr<RenderCore::Techniques::DrawableGeo>, size_t> CreateSphereGeo(RenderCore::IDevice& device, RenderCore::Techniques::IDrawablesPool& drawablesPool)
	{
		auto sphereGeo = ToolsRig::BuildGeodesicSphere();
		auto sphereVb = CreateVB(device, sphereGeo);
		auto geo = drawablesPool.CreateGeo();
		geo->_vertexStreams[0]._resource = sphereVb;
		geo->_vertexStreamCount = 1;
		return {geo, sphereGeo.size()};
	}

	static std::pair<std::shared_ptr<RenderCore::Techniques::DrawableGeo>, size_t> CreateCubeGeo(RenderCore::IDevice& device, RenderCore::Techniques::IDrawablesPool& drawablesPool)
	{
		auto cubeGeo = ToolsRig::BuildCube();
		auto cubeVb = CreateVB(device, cubeGeo);
		auto geo = drawablesPool.CreateGeo();
		geo->_vertexStreams[0]._resource = cubeVb;
		geo->_vertexStreamCount = 1;
		return {geo, cubeGeo.size()};
	}

	static std::pair<std::shared_ptr<RenderCore::Techniques::DrawableGeo>, size_t> CreateTriangleBasePyramidGeo(RenderCore::IDevice& device, RenderCore::Techniques::IDrawablesPool& drawablesPool)
	{
		auto pyramidGeo = ToolsRig::BuildTriangleBasePyramid();
		auto pyramidVb = CreateVB(device, pyramidGeo);
		auto geo = drawablesPool.CreateGeo();
		geo->_vertexStreams[0]._resource = pyramidVb;
		geo->_vertexStreamCount = 1;
		return {geo, pyramidGeo.size()};
	}

	static RenderCore::UniformsStreamInterface MakeLocalTransformUSI()
	{
		RenderCore::UniformsStreamInterface result;
		result.BindImmediateData(0, Hash64("LocalTransform"));
		return result;
	}
	RenderCore::UniformsStreamInterface s_localTransformUSI = MakeLocalTransformUSI();

	class DrawablesWriterCommon : public IDrawablesWriter
	{
	public:
		std::shared_ptr<RenderCore::Techniques::PipelineAccelerator> _pipelineAccelerator;
		std::shared_ptr<RenderCore::Techniques::DescriptorSetAccelerator> _descriptorSetAccelerator;

		void WriteDrawable(
			RenderCore::Techniques::DrawablesPacket& pkt, 
			const RenderCore::Techniques::DrawableGeo& geo,
			size_t vertexCount,
			const Float4x4& localToWorld,
			uint32_t viewMask)
		{
			struct CustomDrawable : public RenderCore::Techniques::Drawable { unsigned _vertexCount; Float4x4 _localToWorld; uint32_t _viewMask; };
			auto* drawables = pkt._drawables.Allocate<CustomDrawable>(1);
			drawables[0]._pipeline = _pipelineAccelerator.get();
			drawables[0]._descriptorSet = _descriptorSetAccelerator.get();
			drawables[0]._geo = &geo;
			drawables[0]._vertexCount = vertexCount;
			drawables[0]._looseUniformsInterface = &s_localTransformUSI;
			drawables[0]._localToWorld = localToWorld;
			drawables[0]._viewMask = viewMask;
			drawables[0]._drawFn = [](RenderCore::Techniques::ParsingContext& parsingContext, const RenderCore::Techniques::ExecuteDrawableContext& drawFnContext, const RenderCore::Techniques::Drawable& drawable)
				{
					auto localTransform = RenderCore::Techniques::MakeLocalTransform(((CustomDrawable&)drawable)._localToWorld, ExtractTranslation(parsingContext.GetProjectionDesc()._cameraToWorld), ((CustomDrawable&)drawable)._viewMask);
					drawFnContext.ApplyLooseUniforms(RenderCore::ImmediateDataStream(localTransform));

					auto viewMask = ((CustomDrawable&)drawable)._viewMask;
					unsigned v=0;
					while (viewMask) {
						auto lz = xl_ctz8(viewMask);
						v++;
						viewMask ^= 1ull<<lz;
					}
					unsigned viewCount = v; 
					if (!viewCount) return;

					if (viewCount == 1) drawFnContext.Draw(((CustomDrawable&)drawable)._vertexCount);
					else drawFnContext.DrawInstances(((CustomDrawable&)drawable)._vertexCount, viewCount);
				};
		}

		void WriteDrawable(
			RenderCore::Techniques::DrawablesPacket& pkt, 
			const RenderCore::Techniques::DrawableGeo& geo,
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
			drawables[0]._pipeline = _pipelineAccelerator.get();
			drawables[0]._descriptorSet = _descriptorSetAccelerator.get();
			drawables[0]._geo = &geo;
			drawables[0]._vertexCount = vertexCount;
			drawables[0]._looseUniformsInterface = &s_localTransformUSI;
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

		DrawablesWriterCommon(
			RenderCore::IDevice& device,
			RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAcceleratorPool)
		{
			_pipelineAccelerator = pipelineAcceleratorPool.CreatePipelineAccelerator(
				nullptr,
				ParameterBox {},
				ToolsRig::Vertex3D_InputLayout,
				RenderCore::Topology::TriangleList,
				RenderCore::Assets::RenderStateSet{});

			_descriptorSetAccelerator = pipelineAcceleratorPool.CreateDescriptorSetAccelerator(
				nullptr, nullptr,
				{}, {}, {});
		}
	};

	class SphereDrawableWriter : public DrawablesWriterCommon
	{
	public:
		std::shared_ptr<RenderCore::Techniques::DrawableGeo> _geo;
		size_t _vertexCount;

		void WriteDrawables(RenderCore::Techniques::DrawablesPacket& pkt, uint32_t viewMask)
		{
			WriteDrawable(pkt, *_geo, _vertexCount, Identity<Float4x4>(), viewMask);
		}

		SphereDrawableWriter(RenderCore::IDevice& device, RenderCore::Techniques::IDrawablesPool& drawablesPool, RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAcceleratorPool)
		: DrawablesWriterCommon(device, pipelineAcceleratorPool)
		{
			std::tie(_geo, _vertexCount) = CreateSphereGeo(device, drawablesPool);
		}
	};

	std::shared_ptr<IDrawablesWriter> DrawablesWriterHelper::CreateSphereDrawablesWriter()
	{
		return std::make_shared<SphereDrawableWriter>(*_device, *_drawablesPool, *_pipelineAcceleratorPool);
	}

	class ShapeStackDrawableWriter : public DrawablesWriterCommon
	{
	public:
		std::shared_ptr<RenderCore::Techniques::DrawableGeo> _sphereGeo, _cubeGeo;
		size_t _sphereVertexCount, _cubeVertexCount;

		void WriteDrawables(RenderCore::Techniques::DrawablesPacket& pkt, uint32_t viewMask)
		{
			WriteDrawable(
				pkt,
				*_sphereGeo, _sphereVertexCount, 
				AsFloat4x4(Float3{0.f, 1.0f + std::sqrt(8.0f)/2.0f, 0.f}), viewMask);

			Float4x4 transform = Identity<Float4x4>();
			Combine_IntoLHS(transform, RotationY{gPI / 4.0f});
			Combine_IntoLHS(transform, RotationZ{gPI / 4.0f});
			WriteDrawable(
				pkt,
				*_cubeGeo, _cubeVertexCount, 
				transform, viewMask);

			WriteDrawable(
				pkt,
				*_cubeGeo, _cubeVertexCount, 
				AsFloat4x4(Float3{0.f, -1.0f - std::sqrt(8.0f)/2.0f, 0.f}), viewMask);
		}

		ShapeStackDrawableWriter(RenderCore::IDevice& device, RenderCore::Techniques::IDrawablesPool& drawablesPool, RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAcceleratorPool)
		: DrawablesWriterCommon(device, pipelineAcceleratorPool)
		{
			std::tie(_sphereGeo, _sphereVertexCount) = CreateSphereGeo(device, drawablesPool);
			std::tie(_cubeGeo, _cubeVertexCount) = CreateCubeGeo(device, drawablesPool);
		}
	};

	std::shared_ptr<IDrawablesWriter> DrawablesWriterHelper::CreateShapeStackDrawableWriter()
	{
		return std::make_shared<ShapeStackDrawableWriter>(*_device, *_drawablesPool, *_pipelineAcceleratorPool);
	}

	class StonehengeDrawableWriter : public DrawablesWriterCommon
	{
	public:
		std::shared_ptr<RenderCore::Techniques::DrawableGeo> _geo;
		size_t _vertexCount;

		void WriteDrawables(RenderCore::Techniques::DrawablesPacket& pkt, uint32_t viewMask)
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
					*_geo, _vertexCount, 
					transform, viewMask);
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
				*_geo, _vertexCount, 
				baseTransform, viewMask);
		}

		StonehengeDrawableWriter(RenderCore::IDevice& device, RenderCore::Techniques::IDrawablesPool& drawablesPool, RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAcceleratorPool)
		: DrawablesWriterCommon(device, pipelineAcceleratorPool)
		{
			std::tie(_geo, _vertexCount) = CreateCubeGeo(device, drawablesPool);
		}
	};

	std::shared_ptr<IDrawablesWriter> DrawablesWriterHelper::CreateStonehengeDrawableWriter()
	{
		return std::make_shared<StonehengeDrawableWriter>(*_device, *_drawablesPool, *_pipelineAcceleratorPool);
	}

	class FlatPlaneDrawableWriter : public DrawablesWriterCommon
	{
	public:
		std::shared_ptr<RenderCore::Techniques::DrawableGeo> _geo;
		size_t _vertexCount;

		void WriteDrawables(RenderCore::Techniques::DrawablesPacket& pkt, uint32_t viewMask)
		{
			ScaleRotationTranslationM srt {
				Float3 { 1000.f, 1.0f, 1000.f },
				Identity<Float3x3>(),
				Float3 { 0.f, -1.0f, 0.f }
			};
			WriteDrawable(
				pkt,
				*_geo, _vertexCount, 
				AsFloat4x4(srt), viewMask);
		}

		FlatPlaneDrawableWriter(RenderCore::IDevice& device, RenderCore::Techniques::IDrawablesPool& drawablesPool, RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAcceleratorPool)
		: DrawablesWriterCommon(device, pipelineAcceleratorPool)
		{
			std::tie(_geo, _vertexCount) = CreateCubeGeo(device, drawablesPool);
		}
	};

	std::shared_ptr<IDrawablesWriter> DrawablesWriterHelper::CreateFlatPlaneDrawableWriter()
	{
		return std::make_shared<FlatPlaneDrawableWriter>(*_device, *_drawablesPool, *_pipelineAcceleratorPool);
	}

	class FlatPlaneAndBlockerDrawableWriter : public FlatPlaneDrawableWriter
	{
	public:
		void WriteDrawables(RenderCore::Techniques::DrawablesPacket& pkt, uint32_t viewMask)
		{
			FlatPlaneDrawableWriter::WriteDrawables(pkt, viewMask);

			ScaleRotationTranslationM srt {
				Float3 { 1.f, 1.0f, 1.f },
				Identity<Float3x3>(),
				Float3 { 0.f, 15.0f, 0.f }
			};
			WriteDrawable(pkt, *_geo, _vertexCount, AsFloat4x4(srt), viewMask);
		}

		using FlatPlaneDrawableWriter::FlatPlaneDrawableWriter;
	};

	std::shared_ptr<IDrawablesWriter> DrawablesWriterHelper::CreateFlatPlaneAndBlockerDrawableWriter()
	{
		return std::make_shared<FlatPlaneAndBlockerDrawableWriter>(*_device, *_drawablesPool, *_pipelineAcceleratorPool);
	}

	class SharpContactDrawableWriter : public DrawablesWriterCommon
	{
	public:
		std::shared_ptr<RenderCore::Techniques::DrawableGeo> _sphereGeo, _pyramidGeo;
		size_t _sphereVertexCount, _pyramidVertexCount;

		void WriteDrawables(RenderCore::Techniques::DrawablesPacket& pkt, uint32_t viewMask)
		{
			WriteDrawable(pkt, *_sphereGeo, _sphereVertexCount, AsFloat4x4(Float3{0.f, 0.0f, 1.f}), viewMask);
			auto pyramidTransform = MakeObjectToWorld(Float3{0.f, 0.0f, 1.f}, Float3{1.f, 0.f, 0.f}, Float3{0.f, 0.f, -1.f});
			Combine_IntoLHS(pyramidTransform, RotationZ(-gPI/4.0f));
			
			// squeeze in local X & Z to a finer point 
			Combine_IntoRHS(ArbitraryScale(Float3{0.1f, 1.0f, 0.1f}), pyramidTransform);
			
			// Rotate a small amount around X (towards the camera), around the center of the sphere (which is at Float3{0.f, 0.0f, 1.f})
			Combine_IntoLHS(pyramidTransform, Float3{0.f, 0.0f, -1.f});
			Combine_IntoLHS(pyramidTransform, RotationX(gPI*3.0f/16.0f));
			Combine_IntoLHS(pyramidTransform, Float3{0.f, 0.0f, 1.f});

			WriteDrawable(pkt, *_pyramidGeo, _pyramidVertexCount, pyramidTransform, viewMask);
		}

		SharpContactDrawableWriter(RenderCore::IDevice& device, RenderCore::Techniques::IDrawablesPool& drawablesPool, RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAcceleratorPool)
		: DrawablesWriterCommon(device, pipelineAcceleratorPool)
		{
			std::tie(_sphereGeo, _sphereVertexCount) = CreateSphereGeo(device, drawablesPool);
			std::tie(_pyramidGeo, _pyramidVertexCount) = CreateTriangleBasePyramidGeo(device, drawablesPool);
		}
	};

	std::shared_ptr<IDrawablesWriter> DrawablesWriterHelper::CreateSharpContactDrawableWriter()
	{
		return std::make_shared<SharpContactDrawableWriter>(*_device, *_drawablesPool, *_pipelineAcceleratorPool);
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

		void WriteDrawables(RenderCore::Techniques::DrawablesPacket& pkt, uint32_t viewMask)
		{
			for (const auto& transform:_cubes) WriteDrawable(pkt, *_cubeGeo, _cubeVertexCount, transform, viewMask);
			for (const auto& transform:_spheres) WriteDrawable(pkt, *_sphereGeo, _sphereVertexCount, transform, viewMask);
			for (const auto& transform:_pyramid) WriteDrawable(pkt, *_pyramidGeo, _pyramidVertexCount, transform, viewMask);
		}

		void WriteDrawables(RenderCore::Techniques::DrawablesPacket& pkt, const std::shared_ptr<CustomDrawDelegate>& customDraw)
		{
			for (const auto& transform:_cubes) WriteDrawable(pkt, *_cubeGeo, _cubeVertexCount, transform, customDraw);
			for (const auto& transform:_spheres) WriteDrawable(pkt, *_sphereGeo, _sphereVertexCount, transform, customDraw);
			for (const auto& transform:_pyramid) WriteDrawable(pkt, *_pyramidGeo, _pyramidVertexCount, transform, customDraw);
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
					WriteDrawable(pkt, *_cubeGeo, _cubeVertexCount, transform, customDraw, boundaryViewMask);
					// WriteDrawable(pkt, _sphereGeo, _sphereVertexCount, transform, customDraw, boundaryViewMask);
				++bb;
			}
			bb=_sphereBoundingBoxes.begin();
			for (const auto& transform:_spheres) {
				uint64_t withinViewMask = 0, boundaryViewMask = 0;
				cullingDelegate.TestAABB(boundaryViewMask, withinViewMask, viewMask, bb->first, bb->second);
				boundaryViewMask |= withinViewMask;
				if (boundaryViewMask)
					WriteDrawable(pkt, *_sphereGeo, _sphereVertexCount, transform, customDraw, boundaryViewMask);
					// WriteDrawable(pkt, _cubeGeo, _cubeVertexCount, transform, customDraw, boundaryViewMask);
				++bb;
			}
			bb=_pyramidBoundingBoxes.begin();
			for (const auto& transform:_pyramid) {
				uint64_t withinViewMask = 0, boundaryViewMask = 0;
				cullingDelegate.TestAABB(boundaryViewMask, withinViewMask, viewMask, bb->first, bb->second);
				boundaryViewMask |= withinViewMask;
				if (boundaryViewMask)
					WriteDrawable(pkt, *_pyramidGeo, _pyramidVertexCount, transform, customDraw, boundaryViewMask);
					// WriteDrawable(pkt, _sphereGeo, _sphereVertexCount, transform, customDraw, boundaryViewMask);
				++bb;
			}
		}

		void WriteDrawables(
			RenderCore::Techniques::DrawablesPacket& pkt,
			const Float4x4& cullingVolume,
			uint32_t viewMask)
		{
			auto bb=_cubeBoundingBoxes.begin();
			for (const auto& transform:_cubes) {
				if (!CullAABB(cullingVolume, bb->first, bb->second, RenderCore::Techniques::GetDefaultClipSpaceType())) 
					WriteDrawable(pkt, *_cubeGeo, _cubeVertexCount, transform, viewMask);
				++bb;
			}

			bb=_sphereBoundingBoxes.begin();
			for (const auto& transform:_spheres) {
				if (!CullAABB(cullingVolume, bb->first, bb->second, RenderCore::Techniques::GetDefaultClipSpaceType())) 
					WriteDrawable(pkt, *_sphereGeo, _sphereVertexCount, transform, viewMask);
				++bb;
			}

			bb=_pyramidBoundingBoxes.begin();
			for (const auto& transform:_pyramid) {
				if (!CullAABB(cullingVolume, bb->first, bb->second, RenderCore::Techniques::GetDefaultClipSpaceType())) 
					WriteDrawable(pkt, *_pyramidGeo, _pyramidVertexCount, transform, viewMask);
				++bb;
			}
		}

		ShapeWorldDrawableWriter(
			RenderCore::IDevice& device, RenderCore::Techniques::IDrawablesPool& drawablesPool, RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAcceleratorPool,
			const Float2& worldMins, const Float2& worldMaxs)
		: DrawablesWriterCommon(device, pipelineAcceleratorPool)
		{
			std::tie(_sphereGeo, _sphereVertexCount) = CreateSphereGeo(device, drawablesPool);
			std::tie(_pyramidGeo, _pyramidVertexCount) = CreateTriangleBasePyramidGeo(device, drawablesPool);
			std::tie(_cubeGeo, _cubeVertexCount) = CreateCubeGeo(device, drawablesPool);

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

	std::shared_ptr<IDrawablesWriter> DrawablesWriterHelper::CreateShapeWorldDrawableWriter(
		const Float2& worldMins, const Float2& worldMaxs)
	{
		return std::make_shared<ShapeWorldDrawableWriter>(*_device, *_drawablesPool, *_pipelineAcceleratorPool, worldMins, worldMaxs);
	}

	DrawablesWriterHelper::DrawablesWriterHelper(
		RenderCore::IDevice& device, RenderCore::Techniques::IDrawablesPool& drawablesPool,
		RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAcceleratorPool)
	: _device(&device), _drawablesPool(&drawablesPool), _pipelineAcceleratorPool(&pipelineAcceleratorPool)
	{}
    
}
