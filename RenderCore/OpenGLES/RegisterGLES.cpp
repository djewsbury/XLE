// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../Init.h"
#include "../../Utility/FunctionUtils.h"
#include "../../Core/Prefix.h"
#include <memory>

namespace RenderCore { namespace ImplOpenGLES
{
    std::shared_ptr<IDevice> CreateDevice();
    void RegisterCreation()
    {
        // (we can also use __attribute__((constructor)) with clang/gcc)
        static_constructor<&RegisterCreation>::c;
        RegisterDeviceCreationFunction(UnderlyingAPI::OpenGLES, &CreateDevice);
    }
}}
