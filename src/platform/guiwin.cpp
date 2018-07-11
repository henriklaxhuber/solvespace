//-----------------------------------------------------------------------------
// The Win32-based implementation of platform-dependent GUI functionality.
//
// Copyright 2018 whitequark
//-----------------------------------------------------------------------------
#include "solvespace.h"
// Include after solvespace.h to avoid identifier clashes.
#include <windows.h>

namespace SolveSpace {
namespace Platform {

//-----------------------------------------------------------------------------
// Timers
//-----------------------------------------------------------------------------

class TimerImplWin32 : public Timer {
public:
    static HWND WindowHandle() {
        static HWND h;
        if(h == NULL) {
            h = CreateWindowW(L"Message", NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, NULL, NULL);
            ssassert(h != NULL, "cannot create timer window");
        }
        return h;
    }

    static void CALLBACK TimerFunc(HWND hwnd, UINT msg, UINT_PTR event, DWORD time) {
        ssassert(KillTimer(WindowHandle(), event),
                 "cannot stop timer");

        TimerImplWin32 *timer = (TimerImplWin32*)event;
        if(timer->onTimeout) {
            timer->onTimeout();
        }
    }

    void WindUp(unsigned milliseconds) override {
        // We should use SetCoalescableTimer (Win8+) when it's available.
        ssassert(SetTimer(WindowHandle(), (UINT_PTR)this,
                          milliseconds, &TimerImplWin32::TimerFunc),
                 "cannot set timer");
    }

    ~TimerImplWin32() {
        // We have a race condition here--WM_TIMER messages already posted to the queue
        // are not removed--so destructor is "best effort".
        KillTimer(WindowHandle(), (UINT_PTR)this);
    }
};

TimerRef CreateTimer() {
    return std::unique_ptr<TimerImplWin32>(new TimerImplWin32);
}

}
}
