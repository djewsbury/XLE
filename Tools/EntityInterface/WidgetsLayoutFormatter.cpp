// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "WidgetsLayoutFormatter.h"
#include "MinimalBindingEngine.h"
#include "MountedData.h"
#include "../../RenderOverlays/CommonWidgets.h"
#include "../../RenderOverlays/DebuggingDisplay.h"
#include "../../RenderOverlays/LayoutEngine.h"
#include "../../RenderOverlays/ShapesRendering.h"
#include "../../RenderOverlays/DrawText.h"
#include "../../Formatters/FormatterUtils.h"
#include "../../Assets/Marker.h"
#include <vector>
#include <set>
#include <stack>

using namespace PlatformRig::Literals;
using namespace Assets::Literals;
using namespace Utility::Literals;

namespace EntityInterface
{
	using namespace RenderOverlays;
	using namespace RenderOverlays::DebuggingDisplay;

	constexpr auto enter      = "enter"_key;
	constexpr auto escape     = "escape"_key;

	class WidgetsLayoutContext : public EntityInterface::IWidgetsLayoutContext
	{
	public:
		LayoutEngine _layoutEngine;

		MinimalBindingEngine& GetBindingEngine() override { return *_state; }
		std::shared_ptr<MinimalBindingEngine> GetBindingEnginePtr() override { return _state; }
		LayoutEngine& GetLayoutEngine() override { return _layoutEngine; }
		RenderOverlays::GuidStackHelper& GetGuidStack() override { return _layoutEngine.GuidStack(); }

		void PushHierarchicalEnabledState(uint64_t guid) override { _hierarchicalEnabledStates.push_back(guid); }
		void PopHierarchicalEnabledState() override { _hierarchicalEnabledStates.pop_back(); }
		HierarchicalEnabledState EnabledByHierarchy() const override
		{
			for (auto i=_hierarchicalEnabledStates.rbegin(); i!=_hierarchicalEnabledStates.rend(); ++i) {
				if (*i != 0) {
					auto state = _state->IsEnabled(*i);
					return state ? HierarchicalEnabledState::EnableChildren : HierarchicalEnabledState::DisableChildren;
				}
			}
			return HierarchicalEnabledState::NoImpact;
		}

		LayedOutWidgets BuildLayedOutWidgets()
		{
			return _layoutEngine.BuildLayedOutWidgets();
		}

		WidgetsLayoutContext(std::shared_ptr<MinimalBindingEngine> state) : _state(std::move(state)) {}

	private:
		std::shared_ptr<MinimalBindingEngine> _state;
		std::vector<uint64_t> _hierarchicalEnabledStates;
	};

	class CommonWidgetsStyler : public ICommonWidgetsStyler
	{
	public:
		ImbuedNode* BeginSharedLeftRightCtrl(IWidgetsLayoutContext& context, StringSection<> name, const V<uint64_t>& modelValue, uint64_t interactable)
		{
			const auto lineHeight = baseLineHeight+4;
			auto baseNode = context.GetLayoutEngine().NewNode();
			YGNodeStyleSetWidthPercent(baseNode, 100.0f);
			YGNodeStyleSetHeight(baseNode, lineHeight+4);
			YGNodeStyleSetAlignItems(baseNode, YGAlignCenter);
			YGNodeStyleSetFlexDirection(baseNode, YGFlexDirectionRow);
			YGNodeStyleSetMargin(baseNode, YGEdgeAll, 2);
			context.GetLayoutEngine().InsertChildToStackTop(baseNode);
			context.GetLayoutEngine().PushNode(baseNode);

			auto mainCtrl = context.GetLayoutEngine().NewImbuedNode(interactable);
			YGNodeStyleSetFlexGrow(mainCtrl->YGNode(), 1.0f);
			YGNodeStyleSetHeightPercent(mainCtrl->YGNode(), 100.f);
			YGNodeStyleSetMargin(mainCtrl->YGNode(), YGEdgeAll, 2);
			mainCtrl->_nodeAttachments._drawDelegate = [nameStr=name.AsString(), modelValue, interactable](DrawContext& draw, Rect frame, Rect content) {
				if (auto str = modelValue.TryQueryNonLayoutAsString())
					CommonWidgets::Styler{}.LeftRight(draw, frame, interactable, nameStr, *str);
			};
			context.GetLayoutEngine().InsertChildToStackTop(mainCtrl->YGNode());
			return mainCtrl;
		}

