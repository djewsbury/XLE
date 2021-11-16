// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Assets/Marker.h"
#include "../Assets/AssetTraits.h"
#include "../Assets/InitializerPack.h"
#include "../OSServices/Log.h"
#include "../Utility/MemoryUtils.h"
#include "../Utility/PtrUtils.h"
#include "../Utility/IteratorUtils.h"
#include <vector>
#include <utility>
#include <memory>

namespace ConsoleRig
{
	//
	// This file implements 2 functions:
	//
	// 		FindCachedBox<Type>(...)
	// 		TryActualizeCachedBox<Type>(...)
	//
	// Both will check the result of GetDependencyValidation() of the object and rebuild
	// invalidated objects TryActualizeCachedBox() can only be used with classes that have
	// a method like:
	//
	//		static void ConstructToPromise(std::promise<std::shared_ptr<Type>>&& promise);
	//
	// implemented. This will invoke a background compile on first access and return nullptr
	// until the object is aready to go.
	// FindCachedBox<> can also be used with assets with a ConstructToPromise() method, but
	// will throw ::Assets::Exceptions::PendingAsset if the asset is not ready
	//
	///////////////////////////////////////////////////////////////////////////////////////////////

	namespace Internal
	{
		struct IBoxTable { virtual ~IBoxTable(); };
		IBoxTable* GetOrRegisterBoxTable(uint64_t typeId, std::unique_ptr<IBoxTable> table);

		template <typename Box> struct BoxTable : public IBoxTable
		{
			std::vector<std::pair<uint64_t, std::unique_ptr<Box>>>    			_internalTable;
			std::vector<std::pair<uint64_t, ::Assets::PtrToMarkerPtr<Box>>>		_internalFuturesTable;
		};

		template <typename Box> std::vector<std::pair<uint64_t, std::unique_ptr<Box>>>& GetBoxTable()
		{
			static BoxTable<Box>* table = nullptr;
			if (!table)
				table = (BoxTable<Box>*)GetOrRegisterBoxTable(typeid(Box).hash_code(), std::make_unique<Internal::BoxTable<Box>>());
			return table->_internalTable;
		}

		template <typename Box> std::vector<std::pair<uint64_t, ::Assets::PtrToMarkerPtr<Box>>>& GetBoxFutureTable()
		{
			static BoxTable<Box>* table = nullptr;
			if (!table)
				table = (BoxTable<Box>*)GetOrRegisterBoxTable(typeid(Box).hash_code(), std::make_unique<Internal::BoxTable<Box>>());
			return table->_internalFuturesTable;
		}
	}

	template <typename Box, typename... Params> 
		std::enable_if_t<
			::Assets::Internal::HasGetDependencyValidation<Box>::value && !::Assets::Internal::HasConstructToPromiseOverride<std::shared_ptr<Box>, Params...>::value,
			Box&> FindCachedBox(Params... params)
	{
		auto hashValue = ::Assets::Internal::BuildParamHash(params...);
		auto& boxTable = Internal::GetBoxTable<std::decay_t<Box>>();
		auto i = LowerBound(boxTable, hashValue);
		if (i!=boxTable.end() && i->first==hashValue) {
			if (i->second->GetDependencyValidation().GetValidationIndex()!=0) {
				i->second = std::make_unique<Box>(std::forward<Params>(params)...);
				Log(Verbose) << "Created cached box for type (" << typeid(Box).name() << ") -- rebuilding due to validation failure. HashValue:(0x" << std::hex << hashValue << std::dec << ")" << std::endl;
			}
			return *i->second;
		}

		auto ptr = std::make_unique<Box>(std::forward<Params>(params)...);
		Log(Verbose) << "Created cached box for type (" << typeid(Box).name() << ") -- first time. HashValue:(0x" << std::hex << hashValue << std::dec << ")" << std::endl;
		auto i2 = boxTable.emplace(i, std::make_pair(hashValue, std::move(ptr)));
		return *i2->second;
	}

	template <typename Box, typename... Params> 
		std::enable_if_t<
			!::Assets::Internal::HasGetDependencyValidation<Box>::value && !::Assets::Internal::HasConstructToPromiseOverride<std::shared_ptr<Box>, Params...>::value,
			Box&> FindCachedBox(Params... params)
	{
		auto hashValue = ::Assets::Internal::BuildParamHash(params...);
		auto& boxTable = Internal::GetBoxTable<std::decay_t<Box>>();
		auto i = LowerBound(boxTable, hashValue);
		if (i!=boxTable.end() && i->first==hashValue)
			return *i->second;

		auto ptr = std::make_unique<Box>(std::forward<Params>(params)...);
		Log(Verbose) << "Created cached box for type (" << typeid(Box).name() << ") -- first time. HashValue:(0x" << std::hex << hashValue << std::dec << ")" << std::endl;
		auto i2 = boxTable.emplace(i, std::make_pair(hashValue, std::move(ptr)));
		return *i2->second;
	}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	template <typename Box, typename... Params> 
		std::enable_if_t<
			::Assets::Internal::HasGetDependencyValidation<Box>::value && ::Assets::Internal::HasConstructToPromiseOverride<std::shared_ptr<Box>, Params...>::value,
			Box&> FindCachedBox(Params... params)
	{
		auto hashValue = ::Assets::Internal::BuildParamHash(params...);
		auto& boxTable = Internal::GetBoxFutureTable<std::decay_t<Box>>();
		auto i = LowerBound(boxTable, hashValue);
		if (i!=boxTable.end() && i->first==hashValue) {
			if (::Assets::IsInvalidated(*i->second)) {
				i->second = std::make_shared<::Assets::MarkerPtr<Box>>();
				Box::ConstructToPromise(i->second->AdoptPromise(), std::forward<Params>(params)...);
				Log(Verbose) << "Created cached box for type (" << typeid(Box).name() << ") -- rebuilding due to validation failure. HashValue:(0x" << std::hex << hashValue << std::dec << ")" << std::endl;		
			}
			return *i->second->Actualize();
		}

		auto future = std::make_shared<::Assets::MarkerPtr<Box>>();
		Box::ConstructToPromise(future->AdoptPromise(), std::forward<Params>(params)...);
		Log(Verbose) << "Created cached box for type (" << typeid(Box).name() << ") -- first time. HashValue:(0x" << std::hex << hashValue << std::dec << ")" << std::endl;
		auto i2 = boxTable.emplace(i, std::make_pair(hashValue, std::move(future)));
		return *i2->second->Actualize();
	}

