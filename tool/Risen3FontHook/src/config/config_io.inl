bool TryLoadConfigFromIni(const wchar_t* path)
{
    wchar_t buffer[64]{};
    g_replacementAtlasResource = 0;
    g_replacementAtlasIndex = -1;
    g_forcedTargetD3DTexture = 0;
    {
        SpinLock glyphLock(&g_externalGlyphLock);
        g_externalGlyphCount = 0;
    }

    const DWORD indexLength = GetPrivateProfileStringW(
        L"Risen3FontHookTest",
        L"replacementIndex",
        L"",
        buffer,
        ARRAYSIZE(buffer),
        path);

    bool loaded = false;
    if (indexLength > 0 && indexLength < ARRAYSIZE(buffer)) {
        const int index = ParseDecimalInt(buffer, -1);
        if (index >= 0 && index < static_cast<int>(kObservedFontCount)) {
            g_replacementIndex = index;

            wchar_t message[512]{};
            wsprintfW(
                message,
                L"[Risen3FontHookTest] Replacement index loaded from INI: %d (%s)\n",
                g_replacementIndex,
                path);
            Log(message);
            loaded = true;
        }
    }

    const DWORD length = GetPrivateProfileStringW(
        L"Risen3FontHookTest",
        L"replacement",
        L"",
        buffer,
        ARRAYSIZE(buffer),
        path);

    if (length > 0 && length < ARRAYSIZE(buffer)) {
        const uintptr_t value = ParseHexPointer(buffer);
        if (value) {
            g_replacementResource = value;

            wchar_t message[512]{};
            wsprintfW(
                message,
                L"[Risen3FontHookTest] Replacement resource loaded from INI: %p (%s)\n",
                reinterpret_cast<void*>(g_replacementResource),
                path);
            Log(message);
            loaded = true;
        }
    }

    const DWORD flushHookLength = GetPrivateProfileStringW(
        L"Risen3FontHookTest",
        L"enableFlushHook",
        L"",
        buffer,
        ARRAYSIZE(buffer),
        path);

    if (flushHookLength > 0 && flushHookLength < ARRAYSIZE(buffer)) {
        g_enableFlushHook = ParseBoolInt(buffer, false);

        wchar_t message[512]{};
        wsprintfW(
            message,
            L"[Risen3FontHookTest] enableFlushHook=%d (%s)\n",
            g_enableFlushHook ? 1 : 0,
            path);
        Log(message);
        loaded = true;
    }

    const DWORD replacementLength = GetPrivateProfileStringW(
        L"Risen3FontHookTest",
        L"enableReplacement",
        L"",
        buffer,
        ARRAYSIZE(buffer),
        path);

    if (replacementLength > 0 && replacementLength < ARRAYSIZE(buffer)) {
        g_enableReplacement = ParseBoolInt(buffer, false);

        wchar_t message[512]{};
        wsprintfW(
            message,
            L"[Risen3FontHookTest] enableReplacement=%d (%s)\n",
            g_enableReplacement ? 1 : 0,
            path);
        Log(message);
        loaded = true;
    }

    const DWORD commandWriteHookLength = GetPrivateProfileStringW(
        L"Risen3FontHookTest",
        L"enableCommandWriteHook",
        L"",
        buffer,
        ARRAYSIZE(buffer),
        path);

    if (commandWriteHookLength > 0 && commandWriteHookLength < ARRAYSIZE(buffer)) {
        g_enableCommandWriteHook = ParseBoolInt(buffer, true);

        wchar_t message[512]{};
        wsprintfW(
            message,
            L"[Risen3FontHookTest] enableCommandWriteHook=%d (%s)\n",
            g_enableCommandWriteHook ? 1 : 0,
            path);
        Log(message);
        loaded = true;
    }

    const DWORD submitHookLength = GetPrivateProfileStringW(
        L"Risen3FontHookTest",
        L"enableSubmitHook",
        L"",
        buffer,
        ARRAYSIZE(buffer),
        path);

    if (submitHookLength > 0 && submitHookLength < ARRAYSIZE(buffer)) {
        g_enableSubmitHook = ParseBoolInt(buffer, true);

        wchar_t message[512]{};
        wsprintfW(
            message,
            L"[Risen3FontHookTest] enableSubmitHook=%d (%s)\n",
            g_enableSubmitHook ? 1 : 0,
            path);
        Log(message);
        loaded = true;
    }

    const DWORD buildUvHookLength = GetPrivateProfileStringW(
        L"Risen3FontHookTest",
        L"enableBuildUvHook",
        L"",
        buffer,
        ARRAYSIZE(buffer),
        path);

    if (buildUvHookLength > 0 && buildUvHookLength < ARRAYSIZE(buffer)) {
        g_enableBuildUvHook = ParseBoolInt(buffer, true);

        wchar_t message[512]{};
        wsprintfW(
            message,
            L"[Risen3FontHookTest] enableBuildUvHook=%d (%s)\n",
            g_enableBuildUvHook ? 1 : 0,
            path);
        Log(message);
        loaded = true;
    }

    const DWORD buildUvRectOverrideLength = GetPrivateProfileStringW(
        L"Risen3FontHookTest",
        L"enableBuildUvRectOverride",
        L"",
        buffer,
        ARRAYSIZE(buffer),
        path);

    if (buildUvRectOverrideLength > 0 && buildUvRectOverrideLength < ARRAYSIZE(buffer)) {
        g_enableBuildUvRectOverride = ParseBoolInt(buffer, false);

        wchar_t message[512]{};
        wsprintfW(
            message,
            L"[Risen3FontHookTest] enableBuildUvRectOverride=%d (%s)\n",
            g_enableBuildUvRectOverride ? 1 : 0,
            path);
        Log(message);
        loaded = true;
    }

    wchar_t rectBuffer[128]{};
    const DWORD buildUvRectLength = GetPrivateProfileStringW(
        L"Risen3FontHookTest",
        L"buildUvRect",
        L"",
        rectBuffer,
        ARRAYSIZE(rectBuffer),
        path);

    if (buildUvRectLength > 0 && buildUvRectLength < ARRAYSIZE(rectBuffer)) {
        ParseFloat4(rectBuffer, g_buildUvRectOverride);

        wchar_t message[512]{};
        wsprintfW(
            message,
            L"[Risen3FontHookTest] buildUvRect=%d,%d,%d,%d (%s)\n",
            static_cast<int>(g_buildUvRectOverride[0] * 1000.0f),
            static_cast<int>(g_buildUvRectOverride[1] * 1000.0f),
            static_cast<int>(g_buildUvRectOverride[2] * 1000.0f),
            static_cast<int>(g_buildUvRectOverride[3] * 1000.0f),
            path);
        Log(message);
        loaded = true;
    }

    const DWORD glyphCodepointLength = GetPrivateProfileStringW(
        L"Risen3FontHookTest",
        L"glyphCodepoint",
        L"",
        buffer,
        ARRAYSIZE(buffer),
        path);

    if (glyphCodepointLength > 0 && glyphCodepointLength < ARRAYSIZE(buffer)) {
        const uintptr_t value = ParseHexPointer(buffer);
        if (value > 0 && value <= 0xFFFF) {
            g_singleChineseGlyph.codepoint = static_cast<wchar_t>(value);
            wchar_t message[512]{};
            wsprintfW(
                message,
                L"[Risen3FontHookTest] glyphCodepoint=U+%04X (%s)\n",
                static_cast<unsigned>(g_singleChineseGlyph.codepoint),
                path);
            Log(message);
            loaded = true;
        }
    }

    const DWORD glyphFallbackLength = GetPrivateProfileStringW(
        L"Risen3FontHookTest",
        L"glyphFallback",
        L"",
        buffer,
        ARRAYSIZE(buffer),
        path);

    if (glyphFallbackLength > 0 && glyphFallbackLength < ARRAYSIZE(buffer)) {
        const uintptr_t value = ParseHexPointer(buffer);
        if (value > 0 && value <= 0xFFFF) {
            g_singleChineseGlyph.fallback = static_cast<wchar_t>(value);
            wchar_t message[512]{};
            wsprintfW(
                message,
                L"[Risen3FontHookTest] glyphFallback=U+%04X (%s)\n",
                static_cast<unsigned>(g_singleChineseGlyph.fallback),
                path);
            Log(message);
            loaded = true;
        }
    }

    wchar_t glyphRectBuffer[128]{};
    const DWORD glyphRectLength = GetPrivateProfileStringW(
        L"Risen3FontHookTest",
        L"glyphRect",
        L"",
        glyphRectBuffer,
        ARRAYSIZE(glyphRectBuffer),
        path);

    if (glyphRectLength > 0 && glyphRectLength < ARRAYSIZE(glyphRectBuffer)) {
        ParseFloat4(glyphRectBuffer, g_singleChineseGlyph.rect);

        wchar_t message[512]{};
        wsprintfW(
            message,
            L"[Risen3FontHookTest] glyphRect=%d,%d,%d,%d (%s)\n",
            static_cast<int>(g_singleChineseGlyph.rect[0] * 1000.0f),
            static_cast<int>(g_singleChineseGlyph.rect[1] * 1000.0f),
            static_cast<int>(g_singleChineseGlyph.rect[2] * 1000.0f),
            static_cast<int>(g_singleChineseGlyph.rect[3] * 1000.0f),
            path);
        Log(message);
        loaded = true;
    }

    const DWORD glyphAdvanceExtraLength = GetPrivateProfileStringW(
        L"Risen3FontHookTest",
        L"glyphAdvanceExtra",
        L"",
        buffer,
        ARRAYSIZE(buffer),
        path);

    if (glyphAdvanceExtraLength > 0 && glyphAdvanceExtraLength < ARRAYSIZE(buffer)) {
        const wchar_t* cursor = buffer;
        g_singleChineseGlyph.advanceExtra = ParseFloatSimple(&cursor);
        wchar_t message[512]{};
        wsprintfW(
            message,
            L"[Risen3FontHookTest] glyphAdvanceExtra=%d (%s)\n",
            static_cast<int>(g_singleChineseGlyph.advanceExtra * 1000.0f),
            path);
        Log(message);
        loaded = true;
    }

    const DWORD drawGlyphDiagnosticsLength = GetPrivateProfileStringW(
        L"Risen3FontHookTest",
        L"enableDrawGlyphDiagnostics",
        L"",
        buffer,
        ARRAYSIZE(buffer),
        path);
    if (drawGlyphDiagnosticsLength > 0 && drawGlyphDiagnosticsLength < ARRAYSIZE(buffer)) {
        g_enableDrawGlyphDiagnostics = ParseBoolInt(buffer, false);
        if (g_enableDrawGlyphDiagnostics) {
            InterlockedExchange(&g_drawGlyphDiagnosticLogCount, 0);
        }
        wchar_t message[256]{};
        wsprintfW(
            message,
            L"[Risen3FontHookTest] enableDrawGlyphDiagnostics=%d (%s)\n",
            g_enableDrawGlyphDiagnostics ? 1 : 0,
            path);
        Log(message);
        loaded = true;
    }

    wchar_t pathBuffer[MAX_PATH]{};
    const DWORD glyphMapLength = GetPrivateProfileStringW(
        L"Risen3FontHookTest",
        L"glyphMap",
        L"",
        pathBuffer,
        ARRAYSIZE(pathBuffer),
        path);

    if (glyphMapLength > 0 && glyphMapLength < ARRAYSIZE(pathBuffer)) {
        wchar_t resolved[MAX_PATH]{};
        if (ResolveConfigRelativePath(pathBuffer, resolved, ARRAYSIZE(resolved))) {
            lstrcpynW(g_externalGlyphMapPath, resolved, ARRAYSIZE(g_externalGlyphMapPath));
            LoadExternalGlyphMap(g_externalGlyphMapPath);
            loaded = true;
        }
    }

    const DWORD externalAtlasEnableLength = GetPrivateProfileStringW(
        L"Risen3FontHookTest",
        L"enableExternalAtlas",
        L"",
        buffer,
        ARRAYSIZE(buffer),
        path);

    if (externalAtlasEnableLength > 0 && externalAtlasEnableLength < ARRAYSIZE(buffer)) {
        g_enableExternalAtlas = ParseBoolInt(buffer, false);
        ResetGeneratedAtlasTexture();

        wchar_t message[512]{};
        wsprintfW(
            message,
            L"[Risen3FontHookTest] enableExternalAtlas=%d (%s)\n",
            g_enableExternalAtlas ? 1 : 0,
            path);
        Log(message);
        loaded = true;
    }

    wchar_t atlasPathBuffer[MAX_PATH]{};
    DWORD externalAtlasPathLength = GetPrivateProfileStringW(
        L"Risen3FontHookTest",
        L"externalAtlasFile",
        L"",
        atlasPathBuffer,
        ARRAYSIZE(atlasPathBuffer),
        path);
    const bool usedExternalAtlasFileKey = externalAtlasPathLength > 0;
    if (!usedExternalAtlasFileKey) {
        externalAtlasPathLength = GetPrivateProfileStringW(
            L"Risen3FontHookTest",
            L"externalAtlasBmp",
            L"",
            atlasPathBuffer,
            ARRAYSIZE(atlasPathBuffer),
            path);
    }

    if (externalAtlasPathLength > 0 && externalAtlasPathLength < ARRAYSIZE(atlasPathBuffer)) {
        wchar_t resolved[MAX_PATH]{};
        if (ResolveConfigRelativePath(atlasPathBuffer, resolved, ARRAYSIZE(resolved))) {
            lstrcpynW(g_externalAtlasBmpPath, resolved, ARRAYSIZE(g_externalAtlasBmpPath));
            if (LoadExternalAtlasImage(g_externalAtlasBmpPath)) {
                ResetGeneratedAtlasTexture();
            }
            loaded = true;
        }
    }

    const DWORD msdfAtlasEnableLength = GetPrivateProfileStringW(
        L"Risen3FontHookTest",
        L"enableMsdfAtlas",
        L"",
        buffer,
        ARRAYSIZE(buffer),
        path);
    if (msdfAtlasEnableLength > 0 && msdfAtlasEnableLength < ARRAYSIZE(buffer)) {
        g_enableMsdfAtlas = ParseBoolInt(buffer, false);
        wchar_t message[256]{};
        wsprintfW(
            message,
            L"[Risen3FontHookTest] enableMsdfAtlas=%d (%s)\n",
            g_enableMsdfAtlas ? 1 : 0,
            path);
        Log(message);
        loaded = true;
    }

    wchar_t msdfJsonBuffer[MAX_PATH]{};
    const DWORD msdfJsonLength = GetPrivateProfileStringW(
        L"Risen3FontHookTest",
        L"msdfAtlasJson",
        L"",
        msdfJsonBuffer,
        ARRAYSIZE(msdfJsonBuffer),
        path);
    if (msdfJsonLength > 0 && msdfJsonLength < ARRAYSIZE(msdfJsonBuffer)) {
        wchar_t resolved[MAX_PATH]{};
        if (ResolveConfigRelativePath(msdfJsonBuffer, resolved, ARRAYSIZE(resolved))) {
            lstrcpynW(g_msdfAtlasJsonPath, resolved, ARRAYSIZE(g_msdfAtlasJsonPath));
            wchar_t message[512]{};
            wsprintfW(message, L"[Risen3FontHookTest] msdfAtlasJson=%s\n", g_msdfAtlasJsonPath);
            Log(message);
            loaded = true;
        }
    }

    const DWORD msdfPixelSizeLength = GetPrivateProfileStringW(
        L"Risen3FontHookTest",
        L"msdfPixelSize",
        L"",
        buffer,
        ARRAYSIZE(buffer),
        path);
    if (msdfPixelSizeLength > 0 && msdfPixelSizeLength < ARRAYSIZE(buffer)) {
        const int value = ParseDecimalInt(buffer, g_msdfPixelSize);
        if (value >= 8 && value <= 256) {
            g_msdfPixelSize = value;
        }
        wchar_t message[256]{};
        wsprintfW(
            message,
            L"[Risen3FontHookTest] msdfPixelSize=%d (%s)\n",
            g_msdfPixelSize,
            path);
        Log(message);
        loaded = true;
    }

    const DWORD legacyPixelSizeLength = GetPrivateProfileStringW(
        L"Risen3FontHookTest",
        L"freeTypePixelSize",
        L"",
        buffer,
        ARRAYSIZE(buffer),
        path);
    if (legacyPixelSizeLength > 0 && legacyPixelSizeLength < ARRAYSIZE(buffer)) {
        const int value = ParseDecimalInt(buffer, g_msdfPixelSize);
        if (value >= 8 && value <= 256) {
            g_msdfPixelSize = value;
        }
        wchar_t message[256]{};
        wsprintfW(
            message,
            L"[Risen3FontHookTest] freeTypePixelSize legacy alias -> msdfPixelSize=%d (%s)\n",
            g_msdfPixelSize,
            path);
        Log(message);
        loaded = true;
    }

    const DWORD msdfAdvanceScaleLength = GetPrivateProfileStringW(
        L"Risen3FontHookTest",
        L"msdfAdvanceScale",
        L"",
        buffer,
        ARRAYSIZE(buffer),
        path);
    if (msdfAdvanceScaleLength > 0 && msdfAdvanceScaleLength < ARRAYSIZE(buffer)) {
        const wchar_t* cursor = buffer;
        const float value = ParseFloatSimple(&cursor);
        if (value > 0.05f && value < 4.0f) {
            g_msdfAdvanceScale = value;
        }
        wchar_t message[256]{};
        wsprintfW(
            message,
            L"[Risen3FontHookTest] msdfAdvanceScale=%d (%s)\n",
            static_cast<int>(g_msdfAdvanceScale * 1000.0f),
            path);
        Log(message);
        loaded = true;
    }

    const DWORD msdfAdvanceExtraLength = GetPrivateProfileStringW(
        L"Risen3FontHookTest",
        L"msdfAdvanceExtra",
        L"",
        buffer,
        ARRAYSIZE(buffer),
        path);
    if (msdfAdvanceExtraLength > 0 && msdfAdvanceExtraLength < ARRAYSIZE(buffer)) {
        const wchar_t* cursor = buffer;
        g_msdfAdvanceExtra = ParseFloatSimple(&cursor);
        wchar_t message[256]{};
        wsprintfW(
            message,
            L"[Risen3FontHookTest] msdfAdvanceExtra=%d (%s)\n",
            static_cast<int>(g_msdfAdvanceExtra * 1000.0f),
            path);
        Log(message);
        loaded = true;
    }

    const DWORD legacyAdvanceExtraLength = GetPrivateProfileStringW(
        L"Risen3FontHookTest",
        L"freeTypeAdvanceExtra",
        L"",
        buffer,
        ARRAYSIZE(buffer),
        path);
    if (legacyAdvanceExtraLength > 0 && legacyAdvanceExtraLength < ARRAYSIZE(buffer)) {
        const wchar_t* cursor = buffer;
        g_msdfAdvanceExtra = ParseFloatSimple(&cursor);
        wchar_t message[256]{};
        wsprintfW(
            message,
            L"[Risen3FontHookTest] freeTypeAdvanceExtra legacy alias -> msdfAdvanceExtra=%d (%s)\n",
            static_cast<int>(g_msdfAdvanceExtra * 1000.0f),
            path);
        Log(message);
        loaded = true;
    }

    const DWORD msdfMetricOffsetYLength = GetPrivateProfileStringW(
        L"Risen3FontHookTest",
        L"msdfMetricOffsetY",
        L"",
        buffer,
        ARRAYSIZE(buffer),
        path);
    if (msdfMetricOffsetYLength > 0 && msdfMetricOffsetYLength < ARRAYSIZE(buffer)) {
        const wchar_t* cursor = buffer;
        const float value = ParseFloatSimple(&cursor);
        if (value > -128.0f && value < 128.0f) {
            g_msdfMetricOffsetY = value;
        }
        wchar_t message[256]{};
        wsprintfW(
            message,
            L"[Risen3FontHookTest] msdfMetricOffsetY=%d (%s)\n",
            static_cast<int>(g_msdfMetricOffsetY * 1000.0f),
            path);
        Log(message);
        loaded = true;
    }

    if (g_enableMsdfAtlas) {
        if (g_msdfAtlasJsonPath[0]) {
            if (LoadMsdfAtlasJson(g_msdfAtlasJsonPath)) {
                ResetGeneratedAtlasTexture();
            }
            loaded = true;
        } else {
            Log(L"[Risen3FontHookTest] enableMsdfAtlas=1 but msdfAtlasJson is empty.\n");
        }
    }

    const DWORD atlasGetterHookLength = GetPrivateProfileStringW(
        L"Risen3FontHookTest",
        L"enableAtlasGetterHook",
        L"",
        buffer,
        ARRAYSIZE(buffer),
        path);

    if (atlasGetterHookLength > 0 && atlasGetterHookLength < ARRAYSIZE(buffer)) {
        g_enableAtlasGetterHook = ParseBoolInt(buffer, true);

        wchar_t message[512]{};
        wsprintfW(
            message,
            L"[Risen3FontHookTest] enableAtlasGetterHook=%d (%s)\n",
            g_enableAtlasGetterHook ? 1 : 0,
            path);
        Log(message);
        loaded = true;
    }

    const DWORD atlasResourceIndexLength = GetPrivateProfileStringW(
        L"Risen3FontHookTest",
        L"atlasResourceIndex",
        L"",
        buffer,
        ARRAYSIZE(buffer),
        path);

    if (atlasResourceIndexLength > 0 && atlasResourceIndexLength < ARRAYSIZE(buffer)) {
        g_replacementAtlasIndex = ParseDecimalInt(buffer, -1);

        wchar_t message[512]{};
        wsprintfW(
            message,
            L"[Risen3FontHookTest] atlasResourceIndex=%d (%s)\n",
            g_replacementAtlasIndex,
            path);
        Log(message);
        loaded = true;
    }

    const DWORD atlasResourceLength = GetPrivateProfileStringW(
        L"Risen3FontHookTest",
        L"atlasResource",
        L"",
        buffer,
        ARRAYSIZE(buffer),
        path);

    if (atlasResourceLength > 0 && atlasResourceLength < ARRAYSIZE(buffer)) {
        const uintptr_t value = ParseHexPointer(buffer);
        if (value) {
            g_replacementAtlasResource = value;

            wchar_t message[512]{};
            wsprintfW(
                message,
                L"[Risen3FontHookTest] atlasResource=%p (%s)\n",
                reinterpret_cast<void*>(g_replacementAtlasResource),
                path);
            Log(message);
            loaded = true;
        }
    }

    const DWORD textureReplaceHookLength = GetPrivateProfileStringW(
        L"Risen3FontHookTest",
        L"enableTextureReplaceHook",
        L"",
        buffer,
        ARRAYSIZE(buffer),
        path);

    if (textureReplaceHookLength > 0 && textureReplaceHookLength < ARRAYSIZE(buffer)) {
        g_enableTextureReplaceHook = ParseBoolInt(buffer, true);

        wchar_t message[512]{};
        wsprintfW(
            message,
            L"[Risen3FontHookTest] enableTextureReplaceHook=%d (%s)\n",
            g_enableTextureReplaceHook ? 1 : 0,
            path);
        Log(message);
        loaded = true;
    }

    const DWORD shadowResourceLength = GetPrivateProfileStringW(
        L"Risen3FontHookTest",
        L"enableShadowResource",
        L"",
        buffer,
        ARRAYSIZE(buffer),
        path);

    if (shadowResourceLength > 0 && shadowResourceLength < ARRAYSIZE(buffer)) {
        g_enableShadowResource = ParseBoolInt(buffer, true);

        wchar_t message[512]{};
        wsprintfW(
            message,
            L"[Risen3FontHookTest] enableShadowResource=%d (%s)\n",
            g_enableShadowResource ? 1 : 0,
            path);
        Log(message);
        loaded = true;
    }

    const DWORD fullAtlasReplaceLength = GetPrivateProfileStringW(
        L"Risen3FontHookTest",
        L"enableFullFontAtlasReplace",
        L"",
        buffer,
        ARRAYSIZE(buffer),
        path);

    if (fullAtlasReplaceLength > 0 && fullAtlasReplaceLength < ARRAYSIZE(buffer)) {
        g_enableFullFontAtlasReplace = ParseBoolInt(buffer, false);

        wchar_t message[512]{};
        wsprintfW(
            message,
            L"[Risen3FontHookTest] enableFullFontAtlasReplace=%d (%s)\n",
            g_enableFullFontAtlasReplace ? 1 : 0,
            path);
        Log(message);
        loaded = true;
    }

    const DWORD targetTextureLength = GetPrivateProfileStringW(
        L"Risen3FontHookTest",
        L"targetD3DTexture",
        L"",
        buffer,
        ARRAYSIZE(buffer),
        path);

    if (targetTextureLength > 0 && targetTextureLength < ARRAYSIZE(buffer)) {
        const uintptr_t value = ParseHexPointer(buffer);
        if (value) {
            g_forcedTargetD3DTexture = value;

            wchar_t message[512]{};
            wsprintfW(
                message,
                L"[Risen3FontHookTest] targetD3DTexture=%p (%s)\n",
                reinterpret_cast<void*>(g_forcedTargetD3DTexture),
                path);
            Log(message);
            loaded = true;
        }
    }

    const DWORD replaceProbeLength = GetPrivateProfileStringW(
        L"Risen3FontHookTest",
        L"replaceProbeTextures",
        L"",
        buffer,
        ARRAYSIZE(buffer),
        path);

    if (replaceProbeLength > 0 && replaceProbeLength < ARRAYSIZE(buffer)) {
        g_replaceProbeTextures = ParseBoolInt(buffer, false);

        wchar_t message[512]{};
        wsprintfW(
            message,
            L"[Risen3FontHookTest] replaceProbeTextures=%d (%s)\n",
            g_replaceProbeTextures ? 1 : 0,
            path);
        Log(message);
        loaded = true;
    }

    const DWORD replaceWindowLength = GetPrivateProfileStringW(
        L"Risen3FontHookTest",
        L"textureReplaceWindowMs",
        L"",
        buffer,
        ARRAYSIZE(buffer),
        path);

    if (replaceWindowLength > 0 && replaceWindowLength < ARRAYSIZE(buffer)) {
        const int value = ParseDecimalInt(buffer, static_cast<int>(g_textureReplaceWindowMs));
        if (value >= 0 && value <= 10000) {
            g_textureReplaceWindowMs = static_cast<DWORD>(value);
        }

        wchar_t message[512]{};
        wsprintfW(
            message,
            L"[Risen3FontHookTest] textureReplaceWindowMs=%u (%s)\n",
            g_textureReplaceWindowMs,
            path);
        Log(message);
        loaded = true;
    }

    return loaded;
}

