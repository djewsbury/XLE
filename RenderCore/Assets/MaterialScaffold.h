// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Assets/AssetsCore.h"
#include "../../Utility/IteratorUtils.h"
#include "../../Utility/MemoryUtils.h"
#include <memory>

namespace Assets {  class DependencyValidation; class ArtifactRequestResult; class ArtifactRequest; }

namespace RenderCore { namespace Assets
{
	class ShaderPatchCollection;
	class ScaffoldCmdIterator;

	using MaterialGuid = uint64_t;
	MaterialGuid MakeMaterialGuid(StringSection<utf8> name);

    /// <summary>An asset containing compiled material settings</summary>
    /// This is the equivalent of other scaffold objects (like ModelScaffold
    /// and AnimationSetScaffold). It contains the processed and ready-to-use
    /// material information.
	class MaterialScaffold
	{
	public:
		using Machine = IteratorRange<Assets::ScaffoldCmdIterator>;
		Machine				GetMaterialMachine(MaterialGuid guid) const;
		StringSection<>		DehashMaterialName(MaterialGuid guid) const;
		std::vector<uint64_t> GetMaterials() const;

		std::shared_ptr<ShaderPatchCollection> GetShaderPatchCollection(uint64_t hash) const;
		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }

		MaterialScaffold();
		MaterialScaffold(IteratorRange<::Assets::ArtifactRequestResult*> chunks, const ::Assets::DependencyValidation& depVal);
		~MaterialScaffold();

		static const auto CompileProcessType = ConstHash64<'ResM', 'at'>::Value;
		static const ::Assets::ArtifactRequest ChunkRequests[1];
	protected:
		std::vector<std::pair<uint64_t, Machine>> _materialMachines;
		std::vector<std::shared_ptr<ShaderPatchCollection>> _patchCollections;
		IteratorRange<const void*> _resolvedNamesBlock;
		std::unique_ptr<uint8[], PODAlignedDeletor>		_rawMemoryBlock;
		size_t											_rawMemoryBlockSize = 0;
		::Assets::DependencyValidation					_depVal;

		IteratorRange<ScaffoldCmdIterator> GetOuterCommandStream() const;
	};

	static constexpr uint64 ChunkType_ResolvedMat = ConstHash64<'ResM', 'at'>::Value;
	static constexpr uint64 ChunkType_PatchCollections = ConstHash64<'Patc', 'hCol'>::Value;
	static constexpr unsigned ResolvedMat_ExpectedVersion = 1;

}}

