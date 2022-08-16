// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "CompoundObject.h"
#include "ModelRendererConstruction.h"
#include "../../Formatters/IDynamicFormatter.h"
#include "../../Math/Transformations.h"
#include "../../Math/MathSerialization.h"
#include "../../Assets/ConfigFileContainer.h"
#include "../../Utility/Streams/StreamFormatter.h"
#include "../../Utility/Streams/OutputStreamFormatter.h"
#include "../../Utility/Streams/FormatterUtils.h"

namespace RenderCore { namespace Assets
{
	template<typename Formatter>
		static NascentCompoundObject::DrawModelCommand DeserializeDrawModelCommand(Formatter& formatter);

	template<typename Formatter>
		void DeserializeModelRendererConstruction(
			ModelRendererConstruction& result,
			std::shared_ptr<::Assets::OperationContext> loadingContext,
			Formatter& fmttr)
	{
		StringSection<> keyname;
		while (fmttr.TryKeyedItem(keyname)) {
			switch (fmttr.PeekNext()) {
			case FormatterBlob::BeginElement:
				RequireBeginElement(fmttr);
				if (XlEqStringI(keyname, "DrawModel")) {
					auto modelCommand = DeserializeDrawModelCommand(fmttr);
					auto newElement = result.AddElement();
					if (modelCommand._model.empty())
						Throw(std::runtime_error("Missing model name in DrawModel command"));
					newElement.SetModelAndMaterialScaffolds(loadingContext, modelCommand._model, modelCommand._material);
					if (modelCommand._scale || modelCommand._translation) {
						auto modelToObject = AsFloat4x4(
							ScaleRotationTranslationM{
								modelCommand._scale.value_or(Float3{1.0f, 1.0f, 1.0f}), 
								Identity<Float3x3>(), 
								modelCommand._translation.value_or(Float3{0.f, 0.f, 0.0f})});
						newElement.SetElementToObject(modelToObject);
					}
					if (!modelCommand._deformerBindPoint.empty())
						newElement.SetDeformerBindPoint(Hash64(modelCommand._deformerBindPoint));
				} else {
					SkipElement(fmttr);    // skip the whole element; it's not required
				}
				RequireEndElement(fmttr);
				break;

			case FormatterBlob::Value:
				if (XlEqString(keyname, "Skeleton")) {
					auto skeletonName = RequireStringValue(fmttr).AsString();
					result.SetSkeletonScaffold(loadingContext, skeletonName);
				} else
					Throw(Utility::FormatException("Unexpected attribute in CompoundObject", fmttr.GetLocation()));
				break;

			default:
				Throw(Utility::FormatException("Expecting element or value", fmttr.GetLocation()));
			}
		}
	}

	template void DeserializeModelRendererConstruction(
		ModelRendererConstruction&,
		std::shared_ptr<::Assets::OperationContext>,
		Formatters::IDynamicFormatter&);

	template void DeserializeModelRendererConstruction(
		ModelRendererConstruction&,
		std::shared_ptr<::Assets::OperationContext>,
		InputStreamFormatter<>&);

	uint64_t CompoundObjectScaffold::GetHash() const { return _modelRendererConstruction->GetHash(); }

	InputStreamFormatter<> CompoundObjectScaffold::OpenConfiguration() const
	{
		auto container = ::Assets::ConfigFileContainer<>(_blob, _depVal);
		return container.GetRootFormatter();
	}

	CompoundObjectScaffold::CompoundObjectScaffold() {}
	CompoundObjectScaffold::CompoundObjectScaffold(
		std::shared_ptr<ModelRendererConstruction> modelRendererConstruction,
		::Assets::Blob blob,
		::Assets::DependencyValidation depVal)
	: _modelRendererConstruction(std::move(modelRendererConstruction))
	, _blob(std::move(blob))
	, _depVal(std::move(depVal))
	{}

	CompoundObjectScaffold::CompoundObjectScaffold(const ::Assets::Blob& blob, const ::Assets::DependencyValidation& depVal, StringSection<>)
	: _blob(blob)
	, _depVal(depVal)
	{
		auto container = ::Assets::ConfigFileContainer<>(_blob, _depVal);
		auto fmttr = container.GetRootFormatter();
		_modelRendererConstruction = std::make_shared<ModelRendererConstruction>();
		std::shared_ptr<::Assets::OperationContext> operationContext;		// todo
		DeserializeModelRendererConstruction(*_modelRendererConstruction, operationContext, fmttr);
	}

