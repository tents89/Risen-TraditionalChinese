int __fastcall HookGetGlyphMetrics(void* font, wchar_t ch, void* outMetrics)
{
    if (!g_originalGetGlyphMetrics) {
        return 0xFF000001;
    }

    GetOrCreateFontState(font);

    ChineseGlyphEntry* glyph = FindChineseGlyph(ch);
    if (glyph) {
        const int originalResult = g_originalGetGlyphMetrics(font, ch, outMetrics);

        if (outMetrics && glyph->hasPlaneBounds) {
            auto* metrics = static_cast<GlyphMetrics*>(outMetrics);
            ApplyMsdfGlyphMetrics(glyph, metrics);
        } else if (outMetrics && glyph->naturalAdvance > 0.0f) {
            auto* metrics = static_cast<GlyphMetrics*>(outMetrics);
            metrics->advance = ResolveMsdfGlyphAdvance(glyph, 1.0f);
        }

        MarkMetricsPointer(outMetrics, glyph);
        return glyph->hasPlaneBounds ? 0 : originalResult;
    }

    return g_originalGetGlyphMetrics(font, ch, outMetrics);
}

int __fastcall HookDrawGlyph(
    void* font,
    void* renderContext,
    const void* metrics,
    const void* position,
    const void* bounds,
    int flags1,
    int flags2,
    int flags3,
    float scale)
{
    ChineseGlyphEntry* markedGlyph = ConsumeMarkedMetricsPointer(metrics);
    const bool isMarkedGlyph = markedGlyph != nullptr;
    ObserveFontResource(font);
    FontState* fontState = GetOrCreateFontState(font);

    if (metrics) {
        if (isMarkedGlyph && InterlockedExchange(&g_loggedDrawProbe, 1) == 0) {
            Log(L"[Risen3FontHookTest] Marked U+4E2D metrics reached DrawGlyph.\n");
        }

        if (isMarkedGlyph && g_enableDrawGlyphDiagnostics) {
            const LONG diagIndex = InterlockedIncrement(&g_drawGlyphDiagnosticLogCount);
            if (diagIndex <= kDrawGlyphDiagnosticLogLimit) {
                const uintptr_t resource = ReadFontResource(font);
                const uintptr_t atlasResource = ReadAtlasResourceFromFontResource(resource);
                const auto* glyphMetrics = static_cast<const GlyphMetrics*>(metrics);
                float pos[4]{};
                float bnd[4]{};
                SafeReadFloat4(reinterpret_cast<uintptr_t>(position), pos);
                SafeReadFloat4(reinterpret_cast<uintptr_t>(bounds), bnd);

                wchar_t message[768]{};
                wsprintfW(
                    message,
                    L"[Risen3FontHookTest] DrawGlyphDiag[%d] cp=U+%04X font=%p resource=%p atlas=%p scale=%d metrics=%d,%d,%d,%d adv=%d bearing=%d,%d pos=%d,%d,%d,%d bounds=%d,%d,%d,%d flags=%08X,%08X,%08X\n",
                    diagIndex,
                    markedGlyph ? static_cast<unsigned>(markedGlyph->codepoint) : 0,
                    font,
                    reinterpret_cast<void*>(resource),
                    reinterpret_cast<void*>(atlasResource),
                    static_cast<int>(scale * 1000.0f),
                    glyphMetrics ? static_cast<int>(glyphMetrics->x1 * 1000.0f) : 0,
                    glyphMetrics ? static_cast<int>(glyphMetrics->y1 * 1000.0f) : 0,
                    glyphMetrics ? static_cast<int>(glyphMetrics->x2 * 1000.0f) : 0,
                    glyphMetrics ? static_cast<int>(glyphMetrics->y2 * 1000.0f) : 0,
                    glyphMetrics ? static_cast<int>(glyphMetrics->advance * 1000.0f) : 0,
                    glyphMetrics ? static_cast<int>(glyphMetrics->bearingX * 1000.0f) : 0,
                    glyphMetrics ? static_cast<int>(glyphMetrics->bearingY * 1000.0f) : 0,
                    static_cast<int>(pos[0] * 1000.0f),
                    static_cast<int>(pos[1] * 1000.0f),
                    static_cast<int>(pos[2] * 1000.0f),
                    static_cast<int>(pos[3] * 1000.0f),
                    static_cast<int>(bnd[0] * 1000.0f),
                    static_cast<int>(bnd[1] * 1000.0f),
                    static_cast<int>(bnd[2] * 1000.0f),
                    static_cast<int>(bnd[3] * 1000.0f),
                    static_cast<unsigned>(flags1),
                    static_cast<unsigned>(flags2),
                    static_cast<unsigned>(flags3));
                Log(message);
            }
        }

        if (isMarkedGlyph) {
            const uintptr_t resource = ReadFontResource(font);
            const uintptr_t atlasResource = ReadAtlasResourceFromFontResource(resource);
            const uintptr_t shadowResource = g_enableFullFontAtlasReplace ? 0 : GetOrCreateShadowFontResource(fontState);
            const uintptr_t shadowAtlasResource =
                shadowResource ? ReadAtlasResourceFromFontResource(shadowResource) : 0;
            const uintptr_t bindAtlasResource =
                g_enableFullFontAtlasReplace ? atlasResource : (shadowAtlasResource ? shadowAtlasResource : atlasResource);
            g_markedAtlasResource = bindAtlasResource;
            InterlockedExchange(&g_pendingAtlasSwapCount, bindAtlasResource ? 1 : 0);
            uintptr_t d3dTexture = ReadD3DTextureFromImageResource(atlasResource);
            if (!d3dTexture) {
                d3dTexture = ResolveD3DTextureFromAtlas(atlasResource);
            }
            if (!d3dTexture) {
                d3dTexture = ReadD3DTextureFromImageResource(resource);
            }
            DumpAtlasTextureCandidates(atlasResource);
            if (resource && d3dTexture) {
                g_markedResource = resource;
                g_markedD3DTexture = d3dTexture;
            }
            g_lastMarkedGlyphTick = GetTickCount();
            const uintptr_t resolvedReplacement = ResolveReplacementResource();
            if (InterlockedExchange(&g_loggedResourceProbe, 1) == 0) {
                wchar_t message[512]{};
                wsprintfW(
                    message,
                    L"[Risen3FontHookTest] Marked glyph font=%p resource(font+0x60)=%p atlas=%p shadowResource=%p shadowAtlas=%p d3dTexture=%p replacement=%p index=%d\n",
                    font,
                    reinterpret_cast<void*>(resource),
                    reinterpret_cast<void*>(atlasResource),
                    reinterpret_cast<void*>(shadowResource),
                    reinterpret_cast<void*>(shadowAtlasResource),
                    reinterpret_cast<void*>(d3dTexture),
                    reinterpret_cast<void*>(resolvedReplacement),
                    g_replacementIndex);
                Log(message);
            }
        }
    }

    if (!g_originalDrawGlyph) {
        return 0xFF000001;
    }

    GlyphMetrics* mutableMetrics = nullptr;
    float originalAdvance = 0.0f;
    bool adjustedAdvance = false;
    float adjustedBounds[4]{};
    const void* boundsForDraw = bounds;

    if (isMarkedGlyph && metrics) {
        mutableMetrics = const_cast<GlyphMetrics*>(static_cast<const GlyphMetrics*>(metrics));
        if (mutableMetrics) {
            originalAdvance = mutableMetrics->advance;
            const uintptr_t resource = ReadFontResource(font);
            const float advanceScale = ResolveAdvanceScaleForContext(resource, scale);
            const float scaledExtra = markedGlyph->advanceExtra * advanceScale;
            if (markedGlyph->naturalAdvance > 0.0f) {
                mutableMetrics->advance = ResolveMsdfGlyphAdvance(markedGlyph, advanceScale);
            } else {
                mutableMetrics->advance += scaledExtra;
            }
            adjustedAdvance = true;
        }
    }

    if (isMarkedGlyph && bounds && mutableMetrics && IsLatinOrHalfWidthCodepoint(markedGlyph->codepoint)) {
        const float rightOverhang = mutableMetrics->x2 - mutableMetrics->advance;
        if (rightOverhang > 0.01f &&
            SafeReadFloat4(reinterpret_cast<uintptr_t>(bounds), adjustedBounds)) {
            adjustedBounds[2] += (scale > 0.0f) ? (rightOverhang * scale) : rightOverhang;
            boundsForDraw = adjustedBounds;
        }
    }

    if (isMarkedGlyph) {
        g_activeMarkedReplacementResource = ResolveReplacementResourceForMarkedGlyph(fontState);
        g_activeMarkedGlyph = markedGlyph;
        InterlockedIncrement(&g_markedDrawGlyphDepth);
    }

    const int result = g_originalDrawGlyph(
        font,
        renderContext,
        metrics,
        position,
        boundsForDraw,
        flags1,
        flags2,
        flags3,
        scale);

    if (adjustedAdvance && mutableMetrics) {
        mutableMetrics->advance = originalAdvance;
    }

    if (isMarkedGlyph) {
        InterlockedDecrement(&g_markedDrawGlyphDepth);
        if (!g_enableShadowResource) {
            InterlockedCompareExchange(&g_pendingAtlasSwapCount, 0, 1);
        }
        g_activeMarkedReplacementResource = 0;
        g_activeMarkedGlyph = nullptr;
    }

    return result;
}

