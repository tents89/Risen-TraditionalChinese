bool LoadExternalGlyphMap(const wchar_t* path)
{
    if (!path || !*path) {
        return false;
    }

    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        wchar_t message[512]{};
        wsprintfW(message, L"[Risen3FontHookTest] glyphMap open failed: %s\n", path);
        Log(message);
        return false;
    }

    SpinLock lock(&g_externalGlyphLock);
    g_externalGlyphCount = 0;

    BYTE bom[2]{};
    DWORD read = 0;
    ReadFile(file, bom, sizeof(bom), &read, nullptr);
    if (read == sizeof(bom) && !(bom[0] == 0xFF && bom[1] == 0xFE)) {
        SetFilePointer(file, 0, nullptr, FILE_BEGIN);
    }

    wchar_t line[256]{};
    size_t lineLength = 0;
    wchar_t chunk[256]{};
    while (ReadFile(file, chunk, sizeof(chunk), &read, nullptr) && read > 0) {
        const size_t chars = read / sizeof(wchar_t);
        for (size_t i = 0; i < chars; ++i) {
            const wchar_t ch = chunk[i];
            if (ch == L'\r') {
                continue;
            }

            if (ch == L'\n') {
                line[lineLength] = L'\0';
                ChineseGlyphEntry entry{};
                if (ParseGlyphLine(line, &entry) && g_externalGlyphCount < kExternalGlyphCount) {
                    g_externalGlyphs[g_externalGlyphCount++] = entry;
                }
                lineLength = 0;
                continue;
            }

            if (lineLength + 1 < ARRAYSIZE(line)) {
                line[lineLength++] = ch;
            }
        }
    }

    if (lineLength > 0) {
        line[lineLength] = L'\0';
        ChineseGlyphEntry entry{};
        if (ParseGlyphLine(line, &entry) && g_externalGlyphCount < kExternalGlyphCount) {
            g_externalGlyphs[g_externalGlyphCount++] = entry;
        }
    }

    CloseHandle(file);

    wchar_t message[512]{};
    wsprintfW(
        message,
        L"[Risen3FontHookTest] glyphMap loaded: %u entries (%s)\n",
        static_cast<unsigned>(g_externalGlyphCount),
        path);
    Log(message);
    return g_externalGlyphCount > 0;
}

const char* JsonFindSubstring(const char* begin, const char* end, const char* needle)
{
    if (!begin || !end || !needle || begin >= end) {
        return nullptr;
    }

    const size_t needleLen = strlen(needle);
    if (needleLen == 0 || static_cast<size_t>(end - begin) < needleLen) {
        return nullptr;
    }

    const char* limit = end - needleLen;
    for (const char* p = begin; p <= limit; ++p) {
        if (memcmp(p, needle, needleLen) == 0) {
            return p;
        }
    }
    return nullptr;
}

void JsonSkipSpaces(const char*& cursor, const char* end)
{
    while (cursor < end && (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' || *cursor == '\r')) {
        ++cursor;
    }
}

int64_t JsonParseInt(const char* p, const char* end)
{
    if (!p || p >= end) {
        return 0;
    }
    JsonSkipSpaces(p, end);
    int64_t sign = 1;
    if (p < end && *p == '-') {
        sign = -1;
        ++p;
    } else if (p < end && *p == '+') {
        ++p;
    }
    int64_t value = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        value = value * 10 + (*p - '0');
        ++p;
    }
    return value * sign;
}

double JsonParseDouble(const char* p, const char* end)
{
    if (!p || p >= end) {
        return 0.0;
    }
    JsonSkipSpaces(p, end);
    double sign = 1.0;
    if (p < end && *p == '-') {
        sign = -1.0;
        ++p;
    } else if (p < end && *p == '+') {
        ++p;
    }
    double value = 0.0;
    while (p < end && *p >= '0' && *p <= '9') {
        value = value * 10.0 + static_cast<double>(*p - '0');
        ++p;
    }
    if (p < end && *p == '.') {
        ++p;
        double scale = 0.1;
        while (p < end && *p >= '0' && *p <= '9') {
            value += static_cast<double>(*p - '0') * scale;
            scale *= 0.1;
            ++p;
        }
    }
    if (p < end && (*p == 'e' || *p == 'E')) {
        ++p;
        int expSign = 1;
        if (p < end && *p == '-') { expSign = -1; ++p; }
        else if (p < end && *p == '+') { ++p; }
        int exp = 0;
        while (p < end && *p >= '0' && *p <= '9') {
            exp = exp * 10 + (*p - '0');
            ++p;
        }
        for (int i = 0; i < exp; ++i) {
            value *= (expSign > 0) ? 10.0 : 0.1;
        }
    }
    return value * sign;
}

