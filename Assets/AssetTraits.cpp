// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "AssetTraits.h"
#include "Assets.h"
#include "ConfigFileContainer.h"
#include "ChunkFileContainer.h"

namespace Assets { namespace Internal
{
	const ConfigFileContainer<>& GetConfigFileContainer(StringSection<ResChar> identifier)
	{
		return ::Assets::Legacy::GetAsset<ConfigFileContainer<>>(identifier);
	}

	const ArtifactChunkContainer& GetChunkFileContainer(StringSection<ResChar> identifier)
	{
		return ::Assets::Legacy::GetAsset<ArtifactChunkContainer>(nullptr, identifier);
	}

	std::shared_future<std::shared_ptr<ConfigFileContainer<>>> GetConfigFileContainerFuture(StringSection<ResChar> identifier)
	{
		return ::Assets::GetAssetFuturePtr<ConfigFileContainer<>>(identifier);
	}

	std::shared_future<std::shared_ptr<ArtifactChunkContainer>> GetChunkFileContainerFuture(StringSection<ResChar> identifier)
	{
		return ::Assets::GetAssetFuturePtr<ArtifactChunkContainer>(nullptr, identifier);
	}
}}

