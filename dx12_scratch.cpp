#include <stdio.h>

#include "window.h"

#include "dxutil.h"

#include <dxgi1_4.h>

#include <assert.h>

#define MAX_FRAMES 2

typedef IDXGISwapChain3 SwapchainN;
typedef ID3D12Device7 DeviceN;
typedef ID3D12GraphicsCommandList4 GfxCmdListN;

INT64 qpcTicksPerSecondI64;

struct PerFrame {
    ScopedRelease<ID3D12CommandAllocator> cmdAllocator;
    ScopedRelease<GfxCmdListN> cmdList;
    HANDLE hEvent = nullptr;

    ~PerFrame()
    {
        if (HANDLE const h = hEvent)
        {
            CloseHandle(hEvent);
        }
    }
};

struct ResourceAndRtv {
    ID3D12Resource* resource;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv;
};

struct App {
    Window window = { };

    int posPixelX = 0;
    int velPixelsPerSecX = 256;

    UINT64 frameCounter = 0;
    PerFrame perFrame[MAX_FRAMES];

    ScopedRelease<DeviceN> device;
    ScopedRelease<ID3D12CommandQueue> queue;
    ScopedRelease<ID3D12Fence> fence;
    UINT64 nextFenceValue = 1;

    SwapchainN* swapchain = nullptr;
    ResourceAndRtv swapchainObjects[4];

    int bytesPerRtv = 0;
    ScopedRelease<ID3D12DescriptorHeap> rtvHeap;

    static constexpr DXGI_FORMAT SwapchainResourceFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
    static constexpr DXGI_FORMAT SwapchainRtvFormat = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;

    App() : swapchainObjects{} { }
    constexpr App(const App& other) = delete;
    App& operator=(const App& other) = delete;

    ~App()
    {
        for (const ResourceAndRtv& o : swapchainObjects)
            if (ID3D12Resource* r = o.resource)
                r->Release();
        if (SwapchainN *sc = swapchain)
            sc->Release();
        Window_Destruct(&window);
    }
};
#define APP_FROM_WINDOW(pWindow) reinterpret_cast<App *>(reinterpret_cast<char *>(pWindow) - offsetof(App, window))

const ResourceAndRtv& GetCurrentBackbufferObjects(App& app)
{
    UINT const i = app.swapchain->GetCurrentBackBufferIndex();
    ResourceAndRtv& r = app.swapchainObjects[i];
    assert(!r.rtv.ptr == !r.resource);
    if (r.resource == nullptr)
    {
        app.swapchain->GetBuffer(i, IID_PPV_ARGS(&r.resource)); // fill ptr
        D3D12_CPU_DESCRIPTOR_HANDLE const h = { app.rtvHeap->GetCPUDescriptorHandleForHeapStart().ptr + i * app.bytesPerRtv };
        r.rtv = h;
        D3D12_RENDER_TARGET_VIEW_DESC const desc = {
            app.SwapchainRtvFormat,
            D3D12_RTV_DIMENSION_TEXTURE2D
        };
        app.device->CreateRenderTargetView(r.resource, &desc, h);
    }
    return r;
}

static void
WaitForIdle(App& app)
{
    HANDLE h[MAX_FRAMES];
    for (int i = 0; i < MAX_FRAMES; ++i)
        h[i] = app.perFrame[i].hEvent;
    DWORD const waitRes = WaitForMultipleObjectsEx(MAX_FRAMES, h, TRUE, 5000, FALSE); // WaitAll=TRUE, Alertable=FALSE
    if (waitRes >= WAIT_OBJECT_0 && waitRes < WAIT_OBJECT_0 + MAX_FRAMES)
    {
        // okay
    }
    else
    {
        DWORD const err = GetLastError();
        printf("WaitForIdle: WaitForMultipleObjectsEx=0x%X, GetLastError=0x%X\n", waitRes, err);
        __debugbreak();
    }
}

