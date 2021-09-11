// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Assets/AssetFuture.h"
#include "../Assets/InitializerPack.h"
#include "../OSServices/Log.h"
#include "../Core/Types.h"
#include "../Utility/MemoryUtils.h"
#include "../Utility/PtrUtils.h"
#include "../Utility/IteratorUtils.h"
#include <vector>
#include <algorithm>

namespace ConsoleRig
{

	///////////////////////////////////////////////////////////////////////////////////////////////

	namespace Internal
	{
		struct IBoxTable { virtual ~IBoxTable(); };
		IBoxTable* GetOrRegisterBoxTable(uint64_t typeId, std::unique_ptr<IBoxTable> table);

		template <typename Box> struct BoxTable : public IBoxTable
		{
			std::vector<std::pair<uint64_t, std::unique_ptr<Box>>>    _internalTable;
		};

		template <typename Box> std::vector<std::pair<uint64_t, std::unique_ptr<Box>>>& GetBoxTable()
		{
			static BoxTable<Box>* table = nullptr;
			if (!table)
				table = (BoxTable<Box>*)GetOrRegisterBoxTable(typeid(Box).hash_code(), std::make_unique<Internal::BoxTable<Box>>());
			return table->_internalTable;
		}
	}

	template <typename Box, typename... Params> 
		std::enable_if_t<::Assets::Internal::HasGetDependencyValidation<Box>::value, Box&> FindCachedBox(Params... params)
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
		std::enable_if_t<!::Assets::Internal::HasGetDependencyValidation<Box>::value, Box&> FindCachedBox(Params... params)
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

	///////////////////////////////////////////////////////////////////////////////////////////////

}

