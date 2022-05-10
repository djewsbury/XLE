// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Assets/BlockSerializer.h"
#include "../../Utility/MemoryUtils.h"      // (for ConstHash64)
#include "../../Utility/IteratorUtils.h"
#include <vector>
#include <iosfwd>

namespace RenderCore { class InputElementDesc; }

namespace RenderCore { namespace Assets
{
	static const uint64_t ChunkType_ModelScaffold = ConstHash64<'Mode', 'lSca', 'fold'>::Value;
	static const uint64_t ChunkType_ModelScaffoldLargeBlocks = ConstHash64<'Mode', 'lSca', 'fold', 'Larg'>::Value;
	static const uint64_t ChunkType_AnimationSet = ConstHash64<'Anim', 'Set'>::Value;
	static const uint64_t ChunkType_Skeleton = ConstHash64<'Skel', 'eton'>::Value;
	static const uint64_t ChunkType_RawMat = ConstHash64<'RawM', 'at'>::Value;
	static const uint64_t ChunkType_Metrics = ConstHash64<'Metr', 'ics'>::Value;

	struct GeoInputAssembly;
	struct DrawCallDesc;
	GeoInputAssembly CreateGeoInputAssembly(   
		const std::vector<InputElementDesc>& vertexInputLayout,
		unsigned vertexStride);
	
	std::ostream& SerializationOperator(std::ostream& stream, const GeoInputAssembly& ia);
	std::ostream& SerializationOperator(std::ostream& stream, const DrawCallDesc& dc);

	struct CmdAndRawData
	{
		uint32_t _cmd;
		IteratorRange<const void*> _data;
	};
	template<typename Type, typename Cmd>
		CmdAndRawData MakeCmdAndRawData(Cmd cmd, const Type& obj)
		{
			return CmdAndRawData{(uint32_t)cmd, MakeOpaqueIteratorRange(obj)};
		}

	template<typename Type, typename Cmd>
		CmdAndRawData MakeCmdAndRanged(Cmd cmd, const Type& obj)
		{
			return CmdAndRawData{(uint32_t)cmd, IteratorRange<const void*>{obj.begin(), obj.end()}};
		}

	inline void SerializationOperator(::Assets::BlockSerializer& serializer, const CmdAndRawData& obj)
	{
		serializer << (uint32_t)obj._cmd;
		serializer << (uint32_t)obj._data.size();
		serializer.SerializeRawRange(obj._data);
	}

	template<typename Serializable>
		struct CmdAndSerializable
	{
		uint32_t _cmd;
		const Serializable* _serializable;
	};
	template<typename Serializable, typename Cmd>
		CmdAndSerializable<Serializable> MakeCmdAndSerializable(Cmd cmd, const Serializable& obj)
		{
			return CmdAndSerializable<Serializable>{(uint32_t)cmd, &obj};
		}

	template<typename Serializable>
		inline void SerializationOperator(::Assets::BlockSerializer& serializer, const CmdAndSerializable<Serializable>& obj)
	{
		serializer << (uint32_t)obj._cmd;
		auto recall = serializer.CreateRecall(sizeof(uint32_t));
		serializer << *obj._serializable;
		serializer.PushSizeValueAtRecall(recall);
	}
}}
