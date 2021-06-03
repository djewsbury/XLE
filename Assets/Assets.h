#pragma once

#if defined(__CLR_VER)
	#error Assets.h cannot be included from a C++/CLR header (because <mutex> & <thread> cannot be included in C++/CLR, and these headers ultimately include that header)
#endif

#include "AssetSetManager.h"
#include "AssetFuture.h"
#include "AssetHeap.h"
#include "AssetServices.h"

namespace Assets
{

	template<typename AssetType, typename... Params>
		std::shared_ptr<FuturePtr<AssetType>> MakeAsset(Params... initialisers)
	{
		return Services::GetAssetSets().GetSetForType<AssetType>().Get(std::forward<Params>(initialisers)...);
	}

	template<typename AssetType, typename... Params>
		const std::shared_ptr<AssetType>& Actualize(Params... initialisers)
	{
		auto future = MakeAsset<AssetType>(initialisers...);
		future->StallWhilePending();
		return future->Actualize();
	}

	namespace Legacy
	{
		template<typename AssetType, typename... Params>
			const AssetType& GetAsset(Params... initialisers) { return *Actualize<AssetType>(std::forward<Params>(initialisers)...); }

		template<typename AssetType, typename... Params>
			const AssetType& GetAssetDep(Params... initialisers) { return *Actualize<AssetType>(std::forward<Params>(initialisers)...); }

		template<typename AssetType, typename... Params>
			const AssetType& GetAssetComp(Params... initialisers) { return *Actualize<AssetType>(std::forward<Params>(initialisers)...); }
	}

}

