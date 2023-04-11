// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "TweakableEntityDocument.h"
#include "MinimalBindingEngine.h"
#include "../../RenderOverlays/CommonWidgets.h"
#include "../../RenderOverlays/DebuggingDisplay.h"
#include "../../RenderOverlays/LayoutEngine.h"
#include "../../RenderOverlays/ShapesRendering.h"
#include "../../RenderOverlays/DrawText.h"
#include <vector>
#include <set>
#include <stack>

using namespace PlatformRig::Literals;

namespace EntityInterface
{
	using namespace RenderOverlays;
	using namespace RenderOverlays::DebuggingDisplay;

	constexpr auto enter      = "enter"_key;
	constexpr auto escape     = "escape"_key;

	class WidgetsLayoutFormatter : public EntityInterface::IWidgetsLayoutFormatter
	{
	public:
		LayoutEngine _layoutEngine;

		ImbuedNode* BeginSharedLeftRightCtrl(StringSection<> name, const V<uint64_t>& modelValue, uint64_t interactable)
		{
			const auto lineHeight = baseLineHeight+4;
			auto baseNode = _layoutEngine.NewNode();
			YGNodeStyleSetWidthPercent(baseNode, 100.0f);
			YGNodeStyleSetHeight(baseNode, lineHeight+4);
			YGNodeStyleSetAlignItems(baseNode, YGAlignCenter);
			YGNodeStyleSetFlexDirection(baseNode, YGFlexDirectionRow);
			YGNodeStyleSetMargin(baseNode, YGEdgeAll, 2);
			_layoutEngine.InsertChildToStackTop(baseNode);
			_layoutEngine.PushNode(baseNode);

			auto mainCtrl = _layoutEngine.NewImbuedNode(interactable);
			YGNodeStyleSetFlexGrow(mainCtrl->YGNode(), 1.0f);
			YGNodeStyleSetHeightPercent(mainCtrl->YGNode(), 100.f);
			YGNodeStyleSetMargin(mainCtrl->YGNode(), YGEdgeAll, 2);
			mainCtrl->_nodeAttachments._drawDelegate = [nameStr=name.AsString(), modelValue, interactable](CommonWidgets::Draw& draw, Rect frame, Rect content) {
				if (auto str = modelValue.TryQueryNonLayoutAsString())
					draw.LeftRight(frame, interactable, nameStr, *str);
			};
			_layoutEngine.InsertChildToStackTop(mainCtrl->YGNode());
			return mainCtrl;
		}

