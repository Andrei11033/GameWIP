#include "debug/assert/internal/assert_platform.h"

#include <windows.h>

namespace GameWIP::Debug::Platform
{
    void debugBreak()
    {
        DebugBreak();
    }
}
