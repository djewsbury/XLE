// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "CompoundObject.h"
#include "ModelRendererConstruction.h"
#include "../../Assets/ConfigFileContainer.h"
#include "../../Assets/CompoundAsset.h"
#include "../../Assets/IArtifact.h"
#include "../../Formatters/TextFormatter.h"
#include "../../Formatters/TextOutputFormatter.h"
#include "../../Formatters/FormatterUtils.h"
#include "../../Formatters/IDynamicFormatter.h"
#include "../../Math/Transformations.h"
#include "../../Math/MathSerialization.h"

namespace RenderCore { namespace Assets
{
	template<typename Formatter>
		static NascentCompoundObject::DrawModelCommand DeserializeDrawModelCommand(Formatter& formatter, const ::Assets::DirectorySearchRules&);

	template<typename Formatter>
		void DeserializeModelRendererConstruction(
			ModelRendererConstruction& result,
			Formatter& fmttr,
			const ::Assets::DirectorySearchRules& searchRules)
	{
		char buffer[MaxPath];
		StringSection<> keyname;
		while (fmttr.TryKeyedItem(keyname)) {
			switch (fmttr.PeekNext()) {
			case Formatters::FormatterBlob::BeginElement:
				RequireBeginElement(fmttr);
				if (XlEqStringI(keyname, "DrawModel")) {
					auto modelCommand = DeserializeDrawModelCommand(fmttr, searchRules);
					auto newElement = result.AddElement();
					if (modelCommand._model.empty())
						Throw(std::runtime_error("Missing model name in DrawModel command"));
					if (!modelCommand._compilationConfiguration.empty())
						newElement.SetCompilationConfiguration(modelCommand._compilationConfiguration);
					newElement.SetModelAndMaterials(modelCommand._model, modelCommand._material);
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

			case Formatters::FormatterBlob::Value:
				if (XlEqString(keyname, "Skeleton")) {
					searchRules.ResolveFile(buffer, RequireStringValue(fmttr));
					result.SetSkeletonScaffold(buffer);
				} else
					Throw(Formatters::FormatException("Unexpected attribute in CompoundObject", fmttr.GetLocation()));
				break;

			default:
				Throw(Formatters::FormatException("Expecting element or value", fmttr.GetLocation()));
			}
		}
	}

	template void DeserializeModelRendererConstruction(
		ModelRendererConstruction&,
		Formatters::IDynamicInputFormatter&,
		const ::Assets::DirectorySearchRules&);

	template void DeserializeModelRendererConstruction(
		ModelRendererConstruction&,
		Formatters::TextInputFormatter<>&,
		const ::Assets::DirectorySearchRules&);

	static bool IsCompoundFile(StringSection<> extension) { return XlEqStringI(extension, "compound") || XlEqStringI(extension, "hlsl"); }

	static void ModelRendererConstruction_ConstructToPromisedCompoundAsset(
		std::promise<::Assets::ContextImbuedAsset<std::shared_ptr<::AssetsNew::CompoundAssetScaffold>>>&& promise,
		std::shared_ptr<::Assets::OperationContext> opContext,
		StringSection<> initializer)
	{
		if (IsCompoundFile(MakeFileNameSplitter(initializer).Extension())) {
			ConsoleRig::GlobalServices::GetInstance().GetShortTaskThreadPool().Enqueue(
				[init=initializer.AsString(), promise=std::move(promise)]() mutable {
					TRY {
						promise.set_value(::Assets::AutoConstructAsset<::Assets::ContextImbuedAsset<std::shared_ptr<::AssetsNew::CompoundAssetScaffold>>>(init));
					} CATCH (...) {
						promise.set_exception(std::current_exception());
					} CATCH_END
				});
		} else {
			ConsoleRig::GlobalServices::GetInstance().GetLongTaskThreadPool().Enqueue(
				[promise=std::move(promise), opContext=std::move(opContext), init=initializer.AsString()]() mutable {
					TRY {
						::Assets::DefaultCompilerConstructionSynchronously(
							std::move(promise),
							s_CompoundObjectScaffold_CompileProcessType,
							::Assets::InitializerPack{init}, opContext.get());
					} CATCH (...) {
						promise.set_exception(std::current_exception());
					} CATCH_END
				});
		}
	}

	static void ModelRendererConstruction_ConstructToPromise(
		std::promise<::Assets::AssetWrapper<std::shared_ptr<ModelRendererConstruction>>>&& promise,
		std::shared_ptr<::AssetsNew::CompoundAssetUtil> util,
		StringSection<> initializer)
	{
		auto splitName = MakeFileNameSplitter(initializer);
		auto containerInitializer = splitName.AllExceptParameters();
		::Assets::WhenAll(::Assets::GetAssetFutureFn< ModelRendererConstruction_ConstructToPromisedCompoundAsset > (util->_opContext, containerInitializer)).ThenConstructToPromise(
			std::move(promise),
			[util=std::move(util), mat=splitName.Parameters().AsString()](const auto& scaffold) {
				auto chunk = scaffold.get()->GetChunk(Hash64(mat), s_CompoundObjectScaffold_ComponentName);
				auto construction = std::make_shared<RenderCore::Assets::ModelRendererConstruction>();
				Formatters::TextInputFormatter<char> fmttr{ chunk };
				DeserializeModelRendererConstruction(*construction, fmttr, std::get<::Assets::DirectorySearchRules>(scaffold));
				return ::Assets::AssetWrapper<std::shared_ptr<ModelRendererConstruction>> { std::move(construction), std::get<::Assets::DependencyValidation>(scaffold) };
			});
	}

	std::shared_future<::Assets::AssetWrapper<std::shared_ptr<ModelRendererConstruction>>> GetResolvedCompoundObjectScaffoldFuture(
		std::shared_ptr<::AssetsNew::CompoundAssetUtil> util, StringSection<> initializer)
	{
		return ::Assets::GetAssetFutureFn< ModelRendererConstruction_ConstructToPromise > (util, initializer);
	}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	template<typename Formatter>
		static std::vector<std::pair<std::string, float>> DeserializeWeights(Formatter& formatter)
	{
		std::vector<std::pair<std::string, float>> result;
		while (formatter.PeekNext() == FormatterBlob::KeyedItem) {
			auto name = RequireKeyedItem(formatter);
			if (formatter.PeekNext() != FormatterBlob::Value)
				Throw(Formatters::FormatException("Unexpected element while serializing Weights in DrawModelCommand", formatter.GetLocation()));
			result.push_back({name.AsString(), Conversion::Convert<float>(RequireStringValue(formatter))});
		}
		return result;
	}

	template<typename Formatter>
		static NascentCompoundObject::DrawModelCommand DeserializeDrawModelCommand(Formatter& formatter, const ::Assets::DirectorySearchRules& searchRules)
	{
		NascentCompoundObject::DrawModelCommand result;
		char buffer[MaxPath];

		while (formatter.PeekNext() == Formatters::FormatterBlob::KeyedItem) {
			auto name = RequireKeyedItem(formatter);
			switch (formatter.PeekNext()) {
			case Formatters::FormatterBlob::BeginElement:
				Throw(Formatters::FormatException("Unexpected element while serializing MorphDeformer", formatter.GetLocation()));
				break;

			case Formatters::FormatterBlob::Value:
				{
					if (XlEqString(name, "Model")) {
						searchRules.ResolveFile(buffer, RequireStringValue(formatter));
						result._model = buffer;
					} else if (XlEqString(name, "Material")) {
						searchRules.ResolveFile(buffer, RequireStringValue(formatter));
						result._material = buffer;
					} else if (XlEqString(name, "Scale")) {
						result._scale = Formatters::RequireCastValue<Float3>(formatter);
					} else if (XlEqString(name, "Translation")) {
						result._translation = Formatters::RequireCastValue<Float3>(formatter);
					} else if (XlEqString(name, "DeformerBindPoint")) {
						result._deformerBindPoint = RequireStringValue(formatter).AsString();
					} else if (XlEqString(name, "CompilationConfiguration")) {
						searchRules.ResolveFile(buffer, RequireStringValue(formatter));
						result._compilationConfiguration = buffer;
					} else 
						Throw(Formatters::FormatException(StringMeld<512>() << "Unknown attribute (" << name << ") while serializing DrawModelCommand", formatter.GetLocation()));
				}
				break;

			default:
				Throw(Formatters::FormatException("Expecting element or value", formatter.GetLocation()));
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
		while (formatter.PeekNext() == Formatters::FormatterBlob::KeyedItem) {
			auto name = RequireKeyedItem(formatter);
			switch (formatter.PeekNext()) {
			case Formatters::FormatterBlob::BeginElement:
				RequireBeginElement(formatter);
				if (XlEqStringI(name, "DrawModel")) {
					_commands.emplace_back(DeserializeDrawModelCommand(formatter, {}));
				} else {
					SkipElement(formatter);    // skip the whole element; it's not required
				}
				RequireEndElement(formatter);
				break;

			case Formatters::FormatterBlob::Value:
				if (XlEqString(name, "Skeleton")) {
					_skeleton = RequireStringValue(formatter).AsString();
				} else
					Throw(Formatters::FormatException("Unexpected attribute in CompoundObject", formatter.GetLocation()));
				break;

			default:
				Throw(Formatters::FormatException("Expecting element or value", formatter.GetLocation()));
			}
		}
	}

	uint64_t NascentCompoundObject::GetHash() const
	{
		if (expect_evaluation(_hash, true)) return _hash;

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
		Formatters::TextInputFormatter<>& formatter,
		const ::Assets::DirectorySearchRules& searchRules,
		const ::Assets::DependencyValidation& depVal)
	: _depVal(depVal)
	{
		Construct(formatter);
	}

	NascentCompoundObject::NascentCompoundObject(
		Formatters::IDynamicInputFormatter& formatter,
		const ::Assets::DirectorySearchRules& searchRules,
		const ::Assets::DependencyValidation& depVal)
	: _depVal(depVal)
	{
		Construct(formatter);
	}

	NascentCompoundObject::NascentCompoundObject()
	{}

	NascentCompoundObject::~NascentCompoundObject() {}

	void NascentCompoundObject::SerializeMethod(Formatters::TextOutputFormatter& formatter) const
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
			if (!cmd._compilationConfiguration.empty())
				formatter.WriteKeyedValue("CompilationConfiguration", cmd._compilationConfiguration);
			formatter.EndElement(ele);
		}

		if (!_skeleton.empty())
			formatter.WriteKeyedValue("Skeleton", _skeleton);
	}


#if 0
	static const auto s_compoundObjectCompileProcessType = ConstHash64Legacy<'Comp', 'ound'>::Value;

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