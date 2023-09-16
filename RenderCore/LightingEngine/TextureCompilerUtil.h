// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Assets/DepVal.h"
#include "../../Assets/OperationContext.h"
#include "../../Utility/StringFormat.h"
#include <memory>
#include <vector>
#include <functional>

namespace RenderCore { namespace BufferUploads { class IAsyncDataSource; }}
namespace Assets { struct DependentFileState; }
namespace RenderCore { class TextureDesc; }

namespace RenderCore { namespace LightingEngine
{
	enum class EquirectFilterMode { ToCubeMap, ToGlossySpecular, ProjectToSphericalHarmonic, ToGlossySpecularReference, ToDiffuseReference };
	struct EquirectFilterParams
	{
		unsigned _sampleCount = 1;
		unsigned _idealCmdListCostMS = 1500;
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
		unsigned totalSampleCount);
}}
