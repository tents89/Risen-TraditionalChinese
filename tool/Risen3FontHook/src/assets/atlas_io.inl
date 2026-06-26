bool LoadExternalAtlasBmp(const wchar_t* path)
{
    if (!path || !*path) {
        return false;
    }

    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        wchar_t message[512]{};
        wsprintfW(message, L"[Risen3FontHookTest] externalAtlasBmp open failed: %s\n", path);
        Log(message);
        return false;
    }

    BITMAPFILEHEADER fileHeader{};
    BITMAPINFOHEADER infoHeader{};
    DWORD read = 0;
    const bool headersOk =
        ReadFile(file, &fileHeader, sizeof(fileHeader), &read, nullptr) && read == sizeof(fileHeader) &&
        ReadFile(file, &infoHeader, sizeof(infoHeader), &read, nullptr) && read == sizeof(infoHeader);

    if (!headersOk || fileHeader.bfType != 0x4D42 || infoHeader.biWidth <= 0 || infoHeader.biHeight == 0 ||
        (infoHeader.biBitCount != 8 && infoHeader.biBitCount != 24 && infoHeader.biBitCount != 32) ||
        (infoHeader.biCompression != BI_RGB && infoHeader.biCompression != BI_BITFIELDS)) {
        CloseHandle(file);
        Log(L"[Risen3FontHookTest] externalAtlasBmp unsupported BMP format.\n");
        return false;
    }

    const uint32_t width = static_cast<uint32_t>(infoHeader.biWidth);
    const uint32_t height = static_cast<uint32_t>(infoHeader.biHeight < 0 ? -infoHeader.biHeight : infoHeader.biHeight);
    if (width == 0 || height == 0 || width > 8192 || height > 8192) {
        CloseHandle(file);
        Log(L"[Risen3FontHookTest] externalAtlasBmp rejected size.\n");
        return false;
    }

    const uint32_t bytesPerPixel = infoHeader.biBitCount / 8;
    const uint32_t rowPitch = ((width * bytesPerPixel + 3u) / 4u) * 4u;
    const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    auto* pixels = static_cast<uint32_t*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, pixelCount * sizeof(uint32_t)));
    auto* row = static_cast<BYTE*>(HeapAlloc(GetProcessHeap(), 0, rowPitch));
    if (!pixels || !row) {
        if (pixels) {
            HeapFree(GetProcessHeap(), 0, pixels);
        }
        if (row) {
            HeapFree(GetProcessHeap(), 0, row);
        }
        CloseHandle(file);
        return false;
    }

    SetFilePointer(file, fileHeader.bfOffBits, nullptr, FILE_BEGIN);
    const bool topDown = infoHeader.biHeight < 0;
    bool ok = true;
    for (uint32_t srcY = 0; srcY < height; ++srcY) {
        if (!ReadFile(file, row, rowPitch, &read, nullptr) || read != rowPitch) {
            ok = false;
            break;
        }

        const uint32_t dstY = topDown ? srcY : (height - 1u - srcY);
        uint32_t* dst = pixels + static_cast<size_t>(dstY) * width;
        if (bytesPerPixel == 1) {
            for (uint32_t x = 0; x < width; ++x) {
                const uint8_t gray = row[x];
                dst[x] = (static_cast<uint32_t>(gray) << 24) | 0x00FFFFFFu;
            }
        } else {
            for (uint32_t x = 0; x < width; ++x) {
                const BYTE* src = row + static_cast<size_t>(x) * bytesPerPixel;
                const uint8_t b = src[0];
                const uint8_t g = src[1];
                const uint8_t r = src[2];
                const uint8_t a = bytesPerPixel == 4 ? src[3] : 0xFF;
                dst[x] = (static_cast<uint32_t>(a) << 24) |
                    (static_cast<uint32_t>(r) << 16) |
                    (static_cast<uint32_t>(g) << 8) |
                    static_cast<uint32_t>(b);
            }
        }
    }

    HeapFree(GetProcessHeap(), 0, row);
    CloseHandle(file);

    if (!ok) {
        HeapFree(GetProcessHeap(), 0, pixels);
        Log(L"[Risen3FontHookTest] externalAtlasBmp read failed.\n");
        return false;
    }

    {
        SpinLock lock(&g_externalAtlasLock);
        ReleaseExternalAtlasBitmap();
        g_externalAtlas.pixels = pixels;
        g_externalAtlas.width = width;
        g_externalAtlas.height = height;
    }

    wchar_t message[512]{};
    wsprintfW(
        message,
        L"[Risen3FontHookTest] externalAtlasBmp loaded: %ux%u (%s)\n",
        width,
        height,
        path);
    Log(message);
    return true;
}