		template<typename Type>
			void WriteHalfDoubleTemplate(IWidgetsLayoutContext& context, StringSection<> name, const V<Type>& modelValue, const V<Type>& minValue, const V<Type>& maxValue)
		{
			uint64_t interactable = (modelValue._type == MinimalBindingValueType::Constant) ? context.GetGuidStack().MakeGuid(name) : modelValue._id;
			
			auto enabledByHierarchy = context.EnabledByHierarchy();
			if (enabledByHierarchy == HierarchicalEnabledState::EnableChildren || context.GetBindingEngine().IsEnabled(interactable)) {
			
				auto mainCtrl = BeginSharedLeftRightCtrl(context, name, modelValue, interactable);
				mainCtrl->_nodeAttachments._ioDelegate = [modelValue, minValue, maxValue](auto& ioContext, Rect frame, Rect content) {
					bool leftSide = ioContext.GetEvent()._mousePosition[0] < (frame._topLeft[0]+frame._bottomRight[0])/2;
					if (ioContext.GetEvent().IsRelease_LButton()) {
						auto currentValue = modelValue.QueryNonLayout().value();
						auto newValue = currentValue;
						if (leftSide) 	newValue = std::max(minValue.QueryNonLayout().value(), currentValue/2);
						else 			newValue = std::min(maxValue.QueryNonLayout().value(), currentValue*2);
						if (newValue != currentValue) {
							modelValue.Set(newValue);
							return PlatformRig::ProcessInputResult::Consumed;
						}
					}
					return PlatformRig::ProcessInputResult::Passthrough;
				};

				if (enabledByHierarchy == HierarchicalEnabledState::NoImpact)
					DeactivateButton(context, interactable);
				context.GetLayoutEngine().PopNode();

			} else {
				DisabledStateButton(context, interactable, name, enabledByHierarchy);
			}
		}

		void WriteHalfDoubleInt(IWidgetsLayoutContext& context, StringSection<> name, const V<int64_t>& modelValue, const V<int64_t>& min, const V<int64_t>& max) override { WriteHalfDoubleTemplate(context, name, modelValue, min, max); }
		void WriteHalfDoubleFloat(IWidgetsLayoutContext& context, StringSection<> name, const V<float>& modelValue, const V<float>& min, const V<float>& max) override { WriteHalfDoubleTemplate(context, name, modelValue, min, max); }

		template<typename Type>
			void WriteDecrementIncrementTemplate(IWidgetsLayoutContext& context, StringSection<> name, const V<Type>& modelValue, const V<Type>& minValue, const V<Type>& maxValue)
		{
			uint64_t interactable = (modelValue._type == MinimalBindingValueType::Constant) ? context.GetGuidStack().MakeGuid(name) : modelValue._id;
			
			auto enabledByHierarchy = context.EnabledByHierarchy();
			if (enabledByHierarchy == HierarchicalEnabledState::EnableChildren || context.GetBindingEngine().IsEnabled(interactable)) {
			
				auto mainCtrl = BeginSharedLeftRightCtrl(context, name, modelValue, interactable);
				mainCtrl->_nodeAttachments._ioDelegate = [modelValue, minValue, maxValue](auto& ioContext, Rect frame, Rect content) {
					bool leftSide = ioContext.GetEvent()._mousePosition[0] < (frame._topLeft[0]+frame._bottomRight[0])/2;
					if (ioContext.GetEvent().IsRelease_LButton()) {
						auto currentValue = modelValue.QueryNonLayout().value();
						auto newValue = currentValue;
						if (leftSide) 	newValue = std::max(minValue.QueryNonLayout().value(), currentValue-1);
						else 			newValue = std::min(maxValue.QueryNonLayout().value(), currentValue+1);
						if (newValue != currentValue) {
							modelValue.Set(newValue);
							return PlatformRig::ProcessInputResult::Consumed;
						}
					}
					return PlatformRig::ProcessInputResult::Passthrough;
				};

				if (enabledByHierarchy == HierarchicalEnabledState::NoImpact)
					DeactivateButton(context, interactable);
				context.GetLayoutEngine().PopNode();

			} else {
				DisabledStateButton(context, interactable, name, enabledByHierarchy);
			}
		}

