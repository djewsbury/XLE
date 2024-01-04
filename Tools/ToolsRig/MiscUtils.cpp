// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "MiscUtils.h"
#include "../../RenderCore/Techniques/SubFrameEvents.h"
#include "../../RenderCore/Assets/ModelScaffold.h"
#include "../../RenderCore/Assets/RawMaterial.h"
#include "../../Assets/AssetServices.h"
#include "../../Assets/OperationContext.h"
#include "../../Assets/IntermediateCompilers.h"
#include "../../Utility/Threading/Mutex.h"
#include "../../Utility/Streams/PathUtils.h"
#include <stack>
#include <future>

namespace ToolsRig
{
	constexpr auto s_ModelScaffold_CompileProcessType = GetCompileProcessType((RenderCore::Assets::ModelScaffold*)nullptr);
	constexpr auto s_AnimationSetScaffold_CompileProcessType = GetCompileProcessType((RenderCore::Assets::AnimationSetScaffold*)nullptr);
	constexpr auto s_SkeletonScaffold_CompileProcessType = GetCompileProcessType((RenderCore::Assets::SkeletonScaffold*)nullptr);
	constexpr auto s_RawMaterialSet_CompileProcessType = GetCompileProcessType((RenderCore::Assets::RawMaterialSet*)nullptr);

	std::vector<std::pair<std::string, std::string>> GetModelExtensions()
	{
		return ::Assets::Services::GetIntermediateCompilers().GetExtensionsForTargetCode(s_ModelScaffold_CompileProcessType);
	}

	std::vector<std::pair<std::string, std::string>> GetAnimationSetExtensions()
	{
		return ::Assets::Services::GetIntermediateCompilers().GetExtensionsForTargetCode(s_AnimationSetScaffold_CompileProcessType);
	}

	CompilationTarget::BitField FindCompilationTargets(StringSection<> ext)
	{
		auto types = ::Assets::Services::GetIntermediateCompilers().GetTargetCodesForExtension(ext);
		CompilationTarget::BitField result = 0;
		for (auto t:types) {
			if (t == s_ModelScaffold_CompileProcessType) {
				result |= CompilationTarget::Flags::Model;
			} else if (t == s_AnimationSetScaffold_CompileProcessType) {
				result |= CompilationTarget::Flags::Animation;
			} else if (t == s_SkeletonScaffold_CompileProcessType) {
				result |= CompilationTarget::Flags::Skeleton;
			} else if (t == s_RawMaterialSet_CompileProcessType) {
				result |= CompilationTarget::Flags::Material;
			}
		}
		return result;
	}

	namespace Internal
	{
		TreeOfDirectories CalculateDirectoriesByCompilationTargets(StringSection<> base)
		{
			auto modelExts = ::Assets::Services::GetIntermediateCompilers().GetExtensionsForTargetCode(s_ModelScaffold_CompileProcessType);
			auto animationExts = ::Assets::Services::GetIntermediateCompilers().GetExtensionsForTargetCode(s_AnimationSetScaffold_CompileProcessType);
			auto skeletonExts = ::Assets::Services::GetIntermediateCompilers().GetExtensionsForTargetCode(s_SkeletonScaffold_CompileProcessType);
			auto materialExts = ::Assets::Services::GetIntermediateCompilers().GetExtensionsForTargetCode(s_RawMaterialSet_CompileProcessType);

			TreeOfDirectories result;

			result._directories.push_back(TreeOfDirectories::Directory{
				(unsigned)result._stringTable.size(),
				~0u, 0, 0,
				0, 0
			});
			result._stringTable.insert(result._stringTable.end(), base.begin(), base.end());
			result._stringTable.push_back(0);

			struct PendingDirectory
			{
				::Assets::FileSystemWalker _walker;
				unsigned _indexInResult;
			};
			std::stack<PendingDirectory> pendingDirectories;
			pendingDirectories.emplace(PendingDirectory{::Assets::MainFileSystem::BeginWalk(base), 0});

			while (!pendingDirectories.empty()) {

				auto pendingDir = std::move(pendingDirectories.top());
				pendingDirectories.pop();

				// Find the targets in this immediate directory
				CompilationTarget::BitField fileTargets = 0;
				for (auto f=pendingDir._walker.begin_files(); f!=pendingDir._walker.end_files(); ++f) {
					auto mountedName = f.Desc()._mountedName;
					auto splitName = MakeFileNameSplitter(mountedName);
					auto ext = splitName.Extension();
					auto i = std::find_if(modelExts.begin(), modelExts.end(), [ext](const auto& q) { return XlEqStringI(ext, q.first); });
					if (i != modelExts.end()) fileTargets |= CompilationTarget::Model;
					i = std::find_if(animationExts.begin(), animationExts.end(), [ext](const auto& q) { return XlEqStringI(ext, q.first); });
					if (i != animationExts.end()) fileTargets |= CompilationTarget::Animation;

					i = std::find_if(skeletonExts.begin(), skeletonExts.end(), [ext](const auto& q) { return XlEqStringI(ext, q.first); });
					if (i != skeletonExts.end()) {
						// To help filter out excess hits, we'll only consider a file a target for a skeleton if it isn't also a model or if it has "skel" in the name
						if (!(fileTargets & CompilationTarget::Model))
							fileTargets |= CompilationTarget::Skeleton;
						else if (XlFindStringI(splitName.File(), "skel"))
							fileTargets |= CompilationTarget::Skeleton;
					}

					i = std::find_if(materialExts.begin(), materialExts.end(), [ext](const auto& q) { return XlEqStringI(ext, q.first); });
					if (i != materialExts.end()) fileTargets |= CompilationTarget::Material;
				}
				result._directories[pendingDir._indexInResult]._fileTargets = fileTargets;

				// propagate up to parents
				{
					auto parent = result._directories[pendingDir._indexInResult]._parent;
					while (parent != ~0u) {
						result._directories[parent]._subtreeTargets |= fileTargets;
						parent = result._directories[parent]._parent;
					}
				}

				// Queue up children
				result._directories[pendingDir._indexInResult]._childrenStart = (unsigned)result._directories.size();

				for (auto dir = pendingDir._walker.begin_directories(); dir != pendingDir._walker.end_directories(); ++dir) {
					auto name = dir.Name();
					if (name.empty() || name[0] == '.') continue;

					result._directories.push_back(TreeOfDirectories::Directory{
						(unsigned)result._stringTable.size(),
						pendingDir._indexInResult, 0, 0,
						0, 0
					});
					result._stringTable.insert(result._stringTable.end(), name.begin(), name.end());
					result._stringTable.push_back(0);

					pendingDirectories.emplace(PendingDirectory{std::move(*dir), (unsigned)result._directories.size()-1});
					++result._directories[pendingDir._indexInResult]._childCount;
				}

			}

			return result;
		}
	}

