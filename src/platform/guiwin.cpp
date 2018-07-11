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

//-----------------------------------------------------------------------------
// Menus
//-----------------------------------------------------------------------------

class MenuImplWin32;

class MenuItemImplWin32 : public MenuItem {
public:
    std::shared_ptr<MenuImplWin32> menu;

    HMENU Handle();

    MENUITEMINFOW GetInfo(UINT mask) {
        MENUITEMINFOW mii = {};
        mii.cbSize = sizeof(mii);
        mii.fMask  = mask;
        ssassert(GetMenuItemInfoW(Handle(), (UINT_PTR)this, FALSE, &mii),
                 "cannot get menu item info");
        return mii;
    }

    void SetAccelerator(KeyboardEvent accel) override {
        MENUITEMINFOW mii = GetInfo(MIIM_TYPE);

        std::wstring nameW(mii.cch, L'\0');
        mii.dwTypeData = &nameW[0];
        mii.cch++;
        ssassert(GetMenuItemInfoW(Handle(), (UINT_PTR)this, FALSE, &mii),
                 "cannot get menu item string");

        std::string name = Narrow(nameW);
        if(name.find('\t') != std::string::npos) {
            name = name.substr(0, name.find('\t'));
        }
        name += '\t';
        name += AcceleratorDescription(accel);

        nameW = Widen(name);
        mii.fMask      = MIIM_STRING;
        mii.dwTypeData = &nameW[0];
        ssassert(SetMenuItemInfoW(Handle(), (UINT_PTR)this, FALSE, &mii),
                 "cannot set menu item string");
    }

    void SetIndicator(Indicator type) override {
        MENUITEMINFOW mii = GetInfo(MIIM_FTYPE);
        switch(type) {
            case Indicator::NONE:
            case Indicator::CHECK_MARK:
                mii.fType &= ~MFT_RADIOCHECK;
                break;

            case Indicator::RADIO_MARK:
                mii.fType |= MFT_RADIOCHECK;
                break;
        }
        ssassert(SetMenuItemInfoW(Handle(), (UINT_PTR)this, FALSE, &mii),
                 "cannot set menu item type");
    }

    void SetActive(bool active) override {
        MENUITEMINFOW mii = GetInfo(MIIM_STATE);
        if(active) {
            mii.fState |= MFS_CHECKED;
        } else {
            mii.fState &= ~MFS_CHECKED;
        }
        ssassert(SetMenuItemInfoW(Handle(), (UINT_PTR)this, FALSE, &mii),
                 "cannot set menu item state");
    }

    void SetEnabled(bool enabled) override {
        MENUITEMINFOW mii = GetInfo(MIIM_STATE);
        if(enabled) {
            mii.fState &= ~(MFS_DISABLED|MFS_GRAYED);
        } else {
            mii.fState |= MFS_DISABLED|MFS_GRAYED;
        }
        ssassert(SetMenuItemInfoW(Handle(), (UINT_PTR)this, FALSE, &mii),
                 "cannot set menu item state");
    }
};

void TriggerMenu(int id) {
    MenuItemImplWin32 *menuItem = (MenuItemImplWin32 *)id;
    if(menuItem->onTrigger) {
        menuItem->onTrigger();
    }
}

int64_t contextMenuCancelTime = 0;

class MenuImplWin32 : public Menu {
public:
    HMENU h;

    std::weak_ptr<MenuImplWin32> weakThis;
    std::vector<std::shared_ptr<MenuItemImplWin32>> menuItems;
    std::vector<std::shared_ptr<MenuImplWin32>>     subMenus;

    MenuImplWin32() {
        h = CreatePopupMenu();
        ssassert(h != NULL, "cannot create popup menu");

        MENUINFO mi = {};
        mi.cbSize  = sizeof(mi);
        mi.fMask   = MIM_STYLE;
        mi.dwStyle = MNS_NOTIFYBYPOS;
        ssassert(SetMenuInfo(h, &mi), "cannot set menu style");
    }

    MenuItemRef AddItem(const std::string &label,
                        std::function<void()> onTrigger = NULL) override {
        auto menuItem = std::make_shared<MenuItemImplWin32>();
        menuItem->menu = weakThis.lock();
        menuItem->onTrigger = onTrigger;
        menuItems.push_back(menuItem);

        ssassert(AppendMenuW(h, MF_STRING, (UINT_PTR)&*menuItem, Widen(label).c_str()),
                 "cannot append menu item");

        return menuItem;
    }

    MenuRef AddSubMenu(const std::string &label) override {
        auto subMenu = std::make_shared<MenuImplWin32>();
        subMenu->weakThis = subMenu;
        subMenus.push_back(subMenu);

        ssassert(AppendMenuW(h, MF_STRING|MF_POPUP, (UINT_PTR)subMenu->h, Widen(label).c_str()),
                 "cannot append submenu");

        return subMenu;
    }

    void AddSeparator() override {
        ssassert(AppendMenuW(h, MF_SEPARATOR, 0, L""),
                 "cannot append menu separator");
    }

    void PopUp() override {
        POINT pos;
        ssassert(GetCursorPos(&pos),
                 "cannot get cursor position");
        int id = TrackPopupMenu(h, TPM_TOPALIGN|TPM_RIGHTBUTTON|TPM_RETURNCMD,
                                pos.x, pos.y, 0, GetActiveWindow(), NULL);
        if(id == 0) {
            contextMenuCancelTime = GetMilliseconds();
        } else {
            TriggerMenu(id);
        }
    }

    void Clear() override {
        for(int n = GetMenuItemCount(h) - 1; n >= 0; n--) {
            ssassert(RemoveMenu(h, n, MF_BYPOSITION),
                     "cannot remove menu item");
        }
        menuItems.clear();
        subMenus.clear();
    }

    ~MenuImplWin32() {
        Clear();
        ssassert(DestroyMenu(h), "cannot destroy menu");
    }
};

HMENU MenuItemImplWin32::Handle() {
    return menu->h;
}

MenuRef CreateMenu() {
    auto menu = std::make_shared<MenuImplWin32>();
    menu->weakThis = menu;
    return menu;
}

class MenuBarImplWin32 : public MenuBar {
public:
    HMENU h;

    std::vector<std::shared_ptr<MenuImplWin32>> subMenus;

    MenuBarImplWin32() {
        h = ::CreateMenu();
        ssassert(h != NULL, "cannot create menu bar");
    }

    MenuRef AddSubMenu(const std::string &label) override {
        auto subMenu = std::make_shared<MenuImplWin32>();
        subMenu->weakThis = subMenu;
        subMenus.push_back(subMenu);

        BOOL result = AppendMenuW(h, MF_STRING|MF_POPUP,
                                  (UINT_PTR)subMenu->h, Widen(label).c_str());
        ssassert(result, "cannot append menu item");

        return subMenu;
    }

    void Clear() override {
        for(int n = GetMenuItemCount(h) - 1; n >= 0; n--) {
            ssassert(RemoveMenu(h, n, MF_BYPOSITION),
                     "cannot remove menu from menubar");
        }
        subMenus.clear();
    }

    ~MenuBarImplWin32() {
        Clear();
        ssassert(DestroyMenu(h), "cannot destroy menubar");
    }

    void *NativePtr() override {
        return h;
    }
};

MenuBarRef GetOrCreateMainMenu(bool *unique) {
    *unique = false;
    return std::make_shared<MenuBarImplWin32>();
}

}
}
