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

		static const auto CompileProcessType = ConstHash64Legacy<'Comp', 'ound'>::Value;
	private:
		::Assets::DependencyValidation _depVal;
		mutable uint64_t _hash = 0;

		template<typename Formatter>
			void Construct(Formatter&);
	};

	class CompoundObjectScaffold
	{
	public:
		auto GetModelRendererConstruction() const -> const std::shared_ptr<ModelRendererConstruction>& { return _modelRendererConstruction; }
		Formatters::TextInputFormatter<char> OpenConfiguration() const;

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }
		uint64_t GetHash() const;

		bool AreScaffoldsInvalidated() const;
		::Assets::DependencyValidation MakeScaffoldsDependencyValidation() const;

		CompoundObjectScaffold();
		CompoundObjectScaffold(
			std::shared_ptr<RenderCore::Assets::ModelRendererConstruction> modelRendererConstruction,
			::Assets::Blob blob,
			::Assets::DependencyValidation depVal);
		CompoundObjectScaffold(const ::Assets::Blob& blob, const ::Assets::DependencyValidation& depVal, StringSection<> requestParameters);
		~CompoundObjectScaffold();

		static const auto CompileProcessType = ConstHash64Legacy<'Comp', 'ound'>::Value;
	private:
		std::shared_ptr<RenderCore::Assets::ModelRendererConstruction> _modelRendererConstruction;
		::Assets::Blob _blob;
		::Assets::DependencyValidation _depVal;
	};

	template<typename Formatter>
		void DeserializeModelRendererConstruction(
			RenderCore::Assets::ModelRendererConstruction& dst,
			Formatter&);
}}