int __fastcall HookBuildGlyphCommand(
    void* renderContext,
    void* resource,
    void* unused,
    const void* rect,
    const void* colorOrUv,
    int arg5,
    int arg6,
    int arg7)
{
    DWORD beforeCount = 0;
    SafeReadDword(reinterpret_cast<uintptr_t>(renderContext) + 0xB8, &beforeCount);

    if (g_markedDrawGlyphDepth > 0) {

        const uintptr_t replacement = g_activeMarkedReplacementResource;
        if (replacement) {
            if (reinterpret_cast<uintptr_t>(resource) != replacement &&
                InterlockedExchange(&g_loggedReplacementApplied, 1) == 0) {
                Log(L"[Risen3FontHookTest] Replacing BuildGlyphCommand resource for marked glyph.\n");
            }
            resource = reinterpret_cast<void*>(replacement);
        }
        
        const LONG logIndex = InterlockedIncrement(&g_markedBuildCommandLogCount);
        if (logIndex <= kMarkedBuildCommandLogLimit) {
            uintptr_t resourceId = 0;
            SafeReadQword(reinterpret_cast<uintptr_t>(resource) + 0x08, &resourceId);

            wchar_t message[512]{};
            wsprintfW(
                message,
                L"[Risen3FontHookTest] Marked BuildGlyphCommand[%d] rcx=%p rdx(resource)=%p resource+08=%p r8=%p r9(rect)=%p stackArgs=%08X,%08X,%08X\n",
                logIndex,
                renderContext,
                resource,
                reinterpret_cast<void*>(resourceId),
                unused,
                rect,
                arg5,
                arg6,
                arg7);
            Log(message);
        }
    }

    if (!g_originalBuildGlyphCommand) {
        return 0xFF000001;
    }

    const int result = g_originalBuildGlyphCommand(
        renderContext,
        resource,
        unused,
        rect,
        colorOrUv,
        arg5,
        arg6,
        arg7);

    if (g_markedDrawGlyphDepth > 0) {
        uintptr_t commandBase = 0;
        DWORD afterCount = 0;
        SafeReadQword(reinterpret_cast<uintptr_t>(renderContext) + 0xB0, &commandBase);
        SafeReadDword(reinterpret_cast<uintptr_t>(renderContext) + 0xB8, &afterCount);

        if (commandBase && afterCount > beforeCount) {
            const DWORD commandIndex = afterCount - 1;
            const uintptr_t command = commandBase + static_cast<uintptr_t>(commandIndex) * 0x80;
            uintptr_t resourceOrTexture = 0;
            DWORD commandWidth = 0;
            DWORD commandHeight = 0;
            DWORD commandState0 = 0;
            DWORD commandState1 = 0;
            SafeReadQword(command + 0x68, &resourceOrTexture);
            SafeReadDword(command + 0x70, &commandWidth);
            SafeReadDword(command + 0x74, &commandHeight);
            SafeReadDword(command + 0x60, &commandState0);
            SafeReadDword(command + 0x64, &commandState1);

            uintptr_t valueVtable = 0;
            uintptr_t valueTag = 0;
            DWORD valueId = 0;
            DWORD valueWidth = 0;
            DWORD valueHeight = 0;
            wchar_t format[5]{};
            SafeReadQword(resourceOrTexture + 0x00, &valueVtable);
            SafeReadQword(resourceOrTexture + 0x08, &valueTag);
            SafeReadDword(resourceOrTexture + 0x08, &valueId);
            SafeReadDword(resourceOrTexture + 0x10, &valueWidth);
            SafeReadDword(resourceOrTexture + 0x14, &valueHeight);
            ReadAscii4(resourceOrTexture + 0x18, format);
            const wchar_t* kind = ClassifyResourceOrTexture(resourceOrTexture);

            const LONG logIndex = InterlockedIncrement(&g_postBuildCommandLogCount);
            if (logIndex <= kPostBuildCommandLogLimit) {
                wchar_t message[768]{};
                wsprintfW(
                    message,
                    L"[Risen3FontHookTest] PostBuildCommand[%d] before=%u after=%u command[%u]=%p command+68=%p kind=%s vtable=%p tag=%p id=%08X size=%ux%u fmt=%s cmdSize=%ux%u state=%08X,%08X\n",
                    logIndex,
                    beforeCount,
                    afterCount,
                    commandIndex,
                    reinterpret_cast<void*>(command),
                    reinterpret_cast<void*>(resourceOrTexture),
                    kind,
                    reinterpret_cast<void*>(valueVtable),
                    reinterpret_cast<void*>(valueTag),
                    valueId,
                    valueWidth,
                    valueHeight,
                    format,
                    commandWidth,
                    commandHeight,
                    commandState0,
                    commandState1);
                Log(message);
            }
        }
    }

    return result;
}

