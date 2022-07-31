// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "SHCoefficients.h"
#include "../Assets/TextureCompiler.h"
#include "../Assets/DeferredConstruction.h"
#include "../../Assets/Continuation.h"

namespace RenderCore { namespace LightingEngine 
{

	SHCoefficients::SHCoefficients(IteratorRange<const Float4*> coefficients)
	{
		assert(coefficients.size() <= dimof(_coefficients));
		assert(coefficients.size() >= 9);
		_coefficientCount = std::min(coefficients.size(), dimof(_coefficients));
		for (unsigned c=0; c<_coefficientCount; ++c)
			_coefficients[c] = coefficients[c];
	}

	void SHCoefficientsAsset::ConstructToPromise(
		std::promise<SHCoefficientsAsset>&& promise,
		StringSection<> srcTexture)
	{
		Assets::TextureCompilationRequest request;
		request._operation = Assets::TextureCompilationRequest::Operation::ProjectToSphericalHarmonic; 
		request._srcFile = srcTexture.AsString();
		request._format = Format::R32G32B32A32_FLOAT;
		request._coefficientCount = 25;
		auto srcFuture = ::Assets::NewMarkerPtr<RenderCore::Assets::TextureArtifact>(request);
		::Assets::WhenAll(srcFuture).ThenConstructToPromise(
			std::move(promise),
			[](std::promise<SHCoefficientsAsset>&& thatPromise, std::shared_ptr<RenderCore::Assets::TextureArtifact> textureArtifact) {
				::Assets::WhenAll(textureArtifact->BeginLoadRawData()).ThenConstructToPromise(
					std::move(thatPromise),
					[depVal = textureArtifact->GetDependencyValidation()](auto rawData) {
						if (rawData._data.size() < 8*sizeof(Float4)
							|| rawData._desc._format != Format::R32G32B32A32_FLOAT) {
							Throw(::Assets::Exceptions::ConstructionError(
								::Assets::Exceptions::ConstructionError::Reason::FormatNotUnderstood,
								depVal, ::Assets::AsBlob("Not enough SH coefficients or unexpected format")));
						}

						auto data = MakeIteratorRange((const Float4*)AsPointer(rawData._data.begin()), (const Float4*)AsPointer(rawData._data.end()));
						SHCoefficientsAsset res{data};
						res._depVal = depVal;
						return res;
					});
			});
	}

}}