const char* JsonFindObjectEnd(const char* begin, const char* end)
{
    int depth = 0;
    bool inString = false;
    for (const char* p = begin; p < end; ++p) {
        if (inString) {
            if (*p == '\\' && p + 1 < end) {
                ++p;
                continue;
            }
            if (*p == '"') {
                inString = false;
            }
            continue;
        }
        if (*p == '"') {
            inString = true;
        } else if (*p == '{') {
            ++depth;
        } else if (*p == '}') {
            --depth;
            if (depth == 0) {
                return p;
            }
        }
    }
    return nullptr;
}

bool IsLatinOrHalfWidthCodepoint(int codepoint)
{
    return codepoint >= 0 && codepoint <= 0x00FF;
}

bool LoadMsdfAtlasJson(const wchar_t* path)
{
    if (!path || !*path) {
        return false;
    }

    BYTE* data = nullptr;
    DWORD size = 0;
    if (!LoadWholeFile(path, &data, &size) || !data || size < 16) {
        wchar_t message[512]{};
        wsprintfW(message, L"[Risen3FontHookTest] msdfAtlasJson open failed: %s\n", path);
        Log(message);
        if (data) HeapFree(GetProcessHeap(), 0, data);
        return false;
    }

    const char* begin = reinterpret_cast<const char*>(data);
    const char* end = begin + size;

    uint32_t atlasWidth = 0;
    uint32_t atlasHeight = 0;
    bool yOriginBottom = true;

    if (const char* p = JsonFindSubstring(begin, end, "\"width\":")) {
        atlasWidth = static_cast<uint32_t>(JsonParseInt(p + 8, end));
    }
    if (const char* p = JsonFindSubstring(begin, end, "\"height\":")) {
        atlasHeight = static_cast<uint32_t>(JsonParseInt(p + 9, end));
    }
    if (const char* p = JsonFindSubstring(begin, end, "\"yOrigin\":\"")) {
        const char* v = p + 11;
        yOriginBottom = (v < end && (*v == 'b' || *v == 'B'));
    }

    if (!atlasWidth || !atlasHeight) {
        HeapFree(GetProcessHeap(), 0, data);
        Log(L"[Risen3FontHookTest] MSDF JSON: atlas width/height not found.\n");
        return false;
    }

    const char* glyphsKey = JsonFindSubstring(begin, end, "\"glyphs\":");
    if (!glyphsKey) {
        HeapFree(GetProcessHeap(), 0, data);
        Log(L"[Risen3FontHookTest] MSDF JSON: glyphs array not found.\n");
        return false;
    }

    const char* p = glyphsKey + 9;
    JsonSkipSpaces(p, end);
    if (p >= end || *p != '[') {
        HeapFree(GetProcessHeap(), 0, data);
        Log(L"[Risen3FontHookTest] MSDF JSON: glyphs is not an array.\n");
        return false;
    }
    ++p;

    SpinLock lock(&g_externalGlyphLock);
    g_externalGlyphCount = 0;
    size_t skippedNoBounds = 0;
    size_t skippedOverflow = 0;

    while (p < end) {
        JsonSkipSpaces(p, end);
        if (p < end && *p == ',') {
            ++p;
            JsonSkipSpaces(p, end);
        }
        if (p >= end || *p == ']') {
            break;
        }
        if (*p != '{') {
            ++p;
            continue;
        }

        const char* objStart = p;
        const char* objEnd = JsonFindObjectEnd(p, end);
        if (!objEnd) {
            break;
        }

        int unicode = -1;
        double advance = 0.0;
        bool hasAtlasBounds = false;
        bool hasPlaneBounds = false;
        double abLeft = 0.0, abBottom = 0.0, abRight = 0.0, abTop = 0.0;
        double pbLeft = 0.0, pbBottom = 0.0, pbRight = 0.0, pbTop = 0.0;

        if (const char* up = JsonFindSubstring(objStart, objEnd, "\"unicode\":" )) {
            unicode = static_cast<int>(JsonParseInt(up + 10, objEnd));
        }
        if (const char* ap = JsonFindSubstring(objStart, objEnd, "\"advance\":" )) {
            advance = JsonParseDouble(ap + 10, objEnd);
        }
        if (const char* abp = JsonFindSubstring(objStart, objEnd, "\"atlasBounds\":" )) {
            JsonSkipSpaces(abp += 14, objEnd);
            if (abp < objEnd && *abp == '{') {
                const char* abEnd = JsonFindObjectEnd(abp, objEnd);
                if (abEnd) {
                    if (const char* k = JsonFindSubstring(abp, abEnd, "\"left\":" )) {
                        abLeft = JsonParseDouble(k + 7, abEnd);
                    }
                    if (const char* k = JsonFindSubstring(abp, abEnd, "\"bottom\":" )) {
                        abBottom = JsonParseDouble(k + 9, abEnd);
                    }
                    if (const char* k = JsonFindSubstring(abp, abEnd, "\"right\":" )) {
                        abRight = JsonParseDouble(k + 8, abEnd);
                    }
                    if (const char* k = JsonFindSubstring(abp, abEnd, "\"top\":" )) {
                        abTop = JsonParseDouble(k + 6, abEnd);
                    }
                    hasAtlasBounds = true;
                }
            }
        }
        if (const char* pbp = JsonFindSubstring(objStart, objEnd, "\"planeBounds\":" )) {
            JsonSkipSpaces(pbp += 14, objEnd);
            if (pbp < objEnd && *pbp == '{') {
                const char* pbEnd = JsonFindObjectEnd(pbp, objEnd);
                if (pbEnd) {
                    if (const char* k = JsonFindSubstring(pbp, pbEnd, "\"left\":" )) {
                        pbLeft = JsonParseDouble(k + 7, pbEnd);
                    }
                    if (const char* k = JsonFindSubstring(pbp, pbEnd, "\"bottom\":" )) {
                        pbBottom = JsonParseDouble(k + 9, pbEnd);
                    }
                    if (const char* k = JsonFindSubstring(pbp, pbEnd, "\"right\":" )) {
                        pbRight = JsonParseDouble(k + 8, pbEnd);
                    }
                    if (const char* k = JsonFindSubstring(pbp, pbEnd, "\"top\":" )) {
                        pbTop = JsonParseDouble(k + 6, pbEnd);
                    }
                    hasPlaneBounds = true;
                }
            }
        }

        p = objEnd + 1;

        if (unicode <= 0 || unicode > 0xFFFF) {
            continue;
        }
        if (!hasAtlasBounds) {
            ++skippedNoBounds;
            continue;
        }
        if (g_externalGlyphCount >= kExternalGlyphCount) {
            ++skippedOverflow;
            continue;
        }

        ChineseGlyphEntry& e = g_externalGlyphs[g_externalGlyphCount++];
        e.codepoint = static_cast<wchar_t>(unicode);
        e.fallback = e.codepoint;
        e.rect[0] = static_cast<float>(abLeft);
        e.rect[2] = static_cast<float>(abRight);
        if (yOriginBottom) {
            e.rect[1] = static_cast<float>(atlasHeight) - static_cast<float>(abTop);
            e.rect[3] = static_cast<float>(atlasHeight) - static_cast<float>(abBottom);
        } else {
            e.rect[1] = static_cast<float>(abTop);
            e.rect[3] = static_cast<float>(abBottom);
        }
        e.plane[0] = static_cast<float>(pbLeft);
        e.plane[1] = static_cast<float>(pbBottom);
        e.plane[2] = static_cast<float>(pbRight);
        e.plane[3] = static_cast<float>(pbTop);
        e.hasPlaneBounds = hasPlaneBounds;
        e.advanceExtra = g_msdfAdvanceExtra;
        e.naturalAdvance = static_cast<float>(advance) * static_cast<float>(g_msdfPixelSize) * g_msdfAdvanceScale;
        e.enabled = true;
    }

    HeapFree(GetProcessHeap(), 0, data);

    wchar_t message[512]{};
    wsprintfW(
        message,
        L"[Risen3FontHookTest] msdfAtlasJson loaded: %u glyphs atlas=%ux%u yOrigin=%s skipped(noBounds=%u, overflow=%u) (%s)\n",
        static_cast<unsigned>(g_externalGlyphCount),
        atlasWidth,
        atlasHeight,
        yOriginBottom ? L"bottom" : L"top",
        static_cast<unsigned>(skippedNoBounds),
        static_cast<unsigned>(skippedOverflow),
        path);
    Log(message);
    return g_externalGlyphCount > 0;
}

