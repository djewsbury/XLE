#include "LightStencilingGeometry.h"
#include "../Metal/DeviceContext.h"
#include "../../Tools/ToolsRig/VisualisationGeo.h"

namespace RenderCore { namespace LightingEngine
{
	void LightStencilingGeometry::CompleteInitialization(IThreadContext& threadContext)
	{
		if (_pendingGeoInitBuffer.empty()) return;
		auto& metalContext = *Metal::DeviceContext::Get(threadContext);
		auto blitEncoder = metalContext.BeginBlitEncoder();
		blitEncoder.Write(*_geo, MakeIteratorRange(_pendingGeoInitBuffer));
		blitEncoder.Write(*_lowDetailHemiSphereVB, MakeIteratorRange(_pendingLowDetailHemisphereVB));
		blitEncoder.Write(*_lowDetailHemiSphereIB, MakeIteratorRange(_pendingLowDetailHemisphereIB));
		_pendingGeoInitBuffer.clear();
		_pendingLowDetailHemisphereVB.clear();
		_pendingLowDetailHemisphereIB.clear();

		Metal::BarrierHelper(metalContext).Add(*_geo, BindFlag::TransferDst, Metal::BarrierResourceUsage::AllCommandsRead());
		Metal::BarrierHelper(metalContext).Add(*_lowDetailHemiSphereVB, BindFlag::TransferDst, Metal::BarrierResourceUsage::AllCommandsRead());
		Metal::BarrierHelper(metalContext).Add(*_lowDetailHemiSphereIB, BindFlag::TransferDst, Metal::BarrierResourceUsage::AllCommandsRead());
	}

	LightStencilingGeometry::LightStencilingGeometry(IDevice& device)
	{
		auto sphereGeo = ToolsRig::BuildRoughGeodesicHemiSphereP(4);
		auto cubeGeo = ToolsRig::BuildCubeP();
		std::vector<uint8_t> geoInitBuffer;
		geoInitBuffer.resize((sphereGeo.size() + cubeGeo.size()) * sizeof(Float3));
		std::memcpy(geoInitBuffer.data(), cubeGeo.data(), cubeGeo.size() * sizeof(Float3));
		std::memcpy(PtrAdd(geoInitBuffer.data(), cubeGeo.size() * sizeof(Float3)), sphereGeo.data(), sphereGeo.size() * sizeof(Float3));
		_geo = device.CreateResource(CreateDesc(BindFlag::VertexBuffer|BindFlag::TransferDst, LinearBufferDesc::Create((unsigned)geoInitBuffer.size())), "light-stenciling-geometry");
		_pendingGeoInitBuffer = std::move(geoInitBuffer);
		_cubeOffsetAndCount = {0u, (unsigned)cubeGeo.size()};
		_sphereOffsetAndCount = {(unsigned)cubeGeo.size(), (unsigned)sphereGeo.size()};

		auto lowDetailHemi = ToolsRig::BuildIndexedRoughGeodesicHemiSphereP(0);
		// float b = tan(pi/6)/tan(2*pi/6);
		// float c = 1.0/cos(pi/6);
		// float underestimationFactor = c*sin(pi/3)/(1+b);
		//								= sin(pi/3)*cos(pi/6);
		float underestimationFactor = std::sin(gPI/3.0f) * std::cos(gPI/6.0f);
		for (auto& pt:lowDetailHemi.second) pt /= underestimationFactor;
		_lowDetailHemiSphereVB = device.CreateResource(CreateDesc(BindFlag::VertexBuffer|BindFlag::TransferDst, LinearBufferDesc::Create(unsigned(sizeof(decltype(lowDetailHemi.second)::value_type)*lowDetailHemi.second.size()))), "light-stenciling-geometry");
		_pendingLowDetailHemisphereVB = std::move(lowDetailHemi.second);
		std::vector<uint16_t> ib; ib.reserve(lowDetailHemi.first.size());
		std::copy(lowDetailHemi.first.begin(), lowDetailHemi.first.end(), std::back_inserter(ib));
		_lowDetailHemiSphereIB = device.CreateResource(CreateDesc(BindFlag::IndexBuffer|BindFlag::TransferDst, LinearBufferDesc::Create(unsigned(sizeof(uint16_t)*ib.size()))), "light-stenciling-geometry");
		_lowDetailHemiSphereIndexCount = (unsigned)ib.size();
		_pendingLowDetailHemisphereIB = std::move(ib);
	} 
}}