	CompoundObjectScaffold::~CompoundObjectScaffold() {}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	template<typename Formatter>
		static std::vector<std::pair<std::string, float>> DeserializeWeights(Formatter& formatter)
	{
		std::vector<std::pair<std::string, float>> result;
		while (formatter.PeekNext() == FormatterBlob::KeyedItem) {
			auto name = RequireKeyedItem(formatter);
			if (formatter.PeekNext() != FormatterBlob::Value)
				Throw(Utility::FormatException("Unexpected element while serializing Weights in DrawModelCommand", formatter.GetLocation()));
			result.push_back({name.AsString(), Conversion::Convert<float>(RequireStringValue(formatter))});
		}
		return result;
	}

	template<typename Formatter>
		static NascentCompoundObject::DrawModelCommand DeserializeDrawModelCommand(Formatter& formatter)
	{
		NascentCompoundObject::DrawModelCommand result;

		while (formatter.PeekNext() == FormatterBlob::KeyedItem) {
			auto name = RequireKeyedItem(formatter);
			switch (formatter.PeekNext()) {
			case FormatterBlob::BeginElement:
				{
					RequireBeginElement(formatter);
					if (XlEqString(name, "MorphDeformer")) {
						// auto morphDeformer = DeserializeMorphDeformer(formatter);
					} else
						Throw(Utility::FormatException("Unexpected element while serializing MorphDeformer", formatter.GetLocation()));
					RequireEndElement(formatter);
				}
				break;

			case FormatterBlob::Value:
				{
					if (XlEqString(name, "Model")) {
						result._model = RequireStringValue(formatter).AsString();
					} else if (XlEqString(name, "Material")) {
						result._material = RequireStringValue(formatter).AsString();
					} else if (XlEqString(name, "Scale")) {
						result._scale = RequireCastValue<Float3>(formatter);
					} else if (XlEqString(name, "Translation")) {
						result._translation = RequireCastValue<Float3>(formatter);
					} else if (XlEqString(name, "DeformerBindPoint")) {
						result._deformerBindPoint = RequireStringValue(formatter).AsString();
					} else 
						Throw(Utility::FormatException(StringMeld<512>() << "Unknown attribute (" << name << ") while serializing DrawModelCommand", formatter.GetLocation()));
				}
				break;

			default:
				Throw(Utility::FormatException("Expecting element or value", formatter.GetLocation()));
			}
		}

		#if defined(_DEBUG)
			result._description = Concatenate(result._model, ":", result._material);
		#endif	
		return result;
	}

	template<typename Formatter>
		void NascentCompoundObject::Construct(Formatter& formatter)
	{
		while (formatter.PeekNext() == FormatterBlob::KeyedItem) {
			auto name = RequireKeyedItem(formatter);
			switch (formatter.PeekNext()) {
			case FormatterBlob::BeginElement:
				RequireBeginElement(formatter);
				if (XlEqStringI(name, "DrawModel")) {
					_commands.emplace_back(DeserializeDrawModelCommand(formatter));
				} else {
					SkipElement(formatter);    // skip the whole element; it's not required
				}
				RequireEndElement(formatter);
				break;

			case FormatterBlob::Value:
				if (XlEqString(name, "Skeleton")) {
					_skeleton = RequireStringValue(formatter).AsString();
				} else
					Throw(Utility::FormatException("Unexpected attribute in CompoundObject", formatter.GetLocation()));
				break;

			default:
				Throw(Utility::FormatException("Expecting element or value", formatter.GetLocation()));
			}
		}
	}

	uint64_t NascentCompoundObject::GetHash() const
	{
		if (__builtin_expect(_hash, true)) return _hash;

		_hash = Hash64(_skeleton);
		for (auto&cmd:_commands) {
			_hash = Hash64(cmd._model, _hash);
			_hash = Hash64(cmd._material, _hash);
			_hash = Hash64(cmd._deformerBindPoint, _hash);
			// potential ambiguity here if one of these is missing, and but the others are identical!
			if (cmd._translation) _hash = Hash64(MakeOpaqueIteratorRange(cmd._translation.value()), _hash);
			if (cmd._scale) _hash = Hash64(MakeOpaqueIteratorRange(cmd._scale.value()), _hash);
		}
		return _hash;
	}

	NascentCompoundObject::NascentCompoundObject(
		InputStreamFormatter<>& formatter,
		const ::Assets::DirectorySearchRules& searchRules,
		const ::Assets::DependencyValidation& depVal)
	: _depVal(depVal)
	{
		Construct(formatter);
	}

	NascentCompoundObject::NascentCompoundObject(
		Formatters::IDynamicFormatter& formatter,
		const ::Assets::DirectorySearchRules& searchRules,
		const ::Assets::DependencyValidation& depVal)
	: _depVal(depVal)
	{
		Construct(formatter);
	}

	NascentCompoundObject::NascentCompoundObject()
	{}

	NascentCompoundObject::~NascentCompoundObject() {}