bool TryLoadConfigFromDllDirectory()
{
    if (!g_module) {
        return false;
    }

    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(g_module, path, ARRAYSIZE(path));
    if (length == 0 || length >= ARRAYSIZE(path)) {
        return false;
    }

    wchar_t* slash = wcsrchr(path, L'\\');
    if (!slash) {
        return false;
    }

    *(slash + 1) = L'\0';
    lstrcatW(path, L"Risen3FontHook.ini");
    const bool loaded = TryLoadConfigFromIni(path);
    if (loaded) {
        lstrcpynW(g_configPath, path, ARRAYSIZE(g_configPath));
        WIN32_FILE_ATTRIBUTE_DATA attributes{};
        if (GetFileAttributesExW(path, GetFileExInfoStandard, &attributes)) {
            g_configLastWriteTime = attributes.ftLastWriteTime;
        }
    }

    return loaded;
}

bool TryLoadConfigFromCurrentDirectory()
{
    return TryLoadConfigFromIni(L".\\Risen3FontHook.ini");
}

bool TryLoadConfigFromEnvironment()
{
    wchar_t buffer[64]{};
    const DWORD length = GetEnvironmentVariableW(L"RISEN3_FONT_TEST_RESOURCE", buffer, ARRAYSIZE(buffer));
    if (length == 0 || length >= ARRAYSIZE(buffer)) {
        return false;
    }

    const uintptr_t value = ParseHexPointer(buffer);
    if (!value) {
        return false;
    }

    g_replacementResource = value;

    wchar_t message[256]{};
    wsprintfW(
        message,
        L"[Risen3FontHookTest] Replacement resource loaded from environment: %p\n",
        reinterpret_cast<void*>(g_replacementResource));
    Log(message);
    return true;
}