bool LoadExternalAtlasDds(const wchar_t* path)
{
    if (!path || !*path) {
        return false;
    }

    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        wchar_t message[512]{};
        wsprintfW(message, L"[Risen3FontHookTest] externalAtlas DDS open failed: %s\n", path);
        Log(message);
        return false;
    }

    LARGE_INTEGER fileSize{};
    if (!GetFileSizeEx(file, &fileSize) || fileSize.QuadPart < static_cast<LONGLONG>(sizeof(DWORD) + sizeof(DdsHeader))) {
        CloseHandle(file);
        Log(L"[Risen3FontHookTest] externalAtlas DDS rejected size.\n");
        return false;
    }

    DWORD read = 0;
    DWORD magic = 0;
    DdsHeader header{};
    const bool headerOk =
        ReadFile(file, &magic, sizeof(magic), &read, nullptr) && read == sizeof(magic) &&
        ReadFile(file, &header, sizeof(header), &read, nullptr) && read == sizeof(header);
    if (!headerOk || magic != kDdsMagic || header.size != sizeof(DdsHeader) ||
        header.pixelFormat.size != sizeof(DdsPixelFormat) ||
        header.width == 0 || header.height == 0 || header.width > 8192 || header.height > 8192) {
        CloseHandle(file);
        Log(L"[Risen3FontHookTest] externalAtlas DDS unsupported header.\n");
        return false;
    }

    DdsHeaderDxt10 dxt10{};
    bool hasDxt10 = false;
    if ((header.pixelFormat.flags & kDdsPfFourCc) && header.pixelFormat.fourCC == MakeFourCc('D', 'X', '1', '0')) {
        if (!ReadFile(file, &dxt10, sizeof(dxt10), &read, nullptr) || read != sizeof(dxt10)) {
            CloseHandle(file);
            Log(L"[Risen3FontHookTest] externalAtlas DDS truncated DX10 header.\n");
            return false;
        }
        hasDxt10 = true;
    }

    const uint32_t width = header.width;
    const uint32_t height = header.height;
    const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    auto* pixels = static_cast<uint32_t*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, pixelCount * sizeof(uint32_t)));
    if (!pixels) {
        CloseHandle(file);
        return false;
    }

    const DWORD fourCC = header.pixelFormat.fourCC;
    const bool classicDxt1 = !hasDxt10 && (header.pixelFormat.flags & kDdsPfFourCc) && fourCC == MakeFourCc('D', 'X', 'T', '1');
    const bool classicDxt3 = !hasDxt10 && (header.pixelFormat.flags & kDdsPfFourCc) && fourCC == MakeFourCc('D', 'X', 'T', '3');
    const bool classicDxt5 = !hasDxt10 && (header.pixelFormat.flags & kDdsPfFourCc) && fourCC == MakeFourCc('D', 'X', 'T', '5');
    const bool dx10Bc1 = hasDxt10 && (dxt10.dxgiFormat == 71 || dxt10.dxgiFormat == 72);
    const bool dx10Bc2 = hasDxt10 && (dxt10.dxgiFormat == 74 || dxt10.dxgiFormat == 75);
    const bool dx10Bc3 = hasDxt10 && (dxt10.dxgiFormat == 77 || dxt10.dxgiFormat == 78);
    const bool dx10Bc4 = hasDxt10 && (dxt10.dxgiFormat == 80 || dxt10.dxgiFormat == 81);
    const bool isBc1 = classicDxt1 || dx10Bc1;
    const bool isBc2 = classicDxt3 || dx10Bc2;
    const bool isBc3 = classicDxt5 || dx10Bc3;
    const bool isBc4 = dx10Bc4 || (!hasDxt10 && (header.pixelFormat.flags & kDdsPfFourCc) &&
        (fourCC == MakeFourCc('A', 'T', 'I', '1') || fourCC == MakeFourCc('B', 'C', '4', 'U') ||
            fourCC == MakeFourCc('B', 'C', '4', 'S')));

    bool ok = true;
    if (isBc1 || isBc2 || isBc3 || isBc4) {
        const uint32_t blockBytes = (isBc1 || isBc4) ? 8u : 16u;
        const uint32_t blocksWide = (width + 3u) / 4u;
        const uint32_t blocksHigh = (height + 3u) / 4u;
        BYTE block[16]{};
        for (uint32_t by = 0; by < blocksHigh && ok; ++by) {
            for (uint32_t bx = 0; bx < blocksWide; ++bx) {
                if (!ReadFile(file, block, blockBytes, &read, nullptr) || read != blockBytes) {
                    ok = false;
                    break;
                }

                uint32_t colors[16]{};
                uint8_t alpha[16]{};
                if (isBc1) {
                    DecodeBcColorBlock(block, colors, true);
                } else if (isBc2) {
                    for (int i = 0; i < 16; ++i) {
                        const uint8_t nibble = static_cast<uint8_t>((block[i / 2] >> ((i & 1) ? 4 : 0)) & 0x0F);
                        alpha[i] = static_cast<uint8_t>((nibble << 4) | nibble);
                    }
                    DecodeBcColorBlock(block + 8, colors, false);
                } else if (isBc3) {
                    DecodeBcAlphaBlock(block, alpha);
                    DecodeBcColorBlock(block + 8, colors, false);
                } else {
                    DecodeBcAlphaBlock(block, alpha);
                    for (int i = 0; i < 16; ++i) {
                        colors[i] = (static_cast<uint32_t>(alpha[i]) << 24) | 0x00FFFFFFu;
                    }
                }

                for (uint32_t py = 0; py < 4; ++py) {
                    for (uint32_t px = 0; px < 4; ++px) {
                        const uint32_t index = py * 4u + px;
                        uint32_t argb = colors[index];
                        if (isBc2 || isBc3) {
                            const uint8_t a = alpha[index];
                            const uint8_t r = static_cast<uint8_t>((argb >> 16) & 0xFF);
                            const uint8_t g = static_cast<uint8_t>((argb >> 8) & 0xFF);
                            const uint8_t b = static_cast<uint8_t>(argb & 0xFF);
                            argb = NormalizeAtlasPixel(r, g, b, a);
                        }
                        WriteDecodedPixel(pixels, width, height, bx * 4u + px, by * 4u + py, argb);
                    }
                }
            }
        }
    } else {
        DWORD bitsPerPixel = header.pixelFormat.rgbBitCount;
        bool isRgba8 = false;
        bool isBgra8 = false;
        bool isBgrx8 = false;
        bool isR8 = false;
        bool isA8 = false;

        if (hasDxt10) {
            isRgba8 = dxt10.dxgiFormat == 28 || dxt10.dxgiFormat == 29;
            isBgra8 = dxt10.dxgiFormat == 87 || dxt10.dxgiFormat == 91;
            isBgrx8 = dxt10.dxgiFormat == 88 || dxt10.dxgiFormat == 93;
            isR8 = dxt10.dxgiFormat == 61 || dxt10.dxgiFormat == 62;
            isA8 = dxt10.dxgiFormat == 65;
            bitsPerPixel = (isR8 || isA8) ? 8u : 32u;
        }

        const bool classicRgb = !hasDxt10 && (header.pixelFormat.flags & kDdsPfRgb);
        const bool classicLuminance = !hasDxt10 && (header.pixelFormat.flags & kDdsPfLuminance);
        const bool classicAlpha = !hasDxt10 && (header.pixelFormat.flags & kDdsPfAlpha);
        if (!isRgba8 && !isBgra8 && !isBgrx8 && !isR8 && !isA8 && !classicRgb && !classicLuminance && !classicAlpha) {
            ok = false;
        } else {
            const uint32_t rowPitch = header.pitchOrLinearSize
                ? header.pitchOrLinearSize
                : ((width * bitsPerPixel + 7u) / 8u);
            auto* row = static_cast<BYTE*>(HeapAlloc(GetProcessHeap(), 0, rowPitch));
            if (!row) {
                ok = false;
            } else {
                for (uint32_t y = 0; y < height && ok; ++y) {
                    if (!ReadFile(file, row, rowPitch, &read, nullptr) || read != rowPitch) {
                        ok = false;
                        break;
                    }

                    for (uint32_t x = 0; x < width; ++x) {
                        uint32_t argb = 0;
                        if (isR8 || isA8 || classicAlpha || (classicLuminance && bitsPerPixel == 8u)) {
                            const uint8_t gray = row[x];
                            argb = (static_cast<uint32_t>(gray) << 24) | 0x00FFFFFFu;
                        } else if (classicLuminance && bitsPerPixel == 16u) {
                            const BYTE* src = row + static_cast<size_t>(x) * 2u;
                            const uint8_t gray = src[0];
                            const uint8_t a = src[1];
                            argb = NormalizeAtlasPixel(gray, gray, gray, a);
                        } else if (isRgba8) {
                            const BYTE* src = row + static_cast<size_t>(x) * 4u;
                            argb = NormalizeAtlasPixel(src[0], src[1], src[2], src[3]);
                        } else if (isBgra8 || isBgrx8) {
                            const BYTE* src = row + static_cast<size_t>(x) * 4u;
                            argb = NormalizeAtlasPixel(src[2], src[1], src[0], isBgrx8 ? 0xFF : src[3]);
                        } else if (classicRgb) {
                            const DWORD bytes = bitsPerPixel / 8u;
                            const BYTE* src = row + static_cast<size_t>(x) * bytes;
                            uint32_t value = 0;
                            for (DWORD i = 0; i < bytes && i < sizeof(value); ++i) {
                                value |= static_cast<uint32_t>(src[i]) << (8u * i);
                            }
                            const uint8_t r = ExtractMask8(value, header.pixelFormat.rBitMask);
                            const uint8_t g = ExtractMask8(value, header.pixelFormat.gBitMask);
                            const uint8_t b = ExtractMask8(value, header.pixelFormat.bBitMask);
                            const uint8_t a = (header.pixelFormat.flags & kDdsPfAlphaPixels)
                                ? ExtractMask8(value, header.pixelFormat.aBitMask)
                                : 0xFF;
                            argb = NormalizeAtlasPixel(r, g, b, a);
                        }
                        pixels[static_cast<size_t>(y) * width + x] = argb;
                    }
                }
                HeapFree(GetProcessHeap(), 0, row);
            }
        }
    }

    CloseHandle(file);

    if (!ok) {
        HeapFree(GetProcessHeap(), 0, pixels);
        Log(L"[Risen3FontHookTest] externalAtlas DDS unsupported or truncated data.\n");
        return false;
    }

    {
        SpinLock lock(&g_externalAtlasLock);
        ReleaseExternalAtlasBitmap();
        g_externalAtlas.pixels = pixels;
        g_externalAtlas.width = width;
        g_externalAtlas.height = height;
    }

    wchar_t message[512]{};
    wsprintfW(
        message,
        L"[Risen3FontHookTest] externalAtlas DDS loaded: %ux%u (%s)\n",
        width,
        height,
        path);
    Log(message);
    return true;
}