void LogSubmitGlyphCommandState(const wchar_t* phase, void* renderContext, void* clipRect)
{
    if (g_markedDrawGlyphDepth <= 0) {
        return;
    }

    const LONG logIndex = InterlockedIncrement(&g_submitGlyphCommandLogCount);
    if (logIndex > kSubmitGlyphCommandLogLimit) {
        return;
    }

    const uintptr_t ctx = reinterpret_cast<uintptr_t>(renderContext);
    uintptr_t commandBase = 0;
    DWORD commandCount = 0;
    DWORD commandCapacity = 0;
    SafeReadQword(ctx + 0xB0, &commandBase);
    SafeReadDword(ctx + 0xB8, &commandCount);
    SafeReadDword(ctx + 0xBC, &commandCapacity);

    uintptr_t command = 0;
    uintptr_t resourceOrTexture = 0;
    DWORD commandWidth = 0;
    DWORD commandHeight = 0;
    DWORD commandState0 = 0;
    DWORD commandState1 = 0;
    if (commandBase && commandCount > 0) {
        command = commandBase + static_cast<uintptr_t>(commandCount - 1) * 0x80;
        SafeReadQword(command + 0x68, &resourceOrTexture);
        SafeReadDword(command + 0x70, &commandWidth);
        SafeReadDword(command + 0x74, &commandHeight);
        SafeReadDword(command + 0x60, &commandState0);
        SafeReadDword(command + 0x64, &commandState1);
    }

    uintptr_t valueVtable = 0;
    uintptr_t valueTag = 0;
    DWORD valueId = 0;
    DWORD valueWidth = 0;
    DWORD valueHeight = 0;
    wchar_t format[5]{};
    SafeReadQword(resourceOrTexture + 0x00, &valueVtable);
    SafeReadQword(resourceOrTexture + 0x08, &valueTag);
    SafeReadDword(resourceOrTexture + 0x08, &valueId);
    SafeReadDword(resourceOrTexture + 0x10, &valueWidth);
    SafeReadDword(resourceOrTexture + 0x14, &valueHeight);
    ReadAscii4(resourceOrTexture + 0x18, format);

    wchar_t message[768]{};
    wsprintfW(
        message,
        L"[Risen3FontHookTest] SubmitGlyphCommand[%d:%s] ctx=%p clip=%p base=%p count=%u cap=%u last=%p last+68=%p kind=%s vtable=%p tag=%p id=%08X size=%ux%u fmt=%s cmdSize=%ux%u state=%08X,%08X\n",
        logIndex,
        phase,
        renderContext,
        clipRect,
        reinterpret_cast<void*>(commandBase),
        commandCount,
        commandCapacity,
        reinterpret_cast<void*>(command),
        reinterpret_cast<void*>(resourceOrTexture),
        ClassifyResourceOrTexture(resourceOrTexture),
        reinterpret_cast<void*>(valueVtable),
        reinterpret_cast<void*>(valueTag),
        valueId,
        valueWidth,
        valueHeight,
        format,
        commandWidth,
        commandHeight,
        commandState0,
        commandState1);
    Log(message);
}

