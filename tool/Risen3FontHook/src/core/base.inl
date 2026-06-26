#include <windows.h>
#include <d3d9.h>

#include <cstdint>
#include <cwchar>
#include <cstring>

namespace {

constexpr uintptr_t kFontVtableRva = 0x9FDC28;
constexpr uintptr_t kGetGlyphMetricsSlotOffset = 0x40;
constexpr uintptr_t kDrawGlyphSlotOffset = 0x50;
constexpr uintptr_t kExpectedGetGlyphMetricsRva = 0x222B60;
constexpr uintptr_t kExpectedDrawGlyphRva = 0x222C80;
constexpr uintptr_t kBuildGlyphCommandRva = 0x282B00;
constexpr uintptr_t kSubmitGlyphCommandRva = 0x282360;
constexpr uintptr_t kCommandResourceWritePatchRva = 0x282F19;
constexpr uintptr_t kBuildUvFromFontResourceRva = 0x281810;
constexpr uintptr_t kAtlasGetterRva = 0x78B230;
constexpr uintptr_t kBindTextureFunctionRva = 0x78C840;
constexpr uintptr_t kAtlasTextureGetterKeyRva = 0xAA1F38;
constexpr uintptr_t kFlushGlyphCommandsRva = 0x2827A0;

using GetGlyphMetricsFn = int(__fastcall*)(void* font, wchar_t ch, void* outMetrics);
using DrawGlyphFn = int(__fastcall*)(
    void* font,
    void* renderContext,
    const void* metrics,
    const void* position,
    const void* bounds,
    int flags1,
    int flags2,
    int flags3,
    float scale);
using BuildGlyphCommandFn = int(__fastcall*)(
    void* renderContext,
    void* resource,
    void* unused,
    const void* rect,
    const void* colorOrUv,
    int arg5,
    int arg6,
    int arg7);
using SubmitGlyphCommandFn = int(__fastcall*)(void* renderContext, void* clipRect);
using BuildUvFromFontResourceFn = int(__fastcall*)(
    void* canvas,
    void* fontResource,
    const void* rect,
    void* outUv,
    void* outWidth,
    void* outHeight);
using AtlasGetterFn = uintptr_t(__fastcall*)(void* fontObject);
using AtlasTextureGetterFn = int(__fastcall*)(void* atlasResource, const void* key, void* outTexture);
using BindTextureFn = int(__fastcall*)(void* wrapperThis, uintptr_t stage, void* gfxTexture);
using FlushGlyphCommandsFn = int(__fastcall*)(void* renderContext);

struct GlyphMetrics {
    float x1;
    float y1;
    float x2;
    float y2;
    float advance;
    float bearingX;
    float bearingY;
};

struct ChineseGlyphEntry {
    wchar_t codepoint;
    wchar_t fallback;
    float rect[4];
    float plane[4];
    bool hasPlaneBounds;
    float advanceExtra;
    float naturalAdvance;
    bool enabled;
};

struct ExternalAtlasBitmap {
    uint32_t* pixels;
    uint32_t width;
    uint32_t height;
};

struct DdsPixelFormat {
    DWORD size;
    DWORD flags;
    DWORD fourCC;
    DWORD rgbBitCount;
    DWORD rBitMask;
    DWORD gBitMask;
    DWORD bBitMask;
    DWORD aBitMask;
};

struct DdsHeader {
    DWORD size;
    DWORD flags;
    DWORD height;
    DWORD width;
    DWORD pitchOrLinearSize;
    DWORD depth;
    DWORD mipMapCount;
    DWORD reserved1[11];
    DdsPixelFormat pixelFormat;
    DWORD caps;
    DWORD caps2;
    DWORD caps3;
    DWORD caps4;
    DWORD reserved2;
};

struct DdsHeaderDxt10 {
    DWORD dxgiFormat;
    DWORD resourceDimension;
    DWORD miscFlag;
    DWORD arraySize;
    DWORD miscFlags2;
};

constexpr wchar_t kProbeChineseChar = L'\u4E2D';
constexpr wchar_t kFallbackGlyphChar = L'V';
constexpr float kProbeAdvanceExtra = 30.0f;
constexpr size_t kExternalGlyphCount = 4096;
constexpr size_t kObservedFontCount = 16;
constexpr size_t kFontStateCount = 16;
constexpr LONG kMarkedBuildCommandLogLimit = 8;
constexpr LONG kPostBuildCommandLogLimit = 8;
constexpr LONG kCommandResourceWriteLogLimit = 8;
constexpr LONG kSubmitGlyphCommandLogLimit = 8;
constexpr LONG kBuildUvLogLimit = 16;
constexpr LONG kTextureReplaceLogLimit = 12;
constexpr LONG kBindTextureProbeLogLimit = 64;
constexpr LONG kFlushTextureLogLimit = 4;
constexpr LONG kDrawGlyphDiagnosticLogLimit = 128;
constexpr DWORD kConfigPollIntervalMs = 1000;
constexpr uintptr_t kFontResourceOffset = 0x60;
constexpr size_t kShadowFontResourceCopySize = 0x100;
constexpr size_t kShadowFontObjectCopySize = 0x100;
constexpr size_t kShadowAtlasPrefixSize = 0x30;
constexpr size_t kShadowAtlasCopySize = 0x100;
constexpr DWORD kDdsMagic = 0x20534444; // "DDS "
constexpr DWORD kDdsPfAlphaPixels = 0x00000001;
constexpr DWORD kDdsPfAlpha = 0x00000002;
constexpr DWORD kDdsPfFourCc = 0x00000004;
constexpr DWORD kDdsPfRgb = 0x00000040;
constexpr DWORD kDdsPfLuminance = 0x00020000;

GetGlyphMetricsFn g_originalGetGlyphMetrics = nullptr;
DrawGlyphFn g_originalDrawGlyph = nullptr;
BuildGlyphCommandFn g_originalBuildGlyphCommand = nullptr;
SubmitGlyphCommandFn g_originalSubmitGlyphCommand = nullptr;
BuildUvFromFontResourceFn g_originalBuildUvFromFontResource = nullptr;
AtlasGetterFn g_originalAtlasGetter = nullptr;
BindTextureFn g_originalBindTexture = nullptr;
FlushGlyphCommandsFn g_originalFlushGlyphCommands = nullptr;
uintptr_t* g_vtableSlot = nullptr;
uintptr_t g_originalSlotValue = 0;
uintptr_t* g_drawGlyphSlot = nullptr;
uintptr_t g_originalDrawGlyphSlotValue = 0;
bool g_installed = false;
LONG g_loggedProbe = 0;
LONG g_loggedDrawProbe = 0;
LONG g_loggedResourceProbe = 0;
LONG g_loggedReplacementResolved = 0;
LONG g_loggedReplacementApplied = 0;
LONG g_loggedBuildCommand = 0;
LONG g_markedDrawGlyphDepth = 0;
LONG g_markedBuildCommandLogCount = 0;
LONG g_postBuildCommandLogCount = 0;
LONG g_commandResourceWriteLogCount = 0;
LONG g_submitGlyphCommandLogCount = 0;
LONG g_buildUvLogCount = 0;
LONG g_textureReplaceLogCount = 0;
LONG g_bindTextureProbeLogCount = 0;
LONG g_flushTextureLogCount = 0;
LONG g_bindTextureDecisionLogCount = 0;
LONG g_atlasDumpLogCount = 0;
LONG g_drawGlyphDiagnosticLogCount = 0;
LONG g_pendingAtlasSwapCount = 0;
LONG g_shadowResourceSerial = 0;
void* g_pendingMarkedMetrics = nullptr;
ChineseGlyphEntry* g_pendingMarkedGlyph = nullptr;
ChineseGlyphEntry* g_activeMarkedGlyph = nullptr;
uintptr_t g_replacementResource = 0;
int g_replacementIndex = -1;
bool g_enableReplacement = false;
bool g_enableCommandWriteHook = true;
bool g_enableSubmitHook = true;
bool g_enableBuildUvHook = true;
bool g_enableBuildUvRectOverride = false;
bool g_enableAtlasGetterHook = true;
bool g_enableTextureReplaceHook = true;
bool g_enableShadowResource = true;
bool g_enableFullFontAtlasReplace = false;
bool g_replaceProbeTextures = false;
bool g_enableFlushHook = false;
HMODULE g_module = nullptr;
void* g_buildGlyphCommandTrampoline = nullptr;
BYTE g_buildGlyphCommandOriginalBytes[16]{};
bool g_buildGlyphCommandHookInstalled = false;
uintptr_t g_activeMarkedReplacementResource = 0;
BYTE g_commandResourceWriteOriginalBytes[16]{};
void* g_commandResourceWriteStub = nullptr;
bool g_commandResourceWriteHookInstalled = false;
void* g_submitGlyphCommandTrampoline = nullptr;
BYTE g_submitGlyphCommandOriginalBytes[16]{};
bool g_submitGlyphCommandHookInstalled = false;
void* g_buildUvFromFontResourceTrampoline = nullptr;
BYTE g_buildUvFromFontResourceOriginalBytes[16]{};
bool g_buildUvFromFontResourceHookInstalled = false;
BYTE g_atlasGetterOriginalBytes[16]{};
void* g_atlasGetterTrampoline = nullptr;
bool g_atlasGetterHookInstalled = false;
BYTE g_bindTextureOriginalBytes[16]{};
void* g_bindTextureTrampoline = nullptr;
bool g_bindTextureHookInstalled = false;
uintptr_t g_markedResource = 0;
uintptr_t g_markedD3DTexture = 0;
uintptr_t g_markedAtlasResource = 0;
uintptr_t g_replacementAtlasResource = 0;
int g_replacementAtlasIndex = -1;
LONG g_atlasGetterLogCount = 0;
LONG g_configReloadLogCount = 0;
float g_buildUvRectOverride[4] = {0.0f, 0.0f, 56.0f, 56.0f};
ChineseGlyphEntry g_singleChineseGlyph = {
    kProbeChineseChar,
    kFallbackGlyphChar,
    {0.0f, 0.0f, 56.0f, 56.0f},
    {0.0f, 0.0f, 0.0f, 0.0f},
    false,
    kProbeAdvanceExtra,
    0.0f,
    true
};
ChineseGlyphEntry g_externalGlyphs[kExternalGlyphCount]{};
size_t g_externalGlyphCount = 0;
LONG g_externalGlyphLock = 0;
ExternalAtlasBitmap g_externalAtlas{};
LONG g_externalAtlasLock = 0;
bool g_enableExternalAtlas = false;
DWORD g_textureReplaceWindowMs = 250;
DWORD g_lastMarkedGlyphTick = 0;
wchar_t g_externalGlyphMapPath[MAX_PATH]{};
wchar_t g_externalAtlasBmpPath[MAX_PATH]{};
uintptr_t g_forcedTargetD3DTexture = 0;
IDirect3DTexture9* g_testAtlasTexture = nullptr;
IDirect3DDevice9* g_testAtlasDevice = nullptr;
LONG g_testAtlasLock = 0;
void* g_flushGlyphCommandsTrampoline = nullptr;
BYTE g_flushGlyphCommandsOriginalBytes[24]{};
bool g_flushGlyphCommandsHookInstalled = false;
HMODULE g_realD3D9 = nullptr;
int g_msdfPixelSize = 42;
float g_msdfAdvanceExtra = 0.0f;
bool g_enableMsdfAtlas = false;
wchar_t g_msdfAtlasJsonPath[MAX_PATH]{};
float g_msdfAdvanceScale = 1.0f;
float g_msdfMetricOffsetY = -40.0f;
bool g_enableDrawGlyphDiagnostics = false;

struct ObservedFont {
    void* font;
    uintptr_t resource;
};

struct ObservedResource {
    uintptr_t resource;
};

struct ScaleContext {
    uintptr_t resource;
    int scale1000;
    float advanceScale;
};

struct FontState {
    void* font;
    uintptr_t originalResource;
    uintptr_t atlasResource;
    uintptr_t shadowResource;
    uintptr_t shadowFontObject;
    uintptr_t shadowAtlasAllocation;
    uintptr_t shadowAtlasResource;
    bool ownsAtlas;
};

ObservedFont g_observedFonts[kObservedFontCount]{};
ObservedResource g_observedResources[kObservedFontCount]{};
ScaleContext g_scaleContexts[kFontStateCount]{};
FontState g_fontStates[kFontStateCount]{};
LONG g_observedFontLock = 0;
LONG g_observedResourceLock = 0;
LONG g_scaleContextLock = 0;
LONG g_fontStateLock = 0;
wchar_t g_configPath[MAX_PATH]{};
FILETIME g_configLastWriteTime{};

void Log(const wchar_t* message)
{
    OutputDebugStringW(message);
}

HMODULE LoadRealD3D9()
{
    if (g_realD3D9) {
        return g_realD3D9;
    }

    wchar_t path[MAX_PATH]{};
    const UINT length = GetSystemDirectoryW(path, ARRAYSIZE(path));
    if (length == 0 || length >= ARRAYSIZE(path)) {
        return nullptr;
    }

    lstrcatW(path, L"\\d3d9.dll");
    g_realD3D9 = LoadLibraryW(path);
    if (!g_realD3D9) {
        Log(L"[Risen3FontHookTest] Failed to load system d3d9.dll.\n");
    } else {
        Log(L"[Risen3FontHookTest] Loaded system d3d9.dll for proxy forwarding.\n");
    }

    return g_realD3D9;
}

FARPROC GetRealD3D9Proc(const char* name)
{
    HMODULE d3d9 = LoadRealD3D9();
    return d3d9 ? GetProcAddress(d3d9, name) : nullptr;
}

class SpinLock {
public:
    explicit SpinLock(LONG* lock) : lock_(lock)
    {
        while (InterlockedCompareExchange(lock_, 1, 0) != 0) {
            Sleep(0);
        }
    }

