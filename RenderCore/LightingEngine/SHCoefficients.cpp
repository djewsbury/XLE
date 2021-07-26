// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "SHCoefficients.h"
#include "../Assets/TextureCompiler.h"
#include "../Assets/AssetFutureContinuation.h"
#include "../Assets/DeferredConstruction.h"

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

	void SHCoefficientsAsset::ConstructToFuture(
		::Assets::FuturePtr<SHCoefficientsAsset>& future,
		StringSection<> srcTexture)
	{
		Assets::TextureCompilationRequest request;
		request._operation = Assets::TextureCompilationRequest::Operation::ProjectToSphericalHarmonic; 
		request._srcFile = srcTexture.AsString();
		request._format = Format::R32G32B32A32_FLOAT;
		request._coefficientCount = 25;
		auto srcFuture = ::Assets::MakeFuture<std::shared_ptr<RenderCore::Assets::TextureArtifact>>(request);
		::Assets::WhenAll(srcFuture).ThenConstructToFuture(
			future,
			[](::Assets::FuturePtr<SHCoefficientsAsset>& thatFuture, std::shared_ptr<RenderCore::Assets::TextureArtifact> textureArtifact) {
				struct Captures
				{
					std::future<RenderCore::Assets::TextureArtifact::RawData> _futureData;
					::Assets::DependencyValidation _depVal;
				};
				auto captures = std::make_shared<Captures>();
				captures->_futureData = textureArtifact->BeginLoadRawData();
				captures->_depVal = textureArtifact->GetDependencyValidation();
				thatFuture.SetPollingFunction(
					[captures=std::move(captures)](::Assets::FuturePtr<SHCoefficientsAsset>& thatFuture) {
						auto resStatus = captures->_futureData.wait_for(std::chrono::seconds{0});
						if (resStatus == std::future_status::timeout)
							return true;

						auto rawData = captures->_futureData.get();
						if (rawData._data.size() < 8*sizeof(Float4)
							|| rawData._desc._format != Format::R32G32B32A32_FLOAT) {
							thatFuture.SetInvalidAsset(captures->_depVal, ::Assets::AsBlob("Not enough SH coefficients or unexpected format"));
							return false;
						}

						auto data = MakeIteratorRange((const Float4*)AsPointer(rawData._data.begin()), (const Float4*)AsPointer(rawData._data.end()));
						auto res = std::make_shared<SHCoefficientsAsset>(data);
						res->_depVal = captures->_depVal;
						thatFuture.SetAsset(std::move(res), {});
						return false;
					});
			});
	}

}}
