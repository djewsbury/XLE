// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Utility/StringUtils.h"
#include <vector>
#include <string>

namespace Assets { class OperationContext; }
namespace RenderCore { class IThreadContext; }
namespace RenderCore { namespace Techniques { class SubFrameEvents; }}
namespace std { template<typename T> class future; }

namespace ToolsRig
{
	class OnChangeCallback
	{
	public:
		virtual void OnChange() = 0;
		virtual ~OnChangeCallback() = default;
	};

	class MessageRelay
	{
	public:
		std::string GetMessages() const;

		unsigned AddCallback(const std::shared_ptr<OnChangeCallback>& callback);
		void RemoveCallback(unsigned);

		void AddMessage(const std::string& msg);

		MessageRelay();
		~MessageRelay();
	private:
		class Pimpl;
		std::unique_ptr<Pimpl> _pimpl;
	};

	std::vector<std::pair<std::string, std::string>> GetModelExtensions();
	std::vector<std::pair<std::string, std::string>> GetAnimationSetExtensions();

	namespace CompilationTarget
	{
		enum Flags { Model = 1<<0, Animation = 1<<1, Skeleton = 1<<2, Material = 1<<3, Texture = 1<<4 };
		using BitField = unsigned;
	}
	CompilationTarget::BitField FindCompilationTargets(StringSection<> ext);

	class TreeOfDirectories
	{
	public:
		struct Directory
		{
			unsigned _nameStart;
			unsigned _parent;
			unsigned _childrenStart, _childCount;
			CompilationTarget::BitField _fileTargets, _subtreeTargets;
		};
		std::vector<Directory> _directories;

		std::vector<char> _stringTable;
		std::vector<std::pair<uint64_t, unsigned>> _hashTableLookup;
	};
	std::future<TreeOfDirectories> CalculateDirectoriesByCompilationTargets(StringSection<> base);
	
	// ITreeOfDirectoriesHelper is required to isolate CLR code from futures
	class ITreeOfDirectoriesHelper
	{
	public:
		virtual std::shared_ptr<TreeOfDirectories> Get() = 0;
		virtual bool IsReady() const = 0;
		virtual ~ITreeOfDirectoriesHelper();
	};
	std::shared_ptr<ITreeOfDirectoriesHelper> CalculateDirectoriesByCompilationTargets_Helper(StringSection<> base);

	std::shared_ptr<::Assets::OperationContext> CreateLoadingContext();

	void InvokeCheckCompleteInitialization(
		RenderCore::Techniques::SubFrameEvents&,
		RenderCore::IThreadContext&);
}