void ResizeSwapchain(App& app)
{
    // release old resources
    for (const ResourceAndRtv& o : app.swapchainObjects)
        if (ID3D12Resource* r = o.resource)
            r->Release();
    memset(app.swapchainObjects, 0, sizeof app.swapchainObjects); // trigger create new RTVs
    
    // Would there be a benefit to use IDXGISwapChain3::ResizeBuffers1 (dxgi1_4.h) ?
    HRESULT const hr = app.swapchain->ResizeBuffers(0, app.window.width, app.window.height, app.SwapchainResourceFormat,
                                                    DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH);
    if (FAILED(hr)) {
        printf("ResizeBuffers returned 0x%X\n", hr);
    }
}

void Window_OnKey(Window* window, unsigned virtualKeyCode, KeyMessageFlags keyMessageFlags)
{
    App& app = *APP_FROM_WINDOW(window);
    switch (virtualKeyCode) {
    case VK_ESCAPE:
        if (IsFreshDown(keyMessageFlags))
            PostQuitMessage(0);
        break;
    }
}

static bool
PollEvents()
{
    MSG msg = { };
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message != WM_QUIT) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else {
            return true;
        }
    }
    return false;
}

static void
RecordCommands(App& app, float const dt, unsigned const pfi, const ResourceAndRtv& backbuffer, GfxCmdListN *const cl)
{
    ResourceBarrier(cl, { MakeTransition(backbuffer.resource, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET) });

    const float BackgroundColor[4] = { 122 / 255.0f, 149 / 255.0f, 230 / 255.0f, 1.0f };
    const float BallColor[4] = { 1, 0, 0, 1 };

    cl->ClearRenderTargetView(backbuffer.rtv, BackgroundColor, 0, nullptr);

    constexpr int W = 128;
    constexpr int H = 128;
    int x = app.posPixelX;
    int newX = x + int(app.velPixelsPerSecX * dt);
    if (newX < 0 || app.window.width < newX + W) {
        app.velPixelsPerSecX = -app.velPixelsPerSecX;
        newX = x;
    }
    else {
        app.posPixelX = newX;
    }
    int const beginY = app.window.height / 2u;
    RECT rect = { newX, beginY, newX + W, beginY + H };
    cl->ClearRenderTargetView(backbuffer.rtv, BallColor, 1, &rect);

    ResourceBarrier(cl, { MakeTransition(backbuffer.resource, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT) });
}

static void
MainLoop(App& app)
{
    float const msPerTickF32 = 1000.0f / float(qpcTicksPerSecondI64);
    int64_t lastTicks = QueryPerformanceCounterI64();
    for (;;) {
        Sleep(1);
        if (PollEvents()) {
            return;
        }
        // Sleep if window is minimized:
        if (app.window.width < 2 || app.window.height < 2) {
            Sleep(260);
            continue;
        }
        if (app.window.resize) {
            app.window.resize = false;
            WaitForIdle(app);
            ResizeSwapchain(app);
        }

        // Phase 0 reclaim per-frame resources:
        unsigned const pfi = app.frameCounter % MAX_FRAMES;
        PerFrame const& pf = app.perFrame[pfi];
        HANDLE const hWaitEvent = pf.hEvent;
        DWORD const waitRes = WaitForSingleObjectEx(hWaitEvent, 5000, FALSE);
        if (waitRes != WAIT_OBJECT_0)
        {
            DWORD const err = GetLastError();
            printf("WaitForIdle: WaitForMultipleObjectsEx=0x%X, GetLastError=0x%X\n", waitRes, err);
            __debugbreak();
        }
        ResetEvent(hWaitEvent);
        pf.cmdAllocator->Reset();
        pf.cmdList->Reset(pf.cmdAllocator, nullptr);

        RecordCommands(app, 0.016f, pfi, GetCurrentBackbufferObjects(app), pf.cmdList);
        pf.cmdList->Close();

        {
            ID3D12CommandList* cls[1] = { pf.cmdList };
            app.queue->ExecuteCommandLists(1, cls);
            const bool vsync = true;
            app.swapchain->Present(vsync ? 1 : 0, vsync ? 0 : DXGI_PRESENT_ALLOW_TEARING);
            UINT64 const fenceValue = app.nextFenceValue++;
            HRESULT const signalHr = app.queue->Signal(app.fence, fenceValue);
            if (FAILED(signalHr)) { __debugbreak(); }
            HRESULT const setEventHr = app.fence->SetEventOnCompletion(fenceValue, pf.hEvent);
            if (FAILED(setEventHr)) { __debugbreak(); }
        }
        app.frameCounter++;

        {
            int64_t const nowTicks = QueryPerformanceCounterI64();
            if (((app.frameCounter - 1) % 30) == 0) {
                char buf[256];
                float const ms = (nowTicks - lastTicks) * msPerTickF32;
                snprintf(buf, sizeof buf, "D3D12 LAB: ms = %5.3f (%5.2f fps)\n", ms, 1000.0f / ms);
                SetWindowTextA(app.window.hwnd, buf);
            }
            lastTicks = nowTicks;
        }
    }
}

