// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Math/Vector.h"
#include "../../Assets/DepVal.h"
#include "../../Assets/AssetFuture.h"
#include "../../Utility/IteratorUtils.h"
#include "../../Utility/StringUtils.h"

namespace RenderCore { namespace LightingEngine 
{
	class SHCoefficients
	{
	public:
		IteratorRange<const Float4*> GetCoefficients() const { assert(_coefficientCount!=0); return MakeIteratorRange(_coefficients, &_coefficients[_coefficientCount]); }
		SHCoefficients(IteratorRange<const Float4*> coefficients);
	private:
		Float4 _coefficients[25];
		unsigned _coefficientCount = 0;
	};

	class SHCoefficientsAsset : public SHCoefficients
	{
	public:
		const ::Assets::DependencyValidation GetDependencyValidation() const { return _depVal; }

		static void ConstructToPromise(
			std::promise<std::shared_ptr<SHCoefficientsAsset>>&&,
			StringSection<> initializer);
	protected:
		::Assets::DependencyValidation _depVal;
		using SHCoefficients::SHCoefficients;
	};

}}