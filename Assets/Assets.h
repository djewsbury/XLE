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
		std::shared_future<AssetType> GetAssetFuture(Params&&... initialisers)
	{
		return Services::GetAssetSets().GetSetForType<AssetType>().Get(std::forward<Params>(initialisers)...)->ShareFuture();
	}

	template<typename AssetType, typename... Params>
		std::shared_future<std::shared_ptr<AssetType>> GetAssetFuturePtr(Params&&... initialisers)
	{
		return GetAssetFuture<std::shared_ptr<AssetType>>(std::forward<Params>(initialisers)...);
	}

	template<typename AssetType, typename... Params>
		const AssetType& ActualizeAsset(Params&&... initialisers)
	{
		auto future = Services::GetAssetSets().GetSetForType<AssetType>().Get(std::forward<Params>(initialisers)...);
		future->StallWhilePending();
		return future->Actualize();
	}

	template<typename AssetType, typename... Params>
		const std::shared_ptr<AssetType>& ActualizeAssetPtr(Params&&... initialisers)
	{
		auto future = Services::GetAssetSets().GetSetForType<std::shared_ptr<AssetType>>().Get(std::forward<Params>(initialisers)...);
		future->StallWhilePending();
		return future->Actualize();
	}

	template<typename AssetType, typename... Params>
		std::shared_ptr<Marker<AssetType>> GetAssetMarker(Params&&... initialisers)
	{
		return Services::GetAssetSets().GetSetForType<AssetType>().Get(std::forward<Params>(initialisers)...);
	}

	template<typename AssetType, typename... Params>
		std::shared_ptr<Marker<std::shared_ptr<AssetType>>> GetAssetMarkerPtr(Params&&... initialisers)
	{
		return Services::GetAssetSets().GetSetForType<std::shared_ptr<AssetType>>().Get(std::forward<Params>(initialisers)...);
	}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// C O N S T R U C T I O N   F U N C T I O N   V A R I A T I O N S
//////////////////////////////////////////////////////////////////////////////////////////////////////////////

	template<auto ConstructToPromiseFn, typename... Params>
		std::shared_future<Internal::AssetTypeFromConstructToPromise<ConstructToPromiseFn>> GetAssetFutureFn(Params&&... initialisers)
	{
		using AssetType = Internal::AssetTypeFromConstructToPromise<ConstructToPromiseFn>;
		return Services::GetAssetSets().GetSetForType<AssetType>().template GetFn<ConstructToPromiseFn>(std::forward<Params>(initialisers)...)->ShareFuture();
	}

	template<auto ConstructToPromiseFn, typename... Params>
		const Internal::AssetTypeFromConstructToPromise<ConstructToPromiseFn>& ActualizeAssetFn(Params&&... initialisers)
	{
		using AssetType = Internal::AssetTypeFromConstructToPromise<ConstructToPromiseFn>;
		auto future = Services::GetAssetSets().GetSetForType<AssetType>().template GetFn<ConstructToPromiseFn>(std::forward<Params>(initialisers)...);
		future->StallWhilePending();
		return future->Actualize();
	}

	template<auto ConstructToPromiseFn, typename... Params>
		std::shared_ptr<Marker<Internal::AssetTypeFromConstructToPromise<ConstructToPromiseFn>>> GetAssetMarkerFn(Params&&... initialisers)
	{
		using AssetType = Internal::AssetTypeFromConstructToPromise<ConstructToPromiseFn>;
		return Services::GetAssetSets().GetSetForType<AssetType>().template GetFn<ConstructToPromiseFn>(std::forward<Params>(initialisers)...);
	}

	namespace Legacy
	{
		template<typename AssetType, typename... Params>
			const AssetType& GetAsset(Params&&... initialisers) { return *ActualizeAssetPtr<AssetType>(std::forward<Params>(initialisers)...); }

		template<typename AssetType, typename... Params>
			const AssetType& GetAssetDep(Params&&... initialisers) { return *ActualizeAssetPtr<AssetType>(std::forward<Params>(initialisers)...); }

		template<typename AssetType, typename... Params>
			const AssetType& GetAssetComp(Params&&... initialisers) { return *ActualizeAssetPtr<AssetType>(std::forward<Params>(initialisers)...); }
	}

}

