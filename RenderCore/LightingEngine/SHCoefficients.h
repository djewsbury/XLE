// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Assets/TextureCompiler.h"
#include "../../Math/Vector.h"
#include "../../Assets/DepVal.h"
#include "../../Assets/Marker.h"
#include "../../Utility/IteratorUtils.h"
#include "../../Utility/StringUtils.h"

namespace Assets { class OperationContext; }

namespace RenderCore { namespace LightingEngine 
{
	class SHCoefficients
	{
	public:
		IteratorRange<const Float4*> GetCoefficients() const { return MakeIteratorRange(_coefficients, &_coefficients[_coefficientCount]); }
		SHCoefficients(IteratorRange<const Float4*> coefficients);
		SHCoefficients() = default;
	private:
		Float4 _coefficients[25];
		unsigned _coefficientCount = 0;
	};

	class SHCoefficientsAsset : public SHCoefficients
	{
	public:
		const ::Assets::DependencyValidation GetDependencyValidation() const { return _depVal; }

		enum class CoordinateSystem { YUp, ZUp };

		static void ConstructToPromise(
			std::promise<SHCoefficientsAsset>&&,
			std::shared_ptr<::Assets::OperationContext>,
			StringSection<> initializer,
			CoordinateSystem coordinateSystem = CoordinateSystem::ZUp);
		SHCoefficientsAsset() = default;
	protected:
		::Assets::DependencyValidation _depVal;
		using SHCoefficients::SHCoefficients;
	};

}}