ChineseGlyphEntry* FindChineseGlyph(wchar_t ch)
{
    {
        SpinLock lock(&g_externalGlyphLock);
        for (size_t i = 0; i < g_externalGlyphCount; ++i) {
            if (g_externalGlyphs[i].enabled && g_externalGlyphs[i].codepoint == ch) {
                return &g_externalGlyphs[i];
            }
        }
    }

    if (g_singleChineseGlyph.enabled && g_singleChineseGlyph.codepoint == ch) {
        return &g_singleChineseGlyph;
    }

    return nullptr;
}


void ApplyMsdfGlyphMetrics(const ChineseGlyphEntry* glyph, GlyphMetrics* metrics)
{
    if (!glyph || !metrics) {
        return;
    }

    const float pixelScale = static_cast<float>(g_msdfPixelSize);

    if (glyph->hasPlaneBounds) {
        metrics->x1 = glyph->plane[0] * pixelScale;
        metrics->y1 = glyph->plane[1] * pixelScale + g_msdfMetricOffsetY;
        metrics->x2 = glyph->plane[2] * pixelScale;
        metrics->y2 = glyph->plane[3] * pixelScale + g_msdfMetricOffsetY;
        metrics->bearingX = metrics->x1;
        metrics->bearingY = metrics->y2;
    }

    if (glyph->naturalAdvance > 0.0f) {
        metrics->advance = glyph->naturalAdvance + glyph->advanceExtra;
    }
}

