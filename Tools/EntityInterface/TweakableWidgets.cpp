// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "TweakableEntityDocument.h"
#include "TweakableEntityDocumentInternal.h"
#include "../../RenderOverlays/CommonWidgets.h"
#include "../../RenderOverlays/DebuggingDisplay.h"
#include "../../Foreign/yoga/yoga/Yoga.h"
#include <vector>
#include <set>
#include <stack>

namespace EntityInterface
{
    using namespace RenderOverlays;
    using namespace RenderOverlays::DebuggingDisplay;
    using YogaNodePtr = std::unique_ptr<YGNode, decltype(&YGNodeFree)>;

	static YogaNodePtr MakeUniqueYogaNode()
	{
		return std::unique_ptr<YGNode, decltype(&YGNodeFree)>(YGNodeNew(), &YGNodeFree);
	}

    enum class IODelegateResult { Passthrough, ChangedValue, Consumed };

	class ImbuedNode
	{
	public:
		std::function<void(CommonWidgets::Draw&, Rect frame, Rect content)> _drawDelegate;
		std::function<IODelegateResult(CommonWidgets::Input&, Rect frame, Rect content)> _ioDelegate;

		YGNodeRef YGNode() { return _ygNode.get(); }
		uint64_t GetGuid() const { return _guid; }

		ImbuedNode(YogaNodePtr&& ygNode, uint64_t guid)
		: _ygNode(std::move(ygNode))
		, _guid(guid)
		{}

	private:
		YogaNodePtr _ygNode;
		uint64_t _guid;
	};

	class LayedOutWidgets
	{
	public:
		std::vector<std::pair<Rect, Rect>> _layedOutLocations;
		std::vector<std::unique_ptr<ImbuedNode>> _imbuedNodes;
		std::set<uint64_t> _valuesImpactingLayout;

		void Draw(CommonWidgets::Draw& draw)
		{
			auto i = _imbuedNodes.begin();
			auto i2 = _layedOutLocations.begin();
			for (;i!=_imbuedNodes.end(); ++i, ++i2) {
				if ((*i)->_drawDelegate)
					(*i)->_drawDelegate(draw, i2->first, i2->second);

				if ((*i)->_ioDelegate)
					draw.GetInteractables().Register({i2->second, (*i)->GetGuid()});
			}
		}

		ProcessInputResult ProcessInput(CommonWidgets::Input& input)
		{
			auto topMostId = input.GetInterfaceState().TopMostId();
			auto i = _imbuedNodes.rbegin();		// doing input in reverse order to drawing
			auto i2 = _layedOutLocations.rbegin();
			bool consumed = false;
			for (;i!=_imbuedNodes.rend(); ++i, ++i2)
				if ((*i)->_ioDelegate && (*i)->GetGuid() == topMostId) {
					auto result = (*i)->_ioDelegate(input, i2->first, i2->second);
					switch (result) {
					case IODelegateResult::Consumed: consumed = true; break;
					case IODelegateResult::Passthrough: break;
					case IODelegateResult::ChangedValue:
						if (_valuesImpactingLayout.find((*i)->GetGuid()) != _valuesImpactingLayout.end())
							input._redoLayout = true;
						input._madeChange = true;
						break;
					}
				}
			return consumed ? ProcessInputResult::Consumed : ProcessInputResult::Passthrough;
		}
	};

	
	static const DebuggingDisplay::KeyId enter      = PlatformRig::KeyId_Make("enter");
	static const DebuggingDisplay::KeyId escape     = PlatformRig::KeyId_Make("escape");

	class WidgetsLayoutFormatter : public EntityInterface::IWidgetsLayoutFormatter
	{
	public:
		YGNodeRef NewNode()
		{
			auto ptr = MakeUniqueYogaNode();		// consider having a shared config
			auto res = ptr.get();
			_retainedNodes.push_back(std::move(ptr));
			return res;
		}

		ImbuedNode* NewImbuedNode(uint64_t guid)
		{
			auto ptr = std::make_unique<ImbuedNode>(MakeUniqueYogaNode(), guid);
			auto res = ptr.get();
			_imbuedNodes.push_back(std::move(ptr));
			return res;
		}

