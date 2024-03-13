// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../OSServices/InputSnapshot.h"
#include "../Math/Vector.h"
#include <vector>

namespace PlatformRig
{
    using Coord = int;
    using Coord2 = Int2;

    struct WindowingSystemView
    {
        Coord2 _viewMins = Coord2{0, 0};
        Coord2 _viewMaxs = Coord2{0, 0};
    };

	class InputContext
	{
	public:
        WindowingSystemView _view;

        void* GetService(uint64_t) const;
        void AttachService(uint64_t, void*);

        template<typename Type>
            Type* GetService() const { return (Type*)GetService(typeid(std::decay_t<Type>).hash_code()); static_assert(!std::is_same_v<std::decay_t<Type>, WindowingSystemView>); }
        template<typename Type>
            void AttachService2(Type& type) { AttachService(typeid(std::decay_t<Type>).hash_code(), &type); static_assert(!std::is_same_v<std::decay_t<Type>, WindowingSystemView>); }

        InputContext();
        ~InputContext();
        InputContext(InputContext&&);
        InputContext& operator=(InputContext&&);
        InputContext(const InputContext&);
        InputContext& operator=(const InputContext&);
    private:
        std::vector<std::pair<uint64_t, void*>> _services;
	};

    enum class ProcessInputResult { Passthrough, Consumed };

    ///////////////////////////////////////////////////////////////////////////////////
    class IInputListener
    {
    public:
        using ProcessInputResult = PlatformRig::ProcessInputResult;
        virtual ProcessInputResult OnInputEvent(
			const InputContext& context,
			const OSServices::InputSnapshot& evnt) = 0;
        virtual ~IInputListener() = default;
    };

}

