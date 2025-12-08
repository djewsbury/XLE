// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Assets/AssetsCore.h"
#include "../../Math/Vector.h"
#include "../../Utility/MemoryUtils.h"
#include <string>
#include <optional>

namespace Assets { class OperationContext; class DirectorySearchRules; }
namespace Formatters { class IDynamicInputFormatter; class TextOutputFormatter; template<typename T> class TextInputFormatter; }
namespace AssetsNew { class CompoundAssetScaffold; class CompoundAssetUtil; }
namespace std { template<typename T> class promise; template<typename T> class shared_future; }

namespace RenderCore { namespace Assets
{
	class ModelRendererConstruction;

	class NascentCompoundObject
	{
	public:
		class DrawModelCommand
		{
		public:
			std::string _model;
			std::string _material;
			std::optional<Float3> _translation;
			std::optional<Float3> _scale;
			std::string _deformerBindPoint;
			std::string _compilationConfiguration;

			#if defined(_DEBUG)
				std::string _description;
			#endif	
		};
		std::vector<DrawModelCommand> _commands;

		std::string _skeleton;

		void SerializeMethod(Formatters::TextOutputFormatter& formatter) const;
		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }
		uint64_t GetHash() const;
		
		NascentCompoundObject(
			Formatters::TextInputFormatter<char>&,
			const ::Assets::DirectorySearchRules& searchRules,
			const ::Assets::DependencyValidation& depVal);
		NascentCompoundObject(
			Formatters::IDynamicInputFormatter&,
			const ::Assets::DirectorySearchRules& searchRules,
			const ::Assets::DependencyValidation& depVal);
		NascentCompoundObject();
		~NascentCompoundObject();
	private:
		::Assets::DependencyValidation _depVal;
		mutable uint64_t _hash = 0;

		template<typename Formatter>
			void Construct(Formatter&);
	};

	constexpr uint64_t GetCompileProcessType(NascentCompoundObject*) { return ConstHash64Legacy<'Comp', 'ound'>::Value; }

	static constexpr uint64_t s_CompoundObjectScaffold_ComponentName = ConstHash64("ModelRendererConstruction");
	static constexpr uint64_t s_CompoundObjectScaffold_CompileProcessType = ConstHash64Legacy<'Comp', 'ound'>::Value;

	std::shared_future<::Assets::AssetWrapper<std::shared_ptr<ModelRendererConstruction>>> GetResolvedCompoundObjectScaffoldFuture(
		std::shared_ptr<::AssetsNew::CompoundAssetUtil> util, StringSection<>);

	template<typename Formatter>
		void DeserializeModelRendererConstruction(
			RenderCore::Assets::ModelRendererConstruction& dst,
			Formatter&,
			const ::Assets::DirectorySearchRules&);
}}