	template <typename Box, typename... Params> 
		std::enable_if_t<
			!::Assets::Internal::HasGetDependencyValidation<Box>::value && ::Assets::Internal::HasConstructToPromiseOverride<std::shared_ptr<Box>, Params...>::value,
			Box&> FindCachedBox(Params... params)
	{
		auto hashValue = ::Assets::Internal::BuildParamHash(params...);
		auto& boxTable = Internal::GetBoxFutureTable<std::decay_t<Box>>();
		auto i = LowerBound(boxTable, hashValue);
		if (i!=boxTable.end() && i->first==hashValue) {
			return *i->second->Actualize();
		}

		auto future = std::make_shared<::Assets::MarkerPtr<Box>>();
		Box::ConstructToPromise(future->AdoptPromise(), std::forward<Params>(params)...);
		Log(Verbose) << "Created cached box for type (" << typeid(Box).name() << ") -- first time. HashValue:(0x" << std::hex << hashValue << std::dec << ")" << std::endl;
		auto i2 = boxTable.emplace(i, std::make_pair(hashValue, std::move(future)));
		return *i2->second->Actualize();
	}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	
	template <typename Box, typename... Params> 
		std::enable_if_t<
			::Assets::Internal::HasGetDependencyValidation<Box>::value && ::Assets::Internal::HasConstructToPromiseOverride<std::shared_ptr<Box>, Params...>::value,
			Box*> TryActualizeCachedBox(Params... params)
	{
		auto hashValue = ::Assets::Internal::BuildParamHash(params...);
		auto& boxTable = Internal::GetBoxFutureTable<std::decay_t<Box>>();
		auto i = LowerBound(boxTable, hashValue);
		if (i!=boxTable.end() && i->first==hashValue) {
			if (::Assets::IsInvalidated(*i->second)) {
				i->second = std::make_shared<::Assets::MarkerPtr<Box>>();
				Box::ConstructToPromise(i->second->AdoptPromise(), std::forward<Params>(params)...);
				Log(Verbose) << "Created cached box for type (" << typeid(Box).name() << ") -- rebuilding due to validation failure. HashValue:(0x" << std::hex << hashValue << std::dec << ")" << std::endl;		
			}
			auto* res = i->second->TryActualize();
			if (!res) return nullptr;
			return res->get();
		}

		auto future = std::make_shared<::Assets::MarkerPtr<Box>>();
		Box::ConstructToPromise(future->AdoptPromise(), std::forward<Params>(params)...);
		Log(Verbose) << "Created cached box for type (" << typeid(Box).name() << ") -- first time. HashValue:(0x" << std::hex << hashValue << std::dec << ")" << std::endl;
		auto i2 = boxTable.emplace(i, std::make_pair(hashValue, std::move(future)));
		auto* res = i2->second->TryActualize();
		if (!res) return nullptr;
		return res->get();
	}

	template <typename Box, typename... Params> 
		std::enable_if_t<
			!::Assets::Internal::HasGetDependencyValidation<Box>::value && ::Assets::Internal::HasConstructToPromiseOverride<std::shared_ptr<Box>, Params...>::value,
			Box*> TryActualizeCachedBox(Params... params)
	{
		auto hashValue = ::Assets::Internal::BuildParamHash(params...);
		auto& boxTable = Internal::GetBoxFutureTable<std::decay_t<Box>>();
		auto i = LowerBound(boxTable, hashValue);
		if (i!=boxTable.end() && i->first==hashValue) {
			auto* res = i->second->TryActualize();
			if (!res) return nullptr;
			return res->get();
		}

		auto future = std::make_shared<::Assets::MarkerPtr<Box>>();
		Box::ConstructToPromise(future->AdoptPromise(), std::forward<Params>(params)...);
		Log(Verbose) << "Created cached box for type (" << typeid(Box).name() << ") -- first time. HashValue:(0x" << std::hex << hashValue << std::dec << ")" << std::endl;
		auto i2 = boxTable.emplace(i, std::make_pair(hashValue, std::move(future)));
		auto* res = i2->second->TryActualize();
		if (!res) return nullptr;
		return res->get();
	}

	///////////////////////////////////////////////////////////////////////////////////////////////

}

