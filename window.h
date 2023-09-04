#pragma once

enum KeyMessageFlags {
    KeyMessage_AltHeld    = 1 << 0,
    KeyMessage_DownBefore = 1 << 1,
    KeyMessage_UpNow      = 1 << 2,
};

static bool IsFreshDown(KeyMessageFlags flags)
{
    return (flags & (KeyMessage_UpNow | KeyMessage_DownBefore)) == 0;
}

struct Window {
    struct HWND__ *hwnd;
    int width;
    int height;
    bool paint;
    bool resize;
};

void Window_OnKey(Window* window, unsigned virtualKeyCode, KeyMessageFlags keyMessageFlags);

bool Window_Construct(Window* window, int clientWidth, int clientHeight, const wchar_t* title);

inline void Window_Destruct(Window*)
{
    // nop, the OS destroys the HWND before sendng WM_DESTROY?
}
