// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "LayoutEngine.h"
#include "CommonWidgets.h"

namespace RenderOverlays
{
	using namespace DebuggingDisplay;

	void LayedOutWidgets::Draw(CommonWidgets::Draw& draw)
	{
		auto i = _nodeAttachments.begin();
		auto i2 = _layedOutLocations.begin();
		for (;i!=_nodeAttachments.end(); ++i, ++i2) {
			if (i->_drawDelegate)
				i->_drawDelegate(draw, i2->first, i2->second);

			if (i->_ioDelegate)
				draw.GetInteractables().Register({i2->second, i->GetGuid()});
		}
	}

	auto LayedOutWidgets::ProcessInput(CommonWidgets::Input& input) -> ProcessInputResult
	{
		auto topMostId = input.GetInterfaceState().TopMostId();
		auto i = _nodeAttachments.rbegin();		// doing input in reverse order to drawing
		auto i2 = _layedOutLocations.rbegin();
		for (;i!=_nodeAttachments.rend(); ++i, ++i2)
			if (i->_ioDelegate && i->GetGuid() == topMostId) {
				auto result = i->_ioDelegate(input, i2->first, i2->second);
				if (result == IODelegateResult::Consumed)
					return ProcessInputResult::Consumed;
			}

		return ProcessInputResult::Passthrough;
	}

	void LayoutEngine::InsertChildToStackTop(YGNodeRef node)
	{
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

	void LayoutEngine::PushRoot(YGNodeRef node)
	{
		_workingStack.push(node);
		_roots.push_back(node);
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

	LayedOutWidgets LayoutEngine::BuildLayedOutWidgets(Rect container)
	{
		assert(_workingStack.empty());
		_guidStack.pop();
		assert(_guidStack.empty());

		LayedOutWidgets result;
		for (auto& r:_roots)
			YGNodeCalculateLayout(r, (float)container.Width(), (float)container.Height(), YGDirectionInherit); // YGDirectionLTR);

		result._layedOutLocations.reserve(_imbuedNodes.size());
		for (auto& n:_imbuedNodes)
			if (n->_nodeAttachments._drawDelegate) {
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
				frame._bottomRight[0] = frame._topLeft[0] + (int)YGNodeLayoutGetWidth(ygNode);
				frame._bottomRight[1] = frame._topLeft[1] + (int)YGNodeLayoutGetHeight(ygNode);

				Rect content = frame;
				content._topLeft += Coord2{ YGNodeLayoutGetPadding(ygNode, YGEdgeLeft), YGNodeLayoutGetPadding(ygNode, YGEdgeTop) };
				content._bottomRight -= Coord2{ YGNodeLayoutGetPadding(ygNode, YGEdgeRight), YGNodeLayoutGetPadding(ygNode, YGEdgeBottom) };

				result._layedOutLocations.emplace_back(frame, content);
			}

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

