// *****************************************************************************
// * This file is part of the FreeFileSync project. It is distributed under    *
// * GNU General Public License: https://www.gnu.org/licenses/gpl-3.0          *
// * Copyright (C) Zenju (zenju AT freefilesync DOT org) - All Rights Reserved *
// *****************************************************************************

#include "thread.h"
#include "globals.h"
#if defined(__APPLE__)
    #include <pthread.h> //pthread_setname_np
#else
    #include <sys/prctl.h>
#endif

using namespace zen;




void zen::setCurrentThreadName(const Zstring& threadName)
{
#if defined(__APPLE__)
    ::pthread_setname_np(threadName.c_str()); //macOS: current thread only, 63 bytes max
#else
    ::prctl(PR_SET_NAME, threadName.c_str(), 0, 0, 0);
#endif

}


namespace
{
constinit Global<const std::thread::id> globalMainThreadId;
//ensure no later initialization than during static construction
GLOBAL_RUN_ONCE(globalMainThreadId.set(std::make_unique<std::thread::id>(std::this_thread::get_id())));
}


bool zen::runningOnMainThread()
{
    if (const auto globalTId = globalMainThreadId.get())
        return std::this_thread::get_id() == *globalTId;

    assert(false);
    return true; //called during static initialization => "very likely" the main thread :>
}