		ImbuedNode* BeginSharedLeftRightCtrl(StringSection<> name, uint64_t interactable)
		{
			const auto lineHeight = baseLineHeight+4;
			auto baseNode = NewNode();
			YGNodeStyleSetWidthPercent(baseNode, 100.0f);
			YGNodeStyleSetHeight(baseNode, lineHeight+4);
			YGNodeStyleSetAlignItems(baseNode, YGAlignCenter);
			YGNodeStyleSetFlexDirection(baseNode, YGFlexDirectionRow);
			YGNodeStyleSetMargin(baseNode, YGEdgeAll, 2);
			YGNodeInsertChild(_workingStack.top(), baseNode, YGNodeGetChildCount(_workingStack.top()));
			_workingStack.push(baseNode);

			auto mainCtrl = NewImbuedNode(interactable);
			YGNodeStyleSetFlexGrow(mainCtrl->YGNode(), 1.0f);
			YGNodeStyleSetHeightPercent(mainCtrl->YGNode(), 100.f);
			YGNodeStyleSetMargin(mainCtrl->YGNode(), YGEdgeAll, 2);
			mainCtrl->_drawDelegate = [nameStr=name.AsString(), state=_state, interactable](CommonWidgets::Draw& draw, Rect frame, Rect content) {
				draw.LeftRight(frame, interactable, nameStr, state->GetWorkingValueAsString(interactable));
			};
			YGNodeInsertChild(_workingStack.top(), mainCtrl->YGNode(), YGNodeGetChildCount(_workingStack.top()));
			return mainCtrl;
		}

		template<typename Type>
			void WriteHalfDoubleTemplate(StringSection<> name, Type initialValue, Type minValue, Type maxValue)
		{
			uint64_t interactable = _guidStack.MakeGuid(name);
			
			auto enabledByHierarchy = EnabledByHierarchy();
			if (enabledByHierarchy == HierarchicalEnabledState::EnableChildren || _state->IsEnabled(interactable)) {
				_state->InitializeValue(interactable, initialValue);
			
				auto mainCtrl = BeginSharedLeftRightCtrl(name, interactable);
				mainCtrl->_ioDelegate = [interactable, state=_state, minValue, maxValue](CommonWidgets::Input& input, Rect frame, Rect content) {
					bool leftSide = input.GetEvent()._mousePosition[0] < (frame._topLeft[0]+frame._bottomRight[0])/2;
					if (input.GetEvent().IsRelease_LButton()) {
						auto currentValue = state->GetWorkingValue<Type>(interactable);
						auto newValue = currentValue;
						if (leftSide) 	newValue = std::max(minValue, currentValue/2);
						else 			newValue = std::min(maxValue, currentValue*2);
						if (newValue != currentValue) {
							state->SetWorkingValue(interactable, newValue);
							return IODelegateResult::ChangedValue;
						}
					}
					return IODelegateResult::Passthrough;
				};

				if (enabledByHierarchy == HierarchicalEnabledState::NoImpact)
					DeactivateButton(interactable);
				_workingStack.pop();

			} else {
				DisabledStateButton(interactable, name, enabledByHierarchy);
			}
		}

		void WriteHalfDoubleInt(StringSection<> name, int64_t initialValue, int64_t min, int64_t max) override { WriteHalfDoubleTemplate(name, initialValue, min, max); }
		void WriteHalfDoubleFloat(StringSection<> name, float initialValue, float min, float max) override { WriteHalfDoubleTemplate(name, initialValue, min, max); }

		template<typename Type>
			void WriteDecrementIncrementTemplate(StringSection<> name, Type initialValue, Type minValue, Type maxValue)
		{
			uint64_t interactable = _guidStack.MakeGuid(name);
			
			auto enabledByHierarchy = EnabledByHierarchy();
			if (enabledByHierarchy == HierarchicalEnabledState::EnableChildren || _state->IsEnabled(interactable)) {
				_state->InitializeValue(interactable, initialValue);
			
				auto mainCtrl = BeginSharedLeftRightCtrl(name, interactable);
				mainCtrl->_ioDelegate = [interactable, state=_state, minValue, maxValue](CommonWidgets::Input& input, Rect frame, Rect content) {
					bool leftSide = input.GetEvent()._mousePosition[0] < (frame._topLeft[0]+frame._bottomRight[0])/2;
					if (input.GetEvent().IsRelease_LButton()) {
						auto currentValue = state->GetWorkingValue<Type>(interactable);
						auto newValue = currentValue;
						if (leftSide) 	newValue = std::max(minValue, currentValue-1);
						else 			newValue = std::min(maxValue, currentValue+1);
						if (newValue != currentValue) {
							state->SetWorkingValue(interactable, newValue);
							return IODelegateResult::ChangedValue;
						}
					}
					return IODelegateResult::Passthrough;
				};

				if (enabledByHierarchy == HierarchicalEnabledState::NoImpact)
					DeactivateButton(interactable);
				_workingStack.pop();

			} else {
				DisabledStateButton(interactable, name, enabledByHierarchy);
			}
		}

