// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once
#include "OverlayPrimitives.h"
#include "../PlatformRig/InputContext.h"		// for ProcessInputResult
#include "../Math/Matrix.h"
#include "../Utility/StringUtils.h"
#include "../Utility/MemoryUtils.h"
#include <stack>
#include <vector>
#include <memory>

namespace PlatformRig { class InputContext; }
namespace OSServices { class InputSnapshot; }

struct YGNode;
typedef struct YGNode* YGNodeRef;
struct YGSize;
extern "C" { void YGNodeFree(const YGNodeRef); }
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmicrosoft-enum-forward-reference"
enum YGMeasureMode;
#pragma GCC diagnostic pop

namespace RenderOverlays
{

	struct ImmediateLayout /////////////////////////////////////////////////////////////////////
	{
		Rect    _maximumSize;
		Coord   _caret;
		Coord   _paddingInternalBorder;
		Coord   _paddingBetweenAllocations;

		enum Direction { Row, Column };
		Direction _direction = Direction::Row;

		ImmediateLayout(const Rect& maximumSize, Direction direction = Direction::Row);
		ImmediateLayout();
		Rect    Allocate(Coord size);
		Rect    AllocateFraction(float proportionOfSize);
		Coord   GetSpaceRemaining() const;
		Coord   GetMaxMainAxis() const;
		Rect    GetMaximumSize() const { return _maximumSize; }
		Direction GetDirection() const { return _direction; }
		void    SetDirection(Direction dir);

		// ----- legacy interface --- (prefer methods above)
		Rect    AllocateFullWidth(Coord height)
		{
			SetDirection(Direction::Column);
			return Allocate(height);
		}
		Rect    AllocateFullHeight(Coord width)
		{
			SetDirection(Direction::Row);
			return Allocate(width);
		}
		Rect    AllocateFullWidthFraction(float proportionOfWidth)
		{
			SetDirection(Direction::Column);
			return AllocateFraction(proportionOfWidth);
		}
		Rect    AllocateFullHeightFraction(float proportionOfHeight)
		{
			SetDirection(Direction::Row);
			return AllocateFraction(proportionOfHeight);
		}
		Coord   GetWidthRemaining()
		{
			assert(_direction == Direction::Row);
			return GetSpaceRemaining();
		}
	};

/////////////////////////////////////////////////////////////////////////////////////////////////////////

	struct DrawContext;
	struct IOContext;

	class LayedOutWidgets
	{
	public:
		struct NodeDelegates
		{
			std::function<void(DrawContext&, Rect frame, Rect content)> _drawDelegate;
			std::function<PlatformRig::ProcessInputResult(IOContext&, Rect frame, Rect content)> _ioDelegate;
			uint64_t _guid;

			uint64_t GetGuid() const { return _guid; }

			NodeDelegates() = default;
			NodeDelegates(NodeDelegates&&) = default;
			NodeDelegates& operator=(NodeDelegates&&) = default;
		};

		std::vector<std::pair<Rect, Rect>> _layedOutLocations;
		std::vector<NodeDelegates> _nodeAttachments;
		Coord2 _dimensions;
		Coord2 _mins, _maxs;

		void Draw(DrawContext& draw, const Float3x3& transform = Identity<Float3x3>());
		PlatformRig::ProcessInputResult ProcessInput(IOContext&, const Float3x3& transform = Identity<Float3x3>());

		LayedOutWidgets() = default;
		LayedOutWidgets(LayedOutWidgets&&) = default;
		LayedOutWidgets& operator=(LayedOutWidgets&&) = default;
	};

/////////////////////////////////////////////////////////////////////////////////////////////////////////

		//  C O N S T R U C T I O N   T I M E  //

	using YogaNodePtr = std::unique_ptr<YGNode, decltype(&YGNodeFree)>;
	class GuidStackHelper;
	class ImbuedNode;

	class GuidStackHelper
	{
	public:
		uint64_t MakeGuid(StringSection<> name) { return Hash64(name, _guidStack.top()); }
		uint64_t MakeGuid(StringSection<> name, StringSection<> concatenation) { return Hash64(name, Hash64(concatenation, _guidStack.top())); }
		uint64_t MakeGuid(uint64_t guid) { return HashCombine(guid, _guidStack.top()); }
		uint64_t MakeGuid() { return IntegerHash64(_incrementingId++) ^ _guidStack.top(); }

		void push(uint64_t guid) { return _guidStack.push(guid); }
		void pop() { return _guidStack.pop(); }
		uint64_t top() const { return _guidStack.top(); }
		bool empty() const { return _guidStack.empty(); }

		void Reset()
		{
			_guidStack = {};
			_guidStack.push(DefaultSeed64);
			_incrementingId = 0;
		}

		GuidStackHelper()
		{
			_guidStack.push(DefaultSeed64);
		}
	private:
		std::stack<uint64_t> _guidStack;
		unsigned _incrementingId = 0;
	};

	class LayoutEngine
	{
	public:
		YGNodeRef NewNode();
		ImbuedNode* NewImbuedNode(uint64_t guid);

		YGNodeRef InsertNewNode();
		ImbuedNode* InsertNewImbuedNode(uint64_t guid);

		YGNodeRef InsertAndPushNewNode();
		ImbuedNode* InsertAndPushNewImbuedNode(uint64_t guid);

		ImbuedNode* Find(uint64_t guid);

		LayedOutWidgets BuildLayedOutWidgets(Coord2 offsetToOutput = Coord2(0,0), std::optional<Rect> viewportRect = {});

		void InsertChildToStackTop(YGNodeRef);
		YGNodeRef GetTopmostNode();
		void PushNode(YGNodeRef);
		void PopNode();

		void PushRoot(YGNodeRef, Rect containerSize);
		GuidStackHelper& GuidStack() { return _guidStack; }

		LayoutEngine();
		~LayoutEngine();

	private:
		std::stack<YGNodeRef> _workingStack;
		std::vector<std::pair<YGNodeRef, Rect>> _roots;
		GuidStackHelper _guidStack;

		std::vector<std::unique_ptr<ImbuedNode>> _imbuedNodes;
		std::vector<YogaNodePtr> _retainedNodes;
	};

	YogaNodePtr MakeUniqueYogaNode();

	class ImbuedNode
	{
	public:
		LayedOutWidgets::NodeDelegates _nodeAttachments;

		std::function<YGSize(float, YGMeasureMode, float, YGMeasureMode)> _measureDelegate;
		std::function<void(YGNodeRef, Rect, Rect)> _postCalculateDelegate;

		YGNodeRef YGNode() { return _ygNode.get(); }
		operator YGNodeRef() { return YGNode(); }
		uint64_t Guid() const { return _nodeAttachments._guid; }
		ImbuedNode(YogaNodePtr&& ygNode, uint64_t guid, unsigned rootIndex) : _ygNode(std::move(ygNode)), _rootIndex(rootIndex) { _nodeAttachments._guid = guid; }
	private:
		YogaNodePtr _ygNode;
		unsigned _rootIndex;
		friend class LayoutEngine;
	};

}