		void WriteDecrementIncrementInt(IWidgetsLayoutContext& context, StringSection<> name, const V<int64_t>& modelValue, const V<int64_t>& min, const V<int64_t>& max) override { WriteDecrementIncrementTemplate(context, name, modelValue, min, max); }
		void WriteDecrementIncrementFloat(IWidgetsLayoutContext& context, StringSection<> name, const V<float>& modelValue, const V<float>& min, const V<float>& max) override { WriteDecrementIncrementTemplate(context, name, modelValue, min, max); }

		ImbuedNode* HorizontalControlLabel(IWidgetsLayoutContext& context, StringSection<> name)
		{
			auto labelNode = context.GetLayoutEngine().NewImbuedNode(0);
			context.GetLayoutEngine().InsertChildToStackTop(*labelNode);

			auto* defaultFonts = RenderOverlays::CommonWidgets::Styler::TryGetDefaultFontsBox();
			assert(defaultFonts);
			auto maxWidth = RenderOverlays::StringWidth(*defaultFonts->_buttonFont, name);
			YGNodeStyleSetWidth(*labelNode, maxWidth);
			YGNodeStyleSetHeight(*labelNode, defaultFonts->_buttonFont->GetFontProperties()._lineHeight);
			
			// We can't grow, but we can shrink -- our "width" property is the length of the entire string, and if it's shrunk,
			// we'll adjust the string with a ellipsis
			YGNodeStyleSetFlexGrow(*labelNode, 0.f);
			YGNodeStyleSetFlexShrink(*labelNode, 1.f);

			struct AttachedData
			{
				std::string _originalLabel;
				unsigned _cachedWidth = ~0u;
				std::string _fitLabel;
			};
			auto attachedData = std::make_shared<AttachedData>();
			attachedData->_originalLabel = name.AsString();
			attachedData->_fitLabel = attachedData->_originalLabel;
			attachedData->_cachedWidth = (unsigned)maxWidth;

			labelNode->_nodeAttachments._drawDelegate = [attachedData, font=defaultFonts->_buttonFont](DrawContext& draw, Rect frame, Rect content) {
				// We don't get a notification after layout is finished -- so typically on the first render we may have to adjust
				// our string to fit
				if (content.Width() != attachedData->_cachedWidth) {
					attachedData->_cachedWidth = content.Width();
					char buffer[MaxPath];
					auto fitWidth = StringEllipsisDoubleEnded(buffer, dimof(buffer), *font, MakeStringSection(attachedData->_originalLabel), MakeStringSection("/\\"), (float)content.Width());
					attachedData->_fitLabel = buffer;
				}
				DrawText().Font(*font).Alignment(TextAlignment::Right).Draw(draw.GetContext(), content, attachedData->_fitLabel);
			};

			return labelNode;
		}

		void WriteHorizontalCombo(IWidgetsLayoutContext& context, StringSection<> name, const V<int64_t>& modelValue, IteratorRange<const std::pair<int64_t, const char*>*> options) override
		{
			uint64_t interactable = (modelValue._type == MinimalBindingValueType::Constant) ? context.GetGuidStack().MakeGuid(name) : modelValue._id;
			const auto lineHeight = baseLineHeight+4;

			auto enabledByHierarchy = context.EnabledByHierarchy();
			if (enabledByHierarchy == HierarchicalEnabledState::EnableChildren || context.GetBindingEngine().IsEnabled(interactable)) {

				auto baseNode = context.GetLayoutEngine().NewNode();
				YGNodeStyleSetWidthPercent(baseNode, 100.0f);
				YGNodeStyleSetHeight(baseNode, lineHeight+4);
				YGNodeStyleSetAlignItems(baseNode, YGAlignCenter);
				YGNodeStyleSetFlexDirection(baseNode, YGFlexDirectionRow);
				YGNodeStyleSetMargin(baseNode, YGEdgeAll, 2);
				context.GetLayoutEngine().InsertChildToStackTop(baseNode);
				context.GetLayoutEngine().PushNode(baseNode);

				HorizontalControlLabel(context, name);

				for (unsigned c=0; c<options.size(); ++c) {
					auto node = context.GetLayoutEngine().NewImbuedNode(interactable+1+c);
					YGNodeStyleSetFlexGrow(node->YGNode(), 1.0f);
					YGNodeStyleSetHeightPercent(node->YGNode(), 100.f);
					context.GetLayoutEngine().InsertChildToStackTop(node->YGNode());
					Corner::BitField corners = 0;
					if (c == 0) corners |= Corner::TopLeft|Corner::BottomLeft;
					if ((c+1) == options.size()) corners |= Corner::TopRight|Corner::BottomRight;
					node->_nodeAttachments._drawDelegate = [nameStr=std::string{options[c].second}, corners, value=options[c].first, modelValue](DrawContext& draw, Rect frame, Rect content) {
						bool selected = modelValue.QueryNonLayout().value() == value;
						OutlineRoundedRectangle(draw.GetContext(), frame, selected ? ColorB{96, 96, 96} : ColorB{64, 64, 64}, 1.f, 0.4f, corners);
						DrawText().Alignment(TextAlignment::Center).Draw(draw.GetContext(), content, nameStr);
					};
					node->_nodeAttachments._ioDelegate = [modelValue, value=options[c].first](auto& ioContext, Rect, Rect) {
						if (ioContext.GetEvent().IsRelease_LButton()) {
							modelValue.Set(value);
							return PlatformRig::ProcessInputResult::Consumed;
						}
						return PlatformRig::ProcessInputResult::Consumed;
					};
				}

				if (enabledByHierarchy == HierarchicalEnabledState::NoImpact)
					DeactivateButton(context, interactable);
				context.GetLayoutEngine().PopNode();

			} else {
				DisabledStateButton(context, interactable, name, enabledByHierarchy);
			}
		}

