// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Utility/StringUtils.h"
#include <vector>
#include <string>

namespace Assets { class OperationContext; }

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
		enum Flags { Model = 1<<0, Animation = 1<<1, Skeleton = 1<<2, Material = 1<<3 };
		using BitField = unsigned;
	}
	CompilationTarget::BitField FindCompilationTargets(StringSection<> ext);

	std::shared_ptr<::Assets::OperationContext> CreateLoadingContext();
}
