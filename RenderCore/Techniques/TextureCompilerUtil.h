// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Assets/DepVal.h"
#include "../../Utility/StringFormat.h"
#include <memory>
#include <vector>
#include <functional>

namespace RenderCore { namespace BufferUploads { class IAsyncDataSource; }}
namespace Assets { struct DependentFileState; }
namespace RenderCore { class TextureDesc; }

namespace RenderCore { namespace Techniques
{
	enum class EquRectFilterMode { ToCubeMap, ToGlossySpecular, ProjectToSphericalHarmonic };
	using ProgressiveTextureFn = std::function<void(std::shared_ptr<BufferUploads::IAsyncDataSource>)>;
	std::shared_ptr<BufferUploads::IAsyncDataSource> EquRectFilter(
		BufferUploads::IAsyncDataSource& dataSrc,
		const TextureDesc& targetDesc,
		EquRectFilterMode filter = EquRectFilterMode::ToCubeMap,
		const ProgressiveTextureFn& progressiveResults = {});

	std::shared_ptr<BufferUploads::IAsyncDataSource> GenerateFromComputeShader(
		StringSection<> shader,
		const TextureDesc& targetDesc);
}}
