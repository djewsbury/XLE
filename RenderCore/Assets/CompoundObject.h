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
namespace Utility { class OutputStreamFormatter; template<typename T> class InputStreamFormatter; }
namespace Formatters { class IDynamicFormatter; }

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

		void SerializeMethod(OutputStreamFormatter& formatter) const;
		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }
		uint64_t GetHash() const;
		
		NascentCompoundObject(
			InputStreamFormatter<char>&,
			const ::Assets::DirectorySearchRules& searchRules,
			const ::Assets::DependencyValidation& depVal);
		NascentCompoundObject(
			Formatters::IDynamicFormatter&,
			const ::Assets::DirectorySearchRules& searchRules,
			const ::Assets::DependencyValidation& depVal);
		NascentCompoundObject();
		~NascentCompoundObject();

		static const auto CompileProcessType = ConstHash64<'Comp', 'ound'>::Value;
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
		InputStreamFormatter<char> OpenConfiguration() const;

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }
		uint64_t GetHash() const;

		CompoundObjectScaffold();
		CompoundObjectScaffold(
			std::shared_ptr<RenderCore::Assets::ModelRendererConstruction> modelRendererConstruction,
			::Assets::Blob blob,
			::Assets::DependencyValidation depVal);
		CompoundObjectScaffold(const ::Assets::Blob& blob, const ::Assets::DependencyValidation& depVal, StringSection<> requestParameters);
		~CompoundObjectScaffold();

		static const auto CompileProcessType = ConstHash64<'Comp', 'ound'>::Value;
	private:
		std::shared_ptr<RenderCore::Assets::ModelRendererConstruction> _modelRendererConstruction;
		::Assets::Blob _blob;
		::Assets::DependencyValidation _depVal;
	};

	template<typename Formatter>
		void DeserializeModelRendererConstruction(
			RenderCore::Assets::ModelRendererConstruction& dst,
			std::shared_ptr<::Assets::OperationContext> loadingContext,
			Formatter&);
}}