    ~SpinLock()
    {
        InterlockedExchange(lock_, 0);
    }

    SpinLock(const SpinLock&) = delete;
    SpinLock& operator=(const SpinLock&) = delete;

private:
    LONG* lock_;
};

bool WritePointer(uintptr_t* address, uintptr_t value)
{
    DWORD oldProtect = 0;
    if (!VirtualProtect(address, sizeof(uintptr_t), PAGE_EXECUTE_READWRITE, &oldProtect)) {
        return false;
    }

    *address = value;
    FlushInstructionCache(GetCurrentProcess(), address, sizeof(uintptr_t));

    DWORD ignored = 0;
    VirtualProtect(address, sizeof(uintptr_t), oldProtect, &ignored);
    return true;
}

bool WriteAbsoluteJump(void* address, void* destination, size_t patchSize)
{
    if (patchSize < 12) {
        return false;
    }

    BYTE patch[12] = {
        0x48, 0xB8,
        0, 0, 0, 0, 0, 0, 0, 0,
        0xFF, 0xE0
    };
    *reinterpret_cast<uint64_t*>(&patch[2]) = reinterpret_cast<uint64_t>(destination);

    DWORD oldProtect = 0;
    if (!VirtualProtect(address, patchSize, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        return false;
    }

    memcpy(address, patch, sizeof(patch));
    if (patchSize > sizeof(patch)) {
        memset(static_cast<BYTE*>(address) + sizeof(patch), 0x90, patchSize - sizeof(patch));
    }

    FlushInstructionCache(GetCurrentProcess(), address, patchSize);

    DWORD ignored = 0;
    VirtualProtect(address, patchSize, oldProtect, &ignored);
    return true;
}