		void WriteDecrementIncrementInt(StringSection<> name, int64_t initialValue, int64_t min, int64_t max) override { WriteDecrementIncrementTemplate(name, initialValue, min, max); }
		void WriteDecrementIncrementFloat(StringSection<> name, float initialValue, float min, float max) override { WriteDecrementIncrementTemplate(name, initialValue, min, max); }

		void HorizontalControlLabel(StringSection<> name)
		{
			auto label = NewImbuedNode(0);
			YGNodeStyleSetWidth(label->YGNode(), 200);
			YGNodeStyleSetHeightPercent(label->YGNode(), 100.f);
			YGNodeInsertChild(_workingStack.top(), label->YGNode(), YGNodeGetChildCount(_workingStack.top()));
			label->_drawDelegate = [nameStr=name.AsString()](CommonWidgets::Draw& draw, Rect frame, Rect content) {
				DrawText().Draw(draw.GetContext(), content, nameStr);
			};
		}

		void WriteHorizontalCombo(StringSection<> name, int64_t initialValue, IteratorRange<const std::pair<int64_t, const char*>*> options) override
		{
			uint64_t interactable = _guidStack.MakeGuid(name);
			const auto lineHeight = baseLineHeight+4;

			auto enabledByHierarchy = EnabledByHierarchy();
			if (enabledByHierarchy == HierarchicalEnabledState::EnableChildren || _state->IsEnabled(interactable)) {
				_state->InitializeValue(interactable, initialValue);

				auto baseNode = NewNode();
				YGNodeStyleSetWidthPercent(baseNode, 100.0f);
				YGNodeStyleSetHeight(baseNode, lineHeight+4);
				YGNodeStyleSetAlignItems(baseNode, YGAlignCenter);
				YGNodeStyleSetFlexDirection(baseNode, YGFlexDirectionRow);
				YGNodeStyleSetMargin(baseNode, YGEdgeAll, 2);
				YGNodeInsertChild(_workingStack.top(), baseNode, YGNodeGetChildCount(_workingStack.top()));
				_workingStack.push(baseNode);

				HorizontalControlLabel(name);

				for (unsigned c=0; c<options.size(); ++c) {
					auto node = NewImbuedNode(interactable+1+c);
					YGNodeStyleSetFlexGrow(node->YGNode(), 1.0f);
					YGNodeStyleSetHeightPercent(node->YGNode(), 100.f);
					YGNodeInsertChild(_workingStack.top(), node->YGNode(), YGNodeGetChildCount(_workingStack.top()));
					Corner::BitField corners = 0;
					if (c == 0) corners |= Corner::TopLeft|Corner::BottomLeft;
					if ((c+1) == options.size()) corners |= Corner::TopRight|Corner::BottomRight;
					node->_drawDelegate = [nameStr=std::string{options[c].second}, corners, state=_state, value=options[c].first, interactable](CommonWidgets::Draw& draw, Rect frame, Rect content) {
						bool selected = state->GetWorkingValue<int64_t>(interactable) == value;
						OutlineRoundedRectangle(draw.GetContext(), frame, selected ? ColorB{96, 96, 96} : ColorB{64, 64, 64}, 1.f, 0.4f, corners);
						DrawText().Alignment(TextAlignment::Center).Draw(draw.GetContext(), content, nameStr);
					};
					node->_ioDelegate = [interactable, state=_state, value=options[c].first](CommonWidgets::Input& input, Rect, Rect) {
						if (input.GetEvent().IsRelease_LButton()) {
							state->SetWorkingValue(interactable, value);
							return IODelegateResult::ChangedValue;
						}
						return IODelegateResult::Consumed;
					};
				}

				if (enabledByHierarchy == HierarchicalEnabledState::NoImpact)
					DeactivateButton(interactable);
				_workingStack.pop();

			} else {
				DisabledStateButton(interactable, name, enabledByHierarchy);
			}
		}