int __fastcall HookSubmitGlyphCommand(void* renderContext, void* clipRect)
{
    LogSubmitGlyphCommandState(L"before", renderContext, clipRect);

    if (!g_originalSubmitGlyphCommand) {
        return 0xFF000001;
    }

    const int result = g_originalSubmitGlyphCommand(renderContext, clipRect);
    LogSubmitGlyphCommandState(L"after", renderContext, clipRect);
    return result;
}

int __fastcall HookBuildUvFromFontResource(
    void* canvas,
    void* fontResource,
    const void* rect,
    void* outUv,
    void* outWidth,
    void* outHeight)
{
    const bool isMarked = g_markedDrawGlyphDepth > 0;

    float rect0 = 0.0f;
    float rect1 = 0.0f;
    float rect2 = 0.0f;
    float rect3 = 0.0f;
    if (isMarked && rect) {
        const uintptr_t rectAddress = reinterpret_cast<uintptr_t>(rect);
        SafeReadFloat(rectAddress + 0x00, &rect0);
        SafeReadFloat(rectAddress + 0x04, &rect1);
        SafeReadFloat(rectAddress + 0x08, &rect2);
        SafeReadFloat(rectAddress + 0x0C, &rect3);
    }

    float usedRect[4] = {rect0, rect1, rect2, rect3};
    const void* rectForOriginal = rect;
    if (isMarked && g_enableBuildUvRectOverride) {
        const float* sourceRect = g_activeMarkedGlyph ? g_activeMarkedGlyph->rect : g_buildUvRectOverride;
        usedRect[0] = sourceRect[0];
        usedRect[1] = sourceRect[1];
        usedRect[2] = sourceRect[2];
        usedRect[3] = sourceRect[3];
        rectForOriginal = usedRect;
    }

    uintptr_t originalAtlas = 0;
    uintptr_t replacementAtlas = 0;
    if (isMarked) {
        originalAtlas = ReadAtlasResourceFromFontResource(reinterpret_cast<uintptr_t>(fontResource));
        if (g_replacementAtlasResource) {
            replacementAtlas = g_replacementAtlasResource;
        } else if (g_replacementAtlasIndex == -2) {
            replacementAtlas = ResolveAutoReplacementAtlasResource(originalAtlas);
        } else {
            replacementAtlas = ResolveReplacementAtlasResource();
        }
    }

    float localUv[4]{};
    float localWidth = 0.0f;
    float localHeight = 0.0f;
    void* safeOutUv = outUv ? outUv : localUv;
    void* safeOutWidth = outWidth ? outWidth : &localWidth;
    void* safeOutHeight = outHeight ? outHeight : &localHeight;

    int result = 0xFF000001;
    bool usedExternalUv = false;
    uint32_t externalAtlasWidth = 0;
    uint32_t externalAtlasHeight = 0;

    if (isMarked && g_enableExternalAtlas && g_enableBuildUvRectOverride) {
        {
            SpinLock atlasLock(&g_externalAtlasLock);
            externalAtlasWidth = g_externalAtlas.width;
            externalAtlasHeight = g_externalAtlas.height;
        }

        if (externalAtlasWidth && externalAtlasHeight) {
            const float invWidth = 1.0f / static_cast<float>(externalAtlasWidth);
            const float invHeight = 1.0f / static_cast<float>(externalAtlasHeight);
            const float uv0 = usedRect[0] * invWidth;
            const float uv1 = usedRect[1] * invHeight;
            const float uv2 = usedRect[2] * invWidth;
            const float uv3 = usedRect[3] * invHeight;
            const float glyphWidth = usedRect[2] - usedRect[0];
            const float glyphHeight = usedRect[3] - usedRect[1];

            SafeWriteFloat4(reinterpret_cast<uintptr_t>(safeOutUv), uv0, uv1, uv2, uv3);
            SafeWriteFloat(reinterpret_cast<uintptr_t>(safeOutWidth), glyphWidth);
            SafeWriteFloat(reinterpret_cast<uintptr_t>(safeOutHeight), glyphHeight);
            result = 0;
            usedExternalUv = true;
        }
    }

    if (!usedExternalUv) {
        result = g_originalBuildUvFromFontResource
            ? g_originalBuildUvFromFontResource(
                canvas,
                fontResource,
                rectForOriginal,
                safeOutUv,
                safeOutWidth,
                safeOutHeight)
            : 0xFF000001;
    }

    if (isMarked) {
        float uv0 = 0.0f;
        float uv1 = 0.0f;
        float uv2 = 0.0f;
        float uv3 = 0.0f;
        float glyphWidth = 0.0f;
        float glyphHeight = 0.0f;
        if (safeOutUv) {
            const uintptr_t uvAddress = reinterpret_cast<uintptr_t>(safeOutUv);
            SafeReadFloat(uvAddress + 0x00, &uv0);
            SafeReadFloat(uvAddress + 0x04, &uv1);
            SafeReadFloat(uvAddress + 0x08, &uv2);
            SafeReadFloat(uvAddress + 0x0C, &uv3);
        }
        SafeReadFloat(reinterpret_cast<uintptr_t>(safeOutWidth), &glyphWidth);
        SafeReadFloat(reinterpret_cast<uintptr_t>(safeOutHeight), &glyphHeight);

        const LONG logIndex = InterlockedIncrement(&g_buildUvLogCount);
        if (logIndex <= kBuildUvLogLimit) {
            wchar_t message[1024]{};
            wsprintfW(
                message,
                L"[Risen3FontHookTest] BuildUV[%d] canvas=%p resource=%p originalAtlas=%p replacementAtlas=%p rect=%d,%d,%d,%d usedRect=%d,%d,%d,%d override=%d external=%d atlasSize=%ux%u uv=%d,%d,%d,%d size=%d,%d out=%p,%p safe=%p,%p,%p result=%08X\n",
                logIndex,
                canvas,
                fontResource,
                reinterpret_cast<void*>(originalAtlas),
                reinterpret_cast<void*>(replacementAtlas),
                static_cast<int>(rect0 * 1000.0f),
                static_cast<int>(rect1 * 1000.0f),
                static_cast<int>(rect2 * 1000.0f),
                static_cast<int>(rect3 * 1000.0f),
                static_cast<int>(usedRect[0] * 1000.0f),
                static_cast<int>(usedRect[1] * 1000.0f),
                static_cast<int>(usedRect[2] * 1000.0f),
                static_cast<int>(usedRect[3] * 1000.0f),
                g_enableBuildUvRectOverride ? 1 : 0,
                usedExternalUv ? 1 : 0,
                externalAtlasWidth,
                externalAtlasHeight,
                static_cast<int>(uv0 * 1000000.0f),
                static_cast<int>(uv1 * 1000000.0f),
                static_cast<int>(uv2 * 1000000.0f),
                static_cast<int>(uv3 * 1000000.0f),
                static_cast<int>(glyphWidth * 1000.0f),
                static_cast<int>(glyphHeight * 1000.0f),
                outWidth,
                outHeight,
                safeOutUv,
                safeOutWidth,
                safeOutHeight,
                static_cast<unsigned>(result));
            Log(message);
        }
    }

    return result;
}

