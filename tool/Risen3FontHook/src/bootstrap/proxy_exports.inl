BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        DisableThreadLibraryCalls(module);
        Log(L"[Risen3FontHookTest] d3d9 proxy loaded; starting hook init thread.\n");
        HANDLE thread = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
        if (thread) {
            CloseHandle(thread);
        }
    } else if (reason == DLL_PROCESS_DETACH) {
        UninstallHook();
        ReleaseGeneratedResources();
        g_realD3D9 = nullptr;
    }

    return TRUE;
}

extern "C" IDirect3D9* WINAPI Direct3DCreate9(UINT sdkVersion)
{
    using Fn = IDirect3D9* (WINAPI*)(UINT);
    auto* fn = reinterpret_cast<Fn>(GetRealD3D9Proc("Direct3DCreate9"));
    return fn ? fn(sdkVersion) : nullptr;
}

extern "C" HRESULT WINAPI Direct3DCreate9Ex(UINT sdkVersion, IDirect3D9Ex** d3d9)
{
    using Fn = HRESULT (WINAPI*)(UINT, IDirect3D9Ex**);
    auto* fn = reinterpret_cast<Fn>(GetRealD3D9Proc("Direct3DCreate9Ex"));
    return fn ? fn(sdkVersion, d3d9) : E_FAIL;
}

extern "C" void* WINAPI Direct3DShaderValidatorCreate9()
{
    using Fn = void* (WINAPI*)();
    auto* fn = reinterpret_cast<Fn>(GetRealD3D9Proc("Direct3DShaderValidatorCreate9"));
    return fn ? fn() : nullptr;
}

extern "C" void WINAPI PSGPError()
{
    using Fn = void (WINAPI*)();
    auto* fn = reinterpret_cast<Fn>(GetRealD3D9Proc("PSGPError"));
    if (fn) {
        fn();
    }
}

extern "C" void WINAPI PSGPSampleTexture()
{
    using Fn = void (WINAPI*)();
    auto* fn = reinterpret_cast<Fn>(GetRealD3D9Proc("PSGPSampleTexture"));
    if (fn) {
        fn();
    }
}

extern "C" void WINAPI DebugSetLevel(DWORD level)
{
    using Fn = void (WINAPI*)(DWORD);
    auto* fn = reinterpret_cast<Fn>(GetRealD3D9Proc("DebugSetLevel"));
    if (fn) {
        fn(level);
    }
}

extern "C" void WINAPI DebugSetMute()
{
    using Fn = void (WINAPI*)();
    auto* fn = reinterpret_cast<Fn>(GetRealD3D9Proc("DebugSetMute"));
    if (fn) {
        fn();
    }
}

extern "C" int WINAPI D3DPERF_BeginEvent(D3DCOLOR color, LPCWSTR name)
{
    using Fn = int (WINAPI*)(D3DCOLOR, LPCWSTR);
    auto* fn = reinterpret_cast<Fn>(GetRealD3D9Proc("D3DPERF_BeginEvent"));
    return fn ? fn(color, name) : 0;
}

extern "C" int WINAPI D3DPERF_EndEvent()
{
    using Fn = int (WINAPI*)();
    auto* fn = reinterpret_cast<Fn>(GetRealD3D9Proc("D3DPERF_EndEvent"));
    return fn ? fn() : 0;
}

extern "C" DWORD WINAPI D3DPERF_GetStatus()
{
    using Fn = DWORD (WINAPI*)();
    auto* fn = reinterpret_cast<Fn>(GetRealD3D9Proc("D3DPERF_GetStatus"));
    return fn ? fn() : 0;
}

extern "C" BOOL WINAPI D3DPERF_QueryRepeatFrame()
{
    using Fn = BOOL (WINAPI*)();
    auto* fn = reinterpret_cast<Fn>(GetRealD3D9Proc("D3DPERF_QueryRepeatFrame"));
    return fn ? fn() : FALSE;
}

extern "C" void WINAPI D3DPERF_SetMarker(D3DCOLOR color, LPCWSTR name)
{
    using Fn = void (WINAPI*)(D3DCOLOR, LPCWSTR);
    auto* fn = reinterpret_cast<Fn>(GetRealD3D9Proc("D3DPERF_SetMarker"));
    if (fn) {
        fn(color, name);
    }
}

extern "C" void WINAPI D3DPERF_SetOptions(DWORD options)
{
    using Fn = void (WINAPI*)(DWORD);
    auto* fn = reinterpret_cast<Fn>(GetRealD3D9Proc("D3DPERF_SetOptions"));
    if (fn) {
        fn(options);
    }
}

extern "C" void WINAPI D3DPERF_SetRegion(D3DCOLOR color, LPCWSTR name)
{
    using Fn = void (WINAPI*)(D3DCOLOR, LPCWSTR);
    auto* fn = reinterpret_cast<Fn>(GetRealD3D9Proc("D3DPERF_SetRegion"));
    if (fn) {
        fn(color, name);
    }
}