bool HasExtension(const wchar_t* path, const wchar_t* extension)
{
    if (!path || !extension) {
        return false;
    }

    const wchar_t* dot = nullptr;
    for (const wchar_t* p = path; *p; ++p) {
        if (*p == L'.') {
            dot = p;
        }
    }
    return dot && lstrcmpiW(dot, extension) == 0;
}

bool LoadExternalAtlasImage(const wchar_t* path)
{
    if (!path || !*path) {
        return false;
    }

    if (HasExtension(path, L".dds")) {
        return LoadExternalAtlasDds(path);
    }

    if (HasExtension(path, L".bmp")) {
        if (LoadExternalAtlasBmp(path)) {
            return true;
        }

        wchar_t ddsPath[MAX_PATH]{};
        lstrcpynW(ddsPath, path, ARRAYSIZE(ddsPath));
        wchar_t* dot = nullptr;
        for (wchar_t* p = ddsPath; *p; ++p) {
            if (*p == L'.') {
                dot = p;
            }
        }
        if (dot) {
            lstrcpynW(dot, L".dds", static_cast<int>(ARRAYSIZE(ddsPath) - (dot - ddsPath)));
            return LoadExternalAtlasDds(ddsPath);
        }
    }

    if (LoadExternalAtlasBmp(path)) {
        return true;
    }
    return LoadExternalAtlasDds(path);
}

