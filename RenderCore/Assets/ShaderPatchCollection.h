// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Types.h"		// for ShaderStage
#include "../../ShaderParser/ShaderInstantiation.h"
#include "../../Utility/IteratorUtils.h"
#include <utility>
#include <iosfwd>

namespace Formatters { template<typename Char> class TextInputFormatter; class TextOutputFormatter; }
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
		StringSection<> GetOverrideShader(ShaderStage stage) const { return (unsigned(stage)<dimof(_overrideShaders)) ? MakeStringSection(_overrideShaders[unsigned(stage)]) : StringSection<>{}; }

		// setters
		void AddPatch(const std::string& name, const ShaderSourceParser::InstantiationRequest&);
		void SetDescriptorSetFileName(const std::string&);
		void SetPreconfigurationFileName(const std::string&);
		void OverrideShader(ShaderStage, const std::string&);

		void MergeInWithFilenameResolve(const ShaderPatchCollection& dest, const ::Assets::DirectorySearchRules&);

		uint64_t GetHash() const;

		friend bool operator<(const ShaderPatchCollection& lhs, const ShaderPatchCollection& rhs);
		friend bool operator<(const ShaderPatchCollection& lhs, uint64_t rhs);
		friend bool operator<(uint64_t lhs, const ShaderPatchCollection& rhs);

		friend std::ostream& SerializationOperator(std::ostream& str, const ShaderPatchCollection&);
		friend void SerializationOperator(Formatters::TextOutputFormatter& formatter, const ShaderPatchCollection& patchCollection);

		ShaderPatchCollection();
		ShaderPatchCollection(Formatters::TextInputFormatter<utf8>& formatter);
		ShaderPatchCollection(IteratorRange<const std::pair<std::string, ShaderSourceParser::InstantiationRequest>*> patches);
		ShaderPatchCollection(std::vector<std::pair<std::string, ShaderSourceParser::InstantiationRequest>>&& patches);
		~ShaderPatchCollection();

	private:
		std::vector<std::pair<std::string, ShaderSourceParser::InstantiationRequest>> _patches;
		std::string _descriptorSet, _preconfiguration;
		std::string _overrideShaders[3];
		uint64_t _hash = ~0ull;

		void SortAndCalculateHash();
	};
	
	std::vector<ShaderPatchCollection> DeserializeShaderPatchCollectionSet(Formatters::TextInputFormatter<utf8>& formatter);
	void SerializeShaderPatchCollectionSet(Formatters::TextOutputFormatter& formatter, IteratorRange<const ShaderPatchCollection*> patchCollections);
}}