		void BeginCheckboxControl(StringSection<> name, uint64_t interactable)
		{
			const auto lineHeight = baseLineHeight+4;
			auto baseNode = NewNode();
			YGNodeStyleSetWidthPercent(baseNode, 100.0f);
			YGNodeStyleSetHeight(baseNode, lineHeight+4);
			YGNodeStyleSetAlignItems(baseNode, YGAlignCenter);
			YGNodeStyleSetFlexDirection(baseNode, YGFlexDirectionRow);
			YGNodeStyleSetMargin(baseNode, YGEdgeAll, 2);
			YGNodeInsertChild(_workingStack.top(), baseNode, YGNodeGetChildCount(_workingStack.top()));
			_workingStack.push(baseNode);

			HorizontalControlLabel(name);

			auto stateBox = NewImbuedNode(interactable);
			YGNodeStyleSetWidth(stateBox->YGNode(), 16);
			YGNodeStyleSetHeight(stateBox->YGNode(), 16);
			YGNodeInsertChild(_workingStack.top(), stateBox->YGNode(), YGNodeGetChildCount(_workingStack.top()));
			stateBox->_drawDelegate = [interactable, state=_state](CommonWidgets::Draw& draw, Rect frame, Rect content) {
				draw.CheckBox(content, state->GetWorkingValue<bool>(interactable));
			};
			stateBox->_ioDelegate = [interactable, state=_state](CommonWidgets::Input& input, Rect, Rect) {
				if (input.GetEvent().IsRelease_LButton()) {
					state->SetWorkingValue(interactable, !state->GetWorkingValue<bool>(interactable));
					return IODelegateResult::ChangedValue;
				}
				return IODelegateResult::Consumed;
			};
		}

		bool GetCheckbox(StringSection<> name, bool initialValue) override
		{
			uint64_t interactable = _guidStack.MakeGuid(name);
			BeginCheckboxControl(name, interactable);
			_workingStack.pop();
			_state->InitializeValue(interactable, initialValue);
			_valuesImpactingLayout.insert(interactable);
			return _state->GetWorkingValue<bool>(interactable);
		}

		void WriteCheckbox(StringSection<> name, bool initialValue) override
		{
			uint64_t interactable = _guidStack.MakeGuid(name);

			auto enabledByHierarchy = EnabledByHierarchy();
			if (enabledByHierarchy == HierarchicalEnabledState::EnableChildren || _state->IsEnabled(interactable)) {
				_state->InitializeValue(interactable, initialValue);
				BeginCheckboxControl(name, interactable);
				if (enabledByHierarchy == HierarchicalEnabledState::NoImpact)
					DeactivateButton(interactable);
				_workingStack.pop();
			} else {
				DisabledStateButton(interactable, name, enabledByHierarchy);
			}
		}

		static constexpr unsigned baseLineHeight = 20;

		void DeactivateButton(uint64_t ctrlGuid)
		{
			auto newNode = NewImbuedNode(ctrlGuid+32);
			YGNodeStyleSetWidth(newNode->YGNode(), 12);
			YGNodeStyleSetHeight(newNode->YGNode(), 12);
			YGNodeStyleSetMargin(newNode->YGNode(), YGEdgeAll, 2);
			YGNodeStyleSetMarginAuto(newNode->YGNode(), YGEdgeLeft);
			YGNodeInsertChild(_workingStack.top(), newNode->YGNode(), YGNodeGetChildCount(_workingStack.top()));

			newNode->_drawDelegate = [](CommonWidgets::Draw& draw, Rect frame, Rect content) {
				draw.XToggleButton(frame);
			};

			newNode->_ioDelegate = [ctrlGuid, state=_state](CommonWidgets::Input& input, Rect, Rect) {
				if (input.GetEvent().IsRelease_LButton()) {
					state->ToggleEnable(ctrlGuid);
					input._redoLayout = true;
					return IODelegateResult::ChangedValue;
				}
				return IODelegateResult::Consumed;
			};
		}

