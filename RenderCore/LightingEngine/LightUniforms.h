// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Math/Vector.h"

namespace RenderCore { namespace LightingEngine { class EnvironmentalLightingDesc; class LightSourceOperatorDesc; }}

namespace RenderCore { namespace LightingEngine { namespace Internal
{
	class StandardLightDesc;
	

	struct CB_Ambient
	{ 
		Float3      _ambientColour; 
		float       _skyReflectionScale; 
		float       _skyReflectionBlurriness; 
		unsigned    _dummy[3];
	};

	struct CB_RangeFog
	{
		Float3      _rangeFogInscatter;
		float       _rangeFogOpticalThickness;
	};

	struct CB_VolumeFog
	{
		float       _opticalThickness;
		float       _heightStart;
		float       _heightEnd;  unsigned _enableFlag;
		Float3      _sunInscatter; unsigned _dummy1;
		Float3      _ambientInscatter; unsigned _dummy2;
	};

	struct CB_Light
	{
			// Note that this structure is larger than it needs to be
			// for some light types. Only some types need the full 
			// orientation matrix.
			// It seems like we would end up wasting shader constants
			// if we want to store a large number of lights for forward
			// rendering.
		Float3 _position;           float _cutoffRange;
		Float3 _brightness;         float _sourceRadiusX;
		Float3 _orientationX;       float _sourceRadiusY;
		Float3 _orientationY;       unsigned _shape; // float _diffuseWideningMin;
		Float3 _orientationZ;       unsigned _dummy; // float _diffuseWideningMax;
	};

	struct CB_EnvironmentProps
	{
		CB_Light _dominantLight;
		unsigned _lightCount = 0u;
		unsigned _dummy[3];
		Float4 _diffuseSHCoefficients[25];
	};

	CB_Ambient MakeAmbientUniforms(const EnvironmentalLightingDesc& desc);
	CB_RangeFog MakeRangeFogUniforms(const EnvironmentalLightingDesc& desc);
	CB_Light MakeLightUniforms(const StandardLightDesc& light, const LightSourceOperatorDesc& operatorDesc);
	CB_Light MakeBlankLightDesc();
	CB_VolumeFog MakeBlankVolumeFogDesc();
	// CB_BasicEnvironment MakeBasicEnvironmentUniforms(const EnvironmentalLightingDesc& env);
}}}

