// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../RenderOverlays/DebuggingDisplay.h"
#include <vector>

namespace Utility { class IHierarchicalProfiler; }
namespace Assets { class AssetHeapRecord; class OperationContext; class IAssetTracking; }

namespace PlatformRig { namespace Overlays
{
	std::shared_ptr<RenderOverlays::DebuggingDisplay::IWidget> CreateInvalidAssetDisplay(std::shared_ptr<Assets::IAssetTracking> tracking);

	class ITrackedAssetList
	{
	public:
		virtual void Lock() = 0;
		virtual void Unlock() = 0;

		using TypeCodeAndId = std::pair<uint64_t, uint64_t>;
		virtual IteratorRange<const std::pair<TypeCodeAndId,::Assets::AssetHeapRecord>*> GetCurrentRecords() const = 0;
		virtual unsigned BindOnChange(std::function<void()>&&) = 0;
		virtual void UnbindOnChange(unsigned) = 0;
		virtual ~ITrackedAssetList();
	};
	std::shared_ptr<ITrackedAssetList> CreateTrackedAssetList(std::shared_ptr<Assets::IAssetTracking> tracking, ::Assets::AssetState state);

	class OperationContextDisplay : public RenderOverlays::DebuggingDisplay::IWidget ///////////////////////////////////////////////////////////
	{
	public:
		using IOverlayContext = RenderOverlays::IOverlayContext;
		using Layout = RenderOverlays::DebuggingDisplay::Layout;
		using Interactables = RenderOverlays::DebuggingDisplay::Interactables;
		using InterfaceState = RenderOverlays::DebuggingDisplay::InterfaceState;
		using InputSnapshot = PlatformRig::InputSnapshot;

		void    Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState);

		OperationContextDisplay(std::shared_ptr<::Assets::OperationContext>);
		~OperationContextDisplay();
	private:
		std::shared_ptr<::Assets::OperationContext> _opContext;
	};
}}