		void DisabledStateButton(uint64_t interactable, StringSection<> name, HierarchicalEnabledState hierarchyState)
		{
			const auto lineHeight = baseLineHeight+4;
			auto baseNode = NewImbuedNode(interactable);
			YGNodeStyleSetMargin(baseNode->YGNode(), YGEdgeAll, 2);
			YGNodeStyleSetFlexGrow(baseNode->YGNode(), 1.0f);
			YGNodeStyleSetHeight(baseNode->YGNode(), lineHeight+4);
			YGNodeInsertChild(_workingStack.top(), baseNode->YGNode(), YGNodeGetChildCount(_workingStack.top()));

			if (hierarchyState == HierarchicalEnabledState::NoImpact) {
				baseNode->_drawDelegate = [nameStr=name.AsString()](CommonWidgets::Draw& draw, Rect frame, Rect content) {
					draw.DisabledStateControl(frame, nameStr);
				};

				baseNode->_ioDelegate = [interactable, state=_state](CommonWidgets::Input& input, Rect, Rect) {
					if (input.GetEvent().IsRelease_LButton()) {
						state->ToggleEnable(interactable);
						input._redoLayout = true;
						return IODelegateResult::ChangedValue;
					}
					return IODelegateResult::Consumed;
				};
			} else {
				baseNode->_drawDelegate = [nameStr=name.AsString()](CommonWidgets::Draw& draw, Rect frame, Rect content) {
					DrawText().Color({0x5f, 0x5f, 0x5f}).Alignment(TextAlignment::Center).Draw(draw.GetContext(), content, nameStr);
				};
			}
		}

		template<typename Type>
			void WriteBoundedTemplate(StringSection<> name, Type initialValue, Type leftSideValue, Type rightSideValue)
		{
			uint64_t interactable = _guidStack.MakeGuid(name);
			const auto lineHeight = baseLineHeight+4;
			
			auto enabledByHierarchy = EnabledByHierarchy();
			if (enabledByHierarchy == HierarchicalEnabledState::EnableChildren || _state->IsEnabled(interactable)) {
				_state->InitializeValue(interactable, initialValue);

				auto baseNode = NewNode();
				YGNodeStyleSetWidthPercent(baseNode, 100.0f);
				YGNodeStyleSetHeight(baseNode, lineHeight+4);
				YGNodeStyleSetAlignItems(baseNode, YGAlignCenter);
				YGNodeStyleSetFlexDirection(baseNode, YGFlexDirectionRow);
				YGNodeStyleSetMargin(baseNode, YGEdgeAll, 2);
				YGNodeInsertChild(_workingStack.top(), baseNode, YGNodeGetChildCount(_workingStack.top()));
				_workingStack.push(baseNode);

				auto sliderNode = NewImbuedNode(interactable);
				YGNodeStyleSetFlexGrow(sliderNode->YGNode(), 1.0f);
				YGNodeStyleSetHeightPercent(sliderNode->YGNode(), 100.f);
				YGNodeStyleSetMargin(sliderNode->YGNode(), YGEdgeAll, 2);
				sliderNode->_drawDelegate = [nameStr=name.AsString(), state=_state, interactable, leftSideValue, rightSideValue](CommonWidgets::Draw& draw, Rect frame, Rect content) {
					draw.Bounded(frame, interactable, nameStr, state->GetWorkingValue<Type>(interactable), leftSideValue, rightSideValue);
				};
				sliderNode->_ioDelegate = [interactable, state=_state, leftSideValue, rightSideValue](CommonWidgets::Input& input, Rect frame, Rect content) {
					if (input.GetHoverings()._hoveringCtrl) {
						if ((input.GetEvent()._mouseButtonsTransition != 0) && input.GetInterfaceState().GetCapture()._widget._id == interactable && !IsInside(input.GetInterfaceState().GetCapture()._widget._rect, input.GetEvent()._mousePosition)) {
							input._madeChange |= state->TryUpdateValueFromString<Type>(interactable, input.GetHoverings()._textEntry._currentLine);
							input.GetInterfaceState().EndCapturing();
							input.GetHoverings()._hoveringCtrl = 0;
							return IODelegateResult::Consumed;
						} 

						if (input.GetEvent().IsPress(enter)) {
							input._madeChange |= state->TryUpdateValueFromString<Type>(interactable, input.GetHoverings()._textEntry._currentLine);
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
									input._madeChange = true;
								}
								if (input.GetEvent().IsRelease_LButton())
									input.GetInterfaceState().EndCapturing();
							}
						}
					}
					return IODelegateResult::Consumed;
				};
				YGNodeInsertChild(_workingStack.top(), sliderNode->YGNode(), YGNodeGetChildCount(_workingStack.top()));