bool LoadWholeFile(const wchar_t* path, BYTE** outData, DWORD* outSize)
{
    if (!path || !*path || !outData || !outSize) {
        return false;
    }

    *outData = nullptr;
    *outSize = 0;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 256ll * 1024ll * 1024ll) {
        CloseHandle(file);
        return false;
    }

    BYTE* data = static_cast<BYTE*>(HeapAlloc(GetProcessHeap(), 0, static_cast<SIZE_T>(size.QuadPart)));
    if (!data) {
        CloseHandle(file);
        return false;
    }

    DWORD read = 0;
    const bool ok = ReadFile(file, data, static_cast<DWORD>(size.QuadPart), &read, nullptr) &&
        read == static_cast<DWORD>(size.QuadPart);
    CloseHandle(file);
    if (!ok) {
        HeapFree(GetProcessHeap(), 0, data);
        return false;
    }

    *outData = data;
    *outSize = read;
    return true;
}

void ResetGeneratedAtlasTexture()
{
    SpinLock lock(&g_testAtlasLock);
    if (g_testAtlasTexture) {
        g_testAtlasTexture->Release();
        g_testAtlasTexture = nullptr;
    }
    g_testAtlasDevice = nullptr;
}

void ReleaseGeneratedResources()
{
    ResetGeneratedAtlasTexture();
    SpinLock atlasLock(&g_externalAtlasLock);
    ReleaseExternalAtlasBitmap();
}

void FillTestAtlas(IDirect3DTexture9* texture)
{
    if (!texture) {
        return;
    }

    D3DLOCKED_RECT locked{};
    if (FAILED(texture->LockRect(0, &locked, nullptr, 0))) {
        Log(L"[Risen3FontHookTest] FillTestAtlas LockRect failed.\n");
        return;
    }

    for (int y = 0; y < 512; ++y) {
        auto* row = reinterpret_cast<uint32_t*>(static_cast<BYTE*>(locked.pBits) + y * locked.Pitch);
        for (int x = 0; x < 512; ++x) {
            const bool checker = (((x / 16) ^ (y / 16)) & 1) != 0;
            const uint8_t r = checker ? 0xFF : 0x20;
            const uint8_t g = (x >= 220 && x <= 292) ? 0xFF : (checker ? 0x30 : 0x20);
            const uint8_t b = (y >= 220 && y <= 292) ? 0xFF : (checker ? 0x20 : 0xE0);
            row[x] = 0xFF000000u | (static_cast<uint32_t>(r) << 16) |
                (static_cast<uint32_t>(g) << 8) | b;
        }
    }

    texture->UnlockRect(0);
}

bool FillTextureFromExternalAtlas(IDirect3DTexture9* texture)
{
    if (!texture) {
        return false;
    }

    SpinLock lock(&g_externalAtlasLock);
    if (!g_externalAtlas.pixels || !g_externalAtlas.width || !g_externalAtlas.height) {
        return false;
    }

    D3DLOCKED_RECT locked{};
    if (FAILED(texture->LockRect(0, &locked, nullptr, 0))) {
        Log(L"[Risen3FontHookTest] FillTextureFromExternalAtlas LockRect failed.\n");
        return false;
    }

    for (uint32_t y = 0; y < g_externalAtlas.height; ++y) {
        auto* dst = reinterpret_cast<uint32_t*>(static_cast<BYTE*>(locked.pBits) + y * locked.Pitch);
        const uint32_t* src = g_externalAtlas.pixels + static_cast<size_t>(y) * g_externalAtlas.width;
        memcpy(dst, src, static_cast<size_t>(g_externalAtlas.width) * sizeof(uint32_t));
    }

    texture->UnlockRect(0);
    return true;
}

