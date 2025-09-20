// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "SHCoefficients.h"
#include "TextureCompilerUtil.h"
#include "../Assets/TextureCompiler.h"
#include "../Assets/AssetTraits.h"
#include "../../Assets/IArtifact.h"
#include "../../Assets/Continuation.h"

namespace RenderCore { namespace LightingEngine 
{

	SHCoefficients::SHCoefficients(IteratorRange<const Float4*> coefficients)
	{
		assert(coefficients.size() <= dimof(_coefficients));
		assert(coefficients.size() >= 9);
		_coefficientCount = (unsigned)std::min(coefficients.size(), dimof(_coefficients));
		for (unsigned c=0; c<_coefficientCount; ++c)
			_coefficients[c] = coefficients[c];
	}

	void SHCoefficientsAsset::ConstructToPromise(
		std::promise<SHCoefficientsAsset>&& promise,
		std::shared_ptr<::Assets::OperationContext> loadingContext,
		StringSection<> srcTexture,
		CoordinateSystem coordinateSystem)
	{
		EquirectToCubemap request;
		request._filterMode = EquirectFilterMode::ProjectToSphericalHarmonic; 
		request._format = Format::R32G32B32A32_FLOAT;
		request._coefficientCount = 25;
		request._params._upDirection = (coordinateSystem == CoordinateSystem::YUp) ? 1 : 2;

		Assets::TextureCompilerSource srcComponent;
		srcComponent._srcFile = srcTexture.AsString();

		Assets::TextureCompilationRequest request;
		request._subCompiler = TextureCompiler_EquirectFilter(request, srcComponent);
		request._intermediateName = request._subCompiler->GetIntermediateName();
		
		auto srcFuture = ::Assets::ConstructToMarkerPtr<RenderCore::Assets::TextureArtifact>(std::move(loadingContext), request);
		::Assets::WhenAll(std::move(srcFuture)).ThenConstructToPromise(
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