uintptr_t __fastcall HookAtlasGetter(void* fontObject)
{
    const uintptr_t originalAtlas = g_originalAtlasGetter ? g_originalAtlasGetter(fontObject) : 0;
    uintptr_t replacementAtlas = 0;
    const wchar_t* mode = L"off";

    if (g_markedDrawGlyphDepth > 0) {
        if (g_replacementAtlasResource) {
            replacementAtlas = g_replacementAtlasResource;
            mode = L"manual";
        } else if (g_replacementAtlasIndex == -2) {
            replacementAtlas = ResolveAutoReplacementAtlasResource(originalAtlas);
            mode = L"auto";
        } else {
            replacementAtlas = ResolveReplacementAtlasResource();
            mode = L"index";
        }
    }

    const LONG logIndex = InterlockedIncrement(&g_atlasGetterLogCount);
    if (logIndex <= kTextureReplaceLogLimit || g_markedDrawGlyphDepth > 0) {
        wchar_t message[512]{};
        wsprintfW(
            message,
            L"[Risen3FontHookTest] AtlasGetter[%d] fontObject=%p original=%p replacement=%p markedDepth=%d index=%d mode=%s\n",
            logIndex,
            fontObject,
            reinterpret_cast<void*>(originalAtlas),
            reinterpret_cast<void*>(replacementAtlas),
            g_markedDrawGlyphDepth,
            g_replacementAtlasIndex,
            mode);
        Log(message);
    }

    if (replacementAtlas && replacementAtlas != originalAtlas) {
        return replacementAtlas;
    }

    return originalAtlas;
}