IDirect3DTexture9* GetOrCreateTestAtlasTexture(IDirect3DDevice9* device)
{
    if (!device) {
        return nullptr;
    }

    SpinLock lock(&g_testAtlasLock);

    if (g_testAtlasTexture && g_testAtlasDevice == device) {
        return g_testAtlasTexture;
    }

    if (g_testAtlasTexture) {
        g_testAtlasTexture->Release();
        g_testAtlasTexture = nullptr;
        g_testAtlasDevice = nullptr;
    }

    uint32_t textureWidth = 512;
    uint32_t textureHeight = 512;
    bool hasExternalAtlasPixels = false;
    {
        SpinLock atlasLock(&g_externalAtlasLock);
        if (g_externalAtlas.pixels && g_externalAtlas.width && g_externalAtlas.height) {
            textureWidth = g_externalAtlas.width;
            textureHeight = g_externalAtlas.height;
            hasExternalAtlasPixels = true;
        }
    }

    if (g_enableExternalAtlas && !hasExternalAtlasPixels) {
        Log(L"[Risen3FontHookTest] external atlas enabled but no pixels loaded; texture replacement skipped.\n");
        return nullptr;
    }

    D3DDEVICE_CREATION_PARAMETERS creationParameters{};
    HRESULT paramsHr = device->GetCreationParameters(&creationParameters);
    if (SUCCEEDED(paramsHr)) {
        wchar_t paramsMessage[256]{};
        wsprintfW(
            paramsMessage,
            L"[Risen3FontHookTest] Device params adapter=%u deviceType=%u behavior=%08X focus=%p\n",
            creationParameters.AdapterOrdinal,
            static_cast<unsigned>(creationParameters.DeviceType),
            creationParameters.BehaviorFlags,
            creationParameters.hFocusWindow);
        Log(paramsMessage);
    }

    const D3DFORMAT formats[] = {
        D3DFMT_A8R8G8B8,
        D3DFMT_X8R8G8B8,
        D3DFMT_A4R4G4B4
    };
    const D3DPOOL pools[] = {
        D3DPOOL_MANAGED,
        D3DPOOL_DEFAULT
    };

    IDirect3DTexture9* texture = nullptr;
    D3DPOOL usedPool = D3DPOOL_MANAGED;
    DWORD usedUsage = 0;
    D3DFORMAT usedFormat = D3DFMT_A8R8G8B8;
    HRESULT hr = D3DERR_INVALIDCALL;

    const size_t formatCount = g_enableExternalAtlas ? 1u : ARRAYSIZE(formats);
    for (size_t formatIndex = 0; formatIndex < formatCount; ++formatIndex) {
        const D3DFORMAT format = formats[formatIndex];
        for (D3DPOOL pool : pools) {
            usedPool = pool;
            usedUsage = 0;
            usedFormat = format;
            hr = device->CreateTexture(
                textureWidth,
                textureHeight,
                1,
                usedUsage,
                usedFormat,
                usedPool,
                &texture,
                nullptr);

            wchar_t attemptMessage[256]{};
            wsprintfW(
                attemptMessage,
                L"[Risen3FontHookTest] CreateTexture attempt size=%ux%u fmt=%08X pool=%u hr=%08X tex=%p\n",
                textureWidth,
                textureHeight,
                static_cast<unsigned>(usedFormat),
                static_cast<unsigned>(usedPool),
                static_cast<unsigned>(hr),
                texture);
            Log(attemptMessage);

            if (SUCCEEDED(hr) && texture) {
                break;
            }
        }

        if (texture) {
            break;
        }
    }

    if (FAILED(hr) || !texture) {
        wchar_t message[256]{};
        wsprintfW(
            message,
            L"[Risen3FontHookTest] CreateTexture atlas failed size=%ux%u lastFmt=%08X lastPool=%u hr=%08X\n",
            textureWidth,
            textureHeight,
            static_cast<unsigned>(usedFormat),
            static_cast<unsigned>(usedPool),
            static_cast<unsigned>(hr));
        Log(message);
        return nullptr;
    }

    const bool usedExternalAtlas = hasExternalAtlasPixels && FillTextureFromExternalAtlas(texture);
    if (!usedExternalAtlas) {
        if (g_enableExternalAtlas) {
            Log(L"[Risen3FontHookTest] External atlas fill failed; texture replacement skipped.\n");
            texture->Release();
            return nullptr;
        }
        FillTestAtlas(texture);
    }
    g_testAtlasTexture = texture;
    g_testAtlasDevice = device;

    wchar_t message[256]{};
    wsprintfW(
        message,
        L"[Risen3FontHookTest] Created %ux%u %s atlas texture usage=%08X fmt=%08X pool=%u.\n",
        textureWidth,
        textureHeight,
        usedExternalAtlas ? L"external" : L"test",
        usedUsage,
        static_cast<unsigned>(usedFormat),
        static_cast<unsigned>(usedPool));
    Log(message);
    return g_testAtlasTexture;
}

void DumpResourceFingerprint(uintptr_t resource)
{
    if (!resource) {
        return;
    }

    {
        SpinLock lock(&g_observedResourceLock);

        for (size_t i = 0; i < kObservedFontCount; ++i) {
            if (g_observedResources[i].resource == resource) {
                return;
            }
        }

        for (size_t i = 0; i < kObservedFontCount; ++i) {
            if (!g_observedResources[i].resource) {
                g_observedResources[i].resource = resource;
                break;
            }
        }
    }

    uintptr_t q00 = 0;
    uintptr_t q08 = 0;
    uintptr_t q10 = 0;
    uintptr_t q18 = 0;
    uintptr_t q20 = 0;
    uintptr_t q28 = 0;
    uintptr_t q30 = 0;
    uintptr_t q38 = 0;
    uintptr_t q40 = 0;
    uintptr_t q48 = 0;
    uintptr_t q50 = 0;
    uintptr_t q58 = 0;
    uintptr_t q60 = 0;
    uintptr_t q68 = 0;
    uintptr_t q70 = 0;
    uintptr_t q78 = 0;

    SafeReadQword(resource + 0x00, &q00);
    SafeReadQword(resource + 0x08, &q08);
    SafeReadQword(resource + 0x10, &q10);
    SafeReadQword(resource + 0x18, &q18);
    SafeReadQword(resource + 0x20, &q20);
    SafeReadQword(resource + 0x28, &q28);
    SafeReadQword(resource + 0x30, &q30);
    SafeReadQword(resource + 0x38, &q38);
    SafeReadQword(resource + 0x40, &q40);
    SafeReadQword(resource + 0x48, &q48);
    SafeReadQword(resource + 0x50, &q50);
    SafeReadQword(resource + 0x58, &q58);
    SafeReadQword(resource + 0x60, &q60);
    SafeReadQword(resource + 0x68, &q68);
    SafeReadQword(resource + 0x70, &q70);
    SafeReadQword(resource + 0x78, &q78);

    wchar_t line1[512]{};
    wsprintfW(
        line1,
        L"[Risen3FontHookTest] ResourceDump %p +00=%p +08=%p +10=%p +18=%p +20=%p +28=%p +30=%p +38=%p\n",
        reinterpret_cast<void*>(resource),
        reinterpret_cast<void*>(q00),
        reinterpret_cast<void*>(q08),
        reinterpret_cast<void*>(q10),
        reinterpret_cast<void*>(q18),
        reinterpret_cast<void*>(q20),
        reinterpret_cast<void*>(q28),
        reinterpret_cast<void*>(q30),
        reinterpret_cast<void*>(q38));
    Log(line1);

    wchar_t line2[512]{};
    wsprintfW(
        line2,
        L"[Risen3FontHookTest] ResourceDump %p +40=%p +48=%p +50=%p +58=%p +60=%p +68=%p +70=%p +78=%p\n",
        reinterpret_cast<void*>(resource),
        reinterpret_cast<void*>(q40),
        reinterpret_cast<void*>(q48),
        reinterpret_cast<void*>(q50),
        reinterpret_cast<void*>(q58),
        reinterpret_cast<void*>(q60),
        reinterpret_cast<void*>(q68),
        reinterpret_cast<void*>(q70),
        reinterpret_cast<void*>(q78));
    Log(line2);
}