		void BeginCheckboxControl_Internal(IWidgetsLayoutContext& context, StringSection<> name, const V<bool>& modelValue, uint64_t interactable)
		{
			auto baseNode = context.GetLayoutEngine().InsertAndPushNewNode();
			YGNodeStyleSetAlignItems(baseNode, YGAlignCenter);
			YGNodeStyleSetJustifyContent(baseNode, YGJustifySpaceBetween);
			YGNodeStyleSetFlexDirection(baseNode, YGFlexDirectionRow);
			YGNodeStyleSetMargin(baseNode, YGEdgeAll, 2);
			YGNodeStyleSetFlexGrow(baseNode, 1.f);

			auto* label = HorizontalControlLabel(context, name);
			YGNodeStyleSetMargin(*label, YGEdgeRight, 8);

			auto stateBox = context.GetLayoutEngine().InsertNewImbuedNode(interactable);
			YGNodeStyleSetWidth(stateBox->YGNode(), 16);
			YGNodeStyleSetHeight(stateBox->YGNode(), 16);
			stateBox->_nodeAttachments._drawDelegate = [modelValue](DrawContext& draw, Rect frame, Rect content) {
				CommonWidgets::Styler{}.CheckBox(draw, content, modelValue.QueryNonLayout().value());
			};
			stateBox->_nodeAttachments._ioDelegate = [modelValue](auto& ioContext, Rect, Rect) {
				if (ioContext.GetEvent().IsRelease_LButton()) {
					modelValue.Set(!modelValue.QueryNonLayout().value());
					return PlatformRig::ProcessInputResult::Consumed;
				}
				return PlatformRig::ProcessInputResult::Consumed;
			};
			context.GetLayoutEngine().PopNode();
		}

		void WriteCheckbox(IWidgetsLayoutContext& context, StringSection<> name, const V<bool>& modelValue) override
		{
			uint64_t interactable = (modelValue._type == MinimalBindingValueType::Constant) ? context.GetGuidStack().MakeGuid(name) : modelValue._id;

			auto enabledByHierarchy = context.EnabledByHierarchy();
			if (enabledByHierarchy == HierarchicalEnabledState::EnableChildren || context.GetBindingEngine().IsEnabled(interactable)) {
				bool hasDisableButton = enabledByHierarchy == HierarchicalEnabledState::NoImpact;
				if (hasDisableButton) {
					auto disablerWrapping = context.GetLayoutEngine().InsertAndPushNewNode();
					YGNodeStyleSetAlignItems(disablerWrapping, YGAlignCenter);
					YGNodeStyleSetJustifyContent(disablerWrapping, YGJustifyFlexEnd);
					YGNodeStyleSetFlexDirection(disablerWrapping, YGFlexDirectionRow);
				}

				BeginCheckboxControl_Internal(context, name, modelValue, interactable);

				if (hasDisableButton) {
					DeactivateButton(context, interactable);
					context.GetLayoutEngine().PopNode();
				}
			} else {
				DisabledStateButton(context, interactable, name, enabledByHierarchy);
			}
		}

		static constexpr unsigned baseLineHeight = 20;