	void NascentCompoundObject::SerializeMethod(OutputStreamFormatter& formatter) const
	{
		for (auto&cmd:_commands) {
			auto ele = formatter.BeginKeyedElement("DrawModel");
			formatter.WriteKeyedValue("Model", cmd._model);
			if (!cmd._material.empty())
				formatter.WriteKeyedValue("Material", cmd._material);
			if (cmd._translation.has_value())
				formatter.WriteKeyedValue("Translation", ImpliedTyping::AsString(cmd._translation.value()));
			if (cmd._scale.has_value())
				formatter.WriteKeyedValue("Scale", ImpliedTyping::AsString(cmd._scale.value()));
			if (!cmd._deformerBindPoint.empty())
				formatter.WriteKeyedValue("DeformerBindPoint", cmd._deformerBindPoint);
			formatter.EndElement(ele);
		}

		if (!_skeleton.empty())
			formatter.WriteKeyedValue("Skeleton", _skeleton);
	}


#if 0
	static const auto s_compoundObjectCompileProcessType = ConstHash64<'Comp', 'ound'>::Value;

	static void ConstructCompoundObjectFromCompile(
		std::promise<CompoundObjectScaffold>&& promise,
		std::shared_ptr<::Assets::OperationContext> operationContext,
		const ::Assets::IArtifactCollection& artifactCollection,
		std::shared_ptr<RenderCore::IDevice> device)
	{
		auto targetCode = s_compoundObjectCompileProcessType;
		::Assets::ArtifactRequest request { "default-blob", targetCode, ~0u, ::Assets::ArtifactRequest::DataType::SharedBlob };
		auto reqRes = artifactCollection.ResolveRequests(MakeIteratorRange(&request, &request+1));
		if (reqRes.empty()) {
			promise.set_exception(std::make_exception_ptr(::Assets::Exceptions::InvalidAsset{{}, artifactCollection.GetDependencyValidation(), ::Assets::AsBlob("Default compilation result chunk not found")}));
			return;
		}

		auto container = ::Assets::ConfigFileContainer<>(reqRes[0]._sharedBlob, artifactCollection.GetDependencyValidation());
		auto fmttr = container.GetRootFormatter();
		auto modelRendererConstruction = std::make_shared<RenderCore::Assets::ModelRendererConstruction>();
		DeserializeModelRendererConstruction(*modelRendererConstruction, operationContext, fmttr);

		promise.set_value(CompoundObjectScaffold{std::move(modelRendererConstruction), std::move(reqRes[0]._sharedBlob), artifactCollection.GetDependencyValidation()});
	}

	void CompoundObjectScaffold::ConstructToPromise(
		std::promise<CompoundObjectScaffold>&& promise,
		std::shared_ptr<::Assets::OperationContext> operationContext,
		StringSection<> src)
	{
		// Construction needs to be special cased here, because we need a multi-stage construction involving
		// creating the ModelRendererConstruction first, and then the DeformerConstruction
		// (and we'll wait for both)
		TRY {
			::Assets::InitializerPack initializerPack{src};
			auto targetCode = s_compoundObjectCompileProcessType;
			auto marker = ::Assets::Internal::BeginCompileOperation(targetCode, std::move(initializerPack));
			if (!marker)
				Throw(std::runtime_error("No compiler found for compound object construction (" + src.AsString() + ")"));

			auto device = RenderCore::Techniques::Services::GetDevicePtr();

			auto artifactQuery = marker->GetArtifact(targetCode);
			if (artifactQuery.first) {
				ConstructCompoundObjectFromCompile(std::move(promise), operationContext, *artifactQuery.first, device);
			} else {
				assert(artifactQuery.second.Valid());
				::Assets::PollToPromise(
					std::move(promise),
					[pendingCompile=artifactQuery.second](auto timeout) {
						auto stallResult = pendingCompile.StallWhilePending(timeout);
						if (stallResult.value_or(::Assets::AssetState::Pending) == ::Assets::AssetState::Pending)
							return ::Assets::PollStatus::Continue;
						return ::Assets::PollStatus::Finish;
					},
					[pendingCompile=artifactQuery.second, operationContext, device=std::move(device)](std::promise<CompoundObjectScaffold>&& promise) mutable {
						TRY {
							ConstructCompoundObjectFromCompile(std::move(promise), std::move(operationContext), pendingCompile.GetArtifactCollection(), device);
						} CATCH (...) {
							promise.set_exception(std::current_exception());
						} CATCH_END
					});

				if (operationContext) {
					auto operation = operationContext->Begin(Concatenate("Compiling (", src, ") with compiler (", marker->GetCompilerDescription(), ")"));
					operation.EndWithFuture(artifactQuery.second.ShareFuture());
				}
			}
		} CATCH (...) {
			promise.set_exception(std::current_exception());
		} CATCH_END
	}
#endif

}}