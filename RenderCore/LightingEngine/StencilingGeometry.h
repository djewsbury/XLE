#pragma once

#include "../../Math/Vector.h"
#include <vector>

namespace RenderCore { class IThreadContext; class IDevice; class IResource; }
namespace RenderCore { namespace LightingEngine
{
	class LightStencilingGeometry
	{
	public:
		std::shared_ptr<IResource> _geo;
		std::pair<unsigned, unsigned> _sphereOffsetAndCount;
		std::pair<unsigned, unsigned> _cubeOffsetAndCount;

		std::shared_ptr<IResource> _lowDetailHemiSphereVB;
		std::shared_ptr<IResource> _lowDetailHemiSphereIB;
		unsigned _lowDetailHemiSphereIndexCount;

		void CompleteInitialization(IThreadContext&);
		LightStencilingGeometry(IDevice& device);
		LightStencilingGeometry() = default;
	private:
		std::vector<uint8_t> _pendingGeoInitBuffer;
		std::vector<Float3> _pendingLowDetailHemisphereVB;
		std::vector<uint16_t> _pendingLowDetailHemisphereIB;
	};
}}