		void DeactivateButton(IWidgetsLayoutContext& context, uint64_t ctrlGuid)
		{
			auto newNode = context.GetLayoutEngine().InsertNewImbuedNode(ctrlGuid+32);
			YGNodeStyleSetWidth(newNode->YGNode(), 12);
			YGNodeStyleSetHeight(newNode->YGNode(), 12);
			YGNodeStyleSetMargin(newNode->YGNode(), YGEdgeAll, 2);
			YGNodeStyleSetFlexGrow(newNode->YGNode(), 0.f);
			YGNodeStyleSetFlexShrink(newNode->YGNode(), 0.f);

			newNode->_nodeAttachments._drawDelegate = [](DrawContext& draw, Rect frame, Rect content) {
				CommonWidgets::Styler{}.XToggleButton(draw, frame);
			};

			newNode->_nodeAttachments._ioDelegate = [ctrlGuid, state=context.GetBindingEnginePtr()](auto& ioContext, Rect, Rect) {
				if (ioContext.GetEvent().IsRelease_LButton()) {
					state->ToggleEnable(ctrlGuid);
					state->InvalidateModel();
					state->InvalidateLayout();
					return PlatformRig::ProcessInputResult::Consumed;
				}
				return PlatformRig::ProcessInputResult::Consumed;
			};
		}

		void DisabledStateButton(IWidgetsLayoutContext& context, uint64_t interactable, StringSection<> name, HierarchicalEnabledState hierarchyState)
		{
			const auto lineHeight = baseLineHeight+4;
			auto baseNode = context.GetLayoutEngine().InsertNewImbuedNode(interactable);
			YGNodeStyleSetMargin(baseNode->YGNode(), YGEdgeAll, 2);
			YGNodeStyleSetFlexGrow(baseNode->YGNode(), 1.0f);
			YGNodeStyleSetHeight(baseNode->YGNode(), lineHeight+4);

			if (hierarchyState == HierarchicalEnabledState::NoImpact) {
				baseNode->_nodeAttachments._drawDelegate = [nameStr=name.AsString()](DrawContext& draw, Rect frame, Rect content) {
					CommonWidgets::Styler{}.DisabledStateControl(draw, frame, nameStr);
				};

				baseNode->_nodeAttachments._ioDelegate = [interactable, state=context.GetBindingEnginePtr()](auto& ioContext, Rect, Rect) {
					if (ioContext.GetEvent().IsRelease_LButton()) {
						state->ToggleEnable(interactable);
						state->InvalidateModel();
						state->InvalidateLayout();
						return PlatformRig::ProcessInputResult::Consumed;
					}
					return PlatformRig::ProcessInputResult::Consumed;
				};
			} else {
				baseNode->_nodeAttachments._drawDelegate = [nameStr=name.AsString()](DrawContext& draw, Rect frame, Rect content) {
					DrawText().Color({0x5f, 0x5f, 0x5f}).Alignment(TextAlignment::Center).Draw(draw.GetContext(), content, nameStr);
				};
			}
		}

