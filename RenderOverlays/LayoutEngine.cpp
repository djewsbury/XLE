// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "LayoutEngine.h"
#include "CommonWidgets.h"

#include "../Foreign/yoga/yoga/YGNode.h"

namespace RenderOverlays
{
	using namespace DebuggingDisplay;

	static Rect TransformRect(const Float3x3& transform, const Rect& input)
	{
		auto topLeft = transform * Float3(input._topLeft, 1.f);
		auto bottomRight = transform * Float3(input._bottomRight, 1.f);
		return { Truncate(topLeft), Truncate(bottomRight) };
	}

	void LayedOutWidgets::Draw(CommonWidgets::Draw& draw, const Float3x3& transform)
	{
		auto i = _nodeAttachments.begin();
		auto i2 = _layedOutLocations.begin();
		for (;i!=_nodeAttachments.end(); ++i, ++i2) {
			auto frame = TransformRect(transform, i2->first);
			auto content = TransformRect(transform, i2->second);
			if (i->_drawDelegate)
				i->_drawDelegate(draw, frame, content);

			if (i->_ioDelegate)
				draw.GetInteractables().Register({content, i->GetGuid()});
		}
	}

	auto LayedOutWidgets::ProcessInput(const PlatformRig::InputContext& inputContext, const OSServices::InputSnapshot& evnt, const Float3x3& transform) -> PlatformRig::ProcessInputResult
	{
		auto* interfaceState = inputContext.GetService<DebuggingDisplay::InterfaceState>();
		auto topMostId = interfaceState->TopMostId();
		auto i = _nodeAttachments.rbegin();		// doing input in reverse order to drawing
		auto i2 = _layedOutLocations.rbegin();
		for (;i!=_nodeAttachments.rend(); ++i, ++i2)
			if (i->_ioDelegate && i->GetGuid() == topMostId) {
				auto frame = TransformRect(transform, i2->first);
				auto content = TransformRect(transform, i2->second);
				auto result = i->_ioDelegate(inputContext, evnt, frame, content);
				if (result != PlatformRig::ProcessInputResult::Passthrough)
					return result;
			}

		return PlatformRig::ProcessInputResult::Passthrough;
	}

	void LayoutEngine::InsertChildToStackTop(YGNodeRef node)
	{
		assert(!_workingStack.empty());
		YGNodeInsertChild(_workingStack.top(), node, YGNodeGetChildCount(_workingStack.top()));
	}

	void LayoutEngine::PushNode(YGNodeRef node)
	{
		_workingStack.push(node);
	}

	void LayoutEngine::PopNode()
	{
		_workingStack.pop();
	}

	void LayoutEngine::PushRoot(YGNodeRef node, Coord2 containerSize)
	{
		_workingStack.push(node);
		_roots.emplace_back(node, containerSize);
	}

	YGNodeRef LayoutEngine::NewNode()
	{
		auto ptr = MakeUniqueYogaNode();		// consider having a shared config
		auto res = ptr.get();
		_retainedNodes.push_back(std::move(ptr));
		return res;
	}

	ImbuedNode* LayoutEngine::NewImbuedNode(uint64_t guid)
	{
		auto ptr = std::make_unique<ImbuedNode>(MakeUniqueYogaNode(), guid);
		auto res = ptr.get();
		_imbuedNodes.push_back(std::move(ptr));
		return res;
	}

	LayedOutWidgets LayoutEngine::BuildLayedOutWidgets()
	{
		// If you hit this, it means that a node was using PushNode, but not popped with PopNode. It probably means that
		// there's container type node that wasn't closed
		assert(_workingStack.empty());
		_guidStack.pop();
		assert(_guidStack.empty());
		assert(!_roots.empty());
		if (_roots.empty())
			return {};

		for (auto& n:_imbuedNodes)
			if (n->_measureDelegate) {
				n->YGNode()->setContext(n.get());
				n->YGNode()->setMeasureFunc(
					[](YGNode* node, float width, YGMeasureMode widthMode, float height, YGMeasureMode heightMode) -> YGSize {
						return ((ImbuedNode*)node->getContext())->_measureDelegate(width, widthMode, height, heightMode);
					});
			}

		LayedOutWidgets result;
		for (auto& r:_roots)
			YGNodeCalculateLayout(r.first, (float)r.second[0], (float)r.second[1], YGDirectionInherit); // YGDirectionLTR);

		result._layedOutLocations.reserve(_imbuedNodes.size());
		for (auto& n:_imbuedNodes)
			if (n->_nodeAttachments._drawDelegate) {
				auto ygNode = n->YGNode();
				Float2 topLeft { YGNodeLayoutGetLeft(ygNode), YGNodeLayoutGetTop(ygNode) };
				auto parent = YGNodeGetParent(ygNode);
				while (parent) {
					topLeft += Float2 { YGNodeLayoutGetLeft(parent), YGNodeLayoutGetTop(parent) };
					parent = YGNodeGetParent(parent);
				}
				Float2 bottomRight;
				bottomRight[0] = topLeft[0] + (int)YGNodeLayoutGetWidth(ygNode);
				bottomRight[1] = topLeft[1] + (int)YGNodeLayoutGetHeight(ygNode);

				// transform to final frame & content rect (floor to integer here)
				Rect frame { topLeft, bottomRight };
				Rect content {
					topLeft + Int2{ YGNodeLayoutGetPadding(ygNode, YGEdgeLeft), YGNodeLayoutGetPadding(ygNode, YGEdgeTop) },
					bottomRight - Int2{ YGNodeLayoutGetPadding(ygNode, YGEdgeRight), YGNodeLayoutGetPadding(ygNode, YGEdgeBottom) }
				};

				result._layedOutLocations.emplace_back(frame, content);
			} else {
				Rect zero { Coord2(0,0), Coord2(0,0) };
				result._layedOutLocations.emplace_back(zero, zero);
			}

		result._dimensions = {
			YGNodeLayoutGetWidth(_roots[0].first),
			YGNodeLayoutGetHeight(_roots[0].first)
		};

		result._nodeAttachments.reserve(_imbuedNodes.size());
		for (auto& n:_imbuedNodes)
			result._nodeAttachments.emplace_back(std::move(n->_nodeAttachments));
		_imbuedNodes.clear();

		_workingStack = {};
		_roots.clear();
		_guidStack.Reset();
		return result;
	}

	LayoutEngine::LayoutEngine() {}
	LayoutEngine::~LayoutEngine() {}

	YogaNodePtr MakeUniqueYogaNode()
	{
		return std::unique_ptr<YGNode, decltype(&YGNodeFree)>(YGNodeNew(), &YGNodeFree);
	}

}