void ObserveFontResource(void* font)
{
    const uintptr_t resource = ReadFontResource(font);
    if (!font || !resource) {
        return;
    }

    DumpResourceFingerprint(resource);

    SpinLock lock(&g_observedFontLock);

    for (size_t i = 0; i < kObservedFontCount; ++i) {
        if (g_observedFonts[i].font == font) {
            if (g_observedFonts[i].resource != resource) {
                g_observedFonts[i].resource = resource;
                const uintptr_t atlas = ReadAtlasResourceFromFontResource(resource);

                wchar_t message[512]{};
                wsprintfW(
                    message,
                    L"[Risen3FontHookTest] FontResource[%u] updated font=%p resource=%p atlas=%p\n",
                    static_cast<unsigned>(i),
                    font,
                    reinterpret_cast<void*>(resource),
                    reinterpret_cast<void*>(atlas));
                Log(message);
            }
            return;
        }
    }

    for (size_t i = 0; i < kObservedFontCount; ++i) {
        if (!g_observedFonts[i].font) {
            g_observedFonts[i].font = font;
            g_observedFonts[i].resource = resource;
            const uintptr_t atlas = ReadAtlasResourceFromFontResource(resource);

            wchar_t message[256]{};
            wsprintfW(
                message,
                L"[Risen3FontHookTest] FontResource[%u] font=%p resource=%p atlas=%p\n",
                static_cast<unsigned>(i),
                font,
                reinterpret_cast<void*>(resource),
                reinterpret_cast<void*>(atlas));
            Log(message);
            return;
        }
    }
}

FontState* GetOrCreateFontState(void* font)
{
    if (!font) {
        return nullptr;
    }

    const uintptr_t resource = ReadFontResource(font);
    if (!resource) {
        return nullptr;
    }

    SpinLock lock(&g_fontStateLock);

    for (size_t i = 0; i < kFontStateCount; ++i) {
        if (g_fontStates[i].font == font) {
            if (!g_fontStates[i].originalResource) {
                g_fontStates[i].originalResource = resource;
            }
            return &g_fontStates[i];
        }
    }

    for (size_t i = 0; i < kFontStateCount; ++i) {
        if (!g_fontStates[i].font) {
            g_fontStates[i].font = font;
            g_fontStates[i].originalResource = resource;
            g_fontStates[i].atlasResource = 0;
            g_fontStates[i].shadowResource = 0;
            g_fontStates[i].shadowFontObject = 0;
            g_fontStates[i].shadowAtlasAllocation = 0;
            g_fontStates[i].shadowAtlasResource = 0;
            g_fontStates[i].ownsAtlas = false;

            wchar_t message[256]{};
            wsprintfW(
                message,
                L"[Risen3FontHookTest] FontState[%u] created font=%p originalResource=%p\n",
                static_cast<unsigned>(i),
                font,
                reinterpret_cast<void*>(resource));
            Log(message);
            return &g_fontStates[i];
        }
    }

    return nullptr;
}

void ReleaseShadowFontState(FontState* state)
{
    if (!state) {
        return;
    }

    if (state->shadowResource) {
        VirtualFree(reinterpret_cast<void*>(state->shadowResource), 0, MEM_RELEASE);
        state->shadowResource = 0;
    }

    if (state->shadowFontObject) {
        VirtualFree(reinterpret_cast<void*>(state->shadowFontObject), 0, MEM_RELEASE);
        state->shadowFontObject = 0;
    }

    if (state->shadowAtlasAllocation) {
        VirtualFree(reinterpret_cast<void*>(state->shadowAtlasAllocation), 0, MEM_RELEASE);
        state->shadowAtlasAllocation = 0;
    }

    state->shadowAtlasResource = 0;
    state->ownsAtlas = false;
}