		template<typename Type>
			void WriteBoundedTemplate(IWidgetsLayoutContext& context, StringSection<> name, const V<Type>& modelValue, const V<Type>& leftSideValue, const V<Type>& rightSideValue)
		{
			uint64_t interactable = (modelValue._type == MinimalBindingValueType::Constant) ? context.GetGuidStack().MakeGuid(name) : modelValue._id;
			const auto lineHeight = baseLineHeight+4;
			
			auto enabledByHierarchy = context.EnabledByHierarchy();
			if (enabledByHierarchy == HierarchicalEnabledState::EnableChildren || context.GetBindingEngine().IsEnabled(interactable)) {

				auto baseNode = context.GetLayoutEngine().NewNode();
				YGNodeStyleSetWidthPercent(baseNode, 100.0f);
				YGNodeStyleSetHeight(baseNode, lineHeight+4);
				YGNodeStyleSetAlignItems(baseNode, YGAlignCenter);
				YGNodeStyleSetFlexDirection(baseNode, YGFlexDirectionRow);
				YGNodeStyleSetMargin(baseNode, YGEdgeAll, 2);
				context.GetLayoutEngine().InsertChildToStackTop(baseNode);
				context.GetLayoutEngine().PushNode(baseNode);

				auto sliderNode = context.GetLayoutEngine().NewImbuedNode(interactable);
				YGNodeStyleSetFlexGrow(sliderNode->YGNode(), 1.0f);
				YGNodeStyleSetHeightPercent(sliderNode->YGNode(), 100.f);
				YGNodeStyleSetMargin(sliderNode->YGNode(), YGEdgeAll, 2);
				sliderNode->_nodeAttachments._drawDelegate = [nameStr=name.AsString(), interactable, leftSideValue, rightSideValue, modelValue](DrawContext& draw, Rect frame, Rect content) {
					CommonWidgets::Styler{}.Bounded(draw, frame, interactable, nameStr, modelValue.QueryNonLayout().value(), leftSideValue.QueryNonLayout().value(), rightSideValue.QueryNonLayout().value());
				};
				sliderNode->_nodeAttachments._ioDelegate = [interactable, leftSideValue, rightSideValue, modelValue](IOContext& ioContext, Rect frame, Rect content) {
					auto* hoverings = ioContext.GetInputContext().GetService<RenderOverlays::CommonWidgets::HoveringLayer>();
					auto* interfaceState = ioContext.GetInputContext().GetService<RenderOverlays::DebuggingDisplay::InterfaceState>();
					if (!hoverings || !interfaceState) return PlatformRig::ProcessInputResult::Passthrough;

					auto& evnt = ioContext.GetEvent();
					if (hoverings->_hoveringCtrl) {
						Int2 mp { evnt._mousePosition._x, evnt._mousePosition._y };
						if ((evnt._mouseButtonsTransition != 0) && interfaceState->GetCapture()._hotArea._id == interactable && !Contains(interfaceState->GetCapture()._hotArea._rect, mp)) {
							modelValue.TrySetFromString(hoverings->_textEntry._currentLine);
							interfaceState->EndCapturing();
							hoverings->_hoveringCtrl = 0;
							return PlatformRig::ProcessInputResult::Consumed;
						} 

						if (evnt.IsPress(enter)) {
							modelValue.TrySetFromString(hoverings->_textEntry._currentLine);
							interfaceState->EndCapturing();
							hoverings->_hoveringCtrl = 0;
						} else if (evnt.IsPress(escape)) {
							interfaceState->EndCapturing();
							hoverings->_hoveringCtrl = 0;
						} else {
							hoverings->_textEntry.ProcessInput(*interfaceState, evnt);
						}
					} else {
						if (evnt.IsPress_LButton()) {
							interfaceState->BeginCapturing(interfaceState->TopMostHotArea());
						} else if (interfaceState->GetCapture()._hotArea._id == interactable) {
							const unsigned driftThreshold = 4;
							if (interfaceState->GetCapture()._driftDuringCapture[0] < driftThreshold && interfaceState->GetCapture()._driftDuringCapture[1] < driftThreshold) {
								// inside drift threshold
								if (evnt.IsRelease_LButton()) {
									hoverings->_hoveringCtrl = interactable;
									hoverings->_textEntry.Reset(modelValue.TryQueryNonLayoutAsString().value());
								}
							} else {
								// outside of drift threshold
								if (evnt.IsHeld_LButton()) {
									// dragging while captured
									float alpha = (evnt._mousePosition[0] - interfaceState->TopMostHotArea()._rect._topLeft[0]) / float(interfaceState->TopMostHotArea()._rect._bottomRight[0] - interfaceState->TopMostHotArea()._rect._topLeft[0]);
									alpha = std::max(0.f, std::min(1.f, alpha));
									auto newValue = LinearInterpolate(leftSideValue.QueryNonLayout().value(), rightSideValue.QueryNonLayout().value(), alpha);
									modelValue.Set(newValue);
								}
								if (evnt.IsRelease_LButton())
									interfaceState->EndCapturing();
							}
						}
					}
					return PlatformRig::ProcessInputResult::Consumed;
				};
				context.GetLayoutEngine().InsertChildToStackTop(sliderNode->YGNode());

				if (enabledByHierarchy == HierarchicalEnabledState::NoImpact)
					DeactivateButton(context, interactable);
				context.GetLayoutEngine().PopNode();

			} else {

				DisabledStateButton(context, interactable, name, enabledByHierarchy);
			
			}
		}

		void WriteBoundedInt(IWidgetsLayoutContext& context, StringSection<> name, const V<int64_t>& modelValue, const V<int64_t>& leftSideValue, const V<int64_t>& rightSideValue) override { WriteBoundedTemplate(context, name, modelValue, leftSideValue, rightSideValue); }
		void WriteBoundedFloat(IWidgetsLayoutContext& context, StringSection<> name, const V<float>& modelValue, const V<float>& leftSideValue, const V<float>& rightSideValue) override { WriteBoundedTemplate(context, name, modelValue, leftSideValue, rightSideValue); }

