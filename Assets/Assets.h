#pragma once

#if defined(__CLR_VER)
	#error Assets.h cannot be included from a C++/CLR header (because <mutex> & <thread> cannot be included in C++/CLR, and these headers ultimately include that header)
#endif

#include "AssetSetManager.h"
#include "Marker.h"
#include "AssetHeap.h"
#include "AssetServices.h"

namespace Assets
{

	template<typename AssetType, typename... Params>
		std::shared_ptr<Marker<AssetType>> MakeAsset(Params... initialisers)
	{
		return Services::GetAssetSets().GetSetForType<AssetType>().Get(std::forward<Params>(initialisers)...);
	}

	template<typename AssetType, typename... Params>
		std::shared_ptr<Marker<std::shared_ptr<AssetType>>> MakeAssetPtr(Params... initialisers)
	{
		return MakeAsset<std::shared_ptr<AssetType>>(std::forward<Params>(initialisers)...);
	}

	template<typename AssetType, typename... Params>
		const AssetType& ActualizeAsset(Params... initialisers)
	{
		auto future = MakeAsset<AssetType>(std::forward<Params>(initialisers)...);
		future->StallWhilePending();
		return future->Actualize();
	}

	template<typename AssetType, typename... Params>
		const std::shared_ptr<AssetType>& ActualizeAssetPtr(Params... initialisers)
	{
		auto future = MakeAsset<std::shared_ptr<AssetType>>(std::forward<Params>(initialisers)...);
		future->StallWhilePending();
		return future->Actualize();
	}

	template<typename AssetType, typename... Params>
		std::shared_ptr<Marker<AssetType>> MakeFuture(Params... initialisers);		// (implemented in DeferredConstruction.h)

	template<typename AssetType, typename... Params>
		std::shared_ptr<MarkerPtr<AssetType>> MakeFuturePtr(Params... initialisers);		// (implemented in DeferredConstruction.h)

	namespace Legacy
	{
		template<typename AssetType, typename... Params>
			const AssetType& GetAsset(Params... initialisers) { return *ActualizeAssetPtr<AssetType>(std::forward<Params>(initialisers)...); }

		template<typename AssetType, typename... Params>
			const AssetType& GetAssetDep(Params... initialisers) { return *ActualizeAssetPtr<AssetType>(std::forward<Params>(initialisers)...); }

		template<typename AssetType, typename... Params>
			const AssetType& GetAssetComp(Params... initialisers) { return *ActualizeAssetPtr<AssetType>(std::forward<Params>(initialisers)...); }
	}

}