		template<typename Type>
			void WriteHalfDoubleTemplate(StringSection<> name, const V<Type>& modelValue, const V<Type>& minValue, const V<Type>& maxValue)
		{
			uint64_t interactable = (modelValue._type == MinimalBindingValueType::Constant) ? _layoutEngine.GuidStack().MakeGuid(name) : modelValue._id;
			
			auto enabledByHierarchy = EnabledByHierarchy();
			if (enabledByHierarchy == HierarchicalEnabledState::EnableChildren || _state->IsEnabled(interactable)) {
			
				auto mainCtrl = BeginSharedLeftRightCtrl(name, modelValue, interactable);
				mainCtrl->_nodeAttachments._ioDelegate = [modelValue, minValue, maxValue](auto& inputContext, auto& evnt, Rect frame, Rect content) {
					bool leftSide = evnt._mousePosition[0] < (frame._topLeft[0]+frame._bottomRight[0])/2;
					if (evnt.IsRelease_LButton()) {
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
					DeactivateButton(interactable);
				_layoutEngine.PopNode();

			} else {
				DisabledStateButton(interactable, name, enabledByHierarchy);
			}
		}

		void WriteHalfDoubleInt(StringSection<> name, const V<int64_t>& modelValue, const V<int64_t>& min, const V<int64_t>& max) override { WriteHalfDoubleTemplate(name, modelValue, min, max); }
		void WriteHalfDoubleFloat(StringSection<> name, const V<float>& modelValue, const V<float>& min, const V<float>& max) override { WriteHalfDoubleTemplate(name, modelValue, min, max); }

		template<typename Type>
			void WriteDecrementIncrementTemplate(StringSection<> name, const V<Type>& modelValue, const V<Type>& minValue, const V<Type>& maxValue)
		{
			uint64_t interactable = (modelValue._type == MinimalBindingValueType::Constant) ? _layoutEngine.GuidStack().MakeGuid(name) : modelValue._id;
			
			auto enabledByHierarchy = EnabledByHierarchy();
			if (enabledByHierarchy == HierarchicalEnabledState::EnableChildren || _state->IsEnabled(interactable)) {
			
				auto mainCtrl = BeginSharedLeftRightCtrl(name, modelValue, interactable);
				mainCtrl->_nodeAttachments._ioDelegate = [modelValue, minValue, maxValue](auto& inputContext, auto& evnt, Rect frame, Rect content) {
					bool leftSide = evnt._mousePosition[0] < (frame._topLeft[0]+frame._bottomRight[0])/2;
					if (evnt.IsRelease_LButton()) {
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
					DeactivateButton(interactable);
				_layoutEngine.PopNode();

			} else {
				DisabledStateButton(interactable, name, enabledByHierarchy);
			}
		}

		void WriteDecrementIncrementInt(StringSection<> name, const V<int64_t>& modelValue, const V<int64_t>& min, const V<int64_t>& max) override { WriteDecrementIncrementTemplate(name, modelValue, min, max); }
		void WriteDecrementIncrementFloat(StringSection<> name, const V<float>& modelValue, const V<float>& min, const V<float>& max) override { WriteDecrementIncrementTemplate(name, modelValue, min, max); }

		void HorizontalControlLabel(StringSection<> name)
		{
			auto label = _layoutEngine.NewImbuedNode(0);
			YGNodeStyleSetWidth(label->YGNode(), 200);
			YGNodeStyleSetHeightPercent(label->YGNode(), 100.f);
			_layoutEngine.InsertChildToStackTop(label->YGNode());
			label->_nodeAttachments._drawDelegate = [nameStr=name.AsString()](CommonWidgets::Draw& draw, Rect frame, Rect content) {
				DrawText().Draw(draw.GetContext(), content, nameStr);
			};
		}

		void WriteHorizontalCombo(StringSection<> name, const V<int64_t>& modelValue, IteratorRange<const std::pair<int64_t, const char*>*> options) override
		{
			uint64_t interactable = (modelValue._type == MinimalBindingValueType::Constant) ? _layoutEngine.GuidStack().MakeGuid(name) : modelValue._id;
			const auto lineHeight = baseLineHeight+4;

			auto enabledByHierarchy = EnabledByHierarchy();
			if (enabledByHierarchy == HierarchicalEnabledState::EnableChildren || _state->IsEnabled(interactable)) {

				auto baseNode = _layoutEngine.NewNode();
				YGNodeStyleSetWidthPercent(baseNode, 100.0f);
				YGNodeStyleSetHeight(baseNode, lineHeight+4);
				YGNodeStyleSetAlignItems(baseNode, YGAlignCenter);
				YGNodeStyleSetFlexDirection(baseNode, YGFlexDirectionRow);
				YGNodeStyleSetMargin(baseNode, YGEdgeAll, 2);
				_layoutEngine.InsertChildToStackTop(baseNode);
				_layoutEngine.PushNode(baseNode);

				HorizontalControlLabel(name);

				for (unsigned c=0; c<options.size(); ++c) {
					auto node = _layoutEngine.NewImbuedNode(interactable+1+c);
					YGNodeStyleSetFlexGrow(node->YGNode(), 1.0f);
					YGNodeStyleSetHeightPercent(node->YGNode(), 100.f);
					_layoutEngine.InsertChildToStackTop(node->YGNode());
					Corner::BitField corners = 0;
					if (c == 0) corners |= Corner::TopLeft|Corner::BottomLeft;
					if ((c+1) == options.size()) corners |= Corner::TopRight|Corner::BottomRight;
					node->_nodeAttachments._drawDelegate = [nameStr=std::string{options[c].second}, corners, value=options[c].first, modelValue](CommonWidgets::Draw& draw, Rect frame, Rect content) {
						bool selected = modelValue.QueryNonLayout().value() == value;
						OutlineRoundedRectangle(draw.GetContext(), frame, selected ? ColorB{96, 96, 96} : ColorB{64, 64, 64}, 1.f, 0.4f, corners);
						DrawText().Alignment(TextAlignment::Center).Draw(draw.GetContext(), content, nameStr);
					};
					node->_nodeAttachments._ioDelegate = [modelValue, value=options[c].first](auto&, auto& evnt, Rect, Rect) {
						if (evnt.IsRelease_LButton()) {
							modelValue.Set(value);
							return PlatformRig::ProcessInputResult::Consumed;
						}
						return PlatformRig::ProcessInputResult::Consumed;
					};
				}

				if (enabledByHierarchy == HierarchicalEnabledState::NoImpact)
					DeactivateButton(interactable);
				_layoutEngine.PopNode();

			} else {
				DisabledStateButton(interactable, name, enabledByHierarchy);
			}
		}

		void BeginCheckboxControl_Internal(StringSection<> name, const V<bool>& modelValue, uint64_t interactable)
		{
			const auto lineHeight = baseLineHeight+4;
			auto baseNode = _layoutEngine.NewNode();
			YGNodeStyleSetWidthPercent(baseNode, 100.0f);
			YGNodeStyleSetHeight(baseNode, lineHeight+4);
			YGNodeStyleSetAlignItems(baseNode, YGAlignCenter);
			YGNodeStyleSetFlexDirection(baseNode, YGFlexDirectionRow);
			YGNodeStyleSetMargin(baseNode, YGEdgeAll, 2);
			_layoutEngine.InsertChildToStackTop(baseNode);
			_layoutEngine.PushNode(baseNode);

			HorizontalControlLabel(name);

			auto stateBox = _layoutEngine.NewImbuedNode(interactable);
			YGNodeStyleSetWidth(stateBox->YGNode(), 16);
			YGNodeStyleSetHeight(stateBox->YGNode(), 16);
			_layoutEngine.InsertChildToStackTop(stateBox->YGNode());
			stateBox->_nodeAttachments._drawDelegate = [modelValue](CommonWidgets::Draw& draw, Rect frame, Rect content) {
				draw.CheckBox(content, modelValue.QueryNonLayout().value());
			};
			stateBox->_nodeAttachments._ioDelegate = [modelValue](auto&, auto& evnt, Rect, Rect) {
				if (evnt.IsRelease_LButton()) {
					modelValue.Set(!modelValue.QueryNonLayout().value());
					return PlatformRig::ProcessInputResult::Consumed;
				}
				return PlatformRig::ProcessInputResult::Consumed;
			};
		}

		void BeginCheckboxControl(StringSection<> name, const V<bool>& modelValue, uint64_t interactable)
		{
			BeginCheckboxControl_Internal(name, modelValue, interactable);
		}

		void WriteCheckbox(StringSection<> name, const V<bool>& modelValue) override
		{
			uint64_t interactable = (modelValue._type == MinimalBindingValueType::Constant) ? _layoutEngine.GuidStack().MakeGuid(name) : modelValue._id;

			auto enabledByHierarchy = EnabledByHierarchy();
			if (enabledByHierarchy == HierarchicalEnabledState::EnableChildren || _state->IsEnabled(interactable)) {
				BeginCheckboxControl(name, modelValue, interactable);
				if (enabledByHierarchy == HierarchicalEnabledState::NoImpact)
					DeactivateButton(interactable);
				_layoutEngine.PopNode();
			} else {
				DisabledStateButton(interactable, name, enabledByHierarchy);
			}
		}

		static constexpr unsigned baseLineHeight = 20;

		void DeactivateButton(uint64_t ctrlGuid)
		{
			auto newNode = _layoutEngine.NewImbuedNode(ctrlGuid+32);
			YGNodeStyleSetWidth(newNode->YGNode(), 12);
			YGNodeStyleSetHeight(newNode->YGNode(), 12);
			YGNodeStyleSetMargin(newNode->YGNode(), YGEdgeAll, 2);
			YGNodeStyleSetMarginAuto(newNode->YGNode(), YGEdgeLeft);
			_layoutEngine.InsertChildToStackTop(newNode->YGNode());

			newNode->_nodeAttachments._drawDelegate = [](CommonWidgets::Draw& draw, Rect frame, Rect content) {
				draw.XToggleButton(frame);
			};

			newNode->_nodeAttachments._ioDelegate = [ctrlGuid, state=_state](auto&, auto& evnt, Rect, Rect) {
				if (evnt.IsRelease_LButton()) {
					state->ToggleEnable(ctrlGuid);
					state->InvalidateModel();
					state->InvalidateLayout();
					return PlatformRig::ProcessInputResult::Consumed;
				}
				return PlatformRig::ProcessInputResult::Consumed;
			};
		}

		void DisabledStateButton(uint64_t interactable, StringSection<> name, HierarchicalEnabledState hierarchyState)
		{
			const auto lineHeight = baseLineHeight+4;
			auto baseNode = _layoutEngine.NewImbuedNode(interactable);
			YGNodeStyleSetMargin(baseNode->YGNode(), YGEdgeAll, 2);
			YGNodeStyleSetFlexGrow(baseNode->YGNode(), 1.0f);
			YGNodeStyleSetHeight(baseNode->YGNode(), lineHeight+4);
			_layoutEngine.InsertChildToStackTop(baseNode->YGNode());

			if (hierarchyState == HierarchicalEnabledState::NoImpact) {
				baseNode->_nodeAttachments._drawDelegate = [nameStr=name.AsString()](CommonWidgets::Draw& draw, Rect frame, Rect content) {
					draw.DisabledStateControl(frame, nameStr);
				};

				baseNode->_nodeAttachments._ioDelegate = [interactable, state=_state](auto&, auto& evnt, Rect, Rect) {
					if (evnt.IsRelease_LButton()) {
						state->ToggleEnable(interactable);
						state->InvalidateModel();
						state->InvalidateLayout();
						return PlatformRig::ProcessInputResult::Consumed;
					}
					return PlatformRig::ProcessInputResult::Consumed;
				};
			} else {
				baseNode->_nodeAttachments._drawDelegate = [nameStr=name.AsString()](CommonWidgets::Draw& draw, Rect frame, Rect content) {
					DrawText().Color({0x5f, 0x5f, 0x5f}).Alignment(TextAlignment::Center).Draw(draw.GetContext(), content, nameStr);
				};
			}
		}

		template<typename Type>
			void WriteBoundedTemplate(StringSection<> name, const V<Type>& modelValue, const V<Type>& leftSideValue, const V<Type>& rightSideValue)
		{
			uint64_t interactable = (modelValue._type == MinimalBindingValueType::Constant) ? _layoutEngine.GuidStack().MakeGuid(name) : modelValue._id;
			const auto lineHeight = baseLineHeight+4;
			
			auto enabledByHierarchy = EnabledByHierarchy();
			if (enabledByHierarchy == HierarchicalEnabledState::EnableChildren || _state->IsEnabled(interactable)) {

				auto baseNode = _layoutEngine.NewNode();
				YGNodeStyleSetWidthPercent(baseNode, 100.0f);
				YGNodeStyleSetHeight(baseNode, lineHeight+4);
				YGNodeStyleSetAlignItems(baseNode, YGAlignCenter);
				YGNodeStyleSetFlexDirection(baseNode, YGFlexDirectionRow);
				YGNodeStyleSetMargin(baseNode, YGEdgeAll, 2);
				_layoutEngine.InsertChildToStackTop(baseNode);
				_layoutEngine.PushNode(baseNode);

				auto sliderNode = _layoutEngine.NewImbuedNode(interactable);
				YGNodeStyleSetFlexGrow(sliderNode->YGNode(), 1.0f);
				YGNodeStyleSetHeightPercent(sliderNode->YGNode(), 100.f);
				YGNodeStyleSetMargin(sliderNode->YGNode(), YGEdgeAll, 2);
				sliderNode->_nodeAttachments._drawDelegate = [nameStr=name.AsString(), interactable, leftSideValue, rightSideValue, modelValue](CommonWidgets::Draw& draw, Rect frame, Rect content) {
					draw.Bounded(frame, interactable, nameStr, modelValue.QueryNonLayout().value(), leftSideValue.QueryNonLayout().value(), rightSideValue.QueryNonLayout().value());
				};
				sliderNode->_nodeAttachments._ioDelegate = [interactable, leftSideValue, rightSideValue, modelValue](const PlatformRig::InputContext& inputContext, auto& evnt, Rect frame, Rect content) {
					auto* hoverings = inputContext.GetService<RenderOverlays::CommonWidgets::HoveringLayer>();
					auto* interfaceState = inputContext.GetService<RenderOverlays::DebuggingDisplay::InterfaceState>();
					if (!hoverings || !interfaceState) return PlatformRig::ProcessInputResult::Passthrough;

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
				_layoutEngine.InsertChildToStackTop(sliderNode->YGNode());

				if (enabledByHierarchy == HierarchicalEnabledState::NoImpact)
					DeactivateButton(interactable);
				_layoutEngine.PopNode();

			} else {

				DisabledStateButton(interactable, name, enabledByHierarchy);
			
			}
		}

		void WriteBoundedInt(StringSection<> name, const V<int64_t>& modelValue, const V<int64_t>& leftSideValue, const V<int64_t>& rightSideValue) override { WriteBoundedTemplate(name, modelValue, leftSideValue, rightSideValue); }
		void WriteBoundedFloat(StringSection<> name, const V<float>& modelValue, const V<float>& leftSideValue, const V<float>& rightSideValue) override { WriteBoundedTemplate(name, modelValue, leftSideValue, rightSideValue); }

		bool BeginCollapsingContainer(StringSection<> name) override
		{
			uint64_t containerGuid = _layoutEngine.GuidStack().MakeGuid(name, "##collapsingcontainer");
			_layoutEngine.GuidStack().push(containerGuid);
			_hierarchicalEnabledStates.push_back(0);
			bool isOpen = _state->IsEnabled(containerGuid);

			auto outerNode = _layoutEngine.NewNode();
			YGNodeStyleSetPadding(outerNode, YGEdgeAll, 0);       // zero padding because the headerContainer and contentContainers have their own padding
			YGNodeStyleSetMargin(outerNode, YGEdgeAll, 0);
			_layoutEngine.InsertChildToStackTop(outerNode);

			{
				const auto headerHeight = 24u;
				auto headerContainer = _layoutEngine.NewImbuedNode(containerGuid);
				YGNodeStyleSetMargin(headerContainer->YGNode(), YGEdgeAll, 0);
				YGNodeStyleSetWidthPercent(headerContainer->YGNode(), 100.0f);
				YGNodeStyleSetHeight(headerContainer->YGNode(), headerHeight);
				YGNodeStyleSetAlignItems(headerContainer->YGNode(), YGAlignCenter);
				YGNodeStyleSetFlexDirection(headerContainer->YGNode(), YGFlexDirectionRow);
				YGNodeInsertChild(outerNode, headerContainer->YGNode(), YGNodeGetChildCount(outerNode));
				
				headerContainer->_nodeAttachments._drawDelegate = [nameStr=name.AsString(), isOpen](CommonWidgets::Draw& draw, Rect frame, Rect content) {
					draw.SectionHeader(content, nameStr, isOpen);
				};

				headerContainer->_nodeAttachments._ioDelegate = [containerGuid, state=_state](auto&, auto& evnt, Rect, Rect) {
					if (evnt.IsRelease_LButton()) {
						state->ToggleEnable(containerGuid);
						state->InvalidateModel();
						state->InvalidateLayout();
						return PlatformRig::ProcessInputResult::Consumed;
					}
					return PlatformRig::ProcessInputResult::Passthrough;
				};
			}

			auto contentContainer = _layoutEngine.NewNode();
			if (isOpen)
				YGNodeStyleSetMargin(contentContainer, YGEdgeAll, 2);
			YGNodeInsertChild(outerNode, contentContainer, YGNodeGetChildCount(outerNode));

			_layoutEngine.PushNode(contentContainer);       // upcoming nodes will go into the content container
			return isOpen;
		}

		void BeginContainer() override
		{
			uint64_t containerGuid = _layoutEngine.GuidStack().MakeGuid("##container");
			_layoutEngine.GuidStack().push(containerGuid);

			auto contentContainer = _layoutEngine.NewImbuedNode(containerGuid);
			YGNodeStyleSetMargin(contentContainer->YGNode(), YGEdgeAll, 8);
			YGNodeStyleSetPadding(contentContainer->YGNode(), YGEdgeAll, 2);
			_layoutEngine.InsertChildToStackTop(contentContainer->YGNode());
			_layoutEngine.PushNode(contentContainer->YGNode());

			contentContainer->_nodeAttachments._drawDelegate = [](CommonWidgets::Draw& draw, Rect frame, Rect content) {
				draw.RectangleContainer(frame);
			};

			DisabledStateButton(containerGuid, "Enable", EnabledByHierarchy());
			_hierarchicalEnabledStates.push_back(containerGuid);
		}

		void EndContainer() override
		{
			assert(!_layoutEngine.GuidStack().empty());
			_layoutEngine.GuidStack().pop();
			_layoutEngine.PopNode();
			_hierarchicalEnabledStates.pop_back();
		}

		YGNodeRef BeginRoot(Coord2 containerSize)
		{
			auto windowNode = _layoutEngine.NewNode();
			_layoutEngine.PushRoot(windowNode, containerSize);
			return windowNode;
		}

		void EndRoot()
		{
			_layoutEngine.PopNode();
		}

		HierarchicalEnabledState EnabledByHierarchy()
		{
			for (auto i=_hierarchicalEnabledStates.rbegin(); i!=_hierarchicalEnabledStates.rend(); ++i) {
				if (*i != 0) {
					auto state = _state->IsEnabled(*i);
					return state ? HierarchicalEnabledState::EnableChildren : HierarchicalEnabledState::DisableChildren;
				}
			}
			return HierarchicalEnabledState::NoImpact;
		}

		MinimalBindingEngine& GetBindingEngine() override { return *_state; }

		LayedOutWidgets BuildLayedOutWidgets()
		{
			return _layoutEngine.BuildLayedOutWidgets();
		}

		WidgetsLayoutFormatter(std::shared_ptr<MinimalBindingEngine> state) : _state(std::move(state)) {}

	private:
		std::shared_ptr<MinimalBindingEngine> _state;
		std::vector<uint64_t> _hierarchicalEnabledStates;
	};


	class TweakerGroup : public IWidget
	{
	public:
		LayedOutWidgets _layedOutWidgets;
		CommonWidgets::HoveringLayer _hoverings;
		std::shared_ptr<MinimalBindingEngine> _bindingEngine;
		WriteToLayoutFormatter _layoutFn;
		unsigned _lastBuiltLayoutValidationIndex = ~0u;

		void    Render(IOverlayContext& context, Layout& layout, Interactables& interactables, InterfaceState& interfaceState) override
		{
			Rect container = layout.GetMaximumSize();
			container._topLeft += Coord2{layout._paddingInternalBorder, layout._paddingInternalBorder};
			container._bottomRight -= Coord2{layout._paddingInternalBorder, layout._paddingInternalBorder};

			// rebuild the layout now if it's invalidated
			if (_bindingEngine->GetLayoutValidationIndex() != _lastBuiltLayoutValidationIndex) {
				WidgetsLayoutFormatter formatter { _bindingEngine };
				formatter.BeginRoot({container.Width(), container.Height()});
				_layoutFn(formatter);
				formatter.EndRoot();

				_layedOutWidgets = formatter.BuildLayedOutWidgets();
				_lastBuiltLayoutValidationIndex = _bindingEngine->GetLayoutValidationIndex();
			}

			{
				Float3x3 transform {
					1.f, 0.f, container._topLeft[0],
					0.f, 1.f, container._topLeft[1],
					0.f, 0.f, 1.f
				};
				CommonWidgets::Draw draw{context, interactables, interfaceState, _hoverings};
				_layedOutWidgets.Draw(draw, transform);
			}
		}

		ProcessInputResult    ProcessInput(InterfaceState& interfaceState, const OSServices::InputSnapshot& input) override
		{
			PlatformRig::InputContext inputContext;
			inputContext.AttachService2(_hoverings);
			inputContext.AttachService2(interfaceState);
			return _layedOutWidgets.ProcessInput(inputContext, input);
		}

		TweakerGroup(std::shared_ptr<MinimalBindingEngine> bindingEngine, WriteToLayoutFormatter&& layoutFn)
		: _bindingEngine(std::move(bindingEngine)), _layoutFn(layoutFn) {}
	};

	std::shared_ptr<RenderOverlays::DebuggingDisplay::IWidget> CreateWidgetGroup(std::shared_ptr<MinimalBindingEngine> doc, WriteToLayoutFormatter&& layoutFn)
	{
		return std::make_shared<TweakerGroup>(doc, std::move(layoutFn));
	}
}

