// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../ShaderParser/ShaderInstantiation.h"
#include "../../Assets/AssetsCore.h"
#include "../../Assets/AssetUtils.h"
#include "../../Utility/IteratorUtils.h"
#include <utility>
#include <iosfwd>

namespace Utility { template<typename Char> class TextInputFormatter; class TextOutputFormatter; }
namespace Assets { class BlockSerializer; }

namespace RenderCore { namespace Assets
{
	class ShaderPatchCollection
	{
	public:
		// getters
		IteratorRange<const std::pair<std::string, ShaderSourceParser::InstantiationRequest>*> GetPatches() const { return MakeIteratorRange(_patches); }
		StringSection<> GetDescriptorSetFileName() const { return MakeStringSection(_descriptorSet.begin(), _descriptorSet.end()); }
		StringSection<> GetPreconfigurationFileName() const { return MakeStringSection(_preconfiguration.begin(), _preconfiguration.end()); }

		// setters
		void AddPatch(const std::string& name, const ShaderSourceParser::InstantiationRequest&);
		void SetDescriptorSetFileName(const std::string&);
		void SetPreconfigurationFileName(const std::string&);

		void MergeIn(const ShaderPatchCollection& dest);

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }
		uint64_t GetHash() const;

		friend bool operator<(const ShaderPatchCollection& lhs, const ShaderPatchCollection& rhs);
		friend bool operator<(const ShaderPatchCollection& lhs, uint64_t rhs);
		friend bool operator<(uint64_t lhs, const ShaderPatchCollection& rhs);

		friend std::ostream& SerializationOperator(std::ostream& str, const ShaderPatchCollection&);
		friend void SerializationOperator(TextOutputFormatter& formatter, const ShaderPatchCollection& patchCollection);

		ShaderPatchCollection();
		ShaderPatchCollection(TextInputFormatter<utf8>& formatter, const ::Assets::DirectorySearchRules&, const ::Assets::DependencyValidation& depVal);
		ShaderPatchCollection(IteratorRange<const std::pair<std::string, ShaderSourceParser::InstantiationRequest>*> patches);
		ShaderPatchCollection(std::vector<std::pair<std::string, ShaderSourceParser::InstantiationRequest>>&& patches);
		~ShaderPatchCollection();

	private:
		std::vector<std::pair<std::string, ShaderSourceParser::InstantiationRequest>> _patches;
		std::string _descriptorSet, _preconfiguration;
		uint64_t _hash = ~0ull;
		::Assets::DependencyValidation _depVal;

		void SortAndCalculateHash();
	};
	
	std::vector<ShaderPatchCollection> DeserializeShaderPatchCollectionSet(TextInputFormatter<utf8>& formatter, const ::Assets::DirectorySearchRules&, const ::Assets::DependencyValidation& depVal);
	void SerializeShaderPatchCollectionSet(TextOutputFormatter& formatter, IteratorRange<const ShaderPatchCollection*> patchCollections);
}}