void LoadConfig()
{
    if (TryLoadConfigFromDllDirectory()) {
        return;
    }

    if (TryLoadConfigFromCurrentDirectory()) {
        return;
    }

    TryLoadConfigFromEnvironment();
}

void ReloadConfigIfChanged()
{
    if (!g_configPath[0]) {
        return;
    }

    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    if (!GetFileAttributesExW(g_configPath, GetFileExInfoStandard, &attributes)) {
        return;
    }

    if (CompareFileTime(&attributes.ftLastWriteTime, &g_configLastWriteTime) == 0) {
        return;
    }

    g_configLastWriteTime = attributes.ftLastWriteTime;
    if (TryLoadConfigFromIni(g_configPath)) {
        const LONG logIndex = InterlockedIncrement(&g_configReloadLogCount);
        wchar_t message[512]{};
        wsprintfW(
            message,
            L"[Risen3FontHookTest] Config reloaded[%d] %s\n",
            logIndex,
            g_configPath);
        Log(message);
    }
}

float ResolveAdvanceScaleForContext(uintptr_t resource, float scale)
{
    if (!(scale > 0.0f)) {
        return 1.0f;
    }

    const int scale1000 = static_cast<int>(scale * 1000.0f + 0.5f);
    if (scale1000 <= 0) {
        return 1.0f;
    }

    SpinLock lock(&g_scaleContextLock);
    for (size_t i = 0; i < kFontStateCount; ++i) {
        if (g_scaleContexts[i].resource == resource && g_scaleContexts[i].scale1000 == scale1000) {
            return g_scaleContexts[i].advanceScale;
        }
    }

    for (size_t i = 0; i < kFontStateCount; ++i) {
        if (!g_scaleContexts[i].resource) {
            g_scaleContexts[i].resource = resource;
            g_scaleContexts[i].scale1000 = scale1000;
            g_scaleContexts[i].advanceScale = scale;
            return g_scaleContexts[i].advanceScale;
        }
    }

    return scale;
}