uintptr_t GetOrCreateShadowFontResource(FontState* state)
{
    if (!g_enableShadowResource || !state || !state->originalResource) {
        return 0;
    }

    if (state->shadowResource && state->shadowAtlasResource) {
        return state->shadowResource;
    }

    uintptr_t fontObject = 0;
    if (!SafeReadQword(state->originalResource + 0x10, &fontObject) || !fontObject) {
        return 0;
    }

    const uintptr_t atlasResource = ReadAtlasResourceFromFontResource(state->originalResource);
    if (!atlasResource) {
        return 0;
    }

    BYTE* shadowResource = static_cast<BYTE*>(
        VirtualAlloc(nullptr, kShadowFontResourceCopySize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    BYTE* shadowFontObject = static_cast<BYTE*>(
        VirtualAlloc(nullptr, kShadowFontObjectCopySize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    BYTE* shadowAtlasAllocation = static_cast<BYTE*>(
        VirtualAlloc(nullptr, kShadowAtlasCopySize + kShadowAtlasPrefixSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));

    if (!shadowResource || !shadowFontObject || !shadowAtlasAllocation) {
        if (shadowResource) {
            VirtualFree(shadowResource, 0, MEM_RELEASE);
        }
        if (shadowFontObject) {
            VirtualFree(shadowFontObject, 0, MEM_RELEASE);
        }
        if (shadowAtlasAllocation) {
            VirtualFree(shadowAtlasAllocation, 0, MEM_RELEASE);
        }
        Log(L"[Risen3FontHookTest] Shadow resource allocation failed.\n");
        return 0;
    }

    const uintptr_t shadowResourceAddress = reinterpret_cast<uintptr_t>(shadowResource);
    const uintptr_t shadowFontObjectAddress = reinterpret_cast<uintptr_t>(shadowFontObject);
    const uintptr_t shadowAtlasAddress =
        reinterpret_cast<uintptr_t>(shadowAtlasAllocation) + kShadowAtlasPrefixSize;

    if (!SafeCopyMemory(shadowResourceAddress, state->originalResource, kShadowFontResourceCopySize) ||
        !SafeCopyMemory(shadowFontObjectAddress, fontObject, kShadowFontObjectCopySize) ||
        !SafeCopyMemory(shadowAtlasAddress, atlasResource, kShadowAtlasCopySize)) {
        VirtualFree(shadowResource, 0, MEM_RELEASE);
        VirtualFree(shadowFontObject, 0, MEM_RELEASE);
        VirtualFree(shadowAtlasAllocation, 0, MEM_RELEASE);
        Log(L"[Risen3FontHookTest] Shadow resource copy failed.\n");
        return 0;
    }

    SafeWriteQword(shadowResourceAddress + 0x10, shadowFontObjectAddress);
    SafeWriteQword(shadowFontObjectAddress + 0x38, shadowAtlasAddress);
    SafeWriteQword(shadowAtlasAddress + 0x10, shadowResourceAddress);
    const uintptr_t shadowTag =
        (static_cast<uintptr_t>(0x7F00 + (InterlockedIncrement(&g_shadowResourceSerial) & 0xFF)) << 8) | 0x12;
    SafeWriteQword(shadowResourceAddress + 0x08, shadowTag);
    SafeWriteQword(shadowAtlasAddress + 0x08, shadowTag);

    state->atlasResource = atlasResource;
    state->shadowResource = shadowResourceAddress;
    state->shadowFontObject = shadowFontObjectAddress;
    state->shadowAtlasAllocation = reinterpret_cast<uintptr_t>(shadowAtlasAllocation);
    state->shadowAtlasResource = shadowAtlasAddress;
    state->ownsAtlas = true;

    wchar_t message[512]{};
    wsprintfW(
        message,
        L"[Risen3FontHookTest] ShadowResource created originalResource=%p shadowResource=%p originalAtlas=%p shadowAtlas=%p tag=%p fontObject=%p shadowFontObject=%p\n",
        reinterpret_cast<void*>(state->originalResource),
        reinterpret_cast<void*>(state->shadowResource),
        reinterpret_cast<void*>(atlasResource),
        reinterpret_cast<void*>(state->shadowAtlasResource),
        reinterpret_cast<void*>(shadowTag),
        reinterpret_cast<void*>(fontObject),
        reinterpret_cast<void*>(state->shadowFontObject));
    Log(message);
    return state->shadowResource;
}

uintptr_t* FontResourceSlot(void* font)
{
    if (!font) {
        return nullptr;
    }

    return reinterpret_cast<uintptr_t*>(static_cast<BYTE*>(font) + kFontResourceOffset);
}

bool IsLikelyHexDigit(wchar_t ch)
{
    return (ch >= L'0' && ch <= L'9') ||
        (ch >= L'a' && ch <= L'f') ||
        (ch >= L'A' && ch <= L'F');
}

uintptr_t ParseHexPointer(const wchar_t* text)
{
    if (!text || !*text) {
        return 0;
    }

    while (*text == L' ' || *text == L'\t') {
        ++text;
    }

    if (text[0] == L'0' && (text[1] == L'x' || text[1] == L'X')) {
        text += 2;
    }

    uintptr_t value = 0;
    while (IsLikelyHexDigit(*text)) {
        const wchar_t ch = *text++;
        value <<= 4;
        if (ch >= L'0' && ch <= L'9') {
            value += static_cast<uintptr_t>(ch - L'0');
        } else if (ch >= L'a' && ch <= L'f') {
            value += static_cast<uintptr_t>(10 + ch - L'a');
        } else {
            value += static_cast<uintptr_t>(10 + ch - L'A');
        }
    }

    return value;
}

bool IsAbsolutePath(const wchar_t* path)
{
    if (!path || !path[0]) {
        return false;
    }

    if (path[0] == L'\\' && path[1] == L'\\') {
        return true;
    }

    return path[0] && path[1] == L':';
}

bool GetDllDirectoryPath(wchar_t* out, DWORD outCount)
{
    if (!out || outCount == 0 || !g_module) {
        return false;
    }

    const DWORD length = GetModuleFileNameW(g_module, out, outCount);
    if (length == 0 || length >= outCount) {
        return false;
    }

    wchar_t* slash = wcsrchr(out, L'\\');
    if (!slash) {
        return false;
    }

    *(slash + 1) = L'\0';
    return true;
}

bool ResolveConfigRelativePath(const wchar_t* value, wchar_t* out, DWORD outCount)
{
    if (!value || !*value || !out || outCount == 0) {
        return false;
    }

    while (*value == L' ' || *value == L'\t') {
        ++value;
    }

    if (!*value) {
        return false;
    }

    if (IsAbsolutePath(value)) {
        lstrcpynW(out, value, outCount);
        return true;
    }

    wchar_t base[MAX_PATH]{};
    if (!GetDllDirectoryPath(base, ARRAYSIZE(base))) {
        return false;
    }

    lstrcpynW(out, base, outCount);
    const size_t currentLength = wcslen(out);
    if (currentLength + wcslen(value) + 1 >= outCount) {
        return false;
    }

    lstrcatW(out, value);
    return true;
}

int ParseDecimalInt(const wchar_t* text, int fallback)
{
    if (!text || !*text) {
        return fallback;
    }

    while (*text == L' ' || *text == L'\t') {
        ++text;
    }

    int sign = 1;
    if (*text == L'-') {
        sign = -1;
        ++text;
    }

    int value = 0;
    bool foundDigit = false;
    while (*text >= L'0' && *text <= L'9') {
        foundDigit = true;
        value = value * 10 + static_cast<int>(*text - L'0');
        ++text;
    }

    return foundDigit ? value * sign : fallback;
}

bool ParseBoolInt(const wchar_t* text, bool fallback)
{
    if (!text || !*text) {
        return fallback;
    }

    while (*text == L' ' || *text == L'\t') {
        ++text;
    }

    if (*text == L'0') {
        return false;
    }

    if (*text == L'1') {
        return true;
    }

    return fallback;
}

float ParseFloatSimple(const wchar_t** cursor)
{
    if (!cursor || !*cursor) {
        return 0.0f;
    }

    const wchar_t* text = *cursor;
    while (*text == L' ' || *text == L'\t' || *text == L',') {
        ++text;
    }

    float sign = 1.0f;
    if (*text == L'-') {
        sign = -1.0f;
        ++text;
    }

    float value = 0.0f;
    while (*text >= L'0' && *text <= L'9') {
        value = value * 10.0f + static_cast<float>(*text - L'0');
        ++text;
    }

    if (*text == L'.') {
        ++text;
        float scale = 0.1f;
        while (*text >= L'0' && *text <= L'9') {
            value += static_cast<float>(*text - L'0') * scale;
            scale *= 0.1f;
            ++text;
        }
    }

    *cursor = text;
    return value * sign;
}

bool ParseFloat4(const wchar_t* text, float out[4])
{
    if (!text || !out) {
        return false;
    }

    const wchar_t* cursor = text;
    out[0] = ParseFloatSimple(&cursor);
    out[1] = ParseFloatSimple(&cursor);
    out[2] = ParseFloatSimple(&cursor);
    out[3] = ParseFloatSimple(&cursor);
    return true;
}

void SkipSeparators(const wchar_t** cursor)
{
    if (!cursor || !*cursor) {
        return;
    }

    while (**cursor == L' ' || **cursor == L'\t' || **cursor == L',') {
        ++(*cursor);
    }
}

bool ParseHexToken(const wchar_t** cursor, uintptr_t* out)
{
    if (!cursor || !*cursor || !out) {
        return false;
    }

    SkipSeparators(cursor);
    const wchar_t* text = *cursor;
    if (text[0] == L'0' && (text[1] == L'x' || text[1] == L'X')) {
        text += 2;
    }

    uintptr_t value = 0;
    bool foundDigit = false;
    while (IsLikelyHexDigit(*text)) {
        foundDigit = true;
        const wchar_t ch = *text++;
        value <<= 4;
        if (ch >= L'0' && ch <= L'9') {
            value += static_cast<uintptr_t>(ch - L'0');
        } else if (ch >= L'a' && ch <= L'f') {
            value += static_cast<uintptr_t>(10 + ch - L'a');
        } else {
            value += static_cast<uintptr_t>(10 + ch - L'A');
        }
    }

    *cursor = text;
    *out = value;
    return foundDigit;
}

bool ParseGlyphLine(const wchar_t* line, ChineseGlyphEntry* out)
{
    if (!line || !out) {
        return false;
    }

    while (*line == L' ' || *line == L'\t') {
        ++line;
    }

    if (!*line || *line == L'#' || *line == L';') {
        return false;
    }

    uintptr_t codepoint = 0;
    if (!ParseHexToken(&line, &codepoint)) {
        return false;
    }
    if (codepoint == 0 || codepoint > 0xFFFF) {
        return false;
    }

    uintptr_t fallback = 0;
    if (!ParseHexToken(&line, &fallback)) {
        return false;
    }
    if (fallback == 0 || fallback > 0xFFFF) {
        return false;
    }

    float rect[4]{};
    rect[0] = ParseFloatSimple(&line);
    rect[1] = ParseFloatSimple(&line);
    rect[2] = ParseFloatSimple(&line);
    rect[3] = ParseFloatSimple(&line);
    const float advanceExtra = ParseFloatSimple(&line);

    out->codepoint = static_cast<wchar_t>(codepoint);
    out->fallback = static_cast<wchar_t>(fallback);
    out->rect[0] = rect[0];
    out->rect[1] = rect[1];
    out->rect[2] = rect[2];
    out->rect[3] = rect[3];
    out->plane[0] = 0.0f;
    out->plane[1] = 0.0f;
    out->plane[2] = 0.0f;
    out->plane[3] = 0.0f;
    out->hasPlaneBounds = false;
    out->advanceExtra = advanceExtra;
    out->naturalAdvance = 0.0f;
    out->enabled = true;
    return true;
}

