// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DeformerConstruction.h"
#include "DeformGeometryInfrastructure.h"
#include "Services.h"
#include "../Assets/ModelRendererConstruction.h"
#include "../../Assets/Marker.h"
#include "../../Assets/ContinuationUtil.h"
#include "../../Assets/ConfigFileContainer.h"
#include "../../Assets/CompoundAsset.h"
#include "../../Assets/IArtifact.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Formatters/FormatterUtils.h"
#include "../../Formatters/TextFormatter.h"

namespace RenderCore { namespace Techniques
{
	void DeformerConstruction::Add(
		::Assets::PtrToMarkerPtr<IGeoDeformer> deformer,
		DeformOperationInstantiation&& instantiation,
		unsigned elementIdx,
		unsigned geoIdx)
	{
		assert(!_sealed);
		// If we can immediately actualize, just treat it as an actualized deformer
		if (auto* actualized = deformer->TryActualize()) {
			Add(*actualized, std::move(instantiation), elementIdx, geoIdx);
			return;
		}

		StoredGeoEntry newEntry;
		auto i = std::find(_deformerMarkers.begin(), _deformerMarkers.end(), deformer);
		if (i != _deformerMarkers.end()) {
			newEntry._deformerIdx = (unsigned)std::distance(_deformerMarkers.begin(), i);
		} else {
			newEntry._deformerIdx = (unsigned)_deformerMarkers.size();
			_deformerMarkers.push_back(std::move(deformer));
			_deformers.push_back(nullptr);
		}
		newEntry._elementIdx = elementIdx;
		newEntry._geoIdx = geoIdx;
		newEntry._instantiation = std::move(instantiation);
		_storedGeoEntries.emplace_back(std::move(newEntry));
	}

	void DeformerConstruction::Add(
		std::shared_ptr<IGeoDeformer> deformer,
		DeformOperationInstantiation&& instantiation,
		unsigned elementIdx,
		unsigned geoIdx)
	{
		assert(!_sealed);
		StoredGeoEntry newEntry;
		auto i = std::find(_deformers.begin(), _deformers.end(), deformer);
		if (i != _deformers.end()) {
			newEntry._deformerIdx = (unsigned)std::distance(_deformers.begin(), i);
		} else {
			newEntry._deformerIdx = (unsigned)_deformers.size();
			_deformers.push_back(std::move(deformer));
			_deformerMarkers.push_back(nullptr);
		}
		newEntry._elementIdx = elementIdx;
		newEntry._geoIdx = geoIdx;
		newEntry._instantiation = std::move(instantiation);
		_storedGeoEntries.emplace_back(std::move(newEntry));
	}

	auto DeformerConstruction::GetGeoEntries() const -> std::vector<GeoEntry>
	{
		std::vector<GeoEntry> result;
		result.reserve(_storedGeoEntries.size());
		for (const auto& e:_storedGeoEntries)
			result.emplace_back(
				GeoEntry{
					_deformers[e._deformerIdx],
					&e._instantiation,
					e._elementIdx, e._geoIdx});
		return result;
	}

	void DeformerConstruction::Add(std::shared_ptr<IUniformsDeformerConductor> deformer)
	{
		assert(!_sealed);
		assert(!_storedUniformsEntry._deformer);
		_storedUniformsEntry._deformer = std::move(deformer);
	}

	void DeformerConstruction::FulfillWhenNotPending(std::promise<std::shared_ptr<DeformerConstruction>>&& promise, std::shared_ptr<IDevice> device)
	{
		_sealed = true;

		auto strongThis = shared_from_this();
		assert(strongThis);
		::Assets::PollToPromise(
			std::move(promise),
			[strongThis](auto timeout) {
				// wait until all pending scaffold markers are finished
				auto timeoutTime = std::chrono::steady_clock::now() + timeout;
				for (auto& f:strongThis->_deformerMarkers) {
					if (!f) continue;
					auto remainingTime = timeoutTime - std::chrono::steady_clock::now();
					if (remainingTime.count() <= 0) return ::Assets::PollStatus::Continue;
					auto t = f->StallWhilePending(std::chrono::duration_cast<std::chrono::microseconds>(remainingTime));
					if (t.value_or(::Assets::AssetState::Pending) == ::Assets::AssetState::Pending)
						return ::Assets::PollStatus::Continue;
				}
				return ::Assets::PollStatus::Finish;
			},
			[strongThis, device=std::move(device)]() mutable {
				std::vector<std::shared_ptr<IGeoDeformer>> newFinishedDeformers;
				newFinishedDeformers.reserve(strongThis->_deformerMarkers.size());
				for (auto& f:strongThis->_deformerMarkers) {
					if (f) newFinishedDeformers.push_back(f->ActualizeBkgrnd());
					else newFinishedDeformers.push_back(nullptr);
				}
				// After every one has been actualized correctly, move the pointer to the _deformers vector
				for (unsigned c=0; c<newFinishedDeformers.size(); ++c) {
					if (!newFinishedDeformers[c]) continue;
					assert(!strongThis->_deformers[c]);
					strongThis->_deformers[c] = std::move(newFinishedDeformers[c]);
					strongThis->_deformerMarkers[c] = nullptr;
				}

				strongThis->_completedGeoAttachment = CreateGeoDeformerConductor(
					*device, *strongThis->_rendererConstruction, *strongThis);

				return std::move(strongThis);
			});
	}

