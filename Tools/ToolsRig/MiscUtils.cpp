// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "MiscUtils.h"
#include "../../RenderCore/Assets/ModelScaffold.h"
#include "../../RenderCore/Assets/RawMaterial.h"
#include "../../Assets/AssetServices.h"
#include "../../Assets/CompileAndAsyncManager.h"
#include "../../Utility/Threading/Mutex.h"

namespace ToolsRig
{
	std::vector<std::pair<std::string, std::string>> GetModelExtensions()
	{
		return ::Assets::Services::GetAsyncMan().GetIntermediateCompilers().GetExtensionsForTargetCode(
			RenderCore::Assets::ModelScaffold::CompileProcessType);
	}

	std::vector<std::pair<std::string, std::string>> GetAnimationSetExtensions()
	{
		return ::Assets::Services::GetAsyncMan().GetIntermediateCompilers().GetExtensionsForTargetCode(
			RenderCore::Assets::AnimationSetScaffold::CompileProcessType);
	}

	CompilationTarget::BitField FindCompilationTargets(StringSection<> ext)
	{
		auto types = ::Assets::Services::GetAsyncMan().GetIntermediateCompilers().GetTargetCodesForExtension(ext);
		CompilationTarget::BitField result = 0;
		for (auto t:types) {
			if (t == RenderCore::Assets::ModelScaffold::CompileProcessType) {
				result |= CompilationTarget::Flags::Model;
			} else if (t == RenderCore::Assets::AnimationSetScaffold::CompileProcessType) {
				result |= CompilationTarget::Flags::Animation;
			} else if (t == RenderCore::Assets::SkeletonScaffold::CompileProcessType) {
				result |= CompilationTarget::Flags::Skeleton;
			} else if (t == RenderCore::Assets::RawMatConfigurations::CompileProcessType) {
				result |= CompilationTarget::Flags::Material;
			}
		}
		return result;
	}

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
	
}

