
#include "../RawFS.h"
#include "../../Core/SelectConfiguration.h"
#include <sys/time.h>

#ifdef ANDROID
#include <time.h>
#endif

namespace OSServices
{
    static const auto NSEC_PER_SEC = 1000000000ull;

    uint64_t GetPerformanceCounter()
    {
        struct timespec t;
	    clock_gettime(CLOCK_REALTIME, &t);
        return (uint64_t)(t.tv_sec) * NSEC_PER_SEC + (uint64_t)(t.tv_nsec);
    }

    uint64_t GetPerformanceCounterFrequency()
    {
        return NSEC_PER_SEC;
    }

    bool GetCurrentDirectory(uint32_t dim, char dst[])
    {
        if (dim > 0) dst[0] = '\0';
        return false;
    }

    void GetProcessPath(utf8 dst[], size_t bufferCount)
    {
        if (bufferCount > 0) dst[0] = '\0';
    }

    void ChDir(const utf8 path[]) {}

    const char* GetCommandLine() { return ""; }

    ModuleId GetCurrentModuleId() { return 0; }

}