				if (enabledByHierarchy == HierarchicalEnabledState::NoImpact)
					DeactivateButton(interactable);
				_workingStack.pop();

			} else {

				DisabledStateButton(interactable, name, enabledByHierarchy);
			
			}
		}

		void WriteBoundedInt(StringSection<> name, int64_t initialValue, int64_t leftSideValue, int64_t rightSideValue) override { WriteBoundedTemplate(name, initialValue, leftSideValue, rightSideValue); }
		void WriteBoundedFloat(StringSection<> name, float initialValue, float leftSideValue, float rightSideValue) override { WriteBoundedTemplate(name, initialValue, leftSideValue, rightSideValue); }

		bool BeginCollapsingContainer(StringSection<> name) override
		{
			uint64_t containerGuid = _guidStack.MakeGuid(name, "##collapsingcontainer");
			_guidStack.push(containerGuid);
			_hierarchicalEnabledStates.push_back(0);
			bool isOpen = _state->IsEnabled(containerGuid);

			auto outerNode = NewNode();
			YGNodeStyleSetPadding(outerNode, YGEdgeAll, 0);       // zero padding because the headerContainer and contentContainers have their own padding
			YGNodeStyleSetMargin(outerNode, YGEdgeAll, 0);
			YGNodeInsertChild(_workingStack.top(), outerNode, YGNodeGetChildCount(_workingStack.top()));

			{
				const auto headerHeight = 24u;
				auto headerContainer = NewImbuedNode(containerGuid);
				YGNodeStyleSetMargin(headerContainer->YGNode(), YGEdgeAll, 0);
				YGNodeStyleSetWidthPercent(headerContainer->YGNode(), 100.0f);
				YGNodeStyleSetHeight(headerContainer->YGNode(), headerHeight);
				YGNodeStyleSetAlignItems(headerContainer->YGNode(), YGAlignCenter);
				YGNodeStyleSetFlexDirection(headerContainer->YGNode(), YGFlexDirectionRow);
				YGNodeInsertChild(outerNode, headerContainer->YGNode(), YGNodeGetChildCount(outerNode));
				
				headerContainer->_drawDelegate = [nameStr=name.AsString(), isOpen](CommonWidgets::Draw& draw, Rect frame, Rect content) {
					draw.SectionHeader(content, nameStr, isOpen);
				};

				headerContainer->_ioDelegate = [containerGuid, state=_state](CommonWidgets::Input& input, Rect, Rect) {
					if (input.GetEvent().IsRelease_LButton()) {
						state->ToggleEnable(containerGuid);
						input._redoLayout = true;
						return IODelegateResult::ChangedValue;
					}
					return IODelegateResult::Passthrough;
				};
			}

			auto contentContainer = NewNode();
			if (isOpen)
				YGNodeStyleSetMargin(contentContainer, YGEdgeAll, 2);
			YGNodeInsertChild(outerNode, contentContainer, YGNodeGetChildCount(outerNode));

			_workingStack.push(contentContainer);       // upcoming nodes will go into the content container
			return isOpen;
		}

		void BeginContainer() override
		{
			uint64_t containerGuid = _guidStack.MakeGuid("##container");
			_guidStack.push(containerGuid);

			auto contentContainer = NewImbuedNode(containerGuid);
			YGNodeStyleSetMargin(contentContainer->YGNode(), YGEdgeAll, 8);
			YGNodeStyleSetPadding(contentContainer->YGNode(), YGEdgeAll, 2);
			YGNodeInsertChild(_workingStack.top(), contentContainer->YGNode(), YGNodeGetChildCount(_workingStack.top()));
			_workingStack.push(contentContainer->YGNode());

			contentContainer->_drawDelegate = [](CommonWidgets::Draw& draw, Rect frame, Rect content) {
				draw.RectangleContainer(frame);
			};

			DisabledStateButton(containerGuid, "Enable", EnabledByHierarchy());
			_hierarchicalEnabledStates.push_back(containerGuid);
		}

		void EndContainer() override
		{
			assert(!_guidStack.empty());
			assert(!_workingStack.empty());
			_guidStack.pop();
			_workingStack.pop();
			_hierarchicalEnabledStates.pop_back();
		}

		YGNodeRef BeginRoot()
		{
			auto windowNode = NewNode();
			_workingStack.push(windowNode);
			return windowNode;
		}

		void EndRoot()
		{
			auto* node = _workingStack.top();
			_workingStack.pop();
			_roots.push_back(node);
		}

		void Reset()
		{
			_workingStack = {};
			_roots.clear();
			_retainedNodes.clear();
			_imbuedNodes.clear();
			_guidStack.Reset();
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

		LayedOutWidgets BuildLayedOutWidgets(Rect container)
		{
			assert(_workingStack.empty());
			_guidStack.pop();
			assert(_guidStack.empty());

			LayedOutWidgets result;
			for (auto& r:_roots)
				YGNodeCalculateLayout(r, container.Width(), container.Height(), YGDirectionInherit); // YGDirectionLTR);

			result._layedOutLocations.reserve(_imbuedNodes.size());
			for (auto& n:_imbuedNodes)
				if (n->_drawDelegate) {
					auto ygNode = n->YGNode();
					Rect frame;
					frame._topLeft = { YGNodeLayoutGetLeft(ygNode), YGNodeLayoutGetTop(ygNode) };
					auto parent = YGNodeGetParent(ygNode);
					while (parent) {
						Coord2 parentTopLeft = { YGNodeLayoutGetLeft(parent), YGNodeLayoutGetTop(parent) };
						frame._topLeft += parentTopLeft;
						parent = YGNodeGetParent(parent);
					}
					frame._topLeft += container._topLeft;
					frame._bottomRight[0] = frame._topLeft[0] + YGNodeLayoutGetWidth(ygNode);
					frame._bottomRight[1] = frame._topLeft[1] + YGNodeLayoutGetHeight(ygNode);

					Rect content = frame;
					content._topLeft += Coord2{ YGNodeLayoutGetPadding(ygNode, YGEdgeLeft), YGNodeLayoutGetPadding(ygNode, YGEdgeTop) };
					content._bottomRight -= Coord2{ YGNodeLayoutGetPadding(ygNode, YGEdgeRight), YGNodeLayoutGetPadding(ygNode, YGEdgeBottom) };

					result._layedOutLocations.emplace_back(frame, content);
				}

			result._imbuedNodes = std::move(_imbuedNodes);
			result._valuesImpactingLayout = std::move(_valuesImpactingLayout);
			Reset();
			return result;
		}

		WidgetsLayoutFormatter(std::shared_ptr<ArbiterState> state) : _state(std::move(state)) {}

	private:
		std::stack<YGNodeRef> _workingStack;
		std::vector<YGNodeRef> _roots;
		std::shared_ptr<ArbiterState> _state;
		GuidStackHelper _guidStack;
		std::vector<uint64_t> _hierarchicalEnabledStates;
		std::set<uint64_t> _valuesImpactingLayout;

		std::vector<std::unique_ptr<ImbuedNode>> _imbuedNodes;
		std::vector<YogaNodePtr> _retainedNodes;
	};


    class TweakerGroup : public IWidget
	{
	public:
        LayedOutWidgets _layedOutWidgets;
		CommonWidgets::HoveringLayer _hoverings;
		bool _pendingLayout = true;
        std::shared_ptr<ITweakableDocumentInterface> _docInterface;

		void    Render(IOverlayContext& context, Layout& layout, Interactables& interactables, InterfaceState& interfaceState) override
		{
			if (_pendingLayout) {
				WidgetsLayoutFormatter formatter{_docInterface->GetArbiterState()};
				formatter.BeginRoot();
				_docInterface->ExecuteOnFormatter(formatter);
				formatter.EndRoot();

				Rect container = layout.GetMaximumSize();
				container._topLeft += Coord2{layout._paddingInternalBorder, layout._paddingInternalBorder};
				container._bottomRight -= Coord2{layout._paddingInternalBorder, layout._paddingInternalBorder};
				_layedOutWidgets = formatter.BuildLayedOutWidgets(container);
				_pendingLayout = false;
			}

			{
				CommonWidgets::Draw draw{context, interactables, interfaceState, _hoverings};
				_layedOutWidgets.Draw(draw);
			}
		}

		ProcessInputResult    ProcessInput(InterfaceState& interfaceState, const PlatformRig::InputSnapshot& input) override
		{
            ProcessInputResult pir = ProcessInputResult::Passthrough;
			if (_docInterface->TryLock()) {
                TRY {
                    CommonWidgets::Input widgets{interfaceState, input, _hoverings};
                    auto pir = _layedOutWidgets.ProcessInput(widgets);
                    
                    if (widgets._madeChange)
                        _docInterface->IncreaseValidationIndex();
                    if (widgets._redoLayout)
                        _pendingLayout = true;
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