float ResolveMsdfGlyphAdvance(const ChineseGlyphEntry* glyph, float advanceExtraScale)
{
    if (!glyph || !(glyph->naturalAdvance > 0.0f)) {
        return 0.0f;
    }

    if (!(advanceExtraScale > 0.0f)) {
        advanceExtraScale = 1.0f;
    }

    return glyph->naturalAdvance + glyph->advanceExtra * advanceExtraScale;
}

uintptr_t GetObservedResourceByIndex(int index)
{
    if (index < 0 || index >= static_cast<int>(kObservedFontCount)) {
        return 0;
    }

    SpinLock lock(&g_observedFontLock);
    return g_observedFonts[index].resource;
}

uintptr_t ReadAtlasResourceFromFontResource(uintptr_t fontResource)
{
    if (!fontResource) {
        return 0;
    }

    uintptr_t fontObject = 0;
    uintptr_t atlasResource = 0;
    if (!SafeReadQword(fontResource + 0x10, &fontObject) || !fontObject) {
        return 0;
    }

    SafeReadQword(fontObject + 0x38, &atlasResource);
    return atlasResource;
}

uintptr_t ResolveReplacementAtlasResource()
{
    if (g_replacementAtlasResource) {
        return g_replacementAtlasResource;
    }

    if (g_replacementAtlasIndex >= 0) {
        const uintptr_t fontResource = GetObservedResourceByIndex(g_replacementAtlasIndex);
        return ReadAtlasResourceFromFontResource(fontResource);
    }

    return 0;
}

uintptr_t ResolveAutoReplacementAtlasResource(uintptr_t originalAtlas)
{
    SpinLock lock(&g_observedFontLock);

    for (size_t i = 0; i < kObservedFontCount; ++i) {
        const uintptr_t fontResource = g_observedFonts[i].resource;
        if (!fontResource) {
            continue;
        }

        const uintptr_t atlas = ReadAtlasResourceFromFontResource(fontResource);
        if (atlas && atlas != originalAtlas) {
            return atlas;
        }
    }

    return 0;
}

uintptr_t ResolveReplacementResource()
{
    if (!g_enableReplacement) {
        return 0;
    }

    if (g_replacementIndex >= 0) {
        const uintptr_t resource = GetObservedResourceByIndex(g_replacementIndex);
        if (resource && InterlockedExchange(&g_loggedReplacementResolved, 1) == 0) {
            wchar_t message[256]{};
            wsprintfW(
                message,
                L"[Risen3FontHookTest] replacementIndex=%d resolved to resource=%p\n",
                g_replacementIndex,
                reinterpret_cast<void*>(resource));
            Log(message);
        }
        return resource;
    }

    return g_replacementResource;
}