int main()
{
    App app;

    // Enable the D3D12 debug layer.
    ScopedRelease<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController.ptr)))) {
        OutputDebugStringA("Calling ID3D12Debug::EnableDebugLayer().\n");
        debugController->EnableDebugLayer();
    }
    else {
        OutputDebugStringA("FAILED: D3D12GetDebugInterface().\n");
    }

    CheckedHRESULT chr;
    {
        {
            ScopedRelease<ID3D12Device> device0;
            chr << D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device0.ptr));
            device0->QueryInterface(&app.device.ptr);
        }
        ID3D12Device7* const device = app.device;

        {
            D3D12_COMMAND_QUEUE_DESC desc = {
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
                D3D12_COMMAND_QUEUE_FLAG_NONE
            };
            chr << device->CreateCommandQueue(&desc, IID_PPV_ARGS(&app.queue.ptr));
        }

        chr << device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&app.fence.ptr));

        {
            D3D12_DESCRIPTOR_HEAP_DESC desc = {
                D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                64,
            };
            device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&app.rtvHeap.ptr));
            app.bytesPerRtv = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        }

        for (int i = 0; i < MAX_FRAMES; ++i) {
            PerFrame& pf = app.perFrame[i];
            pf.hEvent = CreateEventA(
                NULL,  // default security attributes
                TRUE,  // manual-reset event
                TRUE,  // initial state is signaled
                NULL); // name
            if (!pf.hEvent || GetLastError() != 0) {
                return 1;
            }
            ScopedRelease<ID3D12GraphicsCommandList> cl0;
            chr << device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&pf.cmdAllocator.ptr));
            chr << device->CreateCommandList(DefaultNodeMask, D3D12_COMMAND_LIST_TYPE_DIRECT, pf.cmdAllocator, nullptr, IID_PPV_ARGS(&cl0.ptr));
            cl0->QueryInterface(&pf.cmdList.ptr);
            pf.cmdList->Close();
        }
    }

    Window_Construct(&app.window, 960, 540, L"D3D12 Lab: Initial Window Title");
    {
        DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
        swapChainDesc.Width = app.window.width;
        swapChainDesc.Height = app.window.height;
        swapChainDesc.Format = app.SwapchainResourceFormat;
        swapChainDesc.SampleDesc = { 1, 0 };
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.BufferCount = 1 + MAX_FRAMES; // always includes frontbuffer ???
        swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH; // | DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

        DXGI_SWAP_CHAIN_FULLSCREEN_DESC swapChainFSDesc = {};
        swapChainFSDesc.Windowed = TRUE;

        UINT dxgiFactoryFlags = 0;
#if defined(_DEBUG)
        dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif
        ScopedRelease<IDXGIFactory4> dxgiFactory;
        chr << CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&dxgiFactory.ptr));

        {
            ScopedRelease<IDXGISwapChain1> swapchain1;
            chr << dxgiFactory->CreateSwapChainForHwnd(app.queue, app.window.hwnd,
                &swapChainDesc, &swapChainFSDesc, nullptr, &swapchain1.ptr);
            swapchain1->QueryInterface(&app.swapchain);
        }
    }

    QueryPerformanceFrequency(reinterpret_cast<LARGE_INTEGER*>(&qpcTicksPerSecondI64));

    ShowWindow(app.window.hwnd, SW_SHOWNORMAL);
    MainLoop(app);
    WaitForIdle(app);

    puts("Bye!");
    return 0;
}
