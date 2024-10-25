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
#include "../../Foreign/yoga/yoga/Yoga.h"
#include <vector>
#include <set>
#include <stack>

using namespace OSServices::Literals;
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

	namespace Internal
	{
		struct LabelFittingHelper
		{
			std::string _originalLabel;
			unsigned _cachedWidth = ~0u;
			std::string _fitLabel;
			float _fitWidth = 0.f;

			void Fit(int width, Font& fnt)
			{
				assert(width > 0);
				if (width != _cachedWidth) {
					_cachedWidth = width;
					VLA(char, buffer, _originalLabel.size()+1);
					_fitWidth = StringEllipsisDoubleEnded(buffer, _originalLabel.size()+1, fnt, MakeStringSection(_originalLabel), MakeStringSectionLiteral("/\\"), (float)width);
					_fitLabel = buffer;
				}
			}

			LabelFittingHelper(std::string&& originalLabel) : _originalLabel(std::move(originalLabel)) {}
		};
	}

	class CommonWidgetsStyler : public ICommonWidgetsStyler
	{
	public:
		ImbuedNode* BeginSharedLeftRightCtrl(IWidgetsLayoutContext& context, StringSection<> name, const V<uint64_t>& modelValue, uint64_t interactable)
		{
			auto mainCtrl = context.GetLayoutEngine().InsertNewImbuedNode(interactable);
			YGNodeStyleSetFlexGrow(*mainCtrl, 1.0f);		// fill all available space
			YGNodeStyleSetHeight(*mainCtrl, baseLineHeight+2*_staticData->_verticalPadding);
			ElementMargins(*mainCtrl);
			mainCtrl->_nodeAttachments._drawDelegate = [nameStr=name.AsString(), modelValue, interactable](DrawContext& draw, Rect frame, Rect content) {
				if (auto str = modelValue.TryQueryNonLayoutAsString())
					CommonWidgets::Styler::Get().LeftRight(draw, frame, interactable, nameStr, *str);
			};
			return mainCtrl;
		}

		void ElementMargins(YGNode* node)
		{
			YGNodeStyleSetMargin(node, YGEdgeHorizontal, _staticData->_elementHorizontalMargin);
			YGNodeStyleSetMargin(node, YGEdgeVertical, _staticData->_elementVerticalMargin);
		}

		template<typename Type>
			void WriteHalfDoubleTemplate(IWidgetsLayoutContext& context, StringSection<> name, const V<Type>& modelValue, const V<Type>& minValue, const V<Type>& maxValue)
		{
			uint64_t interactable = (modelValue._type == MinimalBindingValueType::Constant) ? context.GetGuidStack().MakeGuid(name) : modelValue._id;
			
			if (BeginDisableableWidget(context, interactable)) {
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
			}
			EndDisableableWidget(context, name, interactable);
		}

		void WriteHalfDoubleInt(IWidgetsLayoutContext& context, StringSection<> name, const V<int64_t>& modelValue, const V<int64_t>& min, const V<int64_t>& max) override { WriteHalfDoubleTemplate(context, name, modelValue, min, max); }
		void WriteHalfDoubleFloat(IWidgetsLayoutContext& context, StringSection<> name, const V<float>& modelValue, const V<float>& min, const V<float>& max) override { WriteHalfDoubleTemplate(context, name, modelValue, min, max); }

		template<typename Type>
			void WriteDecrementIncrementTemplate(IWidgetsLayoutContext& context, StringSection<> name, const V<Type>& modelValue, const V<Type>& minValue, const V<Type>& maxValue)
		{
			uint64_t interactable = (modelValue._type == MinimalBindingValueType::Constant) ? context.GetGuidStack().MakeGuid(name) : modelValue._id;
			
			if (BeginDisableableWidget(context, interactable)) {
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
			}
			EndDisableableWidget(context, name, interactable);
		}

		void WriteDecrementIncrementInt(IWidgetsLayoutContext& context, StringSection<> name, const V<int64_t>& modelValue, const V<int64_t>& min, const V<int64_t>& max) override { WriteDecrementIncrementTemplate(context, name, modelValue, min, max); }
		void WriteDecrementIncrementFloat(IWidgetsLayoutContext& context, StringSection<> name, const V<float>& modelValue, const V<float>& min, const V<float>& max) override { WriteDecrementIncrementTemplate(context, name, modelValue, min, max); }

		ImbuedNode* HorizontalControlLabel(IWidgetsLayoutContext& context, StringSection<> name)
		{
			auto labelNode = context.GetLayoutEngine().NewImbuedNode(0);
			context.GetLayoutEngine().InsertChildToStackTop(*labelNode);

			auto maxWidth = RenderOverlays::StringWidth(*_font, name);
			YGNodeStyleSetWidth(*labelNode, maxWidth);
			YGNodeStyleSetHeight(*labelNode, _font->GetFontProperties()._lineHeight);
			
			// We can't grow, but we can shrink -- our "width" property is the length of the entire string, and if it's shrunk,
			// we'll adjust the string with a ellipsis
			YGNodeStyleSetFlexGrow(*labelNode, 0.f);
			YGNodeStyleSetFlexShrink(*labelNode, 1.f);
			YGNodeStyleSetMargin(*labelNode, YGEdgeRight, 8);

			auto attachedData = std::make_shared<Internal::LabelFittingHelper>(name.AsString());
			labelNode->_nodeAttachments._drawDelegate = [attachedData, font=_font](DrawContext& draw, Rect frame, Rect content) {
				// We don't get a notification after layout is finished -- so typically on the first render we may have to adjust
				// our string to fit
				attachedData->Fit(content.Width(), *font);
				DrawText().Font(*font).Alignment(TextAlignment::Right).Draw(draw.GetContext(), content, attachedData->_fitLabel);
			};

			return labelNode;
		}

		void WriteHorizontalCombo(IWidgetsLayoutContext& context, StringSection<> name, const V<int64_t>& modelValue, IteratorRange<const std::pair<int64_t, const char*>*> options) override
		{
			uint64_t interactable = (modelValue._type == MinimalBindingValueType::Constant) ? context.GetGuidStack().MakeGuid(name) : modelValue._id;

			if (BeginDisableableWidget(context, interactable)) {

				auto baseNode = context.GetLayoutEngine().InsertAndPushNewNode();
				YGNodeStyleSetHeight(baseNode, baseLineHeight+2*_staticData->_verticalPadding);
				YGNodeStyleSetAlignItems(baseNode, YGAlignCenter);
				YGNodeStyleSetFlexDirection(baseNode, YGFlexDirectionRow);
				ElementMargins(baseNode);
				YGNodeStyleSetFlexGrow(baseNode, 1.f);

				HorizontalControlLabel(context, name);

				for (unsigned c=0; c<options.size(); ++c) {
					auto node = context.GetLayoutEngine().NewImbuedNode(interactable+1+c);
					YGNodeStyleSetFlexGrow(node->YGNode(), 1.0f);
					YGNodeStyleSetHeightPercent(node->YGNode(), 100.f);
					YGNodeStyleSetPadding(node->YGNode(), YGEdgeHorizontal, 4);
					context.GetLayoutEngine().InsertChildToStackTop(node->YGNode());
					Corner::BitField corners = 0;
					if (c == 0) corners |= Corner::TopLeft|Corner::BottomLeft;
					if ((c+1) == options.size()) corners |= Corner::TopRight|Corner::BottomRight;

					auto labelFittingHelper = std::make_shared<Internal::LabelFittingHelper>(options[c].second);
					node->_nodeAttachments._drawDelegate = [labelFittingHelper, corners, value=options[c].first, modelValue, font=_font](DrawContext& draw, Rect frame, Rect content) {
						bool selected = modelValue.QueryNonLayout().value() == value;
						OutlineRoundedRectangle(draw.GetContext(), frame, selected ? ColorB{96, 96, 96} : ColorB{64, 64, 64}, 1.f, 0.4f, 32.f, corners);
						labelFittingHelper->Fit(content.Width(), *font);
						DrawText().Alignment(TextAlignment::Center).Font(*font).Draw(draw.GetContext(), content, labelFittingHelper->_fitLabel);
					};
					node->_nodeAttachments._ioDelegate = [modelValue, value=options[c].first](auto& ioContext, Rect, Rect) {
						if (ioContext.GetEvent().IsRelease_LButton()) {
							modelValue.Set(value);
							return PlatformRig::ProcessInputResult::Consumed;
						}
						return PlatformRig::ProcessInputResult::Consumed;
					};
				}

				context.GetLayoutEngine().PopNode();
			}
			EndDisableableWidget(context, name, interactable);
		}

		void BeginCheckboxControl_Internal(IWidgetsLayoutContext& context, StringSection<> name, const V<bool>& modelValue, uint64_t interactable)
		{
			auto baseNode = context.GetLayoutEngine().InsertAndPushNewNode();
			YGNodeStyleSetAlignItems(baseNode, YGAlignCenter);
			YGNodeStyleSetJustifyContent(baseNode, YGJustifySpaceBetween);
			YGNodeStyleSetFlexDirection(baseNode, YGFlexDirectionRow);
			YGNodeStyleSetHeight(baseNode, baseLineHeight+2*_staticData->_verticalPadding);
			ElementMargins(baseNode);
			YGNodeStyleSetFlexGrow(baseNode, 1.f);

			HorizontalControlLabel(context, name);

			auto stateBox = context.GetLayoutEngine().InsertNewImbuedNode(interactable);
			YGNodeStyleSetWidth(stateBox->YGNode(), 16);
			YGNodeStyleSetHeight(stateBox->YGNode(), 16);
			YGNodeStyleSetMargin(stateBox->YGNode(), YGEdgeHorizontal, _staticData->_checkboxHorizontalMargin);
			stateBox->_nodeAttachments._drawDelegate = [modelValue](DrawContext& draw, Rect frame, Rect content) {
				CommonWidgets::Styler::Get().CheckBox(draw, content, modelValue.QueryNonLayout().value_or(false));
			};
			stateBox->_nodeAttachments._ioDelegate = [modelValue](auto& ioContext, Rect, Rect) {
				if (ioContext.GetEvent().IsRelease_LButton()) {
					modelValue.Set(!modelValue.QueryNonLayout().value_or(false));
					return PlatformRig::ProcessInputResult::Consumed;
				}
				return PlatformRig::ProcessInputResult::Consumed;
			};
			context.GetLayoutEngine().PopNode();
		}

		bool BeginDisableableWidget(IWidgetsLayoutContext& context, uint64_t interactable)
		{
			auto enabledByHierarchy = context.EnabledByHierarchy();
			if (enabledByHierarchy == HierarchicalEnabledState::EnableChildren || context.GetBindingEngine().IsEnabled(interactable)) {
				bool hasDisableButton = enabledByHierarchy == HierarchicalEnabledState::NoImpact;
				if (hasDisableButton) {
					auto disablerWrapping = context.GetLayoutEngine().InsertAndPushNewNode();
					YGNodeStyleSetAlignItems(disablerWrapping, YGAlignCenter);
					YGNodeStyleSetJustifyContent(disablerWrapping, YGJustifyFlexEnd);
					YGNodeStyleSetFlexDirection(disablerWrapping, YGFlexDirectionRow);
				}

				return true;
			} else {
				return false;
			}
		}

		void EndDisableableWidget(IWidgetsLayoutContext& context, StringSection<> name, uint64_t interactable)
		{
			auto enabledByHierarchy = context.EnabledByHierarchy();

			if (enabledByHierarchy == HierarchicalEnabledState::EnableChildren || context.GetBindingEngine().IsEnabled(interactable)) {
				bool hasDisableButton = enabledByHierarchy == HierarchicalEnabledState::NoImpact;
				if (hasDisableButton) {
					DeactivateButton(context, interactable);
					context.GetLayoutEngine().PopNode();
				}
			} else {
				DisabledStateButton(context, interactable, name, enabledByHierarchy);
			}
		}

		void WriteCheckbox(IWidgetsLayoutContext& context, StringSection<> name, const V<bool>& modelValue) override
		{
			uint64_t interactable = (modelValue._type == MinimalBindingValueType::Constant) ? context.GetGuidStack().MakeGuid(name) : modelValue._id;
			if (BeginDisableableWidget(context, interactable))
				BeginCheckboxControl_Internal(context, name, modelValue, interactable);
			EndDisableableWidget(context, name, interactable);
		}

		static constexpr unsigned baseLineHeight = 20;

		void DeactivateButton(IWidgetsLayoutContext& context, uint64_t ctrlGuid)
		{
			auto newNode = context.GetLayoutEngine().InsertNewImbuedNode(ctrlGuid+32);
			YGNodeStyleSetWidth(newNode->YGNode(), _staticData->_deactivateButtonSize);
			YGNodeStyleSetHeight(newNode->YGNode(), _staticData->_deactivateButtonSize);
			YGNodeStyleSetMargin(newNode->YGNode(), YGEdgeAll, 2);
			YGNodeStyleSetFlexGrow(newNode->YGNode(), 0.f);
			YGNodeStyleSetFlexShrink(newNode->YGNode(), 0.f);

			newNode->_nodeAttachments._drawDelegate = [](DrawContext& draw, Rect frame, Rect content) {
				CommonWidgets::Styler::Get().XToggleButton(draw, frame);
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
			auto baseNode = context.GetLayoutEngine().InsertNewImbuedNode(interactable);
			ElementMargins(*baseNode);
			YGNodeStyleSetFlexGrow(baseNode->YGNode(), 1.0f);
			YGNodeStyleSetHeight(baseNode->YGNode(), baseLineHeight+2*_staticData->_verticalPadding);

			if (hierarchyState == HierarchicalEnabledState::NoImpact) {
				baseNode->_nodeAttachments._drawDelegate = [nameStr=name.AsString()](DrawContext& draw, Rect frame, Rect content) {
					CommonWidgets::Styler::Get().DisabledStateControl(draw, frame, nameStr);
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
			
			if (BeginDisableableWidget(context, interactable)) {

				auto sliderNode = context.GetLayoutEngine().InsertNewImbuedNode(interactable);
				YGNodeStyleSetFlexGrow(sliderNode->YGNode(), 1.0f);
				YGNodeStyleSetHeight(sliderNode->YGNode(), baseLineHeight+2*_staticData->_verticalPadding);
				ElementMargins(*sliderNode);
				sliderNode->_nodeAttachments._drawDelegate = [nameStr=name.AsString(), interactable, leftSideValue, rightSideValue, modelValue](DrawContext& draw, Rect frame, Rect content) {
					CommonWidgets::Styler::Get().Bounded(draw, frame, interactable, nameStr, modelValue.QueryNonLayout().value(), leftSideValue.QueryNonLayout().value(), rightSideValue.QueryNonLayout().value());
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
							hoverings->_textEntry.ProcessInput(evnt);
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
			}
			EndDisableableWidget(context, name, interactable);
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
					CommonWidgets::Styler::Get().SectionHeader(draw, content, nameStr, isOpen);
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

			auto contentContainer = context.GetLayoutEngine().InsertAndPushNewImbuedNode(containerGuid);
			YGNodeStyleSetMargin(contentContainer->YGNode(), YGEdgeAll, 8);
			YGNodeStyleSetPadding(contentContainer->YGNode(), YGEdgeAll, 2);

			contentContainer->_nodeAttachments._drawDelegate = [](DrawContext& draw, Rect frame, Rect content) {
				CommonWidgets::Styler::Get().RectangleContainer(draw, frame);
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
			unsigned _elementVerticalMargin = 4;
			unsigned _elementHorizontalMargin = 2;
			unsigned _deactivateButtonSize = 12;
			unsigned _verticalPadding = 4;

			unsigned _checkboxHorizontalMargin = 8;

			StaticData() = default;
			template<typename Formatter>
				StaticData(Formatter& fmttr)
			{
				uint64_t keyname;
				while (fmttr.TryKeyedItem(keyname)) {
					switch (keyname) {
					case "Font"_h: _font = RequireStringValue(fmttr).AsString(); break;

					case "ElementVerticalMargin"_h: _elementVerticalMargin = Formatters::RequireCastValue<decltype(_elementVerticalMargin)>(fmttr); break;
					case "ElementHorizontalMargin"_h: _elementHorizontalMargin = Formatters::RequireCastValue<decltype(_elementHorizontalMargin)>(fmttr); break;

					case "VerticalPadding"_h: _verticalPadding = Formatters::RequireCastValue<decltype(_verticalPadding)>(fmttr); break;

					case "DeactivateButtonSize"_h: _deactivateButtonSize = Formatters::RequireCastValue<decltype(_deactivateButtonSize)>(fmttr); break;

					case "CheckboxHorizontalPadding"_h: _checkboxHorizontalMargin = Formatters::RequireCastValue<decltype(_checkboxHorizontalMargin)>(fmttr); break;

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
			futureFont->StallWhilePending();		// stall
			return futureFont->Actualize();
		}
	};

	

	static YGNodeRef BeginRoot(IWidgetsLayoutContext& context, Rect containerSize)
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
		Rect _lastContainer { {0,0}, { 0, 0 } };
		Float3x3 _lastTransform = Identity<Float3x3>();

		void    Render(IOverlayContext& context, Layout& layout, Interactables& interactables, InterfaceState& interfaceState) override
		{
			Rect container = layout.GetMaximumSize();
			container._topLeft += Coord2{layout._paddingInternalBorder, layout._paddingInternalBorder};
			container._bottomRight -= Coord2{layout._paddingInternalBorder, layout._paddingInternalBorder};

			// rebuild the layout now if it's invalidated
			if (_bindingEngine->GetLayoutDependencyValidation().GetValidationIndex() != _lastBuiltLayoutValidationIndex || _lastContainer != container) {
				WidgetsLayoutContext formatter { _bindingEngine };
				BeginRoot(formatter, container);
				_layoutFn(formatter);
				EndRoot(formatter);

				_layedOutWidgets = formatter.BuildLayedOutWidgets();
				_lastBuiltLayoutValidationIndex = _bindingEngine->GetLayoutDependencyValidation().GetValidationIndex();
				_lastContainer = container;
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

