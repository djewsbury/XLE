// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "TweakableEntityDocument.h"
#include "TweakableEntityDocumentInternal.h"
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

		ImbuedNode* BeginSharedLeftRightCtrl(StringSection<> name, uint64_t interactable)
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
			mainCtrl->_nodeAttachments._drawDelegate = [nameStr=name.AsString(), state=_state, interactable](CommonWidgets::Draw& draw, Rect frame, Rect content) {
				draw.LeftRight(frame, interactable, nameStr, state->GetWorkingValueAsString(interactable));
			};
			_layoutEngine.InsertChildToStackTop(mainCtrl->YGNode());
			return mainCtrl;
		}

		template<typename Type>
			void WriteHalfDoubleTemplate(StringSection<> name, Type initialValue, Type minValue, Type maxValue)
		{
			uint64_t interactable = _layoutEngine.GuidStack().MakeGuid(name);
			
			auto enabledByHierarchy = EnabledByHierarchy();
			if (enabledByHierarchy == HierarchicalEnabledState::EnableChildren || _state->IsEnabled(interactable)) {
				_state->InitializeValue(interactable, initialValue);
			
				auto mainCtrl = BeginSharedLeftRightCtrl(name, interactable);
				mainCtrl->_nodeAttachments._ioDelegate = [interactable, state=_state, minValue, maxValue](CommonWidgets::Input& input, Rect frame, Rect content) {
					bool leftSide = input.GetEvent()._mousePosition[0] < (frame._topLeft[0]+frame._bottomRight[0])/2;
					if (input.GetEvent().IsRelease_LButton()) {
						auto currentValue = state->GetWorkingValue<Type>(interactable);
						auto newValue = currentValue;
						if (leftSide) 	newValue = std::max(minValue, currentValue/2);
						else 			newValue = std::min(maxValue, currentValue*2);
						if (newValue != currentValue) {
							state->SetWorkingValue(interactable, newValue);
							state->InvalidateModel();
							return IODelegateResult::Consumed;
						}
					}
					return IODelegateResult::Passthrough;
				};

				if (enabledByHierarchy == HierarchicalEnabledState::NoImpact)
					DeactivateButton(interactable);
				_layoutEngine.PopNode();

			} else {
				DisabledStateButton(interactable, name, enabledByHierarchy);
			}
		}

		void WriteHalfDoubleInt(StringSection<> name, int64_t initialValue, int64_t min, int64_t max) override { WriteHalfDoubleTemplate(name, initialValue, min, max); }
		void WriteHalfDoubleFloat(StringSection<> name, float initialValue, float min, float max) override { WriteHalfDoubleTemplate(name, initialValue, min, max); }

		template<typename Type>
			void WriteDecrementIncrementTemplate(StringSection<> name, Type initialValue, Type minValue, Type maxValue)
		{
			uint64_t interactable = _layoutEngine.GuidStack().MakeGuid(name);
			
			auto enabledByHierarchy = EnabledByHierarchy();
			if (enabledByHierarchy == HierarchicalEnabledState::EnableChildren || _state->IsEnabled(interactable)) {
				_state->InitializeValue(interactable, initialValue);
			
				auto mainCtrl = BeginSharedLeftRightCtrl(name, interactable);
				mainCtrl->_nodeAttachments._ioDelegate = [interactable, state=_state, minValue, maxValue](CommonWidgets::Input& input, Rect frame, Rect content) {
					bool leftSide = input.GetEvent()._mousePosition[0] < (frame._topLeft[0]+frame._bottomRight[0])/2;
					if (input.GetEvent().IsRelease_LButton()) {
						auto currentValue = state->GetWorkingValue<Type>(interactable);
						auto newValue = currentValue;
						if (leftSide) 	newValue = std::max(minValue, currentValue-1);
						else 			newValue = std::min(maxValue, currentValue+1);
						if (newValue != currentValue) {
							state->SetWorkingValue(interactable, newValue);
							state->InvalidateModel();
							return IODelegateResult::Consumed;
						}
					}
					return IODelegateResult::Passthrough;
				};

				if (enabledByHierarchy == HierarchicalEnabledState::NoImpact)
					DeactivateButton(interactable);
				_layoutEngine.PopNode();

			} else {
				DisabledStateButton(interactable, name, enabledByHierarchy);
			}
		}

		void WriteDecrementIncrementInt(StringSection<> name, int64_t initialValue, int64_t min, int64_t max) override { WriteDecrementIncrementTemplate(name, initialValue, min, max); }
		void WriteDecrementIncrementFloat(StringSection<> name, float initialValue, float min, float max) override { WriteDecrementIncrementTemplate(name, initialValue, min, max); }

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

		void WriteHorizontalCombo(StringSection<> name, int64_t initialValue, IteratorRange<const std::pair<int64_t, const char*>*> options) override
		{
			uint64_t interactable = _layoutEngine.GuidStack().MakeGuid(name);
			const auto lineHeight = baseLineHeight+4;

			auto enabledByHierarchy = EnabledByHierarchy();
			if (enabledByHierarchy == HierarchicalEnabledState::EnableChildren || _state->IsEnabled(interactable)) {
				_state->InitializeValue(interactable, initialValue);

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
					node->_nodeAttachments._drawDelegate = [nameStr=std::string{options[c].second}, corners, state=_state, value=options[c].first, interactable](CommonWidgets::Draw& draw, Rect frame, Rect content) {
						bool selected = state->GetWorkingValue<int64_t>(interactable) == value;
						OutlineRoundedRectangle(draw.GetContext(), frame, selected ? ColorB{96, 96, 96} : ColorB{64, 64, 64}, 1.f, 0.4f, corners);
						DrawText().Alignment(TextAlignment::Center).Draw(draw.GetContext(), content, nameStr);
					};
					node->_nodeAttachments._ioDelegate = [interactable, state=_state, value=options[c].first](CommonWidgets::Input& input, Rect, Rect) {
						if (input.GetEvent().IsRelease_LButton()) {
							state->SetWorkingValue(interactable, value);
							state->InvalidateModel();
							return IODelegateResult::Consumed;
						}
						return IODelegateResult::Consumed;
					};
				}

				if (enabledByHierarchy == HierarchicalEnabledState::NoImpact)
					DeactivateButton(interactable);
				_layoutEngine.PopNode();

			} else {
				DisabledStateButton(interactable, name, enabledByHierarchy);
			}
		}

		void BeginCheckboxControl_Internal(StringSection<> name, uint64_t interactable, bool invalidatesLayout)
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
			stateBox->_nodeAttachments._drawDelegate = [interactable, state=_state](CommonWidgets::Draw& draw, Rect frame, Rect content) {
				draw.CheckBox(content, state->GetWorkingValue<bool>(interactable));
			};
			stateBox->_nodeAttachments._ioDelegate = [interactable, state=_state, invalidatesLayout](CommonWidgets::Input& input, Rect, Rect) {
				if (input.GetEvent().IsRelease_LButton()) {
					state->SetWorkingValue(interactable, !state->GetWorkingValue<bool>(interactable));
					state->InvalidateModel();
					if (invalidatesLayout)
						state->InvalidateLayout();
					return IODelegateResult::Consumed;
				}
				return IODelegateResult::Consumed;
			};
		}

		void BeginCheckboxControl(StringSection<> name, uint64_t interactable)
		{
			BeginCheckboxControl_Internal(name, interactable, false);
		}

		bool GetCheckbox(StringSection<> name, bool initialValue) override
		{
			uint64_t interactable = _layoutEngine.GuidStack().MakeGuid(name);
			BeginCheckboxControl_Internal(name, interactable, true);
			_layoutEngine.PopNode();
			_state->InitializeValue(interactable, initialValue);
			return _state->GetWorkingValue<bool>(interactable);
		}

		void WriteCheckbox(StringSection<> name, bool initialValue) override
		{
			uint64_t interactable = _layoutEngine.GuidStack().MakeGuid(name);

			auto enabledByHierarchy = EnabledByHierarchy();
			if (enabledByHierarchy == HierarchicalEnabledState::EnableChildren || _state->IsEnabled(interactable)) {
				_state->InitializeValue(interactable, initialValue);
				BeginCheckboxControl(name, interactable);
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

			newNode->_nodeAttachments._ioDelegate = [ctrlGuid, state=_state](CommonWidgets::Input& input, Rect, Rect) {
				if (input.GetEvent().IsRelease_LButton()) {
					state->ToggleEnable(ctrlGuid);
					state->InvalidateModel();
					state->InvalidateLayout();
					return IODelegateResult::Consumed;
				}
				return IODelegateResult::Consumed;
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

				baseNode->_nodeAttachments._ioDelegate = [interactable, state=_state](CommonWidgets::Input& input, Rect, Rect) {
					if (input.GetEvent().IsRelease_LButton()) {
						state->ToggleEnable(interactable);
						state->InvalidateModel();
						state->InvalidateLayout();
						return IODelegateResult::Consumed;
					}
					return IODelegateResult::Consumed;
				};
			} else {
				baseNode->_nodeAttachments._drawDelegate = [nameStr=name.AsString()](CommonWidgets::Draw& draw, Rect frame, Rect content) {
					DrawText().Color({0x5f, 0x5f, 0x5f}).Alignment(TextAlignment::Center).Draw(draw.GetContext(), content, nameStr);
				};
			}
		}

		template<typename Type>
			void WriteBoundedTemplate(StringSection<> name, Type initialValue, Type leftSideValue, Type rightSideValue)
		{
			uint64_t interactable = _layoutEngine.GuidStack().MakeGuid(name);
			const auto lineHeight = baseLineHeight+4;
			
			auto enabledByHierarchy = EnabledByHierarchy();
			if (enabledByHierarchy == HierarchicalEnabledState::EnableChildren || _state->IsEnabled(interactable)) {
				_state->InitializeValue(interactable, initialValue);

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
				sliderNode->_nodeAttachments._drawDelegate = [nameStr=name.AsString(), state=_state, interactable, leftSideValue, rightSideValue](CommonWidgets::Draw& draw, Rect frame, Rect content) {
					draw.Bounded(frame, interactable, nameStr, state->GetWorkingValue<Type>(interactable), leftSideValue, rightSideValue);
				};
				sliderNode->_nodeAttachments._ioDelegate = [interactable, state=_state, leftSideValue, rightSideValue](CommonWidgets::Input& input, Rect frame, Rect content) {
					if (input.GetHoverings()._hoveringCtrl) {
						if ((input.GetEvent()._mouseButtonsTransition != 0) && input.GetInterfaceState().GetCapture()._widget._id == interactable && !Contains(input.GetInterfaceState().GetCapture()._widget._rect, input.GetEvent()._mousePosition)) {
							if (state->TryUpdateValueFromString<Type>(interactable, input.GetHoverings()._textEntry._currentLine))
								state->InvalidateModel();
							input.GetInterfaceState().EndCapturing();
							input.GetHoverings()._hoveringCtrl = 0;
							return IODelegateResult::Consumed;
						} 

						if (input.GetEvent().IsPress(enter)) {
							if (state->TryUpdateValueFromString<Type>(interactable, input.GetHoverings()._textEntry._currentLine))
								state->InvalidateModel();
							input.GetInterfaceState().EndCapturing();
							input.GetHoverings()._hoveringCtrl = 0;
						} else if (input.GetEvent().IsPress(escape)) {
							input.GetInterfaceState().EndCapturing();
							input.GetHoverings()._hoveringCtrl = 0;
						} else {
							input.GetHoverings()._textEntry.ProcessInput(input.GetInterfaceState(), input.GetEvent());
						}
					} else {
						if (input.GetEvent().IsPress_LButton()) {
							input.GetInterfaceState().BeginCapturing(input.GetInterfaceState().TopMostWidget());
						} else if (input.GetInterfaceState().GetCapture()._widget._id == interactable) {
							const unsigned driftThreshold = 4;
							if (input.GetInterfaceState().GetCapture()._driftDuringCapture[0] < driftThreshold && input.GetInterfaceState().GetCapture()._driftDuringCapture[1] < driftThreshold) {
								// inside drift threshold
								if (input.GetEvent().IsRelease_LButton()) {
									input.GetHoverings()._hoveringCtrl = interactable;
									input.GetHoverings()._textEntry.Reset(state->GetWorkingValueAsString(interactable));
								}
							} else {
								// outside of drift threshold
								if (input.GetEvent().IsHeld_LButton()) {
									// dragging while captured
									float alpha = (input.GetEvent()._mousePosition[0] - input.GetInterfaceState().TopMostWidget()._rect._topLeft[0]) / float(input.GetInterfaceState().TopMostWidget()._rect._bottomRight[0] - input.GetInterfaceState().TopMostWidget()._rect._topLeft[0]);
									alpha = std::max(0.f, std::min(1.f, alpha));
									auto newValue = LinearInterpolate(leftSideValue, rightSideValue, alpha);
									state->SetWorkingValue(interactable, newValue);
									state->InvalidateModel();
								}
								if (input.GetEvent().IsRelease_LButton())
									input.GetInterfaceState().EndCapturing();
							}
						}
					}
					return IODelegateResult::Consumed;
				};
				_layoutEngine.InsertChildToStackTop(sliderNode->YGNode());

				if (enabledByHierarchy == HierarchicalEnabledState::NoImpact)
					DeactivateButton(interactable);
				_layoutEngine.PopNode();

			} else {

				DisabledStateButton(interactable, name, enabledByHierarchy);
			
			}
		}

		void WriteBoundedInt(StringSection<> name, int64_t initialValue, int64_t leftSideValue, int64_t rightSideValue) override { WriteBoundedTemplate(name, initialValue, leftSideValue, rightSideValue); }
		void WriteBoundedFloat(StringSection<> name, float initialValue, float leftSideValue, float rightSideValue) override { WriteBoundedTemplate(name, initialValue, leftSideValue, rightSideValue); }

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

				headerContainer->_nodeAttachments._ioDelegate = [containerGuid, state=_state](CommonWidgets::Input& input, Rect, Rect) {
					if (input.GetEvent().IsRelease_LButton()) {
						state->ToggleEnable(containerGuid);
						state->InvalidateModel();
						state->InvalidateLayout();
						return IODelegateResult::Consumed;
					}
					return IODelegateResult::Passthrough;
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

		// Stubs for OutputStreamFormatter
		ElementId BeginKeyedElement(StringSection<> name) override { return 0; }
		ElementId BeginSequencedElement() override { return 0; }
		void EndElement(ElementId) override {}
		void WriteKeyedValue(StringSection<> name, StringSection<> value) override {}
		void WriteSequencedValue(StringSection<> value) override {}

		LayedOutWidgets BuildLayedOutWidgets()
		{
			return _layoutEngine.BuildLayedOutWidgets();
		}

		WidgetsLayoutFormatter(std::shared_ptr<ArbiterState> state) : _state(std::move(state)) {}

	private:
		std::shared_ptr<ArbiterState> _state;
		std::vector<uint64_t> _hierarchicalEnabledStates;
	};


	class TweakerGroup : public IWidget
	{
	public:
		LayedOutWidgets _layedOutWidgets;
		CommonWidgets::HoveringLayer _hoverings;
		std::shared_ptr<ITweakableDocumentInterface> _docInterface;

		void    Render(IOverlayContext& context, Layout& layout, Interactables& interactables, InterfaceState& interfaceState) override
		{
			Rect container = layout.GetMaximumSize();
			container._topLeft += Coord2{layout._paddingInternalBorder, layout._paddingInternalBorder};
			container._bottomRight -= Coord2{layout._paddingInternalBorder, layout._paddingInternalBorder};

			if (_docInterface->GetArbiterState()->IsLayoutInvalidated()) {
				_docInterface->GetArbiterState()->ResetLayout();
				WidgetsLayoutFormatter formatter{_docInterface->GetArbiterState()};
				formatter.BeginRoot({container.Width(), container.Height()});
				_docInterface->ExecuteOnFormatter(formatter);
				formatter.EndRoot();

				_layedOutWidgets = formatter.BuildLayedOutWidgets();
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

		ProcessInputResult    ProcessInput(InterfaceState& interfaceState, const PlatformRig::InputSnapshot& input) override
		{
			ProcessInputResult pir = ProcessInputResult::Passthrough;
			if (_docInterface->TryLock()) {
				TRY {
					CommonWidgets::Input widgets{interfaceState, input, _hoverings};
					auto q = _layedOutWidgets.ProcessInput(widgets);
					pir = q == LayedOutWidgets::ProcessInputResult::Consumed ? ProcessInputResult::Consumed : ProcessInputResult::Passthrough;
					
					if (_docInterface->GetArbiterState()->IsModelInvalidated()) {
						_docInterface->IncreaseValidationIndex();
						_docInterface->GetArbiterState()->ResetModel();
					}
				} CATCH(...) {
					_docInterface->Unlock();
					throw;
				} CATCH_END
				_docInterface->Unlock();
			}

			return pir;
		}

		TweakerGroup(std::shared_ptr<ITweakableDocumentInterface> doc)
		: _docInterface(std::move(doc)) {}
	};

	std::shared_ptr<RenderOverlays::DebuggingDisplay::IWidget> CreateWidgetGroup(std::shared_ptr<ITweakableDocumentInterface> doc)
	{
		return std::make_shared<TweakerGroup>(doc);
	}
}

