// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "LayoutEngine.h"
#include "CommonWidgets.h"

#include "../Foreign/yoga/yoga/Yoga.h"

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
			auto frame = TransformRect(transform, i2->_frame);
			auto content = TransformRect(transform, i2->_content);
			if (i->_drawDelegate)
				i->_drawDelegate(draw, frame, content);

			if (i->_ioDelegate)
				draw.GetInteractables().Register(content, i->GetGuid());
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
				auto frame = TransformRect(transform, i2->_frame);
				auto content = TransformRect(transform, i2->_content);
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

	YGNodeRef LayoutEngine::GetTopmostNode()
	{
		assert(!_workingStack.empty());
		return _workingStack.top();
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
		auto ptr = std::make_unique<ImbuedNode>(NewNode(), guid, unsigned(_roots.size()-1));
		auto res = ptr.get();
		_imbuedNodes.push_back(std::move(ptr));
		return res;
	}

	ImbuedNode* LayoutEngine::ImbueNode(YGNodeRef node, uint64_t guid)
	{
		auto ptr = std::make_unique<ImbuedNode>(node, guid, unsigned(_roots.size()-1));
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

	ImbuedNode* LayoutEngine::Find(uint64_t guid)
	{
		for (auto r=_imbuedNodes.rbegin(); r!=_imbuedNodes.rend(); ++r)
			if ((*r)->Guid() == guid)
				return r->get();
		return nullptr;
	}

	LayedOutWidgets LayoutEngine::BuildLayedOutWidgets(Coord2 offsetToOutput, std::optional<Rect> viewportRect, bool captureNodeLayouts)
	{
		// If you hit this, it means that a node was using PushNode, but not popped with PopNode. It probably means that
		// there's container type node that wasn't closed
		assert(_workingStack.empty());
		_guidStack.pop();
		assert(_guidStack.empty());
		if (_roots.empty())
			return {};

		for (auto& n:_imbuedNodes) {
			YGNodeSetContext(n->YGNode(), n.get());
			if (n->_measureDelegate)
				YGNodeSetMeasureFunc(
					n->YGNode(),
					[](YGNodeConstRef node, float width, YGMeasureMode widthMode, float height, YGMeasureMode heightMode) -> YGSize {
						return ((ImbuedNode*)YGNodeGetContext(node))->_measureDelegate(width, widthMode, height, heightMode);
					}
				);
		}

		LayedOutWidgets result;
		for (auto& r:_roots)
			YGNodeCalculateLayout(r.first, (float)r.second.Width(), (float)r.second.Height(), YGDirectionInherit); // YGDirectionLTR);

		result._layedOutLocations.reserve(_imbuedNodes.size());
		for (auto& n:_imbuedNodes) {

			auto ygNode = n->YGNode();
			int zOrder = n->_zorderFixed.value_or(n->_zorderRel);
			bool fixedZOrder = n->_zorderFixed.has_value();
			Float2 topLeft { YGNodeLayoutGetLeft(ygNode), YGNodeLayoutGetTop(ygNode) };
			auto parent = YGNodeGetParent(ygNode);
			while (parent) {
				if (auto* context = (ImbuedNode*)YGNodeGetContext(parent)) {
					if (!fixedZOrder) zOrder += context->_zorderFixed.value_or(context->_zorderRel);
					fixedZOrder |= context->_zorderFixed.has_value();
				}
				topLeft += Float2 { YGNodeLayoutGetLeft(parent), YGNodeLayoutGetTop(parent) };
				parent = YGNodeGetParent(parent);
			}
			topLeft += _roots[n->_rootIndex].second._topLeft;
			topLeft += offsetToOutput;
			Float2 bottomRight;
			bottomRight[0] = topLeft[0] + (int)YGNodeLayoutGetWidth(ygNode);
			bottomRight[1] = topLeft[1] + (int)YGNodeLayoutGetHeight(ygNode);

			// transform to final frame & content rect (floor to integer here)
			Rect frame { topLeft, bottomRight };
			Rect content {
				topLeft + Int2{ YGNodeLayoutGetPadding(ygNode, YGEdgeLeft), YGNodeLayoutGetPadding(ygNode, YGEdgeTop) },
				bottomRight - Int2{ YGNodeLayoutGetPadding(ygNode, YGEdgeRight), YGNodeLayoutGetPadding(ygNode, YGEdgeBottom) }
			};

			result._layedOutLocations.emplace_back(LayedOutWidgets::LayedOutLocation{frame, content, zOrder});
			if (!viewportRect.has_value() || Intersects(*viewportRect, frame))
				if (n->_postCalculateDelegate)
					n->_postCalculateDelegate(ygNode, frame, content, zOrder);

		}

		result._dimensions = {
			YGNodeLayoutGetWidth(_roots[0].first),
			YGNodeLayoutGetHeight(_roots[0].first)
		};

		result._mins = Coord2 { std::numeric_limits<Coord>::max(), std::numeric_limits<Coord>::max() };
		result._maxs = Coord2 { std::numeric_limits<Coord>::min(), std::numeric_limits<Coord>::min() };
		auto childCount = YGNodeGetChildCount(_roots[0].first);
		if (childCount) {
			for (unsigned c=0; c<childCount; c++) {
				auto* child = YGNodeGetChild(_roots[0].first, c);
				Float2 topLeft { YGNodeLayoutGetLeft(child), YGNodeLayoutGetTop(child) };
				topLeft += Float2 { YGNodeLayoutGetLeft(_roots[0].first), YGNodeLayoutGetRight(_roots[0].first) };
				Float2 bottomRight = topLeft + Float2 { YGNodeLayoutGetWidth(child), YGNodeLayoutGetHeight(child) };
				result._mins[0] = std::min(result._mins[0], (Coord)topLeft[0]);
				result._mins[1] = std::min(result._mins[1], (Coord)topLeft[1]);
				result._maxs[0] = std::max(result._maxs[0], (Coord)bottomRight[0]);
				result._maxs[1] = std::max(result._maxs[1], (Coord)bottomRight[1]);
			}
			result._mins += offsetToOutput;
			result._maxs += offsetToOutput;
		}

		// We have to call BuildLayedOutNodes after we've called YGNodeCalculateLayout, but before we begin to tear our internal state down
		if (captureNodeLayouts)
			result._finalNodeState = BuildLayedOutNodes();

		result._nodeAttachments.reserve(_imbuedNodes.size());
		for (auto& n:_imbuedNodes)
			result._nodeAttachments.emplace_back(std::move(n->_nodeAttachments));
		_imbuedNodes.clear();

		_workingStack = {};
		_roots.clear();
		_guidStack.Reset();
		return result;
	}

	static void CaptureLaidOut_VisitNode(std::vector<LayedOutNode>& result, std::vector<std::pair<YGNodeRef, uint64_t>>& guidByNode, YGNodeRef node, unsigned parent)
	{
		LayedOutNode n;
		n._parent = parent;

		int zOrder = 0;
		bool fixedZOrder = false;
		if (auto* context = (ImbuedNode*)YGNodeGetContext(node)) {
			zOrder = context->_zorderFixed.value_or(context->_zorderRel);
			fixedZOrder = context->_zorderFixed.has_value();
		}

		Float2 topLeft { YGNodeLayoutGetLeft(node), YGNodeLayoutGetTop(node) };
		auto p = YGNodeGetParent(node);
		while (p) {
			if (auto* context = (ImbuedNode*)YGNodeGetContext(p)) {
				if (!fixedZOrder) zOrder += context->_zorderFixed.value_or(context->_zorderRel);
				fixedZOrder |= context->_zorderFixed.has_value();
			}

			topLeft += Float2 { YGNodeLayoutGetLeft(p), YGNodeLayoutGetTop(p) };
			p = YGNodeGetParent(p);
		}
		n._frame = Rect {
			(Coord)topLeft[0], (Coord)topLeft[1],
			(Coord)(topLeft[0] + YGNodeLayoutGetWidth(node)),
			(Coord)(topLeft[1] + YGNodeLayoutGetHeight(node))
		};

		if (auto gi = LowerBound(guidByNode, node); gi!=guidByNode.end() && gi->first == node)
			n._guid = gi->second;

		n._direction = (unsigned)YGNodeStyleGetDirection(node);
		n._flexDirection = (unsigned)YGNodeStyleGetFlexDirection(node);
		n._justifyContent = (unsigned)YGNodeStyleGetJustifyContent(node);
		n._alignItems = (unsigned)YGNodeStyleGetAlignItems(node);
		n._alignSelf = (unsigned)YGNodeStyleGetAlignSelf(node);
		n._flexWrap = (unsigned)YGNodeStyleGetFlexWrap(node);
		n._positionType = (unsigned)YGNodeStyleGetPositionType(node);
		n._overflow = (unsigned)YGNodeStyleGetOverflow(node);
		n._display = (unsigned)YGNodeStyleGetDisplay(node);
		n._flexGrow = YGNodeStyleGetFlexGrow(node);
		n._flexShrink = YGNodeStyleGetFlexShrink(node);
		n._flexBasis = YGNodeStyleGetFlexBasis(node).value;
		n._width = YGNodeStyleGetWidth(node).value;
		n._height = YGNodeStyleGetHeight(node).value;
		n._minWidth = YGNodeStyleGetMinWidth(node).value;
		n._maxWidth = YGNodeStyleGetMaxWidth(node).value;
		n._minHeight = YGNodeStyleGetMinHeight(node).value;
		n._maxHeight = YGNodeStyleGetMaxHeight(node).value;
		n._margin[0] = YGNodeStyleGetMargin(node, YGEdgeLeft).value;
		n._margin[1] = YGNodeStyleGetMargin(node, YGEdgeTop).value;
		n._margin[2] = YGNodeStyleGetMargin(node, YGEdgeRight).value;
		n._margin[3] = YGNodeStyleGetMargin(node, YGEdgeBottom).value;
		n._padding[0] = YGNodeStyleGetPadding(node, YGEdgeLeft).value;
		n._padding[1] = YGNodeStyleGetPadding(node, YGEdgeTop).value;
		n._padding[2] = YGNodeStyleGetPadding(node, YGEdgeRight).value;
		n._padding[3] = YGNodeStyleGetPadding(node, YGEdgeBottom).value;
		n._position[0] = YGNodeStyleGetPosition(node, YGEdgeLeft).value;
		n._position[1] = YGNodeStyleGetPosition(node, YGEdgeTop).value;
		n._position[2] = YGNodeStyleGetPosition(node, YGEdgeRight).value;
		n._position[3] = YGNodeStyleGetPosition(node, YGEdgeBottom).value;
		n._columnGap = YGNodeStyleGetGap(node, YGGutterColumn);
		n._rowGap = YGNodeStyleGetGap(node, YGGutterRow);
		n._aspectRatio = YGNodeStyleGetAspectRatio(node);
		n._zorder = zOrder;

		result.emplace_back(std::move(n));
		auto idx = (unsigned)result.size() - 1;
		for (size_t c=0; c<YGNodeGetChildCount(node); ++c)
			CaptureLaidOut_VisitNode(result, guidByNode, YGNodeGetChild(node, c), idx);
	};

	std::vector<LayedOutNode> LayoutEngine::BuildLayedOutNodes()
	{
		std::vector<std::pair<YGNodeRef, uint64_t>> guidByNode;
		for (auto& n:_imbuedNodes)
			if (n->Guid() != 0)
				guidByNode.emplace(LowerBound(guidByNode, n->YGNode()), n->YGNode(), n->Guid());

		std::vector<LayedOutNode> result;
		for (auto& r:_roots) CaptureLaidOut_VisitNode(result, guidByNode, r.first, -1);
		return result;
	}

	LayoutEngine::LayoutEngine() {}
	LayoutEngine::~LayoutEngine() {}

	YogaNodePtr MakeUniqueYogaNode()
	{
		return std::unique_ptr<YGNode, decltype(&YGNodeFree)>(YGNodeNew(), &YGNodeFree);
	}

}