	std::future<TreeOfDirectories> CalculateDirectoriesByCompilationTargets(StringSection<> base)
	{
		std::promise<TreeOfDirectories> promise;
		auto result = promise.get_future();
		ConsoleRig::GlobalServices::GetInstance().GetLongTaskThreadPool().Enqueue(
			[promise=std::move(promise), base=base.AsString()]() mutable {
				TRY {
					promise.set_value(Internal::CalculateDirectoriesByCompilationTargets(base));
				} CATCH (...) {
					promise.set_exception(std::current_exception());
				} CATCH_END
			});

		return result;
	}

	class TreeOfDirectoriesHelper : public ITreeOfDirectoriesHelper
	{
	public:
		std::shared_ptr<TreeOfDirectories> Get()
		{
			ScopedLock(_lock);
			if (!_result)
				_result = std::make_shared<TreeOfDirectories>(std::move(_future.get()));
			return _result;
		}

		bool IsReady() const
		{
			ScopedLock(_lock);
			if (_result) return true;
			return _future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
		}

		TreeOfDirectoriesHelper(std::future<TreeOfDirectories>&& future) : _future(std::move(future)) {}
		~TreeOfDirectoriesHelper() {}
	private:
		mutable Threading::Mutex _lock;
		std::future<TreeOfDirectories> _future;
		std::shared_ptr<TreeOfDirectories> _result;
	};

	std::shared_ptr<ITreeOfDirectoriesHelper> CalculateDirectoriesByCompilationTargets_Helper(StringSection<> base)
	{
		return std::make_shared<TreeOfDirectoriesHelper>(CalculateDirectoriesByCompilationTargets(base));
	}

	ITreeOfDirectoriesHelper::~ITreeOfDirectoriesHelper() = default;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class MessageRelay::Pimpl
	{
	public:
		std::vector<std::string> _messages;
		Threading::RecursiveMutex _lock;

		std::vector<std::pair<unsigned, std::shared_ptr<OnChangeCallback>>> _callbacks;
		unsigned _nextCallbackId = 1;
	};

	std::string MessageRelay::GetMessages() const
	{
		ScopedLock(_pimpl->_lock);
		size_t length = 0;
		for (const auto&m:_pimpl->_messages)
			length += m.size();
		std::string result;
		result.reserve(length);
		for (const auto&m:_pimpl->_messages)
			result.insert(result.end(), m.begin(), m.end());
		return result;
	}

	unsigned MessageRelay::AddCallback(const std::shared_ptr<OnChangeCallback>& callback)
	{
		ScopedLock(_pimpl->_lock);
		_pimpl->_callbacks.push_back(std::make_pair(_pimpl->_nextCallbackId, callback));
		return _pimpl->_nextCallbackId++;
	}

	void MessageRelay::RemoveCallback(unsigned id)
	{
		ScopedLock(_pimpl->_lock);
		auto i = std::find_if(
			_pimpl->_callbacks.begin(), _pimpl->_callbacks.end(),
			[id](const std::pair<unsigned, std::shared_ptr<OnChangeCallback>>& p) { return p.first == id; } );
		_pimpl->_callbacks.erase(i);
	}

	void MessageRelay::AddMessage(const std::string& msg)
	{
		ScopedLock(_pimpl->_lock);
		_pimpl->_messages.push_back(msg);
		for (const auto&cb:_pimpl->_callbacks)
			cb.second->OnChange();
	}

	MessageRelay::MessageRelay()
	{
		_pimpl = std::make_unique<Pimpl>();
	}

	MessageRelay::~MessageRelay()
	{}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	std::shared_ptr<::Assets::OperationContext> CreateLoadingContext()
	{
		return std::make_shared<::Assets::OperationContext>();
	}

	void InvokeCheckCompleteInitialization(
		RenderCore::Techniques::SubFrameEvents& subFrameEvents,
		RenderCore::IThreadContext& threadContext)
	{
		// hidden here because we can't invoke this from CLR code
		subFrameEvents._onCheckCompleteInitialization.Invoke(threadContext);
	}
	
}

