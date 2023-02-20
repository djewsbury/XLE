// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once
#include "OverlayPrimitives.h"
#include "../../Utility/StringUtils.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Foreign/yoga/yoga/Yoga.h"
#include <stack>
#include <vector>
#include <memory>

namespace RenderOverlays
{
	namespace CommonWidgets { class Draw; class Input; }

	enum class IODelegateResult { Passthrough, Consumed };

	class LayedOutWidgets
	{
	public:
		struct NodeDelegates
		{
			std::function<void(CommonWidgets::Draw&, Rect frame, Rect content)> _drawDelegate;
			std::function<IODelegateResult(CommonWidgets::Input&, Rect frame, Rect content)> _ioDelegate;
			uint64_t _guid;

			uint64_t GetGuid() const { return _guid; }

			NodeDelegates() = default;
			NodeDelegates(NodeDelegates&&) = default;
			NodeDelegates& operator=(NodeDelegates&&) = default;
		};

		std::vector<std::pair<Rect, Rect>> _layedOutLocations;
		std::vector<NodeDelegates> _nodeAttachments;

		void Draw(CommonWidgets::Draw& draw);

		enum ProcessInputResult { Passthrough, Consumed };
		ProcessInputResult ProcessInput(CommonWidgets::Input& input);

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
		LayedOutWidgets BuildLayedOutWidgets(Rect container);

		void InsertChildToStackTop(YGNodeRef);
		void PushNode(YGNodeRef);
		void PopNode();

		void PushRoot(YGNodeRef);
		GuidStackHelper& GuidStack() { return _guidStack; }

		LayoutEngine();
		~LayoutEngine();

	private:
		std::stack<YGNodeRef> _workingStack;
		std::vector<YGNodeRef> _roots;
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
