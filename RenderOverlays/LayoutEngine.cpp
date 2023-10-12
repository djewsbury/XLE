// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "LayoutEngine.h"
#include "CommonWidgets.h"

#include "../Foreign/yoga/yoga/Yoga.h"
#include "../Foreign/yoga/yoga/YGNode.h"

namespace RenderOverlays
{
///////////////////////////////////////////////////////////////////////////////////

	ImmediateLayout::ImmediateLayout(const Rect& maximumSize, Direction direction)
	{
		_maximumSize = maximumSize;
		_direction = direction;
		_caret = 0;
		_paddingInternalBorder = 8;
		_paddingBetweenAllocations = 4;
	}

	ImmediateLayout::ImmediateLayout()
	{
		_maximumSize = Rect { {0,0}, {0,0} };
		_direction = Direction::Row;
		_caret = 0;
		_paddingInternalBorder = 8;
		_paddingBetweenAllocations = 4;
	}

	Rect    ImmediateLayout::Allocate(Coord size)
	{
		auto maxMainAxis = GetMaxMainAxis();
		auto maxY = std::min(_caret+size, maxMainAxis);

		Rect result;
		if (_direction == Direction::Row) {
			result._topLeft[0]        = _maximumSize._topLeft[0] + _paddingInternalBorder + _caret;
			result._bottomRight[0]    = _maximumSize._topLeft[0] + _paddingInternalBorder + maxY;
			result._topLeft[1]        = _maximumSize._topLeft[1] + _paddingInternalBorder;
			result._bottomRight[1]    = _maximumSize._bottomRight[1] - _paddingInternalBorder;
		} else {
			result._topLeft[0]        = _maximumSize._topLeft[0] + _paddingInternalBorder;
			result._bottomRight[0]    = _maximumSize._bottomRight[0] - _paddingInternalBorder;
			result._topLeft[1]        = _maximumSize._topLeft[1] + _paddingInternalBorder + _caret;
			result._bottomRight[1]    = _maximumSize._topLeft[1] + _paddingInternalBorder + maxY;
		}
		_caret = std::min(_caret+size+_paddingBetweenAllocations, maxMainAxis);

		return result;
	}

	Rect    ImmediateLayout::AllocateFraction(float proportionOfSize)
	{
		return Allocate(Coord(GetMaxMainAxis() * proportionOfSize));
	}

	Coord   ImmediateLayout::GetSpaceRemaining() const
	{
		return GetMaxMainAxis() - _caret;
	}

	Coord   ImmediateLayout::GetMaxMainAxis() const
	{
		auto maxMainAxis = (_direction == Direction::Row) ? _maximumSize.Width() : _maximumSize.Height();
		maxMainAxis = std::max(0, maxMainAxis - 2*_paddingInternalBorder);
		return maxMainAxis;
	}

	void ImmediateLayout::SetDirection(Direction dir)
	{
		if (_direction == dir)
			return;

		if (_direction == Direction::Row) {
			assert(dir == Direction::Column);
			_maximumSize._topLeft[0] += _caret;
			_caret = 0;
			_direction = Direction::Column;
		} else {
			assert(_direction == Direction::Column);
			assert(dir == Direction::Row);
			_maximumSize._topLeft[1] += _caret;
			_caret = 0;
			_direction = Direction::Row;
		}
	}

///////////////////////////////////////////////////////////////////////////////////

	static Rect TransformRect(const Float3x3& transform, const Rect& input)
	{
		auto topLeft = transform * Float3(input._topLeft, 1.f);
		auto bottomRight = transform * Float3(input._bottomRight, 1.f);
		return { Truncate(topLeft), Truncate(bottomRight) };
	}

	void LayedOutWidgets::Draw(DrawContext& draw, const Float3x3& transform)
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

	auto LayedOutWidgets::ProcessInput(IOContext& ioContext, const Float3x3& transform) -> PlatformRig::ProcessInputResult
	{
		auto* interfaceState = ioContext.GetInputContext().GetService<DebuggingDisplay::InterfaceState>();
		auto topMostId = interfaceState->TopMostId();
		auto i = _nodeAttachments.rbegin();		// doing input in reverse order to drawing
		auto i2 = _layedOutLocations.rbegin();
		for (;i!=_nodeAttachments.rend(); ++i, ++i2)
			if (i->_ioDelegate && i->GetGuid() == topMostId) {
				auto frame = TransformRect(transform, i2->first);
				auto content = TransformRect(transform, i2->second);
				auto result = i->_ioDelegate(ioContext, frame, content);
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

	void LayoutEngine::PushRoot(YGNodeRef node, Rect containerSize)
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
		auto ptr = std::make_unique<ImbuedNode>(MakeUniqueYogaNode(), guid, unsigned(_roots.size()-1));
		auto res = ptr.get();
		_imbuedNodes.push_back(std::move(ptr));
		return res;
	}

	YGNodeRef LayoutEngine::InsertNewNode()
	{
		auto* result = NewNode();
		InsertChildToStackTop(result);
		return result;
	}

	ImbuedNode* LayoutEngine::InsertNewImbuedNode(uint64_t guid)
	{
		auto* result = NewImbuedNode(guid);
		InsertChildToStackTop(*result);
		return result;
	}

	YGNodeRef LayoutEngine::InsertAndPushNewNode()
	{
		auto* result = NewNode();
		InsertChildToStackTop(result);
		PushNode(result);
		return result;
	}

	ImbuedNode* LayoutEngine::InsertAndPushNewImbuedNode(uint64_t guid)
	{
		auto* result = NewImbuedNode(guid);
		InsertChildToStackTop(*result);
		PushNode(*result);
		return result;
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
			YGNodeCalculateLayout(r.first, (float)r.second.Width(), (float)r.second.Height(), YGDirectionInherit); // YGDirectionLTR);

		result._layedOutLocations.reserve(_imbuedNodes.size());
		for (auto& n:_imbuedNodes)
			if (n->_nodeAttachments._drawDelegate || n->_postCalculateDelegate) {
				auto ygNode = n->YGNode();
				Float2 topLeft { YGNodeLayoutGetLeft(ygNode), YGNodeLayoutGetTop(ygNode) };
				auto parent = YGNodeGetParent(ygNode);
				while (parent) {
					topLeft += Float2 { YGNodeLayoutGetLeft(parent), YGNodeLayoutGetTop(parent) };
					parent = YGNodeGetParent(parent);
				}
				topLeft += _roots[n->_rootIndex].second._topLeft;
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

				if (n->_postCalculateDelegate)
					n->_postCalculateDelegate(ygNode, frame, content);

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

