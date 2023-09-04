#include "window.h"

#include <Windows.h>

#include <stdio.h>

#include <stdint.h>

// extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

static LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    Window* window = reinterpret_cast<Window*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
#if 0
    if (window && window->bImguiEnable && ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
        return true;
#endif
    if (window != nullptr || message == WM_CREATE || message == WM_DESTROY || message == WM_CLOSE) {
        switch (message) {
        case WM_CLOSE: {
            printf("%s: WM_CLOSE.\n", __FUNCTION__);
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
        case WM_DESTROY: {
            printf("%s: WM_DESTROY.\n", __FUNCTION__);
            PostQuitMessage(0);
            if (window) {
                window->hwnd = nullptr;
            }
            return 0;
        }
        case WM_CREATE: {
            printf("%s: WM_CREATE.\n", __FUNCTION__);
            LPCREATESTRUCT pCreateStruct = reinterpret_cast<LPCREATESTRUCT>(lParam);
            SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pCreateStruct->lpCreateParams));
            return 0;
        }
        case WM_SIZE: {
            const UINT width = LOWORD(lParam);
            const UINT height = HIWORD(lParam);
            printf("%s: WM_SIZE(%d, %d).\n", __FUNCTION__, width, height);
            window->resize = true;
            window->width  = width;
            window->height = height;
            return 0;
        }
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYUP: {
            const UINT virtualKeyCode = UINT(wParam);
            Window_OnKey(window, virtualKeyCode, KeyMessageFlags(uint32_t(lParam) >> 29));
            return 0;
        }
        case WM_DISPLAYCHANGE:
            puts("WM_DISPLAYCHANGE");
            return DefWindowProc(hWnd, message, wParam, lParam);
        case WM_PAINT:
            window->paint = true;
            return DefWindowProc(hWnd, message, wParam, lParam);

            // do not print these:
        case WM_NCHITTEST:   // 0x0084
        case WM_MOUSEMOVE:   // 0x0200
        case WM_NCMOUSEMOVE: // 0x00A0
        case WM_SETCURSOR:   // 0x0020
        case WM_MOVING:      // 0x0216
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
    }

    // printf("WM_0x%04X\n", message);
    return DefWindowProc(hWnd, message, wParam, lParam);
}

bool Window_Construct(Window* window, int clientWidth, int clientHeight, const wchar_t* title)
{
    *window = { };
    printf("%s: enter.\n", __FUNCTION__);

    HINSTANCE const hInstance = GetModuleHandleA(nullptr);

    constexpr DWORD windowStyle = WS_OVERLAPPEDWINDOW;

    // Initialize the window class.
    WNDCLASSEXW windowClass = { };
    windowClass.cbSize = sizeof windowClass;
    windowClass.style = CS_HREDRAW | CS_VREDRAW; // _class_ style, not window style
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = hInstance;
    windowClass.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512));
    windowClass.lpszClassName = L"XYZ";
    RegisterClassExW(&windowClass);

    RECT windowRect = { 0, 0, clientWidth, clientHeight };
    AdjustWindowRect(&windowRect, windowStyle, FALSE);

    // Create the window and store a handle to it.
    HWND const h = CreateWindowExW(
        0, // dwExStyle
        windowClass.lpszClassName,
        title, // I get gibberish?
        windowStyle,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top,
        nullptr,        // We have no parent window.
        nullptr,        // We aren't using menus.
        hInstance,
        window);

    window->hwnd = h;

    printf("%s: leave.\n", __FUNCTION__);
    return h != nullptr;
}
