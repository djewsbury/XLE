// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "EntityInterface.h"
#include "../ToolsRig/ToolsRigServices.h"
#include "../../Assets/DepVal.h"
#include "../../Assets/Continuation.h"
#include "../../Assets/Assets.h"
#include "../../Math/MathSerialization.h"

namespace EntityInterface
{
	template<typename T>
		class MountedData
	{
	public:
		operator const T&() const { return _data; }
		const T& get() const { return _data; };

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }

		MountedData(Formatters::IDynamicInputFormatter& fmttr)
		: _data(fmttr), _depVal(fmttr.GetDependencyValidation())
		{}
		MountedData() = default;

		static void ConstructToPromise(
			std::promise<MountedData>&& promise,
			::Assets::Initializer<> mountLocation)
		{
			::Assets::WhenAll(ToolsRig::Services::GetEntityMountingTree().BeginFormatter(mountLocation)).ThenConstructToPromise(
				std::move(promise),
				[](auto fmttr) { return MountedData{*fmttr}; });
		}

		static const T& LoadOrDefault(::Assets::Initializer<> mountLocation)
		{
			auto marker = ::Assets::MakeAssetMarker<MountedData>(mountLocation);
			if (auto* actualized = marker->TryActualize())
				return actualized->get();
			static T def;
			return def;
		}
	private:
		T _data;
		::Assets::DependencyValidation _depVal;
	};
}
