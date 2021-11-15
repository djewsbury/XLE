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

	const ChunkFileContainer& GetChunkFileContainer(StringSection<ResChar> identifier)
	{
		return ::Assets::Legacy::GetAsset<ChunkFileContainer>(identifier);
	}

	PtrToFuturePtr<ConfigFileContainer<>> GetConfigFileContainerFuture(StringSection<ResChar> identifier)
	{
		return ::Assets::MakeAssetPtr<ConfigFileContainer<>>(identifier);
	}

	PtrToFuturePtr<ChunkFileContainer> GetChunkFileContainerFuture(StringSection<ResChar> identifier)
	{
		return ::Assets::MakeAssetPtr<ChunkFileContainer>(identifier);
	}
}}

