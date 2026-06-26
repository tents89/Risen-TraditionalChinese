bool InstallInlineHook(void* target, void* hook, size_t patchSize, BYTE* originalBytes, void** trampoline)
{
    if (!target || !hook || !originalBytes || !trampoline || patchSize < 12) {
        return false;
    }

    memcpy(originalBytes, target, patchSize);

    const size_t trampolineSize = patchSize + 12;
    void* localTrampoline = VirtualAlloc(nullptr, trampolineSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!localTrampoline) {
        return false;
    }

    memcpy(localTrampoline, originalBytes, patchSize);
    BYTE* jumpBack = static_cast<BYTE*>(localTrampoline) + patchSize;
    BYTE backPatch[12] = {
        0x48, 0xB8,
        0, 0, 0, 0, 0, 0, 0, 0,
        0xFF, 0xE0
    };
    *reinterpret_cast<uint64_t*>(&backPatch[2]) =
        reinterpret_cast<uint64_t>(static_cast<BYTE*>(target) + patchSize);
    memcpy(jumpBack, backPatch, sizeof(backPatch));
    FlushInstructionCache(GetCurrentProcess(), localTrampoline, trampolineSize);

    if (!WriteAbsoluteJump(target, hook, patchSize)) {
        VirtualFree(localTrampoline, 0, MEM_RELEASE);
        return false;
    }

    *trampoline = localTrampoline;
    return true;
}

