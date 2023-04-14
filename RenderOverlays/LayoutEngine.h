// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once
#include "OverlayPrimitives.h"
#include "../PlatformRig/InputContext.h"		// for ProcessInputResult
#include "../Math/Matrix.h"
#include "../Utility/StringUtils.h"
#include "../Utility/MemoryUtils.h"
#include "../Foreign/yoga/yoga/Yoga.h"
#include <stack>
#include <vector>
#include <memory>

namespace PlatformRig { class InputContext; }
namespace OSServices { class InputSnapshot; }

namespace RenderOverlays
{
	class DrawContext;
	class IOContext;

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

		void push(uint64_t guid) { return _guidStack.push(guid); }
		void pop() { return _guidStack.pop(); }
		uint64_t top() const { return _guidStack.top(); }
		bool empty() const { return _guidStack.empty(); }

		void Reset()
		{
			_guidStack = {};
			_guidStack.push(DefaultSeed64);
		}

		GuidStackHelper()
		{
			_guidStack.push(DefaultSeed64);
		}
	private:
		std::stack<uint64_t> _guidStack;
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

		LayedOutWidgets BuildLayedOutWidgets();

		void InsertChildToStackTop(YGNodeRef);
		void PushNode(YGNodeRef);
		void PopNode();

		void PushRoot(YGNodeRef, Coord2 containerSize);
		GuidStackHelper& GuidStack() { return _guidStack; }

		LayoutEngine();
		~LayoutEngine();

	private:
		std::stack<YGNodeRef> _workingStack;
		std::vector<std::pair<YGNodeRef, Coord2>> _roots;
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

		YGNodeRef YGNode() { return _ygNode.get(); }
		operator YGNodeRef() { return YGNode(); }
		ImbuedNode(YogaNodePtr&& ygNode, uint64_t guid) : _ygNode(std::move(ygNode)) { _nodeAttachments._guid = guid; }
	private:
		YogaNodePtr _ygNode;
	};

}
