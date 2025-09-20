// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Format.h"
#include "../../Assets/DepVal.h"
#include "../../Assets/OperationContext.h"
#include <memory>
#include <functional>

namespace RenderCore { namespace BufferUploads { class IAsyncDataSource; }}
namespace Assets { struct DependentFileState; }
namespace RenderCore { class TextureDesc; class IThreadContext; class IResource; }

namespace RenderCore { namespace LightingEngine
{
	enum class EquirectFilterMode { ToCubeMap, ToCubeMapBokeh, ToGlossySpecular, ProjectToSphericalHarmonic, ToGlossySpecularReference, ToDiffuseReference };
	struct EquirectFilterParams
	{
		unsigned _sampleCount = 1;
		unsigned _idealCmdListCostMS = 1500;
		unsigned _maxSamplesPerCmdList = ~0u;
		unsigned _upDirection = 2;			// 1 = Y, 2 = Z			-- texture compiler requests may be using "coordinate system" as a key for this
	};

	using ProgressiveTextureFn = std::function<void(std::shared_ptr<BufferUploads::IAsyncDataSource>)>;
	std::shared_ptr<BufferUploads::IAsyncDataSource> EquirectFilter(
		BufferUploads::IAsyncDataSource& dataSrc,
		const TextureDesc& targetDesc,
		EquirectFilterMode filter,
		const EquirectFilterParams& params,
		::Assets::OperationContextHelper& opHelper,
		const ProgressiveTextureFn& progressiveResults = {});

	std::shared_ptr<BufferUploads::IAsyncDataSource> GenerateFromSamplingComputeShader(
		StringSection<> shader,
		const TextureDesc& targetDesc,
		unsigned totalSampleCount,
		unsigned idealCmdListCostMS = 1500,
		unsigned maxSamplesPerCmdList = ~0u);

	std::shared_ptr<BufferUploads::IAsyncDataSource> ConversionComputeShader(
		StringSection<> shader,
		BufferUploads::IAsyncDataSource& dataSrc,
		const TextureDesc& targetDesc);

	std::shared_ptr<BufferUploads::IAsyncDataSource> MakeAsyncDataSourceFromResource(
		std::shared_ptr<IThreadContext> threadContext, 
		std::shared_ptr<IResource> resource,
		::Assets::DependencyValidation depVal = {});

	struct EquirectToCubemap
	{
		EquirectFilterMode _filterMode;
		Format _format = Format::Unknown;
		unsigned _faceDim = 512;
		EquirectFilterParams _params;
		enum class MipMapFilter { None, FromSource };
		MipMapFilter _mipMapFilter = MipMapFilter::None;
		unsigned _coefficientCount = 9;		// for ProjectToSphericalHarmonic
	};
}}
