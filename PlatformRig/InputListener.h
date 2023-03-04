// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../OSServices/InputSnapshot.h"
#include "../Math/Vector.h"
#include "../Utility/UTFUtils.h"
#include "../Utility/StringUtils.h"
#include "../Utility/MemoryUtils.h"
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

        void* GetService(uint64_t) const;
        void AttachService(uint64_t, void*);

        template<typename Type>
            Type* GetService() const { return (Type*)GetService(typeid(std::decay_t<Type>).hash_code()); }
        template<typename Type>
            void AttachService2(Type& type) { AttachService(typeid(std::decay_t<Type>).hash_code(), &type); }

        InputContext();
        ~InputContext();
    private:
        std::vector<std::pair<uint64_t, void*>> _services;
	};

    enum class ProcessInputResult { Passthrough, Consumed };

    ///////////////////////////////////////////////////////////////////////////////////
   
    namespace Literals
    {
        #if (COMPILER_ACTIVE == COMPILER_TYPE_GCC || COMPILER_ACTIVE == COMPILER_TYPE_CLANG) && (__cplusplus < 202002L)

            #pragma GCC diagnostic push
            #pragma GCC diagnostic ignored "-Wgnu-string-literal-operator-template"
                template <typename T, T... chars>
                    XLE_CONSTEVAL_OR_CONSTEXPR uint32_t operator"" _key() never_throws { return Internal::ConstHash32_2<DefaultSeed32, chars...>(); }
            #pragma GCC diagnostic pop

        #else

            XLE_CONSTEVAL_OR_CONSTEXPR uint32_t operator"" _key(const char* str, const size_t len) never_throws { return Internal::ConstHash32_1(str, len); }

        #endif
    }

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