		bool BeginCollapsingContainer(IWidgetsLayoutContext& context, StringSection<> name) override
		{
			uint64_t containerGuid = context.GetGuidStack().MakeGuid(name, "##collapsingcontainer");
			context.GetGuidStack().push(containerGuid);
			context.PushHierarchicalEnabledState(0);
			bool isOpen = context.GetBindingEngine().IsEnabled(containerGuid);

			auto outerNode = context.GetLayoutEngine().NewNode();
			YGNodeStyleSetPadding(outerNode, YGEdgeAll, 0);       // zero padding because the headerContainer and contentContainers have their own padding
			YGNodeStyleSetMargin(outerNode, YGEdgeAll, 0);
			context.GetLayoutEngine().InsertChildToStackTop(outerNode);

			{
				const auto headerHeight = 24u;
				auto headerContainer = context.GetLayoutEngine().NewImbuedNode(containerGuid);
				YGNodeStyleSetMargin(headerContainer->YGNode(), YGEdgeAll, 0);
				YGNodeStyleSetWidthPercent(headerContainer->YGNode(), 100.0f);
				YGNodeStyleSetHeight(headerContainer->YGNode(), headerHeight);
				YGNodeStyleSetAlignItems(headerContainer->YGNode(), YGAlignCenter);
				YGNodeStyleSetFlexDirection(headerContainer->YGNode(), YGFlexDirectionRow);
				YGNodeInsertChild(outerNode, headerContainer->YGNode(), YGNodeGetChildCount(outerNode));
				
				headerContainer->_nodeAttachments._drawDelegate = [nameStr=name.AsString(), isOpen](DrawContext& draw, Rect frame, Rect content) {
					CommonWidgets::Styler{}.SectionHeader(draw, content, nameStr, isOpen);
				};

				headerContainer->_nodeAttachments._ioDelegate = [containerGuid, state=context.GetBindingEnginePtr()](auto& ioContext, Rect, Rect) {
					if (ioContext.GetEvent().IsRelease_LButton()) {
						state->ToggleEnable(containerGuid);
						state->InvalidateModel();
						state->InvalidateLayout();
						return PlatformRig::ProcessInputResult::Consumed;
					}
					return PlatformRig::ProcessInputResult::Passthrough;
				};
			}

			auto contentContainer = context.GetLayoutEngine().NewNode();
			if (isOpen)
				YGNodeStyleSetMargin(contentContainer, YGEdgeAll, 2);
			YGNodeInsertChild(outerNode, contentContainer, YGNodeGetChildCount(outerNode));

			context.GetLayoutEngine().PushNode(contentContainer);       // upcoming nodes will go into the content container
			return isOpen;
		}

		void BeginContainer(IWidgetsLayoutContext& context) override
		{
			uint64_t containerGuid = context.GetGuidStack().MakeGuid("##container");
			context.GetGuidStack().push(containerGuid);

			auto contentContainer = context.GetLayoutEngine().NewImbuedNode(containerGuid);
			YGNodeStyleSetMargin(contentContainer->YGNode(), YGEdgeAll, 8);
			YGNodeStyleSetPadding(contentContainer->YGNode(), YGEdgeAll, 2);
			context.GetLayoutEngine().InsertChildToStackTop(contentContainer->YGNode());
			context.GetLayoutEngine().PushNode(contentContainer->YGNode());

			contentContainer->_nodeAttachments._drawDelegate = [](DrawContext& draw, Rect frame, Rect content) {
				CommonWidgets::Styler{}.RectangleContainer(draw, frame);
			};

			DisabledStateButton(context, containerGuid, "Enable", context.EnabledByHierarchy());
			context.PushHierarchicalEnabledState(containerGuid);
		}

		void EndContainer(IWidgetsLayoutContext& context) override
		{
			assert(!context.GetLayoutEngine().GuidStack().empty());
			context.GetLayoutEngine().GuidStack().pop();
			context.GetLayoutEngine().PopNode();
			context.PopHierarchicalEnabledState();
		}

		struct StaticData
		{
			std::string _font;