void LogFlushTextureCommands(void* renderContext)
{
    const LONG logIndex = InterlockedIncrement(&g_flushTextureLogCount);
    if (logIndex > kFlushTextureLogLimit) {
        return;
    }

    const uintptr_t ctx = reinterpret_cast<uintptr_t>(renderContext);
    uintptr_t commandBase = 0;
    DWORD commandCount = 0;
    SafeReadQword(ctx + 0xB0, &commandBase);
    SafeReadDword(ctx + 0xB8, &commandCount);

    wchar_t header[256]{};
    wsprintfW(
        header,
        L"[Risen3FontHookTest] FlushGlyphCommands[%d] ctx=%p commandBase=%p count=%u\n",
        logIndex,
        renderContext,
        reinterpret_cast<void*>(commandBase),
        commandCount);
    Log(header);

    if (!commandBase || commandCount == 0) {
        return;
    }

    const DWORD limit = commandCount < 8 ? commandCount : 8;
    for (DWORD i = 0; i < limit; ++i) {
        const uintptr_t command = commandBase + static_cast<uintptr_t>(i) * 0x80;
        uintptr_t texture = 0;
        DWORD commandWidth = 0;
        DWORD commandHeight = 0;
        DWORD commandState0 = 0;
        DWORD commandState1 = 0;
        SafeReadQword(command + 0x68, &texture);
        SafeReadDword(command + 0x70, &commandWidth);
        SafeReadDword(command + 0x74, &commandHeight);
        SafeReadDword(command + 0x60, &commandState0);
        SafeReadDword(command + 0x64, &commandState1);

        uintptr_t textureVtable = 0;
        DWORD textureId = 0;
        DWORD textureWidth = 0;
        DWORD textureHeight = 0;
        wchar_t format[5]{};
        SafeReadQword(texture + 0x00, &textureVtable);
        SafeReadDword(texture + 0x08, &textureId);
        SafeReadDword(texture + 0x10, &textureWidth);
        SafeReadDword(texture + 0x14, &textureHeight);
        ReadAscii4(texture + 0x18, format);

        wchar_t line[512]{};
        wsprintfW(
            line,
            L"[Risen3FontHookTest] FlushCmd[%u] command=%p tex=%p texId=%08X texSize=%ux%u fmt=%s cmdSize=%ux%u state=%08X,%08X\n",
            i,
            reinterpret_cast<void*>(command),
            reinterpret_cast<void*>(texture),
            textureId,
            textureWidth,
            textureHeight,
            format,
            commandWidth,
            commandHeight,
            commandState0,
            commandState1);
        Log(line);
    }
}

int __fastcall HookFlushGlyphCommands(void* renderContext)
{
    LogFlushTextureCommands(renderContext);

    if (!g_originalFlushGlyphCommands) {
        return 0xFF000001;
    }

    return g_originalFlushGlyphCommands(renderContext);
}

