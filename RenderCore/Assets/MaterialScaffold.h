// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "RawMaterial.h"
#include "../ResourceDesc.h"
#include "../Types.h"
#include "../../Assets/AssetsCore.h"
#include "../../Utility/ParameterBox.h"
#include "../../Utility/Streams/SerializationUtils.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/IteratorUtils.h"
#include <memory>

#include "ScaffoldCmdStream.h"

namespace Assets 
{ 
    class DependencyValidation;
	class ArtifactRequestResult;
	class ArtifactRequest;
}
namespace Utility { class Data; }

namespace RenderCore { namespace Assets
{
    class MaterialImmutableData;

	#pragma pack(push)
	#pragma pack(1)

	/// <summary>Common material settings</summary>
	/// Material parameters and settings are purposefully kept fairly
	/// low-level. These parameters are settings that can be used during
	/// the main render step (rather than some higher level, CPU-side
	/// operation).
	///
	/// Typically, material parameters effect these shader inputs:
	///     Resource bindings (ie, textures assigned to shader resource slots)
	///     Shader constants
	///     Some state settings (like blend modes, etc)
	///
	/// Material parameters can also effect the shader selection through the 
	/// _matParams resource box.
	class MaterialScaffoldMaterial
	{
	public:
		ParameterBox		_bindings;				// shader resource bindings
		ParameterBox		_matParams;				// material parameters used for selecting the appropriate shader variation
		RenderStateSet		_stateSet;				// used by the RenderStateResolver for selecting render state settings (like depth read/write settings, blend modes)
		ParameterBox		_constants;				// values typically passed to shader constants
		uint64_t			_patchCollection = 0ull;
	
		template<typename Serializer>
			void SerializeMethod(Serializer& serializer) const;
	};

	#pragma pack(pop)

	class ShaderPatchCollection;

    /// <summary>An asset containing compiled material settings</summary>
    /// This is the equivalent of other scaffold objects (like ModelScaffold
    /// and AnimationSetScaffold). It contains the processed and ready-to-use
    /// material information.
    class MaterialScaffold
    {
    public:
		using Material = MaterialScaffoldMaterial;

        const MaterialImmutableData&    ImmutableData() const;
        const Material*					GetMaterial(MaterialGuid guid) const;
        StringSection<>					GetMaterialName(MaterialGuid guid) const;

		std::shared_ptr<ShaderPatchCollection> GetShaderPatchCollection(uint64_t hash) const;

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }

        static const auto CompileProcessType = ConstHash64<'ResM', 'at'>::Value;
		static const ::Assets::ArtifactRequest ChunkRequests[2];

        MaterialScaffold(IteratorRange<::Assets::ArtifactRequestResult*> chunks, const ::Assets::DependencyValidation& depVal);
        MaterialScaffold(MaterialScaffold&& moveFrom) never_throws;
        MaterialScaffold& operator=(MaterialScaffold&& moveFrom) never_throws;
        ~MaterialScaffold();

    protected:
        std::unique_ptr<uint8[], PODAlignedDeletor>	_rawMemoryBlock;
		::Assets::DependencyValidation _depVal;

		std::vector<std::shared_ptr<ShaderPatchCollection>> _patchCollections;
    };

	static constexpr uint64 ChunkType_ResolvedMat = ConstHash64<'ResM', 'at'>::Value;
	static constexpr uint64 ChunkType_PatchCollections = ConstHash64<'Patc', 'hCol'>::Value;
	static constexpr unsigned ResolvedMat_ExpectedVersion = 1;

	class MaterialScaffoldCmdStreamForm
	{
	public:
		using Machine = IteratorRange<Assets::ScaffoldCmdIterator>;
		Machine				GetMaterialMachine(MaterialGuid guid) const;
		StringSection<>		DehashMaterialName(MaterialGuid guid) const;
		std::vector<uint64_t> GetMaterials() const;

		std::shared_ptr<ShaderPatchCollection> GetShaderPatchCollection(uint64_t hash) const;
		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }

		MaterialScaffoldCmdStreamForm();
		MaterialScaffoldCmdStreamForm(IteratorRange<::Assets::ArtifactRequestResult*> chunks, const ::Assets::DependencyValidation& depVal);
		~MaterialScaffoldCmdStreamForm();

		static const auto CompileProcessType = ConstHash64<'ResM', 'at'>::Value;
		static const ::Assets::ArtifactRequest ChunkRequests[1];
	protected:
		std::vector<std::pair<uint64_t, Machine>> _materialMachines;
		std::vector<std::shared_ptr<ShaderPatchCollection>> _patchCollections;
		const SerializableVector<std::pair<MaterialGuid, SerializableVector<char>>>* _resolvedNames = nullptr;
		std::unique_ptr<uint8[], PODAlignedDeletor>		_rawMemoryBlock;
		size_t											_rawMemoryBlockSize = 0;
		::Assets::DependencyValidation					_depVal;

		IteratorRange<ScaffoldCmdIterator> GetOuterCommandStream() const;
	};

///////////////////////////////////////////////////////////////////////////////////////////////////

	template<typename Serializer>
        void MaterialScaffold::Material::SerializeMethod(Serializer& serializer) const
    {
        SerializationOperator(serializer, _bindings);
        SerializationOperator(serializer, _matParams);
        SerializationOperator(serializer, _stateSet.GetHash());
        SerializationOperator(serializer, _constants);
		SerializationOperator(serializer, _patchCollection);
    }
		
}}

