// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "InvalidAssetDisplay.h"
#include "../../Assets/AssetSetManager.h"
#include "../../Assets/AssetServices.h"
#include "../../Assets/AssetHeap.h"
#include "../../Assets/OperationContext.h"
#include "../../Utility/Threading/Mutex.h"
#include <sstream>

namespace PlatformRig { namespace Overlays
{
	class InvalidAssetDisplay : public RenderOverlays::DebuggingDisplay::IWidget
	{
	public:
		using IOverlayContext = RenderOverlays::IOverlayContext;
		using Layout = RenderOverlays::DebuggingDisplay::Layout;
		using Interactables = RenderOverlays::DebuggingDisplay::Interactables;
		using InterfaceState = RenderOverlays::DebuggingDisplay::InterfaceState;
		using InputSnapshot = PlatformRig::InputSnapshot;

		void    Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState)
		{
			const unsigned lineHeight = 20;
			const auto titleBkground = RenderOverlays::ColorB { 0, 10, 64 }; 

			using namespace Assets;
			for (const auto&r:_currentRecords) {
				assert(r.second._state == AssetState::Invalid);

				auto titleRect = layout.AllocateFullWidth(lineHeight);
				RenderOverlays::DebuggingDisplay::FillRectangle(context, titleRect, titleBkground);
				RenderOverlays::DebuggingDisplay::DrawText().Draw(context, titleRect, r.second._initializer);

				auto msg = std::stringstream{AsString(r.second._actualizationLog)};
				for (std::string line; std::getline(msg, line, '\n');) {
					auto allocation = layout.AllocateFullWidth(lineHeight);
					if (allocation.Height() <= 0) break;
					RenderOverlays::DebuggingDisplay::DrawText().Color(0xffcfcfcf).Draw(context, allocation, line);
				}
			}
		}

		InvalidAssetDisplay(std::shared_ptr<Assets::IAssetTracking> tracking)
		: _tracking(std::move(tracking))
		{ 
			_signalId = _tracking->BindUpdateSignal(
				[this](IteratorRange<const std::pair<uint64_t, ::Assets::AssetHeapRecord>*> updates) {
					ScopedLock(_currentRecordsLock);
					auto r = _currentRecords.begin();
					TypeCodeAndId lastCode{0,0};
					for (const auto& u:updates) {
						TypeCodeAndId code { u.first, u.second._typeCode };
						assert(code > lastCode); lastCode = code;		// ensure we're in sorted order
						while (r != _currentRecords.end() && r->first < code) ++r;
						if (r != _currentRecords.end() && r->first == code) {
							if (u.second._state == ::Assets::AssetState::Invalid) {
								r->second = u.second;
							} else {
								r = _currentRecords.erase(r);
							}
						} else if (u.second._state == ::Assets::AssetState::Invalid) {
							r = _currentRecords.insert(r, std::make_pair(code, u.second));
						}
					}
				});
		}

		~InvalidAssetDisplay() 
		{
			_tracking->UnbindUpdateSignal(_signalId);
		}
	private:
		using TypeCodeAndId = std::pair<uint64_t, uint64_t>;
		Threading::Mutex _currentRecordsLock;
		std::vector<std::pair<TypeCodeAndId,::Assets::AssetHeapRecord>> _currentRecords;
		std::shared_ptr<::Assets::IAssetTracking> _tracking;
		unsigned _signalId;
	};

	std::shared_ptr<RenderOverlays::DebuggingDisplay::IWidget> CreateInvalidAssetDisplay(std::shared_ptr<Assets::IAssetTracking> tracking)
	{
		return std::make_shared<InvalidAssetDisplay>(std::move(tracking));
	}

	void OperationContextDisplay::Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState)
	{
		const unsigned lineHeight = 20;
		auto activeOperations = _opContext->GetActiveOperations();

		RenderOverlays::DebuggingDisplay::DrawText().Draw(context, layout.AllocateFullWidth(lineHeight), "Asset Operation Context");

		for (const auto&op:activeOperations) {
			auto allocation = layout.AllocateFullWidth(lineHeight);
			if (allocation.Height() < lineHeight) break;
			RenderOverlays::DebuggingDisplay::DrawText().Draw(context, allocation, op);
		}
	}

	OperationContextDisplay::OperationContextDisplay(std::shared_ptr<::Assets::OperationContext> opContext) : _opContext(opContext) {}
	OperationContextDisplay::~OperationContextDisplay() {}

}}