bool RestoreInlineHook(void* target, size_t patchSize, const BYTE* originalBytes)
{
    if (!target || !originalBytes || patchSize == 0) {
        return false;
    }

    DWORD oldProtect = 0;
    if (!VirtualProtect(target, patchSize, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        return false;
    }

    memcpy(target, originalBytes, patchSize);
    FlushInstructionCache(GetCurrentProcess(), target, patchSize);

    DWORD ignored = 0;
    VirtualProtect(target, patchSize, oldProtect, &ignored);
    return true;
}

BYTE* EmitBytes(BYTE* out, const BYTE* bytes, size_t size)
{
    memcpy(out, bytes, size);
    return out + size;
}

BYTE* EmitMovRaxImm64(BYTE* out, uintptr_t value)
{
    *out++ = 0x48;
    *out++ = 0xB8;
    *reinterpret_cast<uintptr_t*>(out) = value;
    return out + sizeof(uintptr_t);
}

BYTE* EmitAbsoluteJump(BYTE* out, uintptr_t destination)
{
    const BYTE jump[] = {
        0xFF, 0x25, 0x00, 0x00, 0x00, 0x00
    };
    out = EmitBytes(out, jump, sizeof(jump));
    *reinterpret_cast<uintptr_t*>(out) = destination;
    return out + sizeof(uintptr_t);
}

void MarkMetricsPointer(void* metrics, ChineseGlyphEntry* glyph)
{
    if (!metrics) {
        return;
    }

    g_pendingMarkedMetrics = metrics;
    g_pendingMarkedGlyph = glyph;
}

ChineseGlyphEntry* ConsumeMarkedMetricsPointer(const void* metrics)
{
    if (!metrics || !g_pendingMarkedMetrics) {
        return nullptr;
    }

    if (g_pendingMarkedMetrics == metrics) {
        ChineseGlyphEntry* glyph = g_pendingMarkedGlyph;
        g_pendingMarkedMetrics = nullptr;
        g_pendingMarkedGlyph = nullptr;
        return glyph;
    }

    return nullptr;
}

uintptr_t ReadFontResource(void* font)
{
    if (!font) {
        return 0;
    }

    return *reinterpret_cast<uintptr_t*>(static_cast<BYTE*>(font) + kFontResourceOffset);
}

uintptr_t ReadAtlasResourceFromFontResource(uintptr_t fontResource);
IDirect3DDevice9* GetDeviceFromD3DTexture(uintptr_t d3dTexture);
bool ResolveConfigRelativePath(const wchar_t* value, wchar_t* out, DWORD outCount);

bool SafeReadQword(uintptr_t address, uintptr_t* out)
{
    if (address < 0x10000 || !out) {
        return false;
    }

    __try {
        *out = *reinterpret_cast<uintptr_t*>(address);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *out = 0;
        return false;
    }
}

bool SafeReadDword(uintptr_t address, DWORD* out)
{
    if (address < 0x10000 || !out) {
        return false;
    }

    __try {
        *out = *reinterpret_cast<DWORD*>(address);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *out = 0;
        return false;
    }
}

bool SafeReadFloat(uintptr_t address, float* out)
{
    if (address < 0x10000 || !out) {
        return false;
    }

    __try {
        *out = *reinterpret_cast<float*>(address);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *out = 0.0f;
        return false;
    }
}

bool SafeWriteFloat(uintptr_t address, float value)
{
    if (address < 0x10000) {
        return false;
    }

    __try {
        *reinterpret_cast<float*>(address) = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SafeWriteFloat4(uintptr_t address, float a, float b, float c, float d)
{
    return SafeWriteFloat(address + 0x00, a) &&
        SafeWriteFloat(address + 0x04, b) &&
        SafeWriteFloat(address + 0x08, c) &&
        SafeWriteFloat(address + 0x0C, d);
}

bool SafeReadFloat4(uintptr_t address, float out[4])
{
    if (!out) {
        return false;
    }

    return SafeReadFloat(address + 0x00, &out[0]) &&
        SafeReadFloat(address + 0x04, &out[1]) &&
        SafeReadFloat(address + 0x08, &out[2]) &&
        SafeReadFloat(address + 0x0C, &out[3]);
}

bool SafeWriteQword(uintptr_t address, uintptr_t value)
{
    if (address < 0x10000) {
        return false;
    }

    __try {
        *reinterpret_cast<uintptr_t*>(address) = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SafeCopyMemory(uintptr_t destination, uintptr_t source, size_t size)
{
    if (destination < 0x10000 || source < 0x10000 || size == 0) {
        return false;
    }

    __try {
        memcpy(reinterpret_cast<void*>(destination), reinterpret_cast<const void*>(source), size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void ReadAscii4(uintptr_t address, wchar_t out[5])
{
    out[0] = L'?';
    out[1] = L'?';
    out[2] = L'?';
    out[3] = L'?';
    out[4] = L'\0';

    DWORD value = 0;
    if (!SafeReadDword(address, &value)) {
        return;
    }

    const char* bytes = reinterpret_cast<const char*>(&value);
    for (int i = 0; i < 4; ++i) {
        const unsigned char ch = static_cast<unsigned char>(bytes[i]);
        out[i] = (ch >= 0x20 && ch <= 0xFF) ? static_cast<wchar_t>(ch) : L'.';
    }
}

bool IsLikelyFontResource(uintptr_t value)
{
    uintptr_t vtable = 0;
    uintptr_t tag = 0;
    if (!SafeReadQword(value + 0x00, &vtable) || !SafeReadQword(value + 0x08, &tag)) {
        return false;
    }

    HMODULE exe = GetModuleHandleW(nullptr);
    if (!exe) {
        return false;
    }

    const uintptr_t base = reinterpret_cast<uintptr_t>(exe);
    return vtable == base + 0x9CD268 && ((tag & 0xFF) == 0x12);
}

bool IsLikelyTextureObject(uintptr_t value)
{
    uintptr_t vtable = 0;
    DWORD width = 0;
    DWORD height = 0;
    wchar_t format[5]{};
    if (!SafeReadQword(value + 0x00, &vtable) ||
        !SafeReadDword(value + 0x10, &width) ||
        !SafeReadDword(value + 0x14, &height)) {
        return false;
    }

    ReadAscii4(value + 0x18, format);

    HMODULE exe = GetModuleHandleW(nullptr);
    if (!exe) {
        return false;
    }

    const uintptr_t base = reinterpret_cast<uintptr_t>(exe);
    const bool knownFormat =
        (format[0] == L'D' && format[1] == L'X' && format[2] == L'T' &&
            (format[3] == L'1' || format[3] == L'5'));
    return vtable == base + 0x9CD088 && width > 0 && height > 0 && knownFormat;
}

const wchar_t* ClassifyResourceOrTexture(uintptr_t value)
{
    if (IsLikelyFontResource(value)) {
        return L"font-resource";
    }

    if (IsLikelyTextureObject(value)) {
        return L"texture";
    }

    return L"unknown";
}

uintptr_t ReadD3DTextureFromImageResource(uintptr_t resource)
{
    uintptr_t textureWrapper = 0;
    uintptr_t d3dTexture = 0;
    if (!SafeReadQword(resource + 0x20, &textureWrapper) || !textureWrapper) {
        return 0;
    }

    SafeReadQword(textureWrapper + 0x10, &d3dTexture);
    if (d3dTexture) {
        return d3dTexture;
    }

    SafeReadQword(textureWrapper + 0x38, &d3dTexture);
    return d3dTexture;
}

void DumpAtlasTextureCandidates(uintptr_t atlasResource)
{
    if (!atlasResource) {
        return;
    }

    const LONG dumpIndex = InterlockedIncrement(&g_atlasDumpLogCount);
    if (dumpIndex > 4) {
        return;
    }

    uintptr_t values[16]{};
    for (size_t i = 0; i < ARRAYSIZE(values); ++i) {
        SafeReadQword(atlasResource + i * sizeof(uintptr_t), &values[i]);
    }

    wchar_t line1[768]{};
    wsprintfW(
        line1,
        L"[Risen3FontHookTest] AtlasDump[%d] atlas=%p +00=%p +08=%p +10=%p +18=%p +20=%p +28=%p +30=%p +38=%p\n",
        dumpIndex,
        reinterpret_cast<void*>(atlasResource),
        reinterpret_cast<void*>(values[0]),
        reinterpret_cast<void*>(values[1]),
        reinterpret_cast<void*>(values[2]),
        reinterpret_cast<void*>(values[3]),
        reinterpret_cast<void*>(values[4]),
        reinterpret_cast<void*>(values[5]),
        reinterpret_cast<void*>(values[6]),
        reinterpret_cast<void*>(values[7]));
    Log(line1);

    wchar_t line2[768]{};
    wsprintfW(
        line2,
        L"[Risen3FontHookTest] AtlasDump[%d] atlas=%p +40=%p +48=%p +50=%p +58=%p +60=%p +68=%p +70=%p +78=%p\n",
        dumpIndex,
        reinterpret_cast<void*>(atlasResource),
        reinterpret_cast<void*>(values[8]),
        reinterpret_cast<void*>(values[9]),
        reinterpret_cast<void*>(values[10]),
        reinterpret_cast<void*>(values[11]),
        reinterpret_cast<void*>(values[12]),
        reinterpret_cast<void*>(values[13]),
        reinterpret_cast<void*>(values[14]),
        reinterpret_cast<void*>(values[15]));
    Log(line2);

    const DWORD wrapperOffsets[] = {0x20, 0x28, 0x30, 0x38, 0x40};
    const DWORD textureOffsets[] = {0x08, 0x10, 0x18, 0x20, 0x38};
    for (DWORD wrapperOffset : wrapperOffsets) {
        uintptr_t wrapper = 0;
        if (!SafeReadQword(atlasResource + wrapperOffset, &wrapper) || !wrapper) {
            continue;
        }

        for (DWORD textureOffset : textureOffsets) {
            uintptr_t texture = 0;
            if (!SafeReadQword(wrapper + textureOffset, &texture) || !texture) {
                continue;
            }

            uintptr_t textureVtable = 0;
            SafeReadQword(texture, &textureVtable);

            wchar_t message[512]{};
            wsprintfW(
                message,
                L"[Risen3FontHookTest] AtlasTextureCandidate atlas=%p wrapper=atlas+%02X:%p texture=wrapper+%02X:%p textureVtable=%p\n",
                reinterpret_cast<void*>(atlasResource),
                wrapperOffset,
                reinterpret_cast<void*>(wrapper),
                textureOffset,
                reinterpret_cast<void*>(texture),
                reinterpret_cast<void*>(textureVtable));
            Log(message);
        }
    }
}

uintptr_t ReadD3DTextureFromGfxTextureView(uintptr_t gfxTexture)
{
    if (!gfxTexture) {
        return 0;
    }

    uintptr_t d3dTexture = 0;
    SafeReadQword(gfxTexture + 0x08, &d3dTexture);
    if (d3dTexture) {
        return d3dTexture;
    }

    SafeReadQword(gfxTexture + 0x38, &d3dTexture);
    return d3dTexture;
}

bool FindD3DTextureSlotInGfxTextureView(uintptr_t gfxTexture, uintptr_t* slot, uintptr_t* value, DWORD* offset)
{
    if (!gfxTexture || !slot || !value || !offset) {
        return false;
    }

    uintptr_t d3dTexture = 0;
    if (SafeReadQword(gfxTexture + 0x08, &d3dTexture) && d3dTexture) {
        *slot = gfxTexture + 0x08;
        *value = d3dTexture;
        *offset = 0x08;
        return true;
    }

    if (SafeReadQword(gfxTexture + 0x38, &d3dTexture) && d3dTexture) {
        *slot = gfxTexture + 0x38;
        *value = d3dTexture;
        *offset = 0x38;
        return true;
    }

    *slot = 0;
    *value = 0;
    *offset = 0;
    return false;
}

IDirect3DDevice9* GetDeviceFromD3DTexture(uintptr_t d3dTexture)
{
    if (!d3dTexture) {
        return nullptr;
    }

    uintptr_t vtable = 0;
    if (!SafeReadQword(d3dTexture, &vtable) || vtable < 0x10000) {
        return nullptr;
    }

    uintptr_t method = 0;
    if (!SafeReadQword(vtable + 0x20, &method) || method < 0x10000) {
        return nullptr;
    }

    IDirect3DDevice9* device = nullptr;
    __try {
        auto* texture = reinterpret_cast<IDirect3DTexture9*>(d3dTexture);
        if (FAILED(texture->GetDevice(&device))) {
            device = nullptr;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        device = nullptr;
    }

    return device;
}

uintptr_t ResolveD3DTextureFromAtlas(uintptr_t atlasResource)
{
    if (!atlasResource) {
        return 0;
    }

    uintptr_t vtable = 0;
    uintptr_t method = 0;
    if (!SafeReadQword(atlasResource, &vtable) || !SafeReadQword(vtable, &method) ||
        method < 0x10000) {
        return 0;
    }

    HMODULE exe = GetModuleHandleW(nullptr);
    if (!exe) {
        return 0;
    }

    uintptr_t texture = 0;
    __try {
        auto* fn = reinterpret_cast<AtlasTextureGetterFn>(method);
        fn(
            reinterpret_cast<void*>(atlasResource),
            reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(exe) + kAtlasTextureGetterKeyRva),
            &texture);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        texture = 0;
    }

    const LONG logIndex = InterlockedIncrement(&g_atlasDumpLogCount);
    if (logIndex <= 8) {
        wchar_t message[512]{};
        wsprintfW(
            message,
            L"[Risen3FontHookTest] ResolveD3DTextureFromAtlas[%d] atlas=%p vtable=%p method0=%p texture=%p\n",
            logIndex,
            reinterpret_cast<void*>(atlasResource),
            reinterpret_cast<void*>(vtable),
            reinterpret_cast<void*>(method),
            reinterpret_cast<void*>(texture));
        Log(message);
    }

    return texture;
}

IDirect3DDevice9* GetDeviceFromAtlas(uintptr_t atlasResource)
{
    const uintptr_t texture = ResolveD3DTextureFromAtlas(atlasResource);
    return GetDeviceFromD3DTexture(texture);
}

void ResetGeneratedAtlasTexture();

DWORD MakeFourCc(char a, char b, char c, char d)
{
    return static_cast<DWORD>(static_cast<unsigned char>(a)) |
        (static_cast<DWORD>(static_cast<unsigned char>(b)) << 8) |
        (static_cast<DWORD>(static_cast<unsigned char>(c)) << 16) |
        (static_cast<DWORD>(static_cast<unsigned char>(d)) << 24);
}

uint8_t Expand5(uint16_t value)
{
    return static_cast<uint8_t>((value << 3) | (value >> 2));
}

uint8_t Expand6(uint16_t value)
{
    return static_cast<uint8_t>((value << 2) | (value >> 4));
}

uint32_t NormalizeAtlasPixel(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    const int rg = static_cast<int>(r) - static_cast<int>(g);
    const int rb = static_cast<int>(r) - static_cast<int>(b);
    const bool grayscale = rg >= -2 && rg <= 2 && rb >= -2 && rb <= 2;
    if (a == 0xFF && grayscale) {
        const uint8_t gray = static_cast<uint8_t>((static_cast<unsigned>(r) + g + b) / 3u);
        return (static_cast<uint32_t>(gray) << 24) | 0x00FFFFFFu;
    }

    return (static_cast<uint32_t>(a) << 24) |
        (static_cast<uint32_t>(r) << 16) |
        (static_cast<uint32_t>(g) << 8) |
        static_cast<uint32_t>(b);
}

uint32_t ColorFromRgb565(uint16_t c)
{
    const uint8_t r = Expand5(static_cast<uint16_t>((c >> 11) & 0x1F));
    const uint8_t g = Expand6(static_cast<uint16_t>((c >> 5) & 0x3F));
    const uint8_t b = Expand5(static_cast<uint16_t>(c & 0x1F));
    return NormalizeAtlasPixel(r, g, b, 0xFF);
}

uint8_t ExtractMask8(uint32_t value, uint32_t mask)
{
    if (!mask) {
        return 0;
    }

    uint32_t shift = 0;
    while (((mask >> shift) & 1u) == 0u && shift < 32u) {
        ++shift;
    }

    uint32_t bits = 0;
    uint32_t shifted = mask >> shift;
    while ((shifted & 1u) != 0u && bits < 32u) {
        ++bits;
        shifted >>= 1;
    }

    const uint32_t raw = (value & mask) >> shift;
    if (bits >= 8u) {
        return static_cast<uint8_t>(raw >> (bits - 8u));
    }

    const uint32_t maxValue = (1u << bits) - 1u;
    return maxValue ? static_cast<uint8_t>((raw * 255u + maxValue / 2u) / maxValue) : 0;
}

void WriteDecodedPixel(uint32_t* pixels, uint32_t width, uint32_t height, uint32_t x, uint32_t y, uint32_t argb)
{
    if (x < width && y < height) {
        pixels[static_cast<size_t>(y) * width + x] = argb;
    }
}

void DecodeBcAlphaBlock(const BYTE* block, uint8_t out[16])
{
    uint8_t values[8]{};
    values[0] = block[0];
    values[1] = block[1];
    if (values[0] > values[1]) {
        values[2] = static_cast<uint8_t>((6u * values[0] + 1u * values[1] + 3u) / 7u);
        values[3] = static_cast<uint8_t>((5u * values[0] + 2u * values[1] + 3u) / 7u);
        values[4] = static_cast<uint8_t>((4u * values[0] + 3u * values[1] + 3u) / 7u);
        values[5] = static_cast<uint8_t>((3u * values[0] + 4u * values[1] + 3u) / 7u);
        values[6] = static_cast<uint8_t>((2u * values[0] + 5u * values[1] + 3u) / 7u);
        values[7] = static_cast<uint8_t>((1u * values[0] + 6u * values[1] + 3u) / 7u);
    } else {
        values[2] = static_cast<uint8_t>((4u * values[0] + 1u * values[1] + 2u) / 5u);
        values[3] = static_cast<uint8_t>((3u * values[0] + 2u * values[1] + 2u) / 5u);
        values[4] = static_cast<uint8_t>((2u * values[0] + 3u * values[1] + 2u) / 5u);
        values[5] = static_cast<uint8_t>((1u * values[0] + 4u * values[1] + 2u) / 5u);
        values[6] = 0;
        values[7] = 255;
    }

    uint64_t indices = 0;
    for (int i = 0; i < 6; ++i) {
        indices |= static_cast<uint64_t>(block[2 + i]) << (8 * i);
    }
    for (int i = 0; i < 16; ++i) {
        out[i] = values[(indices >> (3 * i)) & 0x07u];
    }
}

void DecodeBcColorBlock(const BYTE* block, uint32_t out[16], bool dxt1Alpha)
{
    const uint16_t c0 = static_cast<uint16_t>(block[0] | (block[1] << 8));
    const uint16_t c1 = static_cast<uint16_t>(block[2] | (block[3] << 8));
    uint8_t r[4]{}, g[4]{}, b[4]{}, a[4]{255, 255, 255, 255};

    r[0] = Expand5(static_cast<uint16_t>((c0 >> 11) & 0x1F));
    g[0] = Expand6(static_cast<uint16_t>((c0 >> 5) & 0x3F));
    b[0] = Expand5(static_cast<uint16_t>(c0 & 0x1F));
    r[1] = Expand5(static_cast<uint16_t>((c1 >> 11) & 0x1F));
    g[1] = Expand6(static_cast<uint16_t>((c1 >> 5) & 0x3F));
    b[1] = Expand5(static_cast<uint16_t>(c1 & 0x1F));

    if (!dxt1Alpha || c0 > c1) {
        r[2] = static_cast<uint8_t>((2u * r[0] + r[1] + 1u) / 3u);
        g[2] = static_cast<uint8_t>((2u * g[0] + g[1] + 1u) / 3u);
        b[2] = static_cast<uint8_t>((2u * b[0] + b[1] + 1u) / 3u);
        r[3] = static_cast<uint8_t>((r[0] + 2u * r[1] + 1u) / 3u);
        g[3] = static_cast<uint8_t>((g[0] + 2u * g[1] + 1u) / 3u);
        b[3] = static_cast<uint8_t>((b[0] + 2u * b[1] + 1u) / 3u);
    } else {
        r[2] = static_cast<uint8_t>((r[0] + r[1]) / 2u);
        g[2] = static_cast<uint8_t>((g[0] + g[1]) / 2u);
        b[2] = static_cast<uint8_t>((b[0] + b[1]) / 2u);
        r[3] = g[3] = b[3] = 0;
        a[3] = 0;
    }

    const uint32_t indices = block[4] | (block[5] << 8) | (block[6] << 16) | (block[7] << 24);
    for (int i = 0; i < 16; ++i) {
        const uint32_t index = (indices >> (2 * i)) & 0x03u;
        out[i] = NormalizeAtlasPixel(r[index], g[index], b[index], a[index]);
    }
}

void ReleaseExternalAtlasBitmap()
{
    if (g_externalAtlas.pixels) {
        HeapFree(GetProcessHeap(), 0, g_externalAtlas.pixels);
    }
    g_externalAtlas.pixels = nullptr;
    g_externalAtlas.width = 0;
    g_externalAtlas.height = 0;
}

