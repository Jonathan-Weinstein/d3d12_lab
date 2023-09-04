#pragma once

#include <stdint.h>

#include <stdlib.h>

#include <initializer_list>

#include <Windows.h>

#include <d3d12.h>

#define DefaultNodeMask 0 // means same thing as (1 << 0)

struct view {
    const char* ptr;
    unsigned length;
};
inline constexpr view operator""_view(const char* literal, unsigned long long n)
{
    return { literal, unsigned(n) }; // n = strlen(literal)
}

inline int64_t QueryPerformanceCounterI64()
{
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    return li.QuadPart;
}

struct CheckedHRESULT {
    HRESULT hr = S_OK;
    void operator<<(HRESULT newHR) // "ThrowIfFailed", but does exit and less parens...
    {
        if (FAILED(newHR)) {
            __debugbreak();
            exit(int(hr));
        }
        this->hr = newHR;
    }
};

template<typename T>
struct ScopedRelease {
    T* ptr;
    constexpr ScopedRelease(const ScopedRelease& other) = delete;
    ScopedRelease& operator=(const ScopedRelease& other) = delete;
    constexpr ScopedRelease(T* p = nullptr) : ptr(p) { }
    ~ScopedRelease() { if (T* const p = ptr) p->Release(); }
    operator T* () const { return ptr; }
    T* operator->() const { return ptr; }
};

static const D3D12_HEAP_PROPERTIES DeviceLocalHeapProperties = { D3D12_HEAP_TYPE_DEFAULT };
static const D3D12_HEAP_PROPERTIES CpuCachedHeapProperties = { D3D12_HEAP_TYPE_READBACK };

constexpr inline D3D12_RESOURCE_BARRIER MakeTransition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    return {
        D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
        D3D12_RESOURCE_BARRIER_FLAG_NONE,
        { resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after, }
    };
}

inline void ResourceBarrier(ID3D12GraphicsCommandList* cl, std::initializer_list<D3D12_RESOURCE_BARRIER> barriers)
{
    cl->ResourceBarrier(UINT(barriers.size()), barriers.begin());
}