void __fastcall LogCommandResourceWrite(void* command, uintptr_t value, DWORD state0)
{
    if (g_markedDrawGlyphDepth <= 0) {
        return;
    }

    const LONG logIndex = InterlockedIncrement(&g_commandResourceWriteLogCount);
    if (logIndex > kCommandResourceWriteLogLimit) {
        return;
    }

    uintptr_t vtable = 0;
    uintptr_t tag = 0;
    DWORD id = 0;
    DWORD width = 0;
    DWORD height = 0;
    wchar_t format[5]{};
    SafeReadQword(value + 0x00, &vtable);
    SafeReadQword(value + 0x08, &tag);
    SafeReadDword(value + 0x08, &id);
    SafeReadDword(value + 0x10, &width);
    SafeReadDword(value + 0x14, &height);
    ReadAscii4(value + 0x18, format);

    wchar_t message[768]{};
    wsprintfW(
        message,
        L"[Risen3FontHookTest] CommandWrite[%d] command=%p value=%p kind=%s vtable=%p tag=%p id=%08X size=%ux%u fmt=%s state0=%08X\n",
        logIndex,
        command,
        reinterpret_cast<void*>(value),
        ClassifyResourceOrTexture(value),
        reinterpret_cast<void*>(vtable),
        reinterpret_cast<void*>(tag),
        id,
        width,
        height,
        format,
        state0);
    Log(message);
}