			StaticData() = default;
			template<typename Formatter>
				StaticData(Formatter& fmttr)
			{
				uint64_t keyname;
				while (fmttr.TryKeyedItem(keyname)) {
					switch (keyname) {
					case "Font"_h: _font = RequireStringValue(fmttr).AsString(); break;
					default: SkipValueOrElement(fmttr);
					}
				}
			}
		};
		
		const StaticData* _staticData;
		std::shared_ptr<Font> _font;

		CommonWidgetsStyler()
		{
			_staticData = &EntityInterface::MountedData<StaticData>::LoadOrDefault("cfg/displays/commonwidgets"_initializer);
			_font = ActualizeFont(_staticData->_font);
		}

	private:
		std::shared_ptr<Font> ActualizeFont(StringSection<> name)
		{
			::Assets::PtrToMarkerPtr<Font> futureFont;
			if (name.IsEmpty()) {
				futureFont = MakeFont("Petra", 16);
			} else {
				futureFont = MakeFont(name);
			}
			return futureFont->Actualize();		// stall
		}
	};

	

	static YGNodeRef BeginRoot(IWidgetsLayoutContext& context, Coord2 containerSize)
	{
		auto windowNode = context.GetLayoutEngine().NewNode();
		context.GetLayoutEngine().PushRoot(windowNode, containerSize);
		return windowNode;
	}

	static void EndRoot(IWidgetsLayoutContext& context)
	{
		context.GetLayoutEngine().PopNode();
	}

	std::shared_ptr<ICommonWidgetsStyler> CreateCommonWidgetsStyler()
	{
		return std::make_shared<CommonWidgetsStyler>();
	}

	class TweakerGroup : public IWidget
	{
	public:
		LayedOutWidgets _layedOutWidgets;
		CommonWidgets::HoveringLayer _hoverings;
		std::shared_ptr<MinimalBindingEngine> _bindingEngine;
		WriteToLayoutFormatter _layoutFn;
		unsigned _lastBuiltLayoutValidationIndex = ~0u;
		UInt2 _lastContainerSize { 0, 0 };
		Float3x3 _lastTransform = Identity<Float3x3>();

		void    Render(IOverlayContext& context, Layout& layout, Interactables& interactables, InterfaceState& interfaceState) override
		{
			Rect container = layout.GetMaximumSize();
			container._topLeft += Coord2{layout._paddingInternalBorder, layout._paddingInternalBorder};
			container._bottomRight -= Coord2{layout._paddingInternalBorder, layout._paddingInternalBorder};

			// rebuild the layout now if it's invalidated
			UInt2 containerSize { container.Width(), container.Height() };
			if (_bindingEngine->GetLayoutValidationIndex() != _lastBuiltLayoutValidationIndex || _lastContainerSize != containerSize) {
				WidgetsLayoutContext formatter { _bindingEngine };
				BeginRoot(formatter, containerSize);
				_layoutFn(formatter);
				EndRoot(formatter);

				_layedOutWidgets = formatter.BuildLayedOutWidgets();
				_lastBuiltLayoutValidationIndex = _bindingEngine->GetLayoutValidationIndex();
				_lastContainerSize = containerSize;
			}

			{
				_lastTransform = Float3x3 {
					1.f, 0.f, container._topLeft[0],
					0.f, 1.f, container._topLeft[1],
					0.f, 0.f, 1.f
				};
				DrawContext drawContext{context, interactables, interfaceState, _hoverings};
				_layedOutWidgets.Draw(drawContext, _lastTransform);
			}
		}

		ProcessInputResult    ProcessInput(InterfaceState& interfaceState, const OSServices::InputSnapshot& input) override
		{
			PlatformRig::InputContext inputContext;
			inputContext.AttachService2(_hoverings);
			inputContext.AttachService2(interfaceState);
			IOContext ioContext { inputContext, input };
			return _layedOutWidgets.ProcessInput(ioContext, _lastTransform);
		}

		TweakerGroup(std::shared_ptr<MinimalBindingEngine> bindingEngine, WriteToLayoutFormatter&& layoutFn)
		: _bindingEngine(std::move(bindingEngine)), _layoutFn(layoutFn) {}
	};

	std::shared_ptr<RenderOverlays::DebuggingDisplay::IWidget> CreateWidgetGroup(std::shared_ptr<MinimalBindingEngine> doc, WriteToLayoutFormatter&& layoutFn)
	{
		return std::make_shared<TweakerGroup>(doc, std::move(layoutFn));
	}

	IWidgetsLayoutContext::~IWidgetsLayoutContext() = default;
}