	uint64_t DeformerConstruction::GetHash() const
	{
		// This is used in the SimpleModelRenderer asset interface, so we need a GetHash() function
		// ... however we can't easily create a good hash for it
		return 0;
	}

	DeformerConstruction::DeformerConstruction(std::shared_ptr<Assets::ModelRendererConstruction> rendererConstruction) : _rendererConstruction(std::move(rendererConstruction)) {}
	DeformerConstruction::DeformerConstruction() = default;
	DeformerConstruction::~DeformerConstruction() = default;

	template<typename Formatter>
		void DeserializeDeformerConstruction_Internal(
			RenderCore::Techniques::DeformerConstruction& result,
			Formatter& fmttr)
	{
		auto& techniqueServices = Services::GetInstance();

		StringSection<> keyname;
		while (fmttr.TryKeyedItem(keyname)) {
			switch (fmttr.PeekNext()) {
			case Formatters::FormatterBlob::BeginElement:
				RequireBeginElement(fmttr);
				if (XlEqStringI(keyname, "DeformConfigure")) {
					keyname = RequireKeyedItem(fmttr);
					if (!XlEqString(keyname, "Name")) Throw(Formatters::FormatException("Expecting Name key", fmttr.GetLocation()));
					auto name = RequireStringValue(fmttr);

					auto* configure = techniqueServices.FindDeformConfigure(name);
					if (configure) {
						configure->Configure(result, fmttr);
					} else
						SkipElement(fmttr);		// unknown deformer type
				} else {
					SkipElement(fmttr);    // skip the whole element; it's not required
				}
				RequireEndElement(fmttr);
				break;

			case Formatters::FormatterBlob::Value:
				SkipValueOrElement(fmttr);
				break;

			default:
				Throw(Formatters::FormatException("Expecting element or value", fmttr.GetLocation()));
			}
		}
	}

	template void DeserializeDeformerConstruction_Internal(
		DeformerConstruction&,
		Formatters::TextInputFormatter<>&);

	void DeserializeDeformerConstruction(Formatters::TextInputFormatter<>& cfg, DeformerConstruction& dst)
	{
		DeserializeDeformerConstruction_Internal(dst, cfg);

		if (auto* skinConfigure = Services::GetInstance().FindDeformConfigure("gpu_skin"))
			skinConfigure->Configure(dst);
	}

	Formatters::TextInputFormatter<char>& IDeformConfigure::EmptyFormatter()
	{
		static Formatters::TextInputFormatter<char> dummy;
		return dummy;
	}

////////////////////////////////////////////////////////////////////////////////////////////////////////////

	static bool IsCompoundFile(StringSection<> extension) { return XlEqStringI(extension, "compound") || XlEqStringI(extension, "hlsl"); }

	static constexpr uint64_t s_DeformerConstruction_ComponentName = ConstHash64("DeformerConstruction");
	static constexpr uint64_t s_CompoundObjectScaffold_CompileProcessType = ConstHash64Legacy<'Comp', 'ound'>::Value;

	static void DeformerConstruction_ConstructToPromisedCompoundAsset(
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

	static void DeformerConstruction_ConstructToPromise(
		std::promise<::Assets::AssetWrapper<std::shared_ptr<DeformerConstruction>>>&& promise,
		std::shared_ptr<::AssetsNew::CompoundAssetUtil> util,
		std::shared_ptr<Assets::ModelRendererConstruction> mrc, StringSection<> initializer)
	{
		auto splitName = MakeFileNameSplitter(initializer);
		auto containerInitializer = splitName.AllExceptParameters();
		::Assets::WhenAll(::Assets::GetAssetFutureFn< DeformerConstruction_ConstructToPromisedCompoundAsset > (util->_opContext, containerInitializer)).ThenConstructToPromise(
			std::move(promise),
			[mrc=std::move(mrc), util=std::move(util), mat=splitName.Parameters().AsString()](const auto& scaffold) mutable {
				auto chunk = scaffold.get()->GetChunk(Hash64(mat), s_DeformerConstruction_ComponentName);
				Formatters::TextInputFormatter<char> fmttr{ chunk };
				auto construction = std::make_shared<DeformerConstruction>(std::move(mrc));
				DeserializeDeformerConstruction(fmttr, *construction);
				return ::Assets::AssetWrapper<std::shared_ptr<DeformerConstruction>> { std::move(construction), std::get<::Assets::DependencyValidation>(scaffold) };
			});
	}

	std::shared_future<::Assets::AssetWrapper<std::shared_ptr<DeformerConstruction>>> GetResolvedDeformerConfigurationFuture(
		std::shared_ptr<::AssetsNew::CompoundAssetUtil> util,
		std::shared_ptr<Assets::ModelRendererConstruction> mrc, StringSection<> initializer)
	{
		return ::Assets::GetAssetFutureFn< DeformerConstruction_ConstructToPromise > (util, mrc, initializer);
	}

	std::future<std::shared_ptr<DeformerConstruction>> ToFuture(DeformerConstruction& construction, std::shared_ptr<IDevice> device)
	{
		std::promise<std::shared_ptr<DeformerConstruction>> promise;
		auto result = promise.get_future();
		construction.FulfillWhenNotPending(std::move(promise), std::move(device));
		return result;
	}

	IDeformConfigure::~IDeformConfigure() = default;
}}