int __fastcall HookBindTexture(void* wrapperThis, uintptr_t stage, void* gfxTexture)
{
    if (!g_originalBindTexture) {
        return 0xFF000001;
    }

    if (!gfxTexture) {
        return g_originalBindTexture(wrapperThis, stage, gfxTexture);
    }

    const DWORD now = GetTickCount();
    const DWORD markedAge = g_lastMarkedGlyphTick ? (now - g_lastMarkedGlyphTick) : 0xFFFFFFFFu;
    const bool inMarkedWindow = g_lastMarkedGlyphTick && markedAge <= g_textureReplaceWindowMs;
    const uintptr_t atlasArgument = reinterpret_cast<uintptr_t>(gfxTexture);
    const bool isMarkedAtlas = g_markedAtlasResource && atlasArgument == g_markedAtlasResource;
    bool shouldReplaceAtlasArgument = false;
    if (g_enableFullFontAtlasReplace && isMarkedAtlas) {
        shouldReplaceAtlasArgument = true;
    } else if (isMarkedAtlas && InterlockedCompareExchange(&g_pendingAtlasSwapCount, 0, 1) == 1) {
        shouldReplaceAtlasArgument = true;
    } else if (!g_enableShadowResource && g_replaceProbeTextures && inMarkedWindow && g_markedAtlasResource &&
        InterlockedCompareExchange(&g_pendingAtlasSwapCount, 0, 1) == 1) {
        shouldReplaceAtlasArgument = true;
    }

    if (inMarkedWindow || isMarkedAtlas || g_markedAtlasResource) {
        const LONG decisionIndex = InterlockedIncrement(&g_bindTextureDecisionLogCount);
        if (decisionIndex <= kBindTextureProbeLogLimit) {
            wchar_t message[768]{};
            wsprintfW(
                message,
                L"[Risen3FontHookTest] BindTextureAtlasDecision[%d] wrapper=%p stage=%p arg=%p markedAtlas=%p inWindow=%d age=%u shouldReplace=%d pending=%d probe=%d externalAtlas=%d\n",
                decisionIndex,
                wrapperThis,
                reinterpret_cast<void*>(stage),
                gfxTexture,
                reinterpret_cast<void*>(g_markedAtlasResource),
                inMarkedWindow ? 1 : 0,
                markedAge,
                shouldReplaceAtlasArgument ? 1 : 0,
                g_pendingAtlasSwapCount,
                g_replaceProbeTextures ? 1 : 0,
                g_enableExternalAtlas ? 1 : 0);
            Log(message);
        }
    }

    if (shouldReplaceAtlasArgument) {
        IDirect3DDevice9* device = GetDeviceFromAtlas(atlasArgument);

        if (device) {
            auto* replacement = GetOrCreateTestAtlasTexture(device);
            if (replacement) {
                uintptr_t originalAtlasTexture = 0;
                const uintptr_t atlasTextureSlot = atlasArgument + 0x08;
                SafeReadQword(atlasTextureSlot, &originalAtlasTexture);
                const bool wroteAtlasSlot =
                    originalAtlasTexture &&
                    SafeWriteQword(atlasTextureSlot, reinterpret_cast<uintptr_t>(replacement));

                int result = 0;
                if (wroteAtlasSlot) {
                    result = g_originalBindTexture(wrapperThis, stage, gfxTexture);
                    if (!g_enableFullFontAtlasReplace) {
                        SafeWriteQword(atlasTextureSlot, originalAtlasTexture);
                    }
                } else {
                    result = g_originalBindTexture(wrapperThis, stage, gfxTexture);
                }

                const LONG logIndex = InterlockedIncrement(&g_textureReplaceLogCount);
                if (logIndex <= kTextureReplaceLogLimit) {
                    wchar_t message[512]{};
                    wsprintfW(
                        message,
                        L"[Risen3FontHookTest] HookBindTexture atlas-slot original=%p replacement=%p wrote=%d persistent=%d result=%08X atlas=%p slot=+08\n",
                        reinterpret_cast<void*>(originalAtlasTexture),
                        replacement,
                        wroteAtlasSlot ? 1 : 0,
                        g_enableFullFontAtlasReplace ? 1 : 0,
                        static_cast<unsigned>(result),
                        gfxTexture);
                    Log(message);
                }
                device->Release();
                return result;
            }
            device->Release();
        } else {
            const LONG logIndex = InterlockedIncrement(&g_textureReplaceLogCount);
            if (logIndex <= kTextureReplaceLogLimit) {
                wchar_t message[512]{};
                wsprintfW(
                    message,
                    L"[Risen3FontHookTest] HookBindTexture atlas-slot skipped: device not found atlas=%p\n",
                    gfxTexture);
                Log(message);
            }
        }
    }

    uintptr_t textureSlot = 0;
    uintptr_t currentTexture = 0;
    DWORD textureSlotOffset = 0;
    if (!FindD3DTextureSlotInGfxTextureView(
            reinterpret_cast<uintptr_t>(gfxTexture),
            &textureSlot,
            &currentTexture,
            &textureSlotOffset)) {
        const LONG probeIndex = InterlockedIncrement(&g_bindTextureProbeLogCount);
        if (probeIndex <= kBindTextureProbeLogLimit) {
            wchar_t message[512]{};
            wsprintfW(
                message,
                L"[Risen3FontHookTest] BindTextureProbe[%d] wrapper=%p stage=%p gfx=%p currentTexture=%p target=%p slot=none\n",
                probeIndex,
                wrapperThis,
                reinterpret_cast<void*>(stage),
                gfxTexture,
                reinterpret_cast<void*>(0),
                reinterpret_cast<void*>(g_forcedTargetD3DTexture ? g_forcedTargetD3DTexture : g_markedD3DTexture));
            Log(message);
        }

        return g_originalBindTexture(wrapperThis, stage, gfxTexture);
    }

    const uintptr_t targetTexture = g_forcedTargetD3DTexture ? g_forcedTargetD3DTexture : g_markedD3DTexture;

    const LONG probeIndex = InterlockedIncrement(&g_bindTextureProbeLogCount);
    if (probeIndex <= kBindTextureProbeLogLimit) {
        wchar_t message[512]{};
        wsprintfW(
            message,
            L"[Risen3FontHookTest] BindTextureProbe[%d] wrapper=%p stage=%p gfx=%p currentTexture=%p target=%p slot=+%02X age=%u window=%u\n",
            probeIndex,
            wrapperThis,
            reinterpret_cast<void*>(stage),
            gfxTexture,
            reinterpret_cast<void*>(currentTexture),
            reinterpret_cast<void*>(targetTexture),
            textureSlotOffset,
            markedAge,
            g_textureReplaceWindowMs);
        Log(message);
    }

    const bool allowLegacyTextureSwap = !g_enableShadowResource && !g_enableFullFontAtlasReplace;
    const bool shouldReplace =
        (allowLegacyTextureSwap && inMarkedWindow && targetTexture && currentTexture == targetTexture) ||
        (allowLegacyTextureSwap && inMarkedWindow && !targetTexture && g_replaceProbeTextures && g_markedResource && currentTexture &&
            probeIndex <= kBindTextureProbeLogLimit);

    if (inMarkedWindow || shouldReplace || targetTexture) {
        const LONG decisionIndex = InterlockedIncrement(&g_bindTextureDecisionLogCount);
        if (decisionIndex <= kBindTextureProbeLogLimit) {
            wchar_t message[768]{};
            wsprintfW(
                message,
                L"[Risen3FontHookTest] BindTextureDecision[%d] current=%p target=%p forced=%p marked=%p inWindow=%d age=%u shouldReplace=%d probe=%d externalAtlas=%d markedResource=%p\n",
                decisionIndex,
                reinterpret_cast<void*>(currentTexture),
                reinterpret_cast<void*>(targetTexture),
                reinterpret_cast<void*>(g_forcedTargetD3DTexture),
                reinterpret_cast<void*>(g_markedD3DTexture),
                inMarkedWindow ? 1 : 0,
                markedAge,
                shouldReplace ? 1 : 0,
                g_replaceProbeTextures ? 1 : 0,
                g_enableExternalAtlas ? 1 : 0,
                reinterpret_cast<void*>(g_markedResource));
            Log(message);
        }
    }

    if (!shouldReplace) {
        if (!g_originalBindTexture) {
            return 0xFF000001;
        }

        return g_originalBindTexture(wrapperThis, stage, gfxTexture);
    }

    auto* device = GetDeviceFromD3DTexture(currentTexture);
    if (!device) {
        const LONG logIndex = InterlockedIncrement(&g_textureReplaceLogCount);
        if (logIndex <= kTextureReplaceLogLimit) {
            wchar_t message[512]{};
            wsprintfW(
                message,
                L"[Risen3FontHookTest] HookBindTexture[%d] skipped: GetDevice failed original=%p gfx=%p slot=+%02X\n",
                logIndex,
                reinterpret_cast<void*>(currentTexture),
                gfxTexture,
                textureSlotOffset);
            Log(message);
        }

        return g_originalBindTexture(wrapperThis, stage, gfxTexture);
    }

    auto* replacement = GetOrCreateTestAtlasTexture(device);
    device->Release();
    if (!replacement) {
        return g_originalBindTexture(wrapperThis, stage, gfxTexture);
    }

    if (!SafeWriteQword(textureSlot, reinterpret_cast<uintptr_t>(replacement))) {
        const LONG logIndex = InterlockedIncrement(&g_textureReplaceLogCount);
        if (logIndex <= kTextureReplaceLogLimit) {
            wchar_t message[512]{};
            wsprintfW(
                message,
                L"[Risen3FontHookTest] HookBindTexture[%d] skipped: slot write failed original=%p replacement=%p gfx=%p slot=+%02X\n",
                logIndex,
                reinterpret_cast<void*>(currentTexture),
                replacement,
                gfxTexture,
                textureSlotOffset);
            Log(message);
        }

        return g_originalBindTexture(wrapperThis, stage, gfxTexture);
    }

    const LONG logIndex = InterlockedIncrement(&g_textureReplaceLogCount);
    if (logIndex <= kTextureReplaceLogLimit) {
        wchar_t message[512]{};
        wsprintfW(
            message,
            L"[Risen3FontHookTest] HookBindTexture[%d] swapped gfx slot +%02X original=%p replacement=%p target=%p probeMode=%d\n",
            logIndex,
            textureSlotOffset,
            reinterpret_cast<void*>(currentTexture),
            replacement,
            reinterpret_cast<void*>(targetTexture),
            (!targetTexture && g_replaceProbeTextures) ? 1 : 0);
        Log(message);
    }

    const int result = g_originalBindTexture(wrapperThis, stage, gfxTexture);
    SafeWriteQword(textureSlot, currentTexture);
    return result;
}

uintptr_t ResolveReplacementResourceForMarkedGlyph(FontState* fontState)
{
    if (g_enableFullFontAtlasReplace) {
        return 0;
    }

    uintptr_t replacement = ResolveReplacementResource();
    if (!replacement) {
        replacement = GetOrCreateShadowFontResource(fontState);
    }

    return replacement;
}

