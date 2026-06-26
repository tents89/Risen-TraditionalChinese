bool InstallBuildGlyphCommandHook(uintptr_t base)
{
    auto* target = reinterpret_cast<BYTE*>(base + kBuildGlyphCommandRva);

    const BYTE expectedPrefix[] = {
        0x4C, 0x8B, 0xDC,       // mov r11,rsp
        0x55,                   // push rbp
        0x56,                   // push rsi
        0x57,                   // push rdi
        0x41, 0x55,             // push r13
        0x41, 0x56              // push r14
    };

    if (memcmp(target, expectedPrefix, sizeof(expectedPrefix)) != 0) {
        Log(L"[Risen3FontHookTest] BuildGlyphCommand prefix mismatch; inline hook skipped.\n");
        return false;
    }

    constexpr size_t patchSize = 15;
    if (!InstallInlineHook(
            target,
            reinterpret_cast<void*>(&HookBuildGlyphCommand),
            patchSize,
            g_buildGlyphCommandOriginalBytes,
            &g_buildGlyphCommandTrampoline)) {
        Log(L"[Risen3FontHookTest] BuildGlyphCommand inline hook install failed.\n");
        return false;
    }

    g_originalBuildGlyphCommand = reinterpret_cast<BuildGlyphCommandFn>(g_buildGlyphCommandTrampoline);
    g_buildGlyphCommandHookInstalled = true;
    Log(L"[Risen3FontHookTest] BuildGlyphCommand hook installed at Risen3.exe+0x282B00.\n");
    return true;
}

bool InstallCommandResourceWriteHook(uintptr_t base)
{
    auto* target = reinterpret_cast<BYTE*>(base + kCommandResourceWritePatchRva);

    const BYTE expectedPrefix[] = {
        0x8B, 0x45, 0x6F,                   // mov eax,dword ptr ss:[rbp+6F]
        0x4C, 0x89, 0x6F, 0x68,             // mov qword ptr ds:[rdi+68],r13
        0x89, 0x47, 0x60,                   // mov dword ptr ds:[rdi+60],eax
        0x8B, 0x45, 0x77                    // mov eax,dword ptr ss:[rbp+77]
    };

    constexpr size_t patchSize = sizeof(expectedPrefix);
    if (memcmp(target, expectedPrefix, patchSize) != 0) {
        Log(L"[Risen3FontHookTest] CommandResourceWrite prefix mismatch; inline hook skipped.\n");
        return false;
    }

    constexpr size_t stubSize = 256;
    BYTE* stub = static_cast<BYTE*>(VirtualAlloc(nullptr, stubSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!stub) {
        Log(L"[Risen3FontHookTest] CommandResourceWrite stub allocation failed.\n");
        return false;
    }

    BYTE* out = stub;

    const BYTE originalAndSave[] = {
        0x8B, 0x45, 0x6F,                   // mov eax,dword ptr ss:[rbp+6F]
        0x4C, 0x89, 0x6F, 0x68,             // mov qword ptr ds:[rdi+68],r13
        0x89, 0x47, 0x60,                   // mov dword ptr ds:[rdi+60],eax
        0x8B, 0x45, 0x77,                   // mov eax,dword ptr ss:[rbp+77]
        0x50,                               // push rax
        0x51,                               // push rcx
        0x52,                               // push rdx
        0x41, 0x50,                         // push r8
        0x41, 0x51,                         // push r9
        0x41, 0x52,                         // push r10
        0x41, 0x53,                         // push r11
        0x48, 0x83, 0xEC, 0x28,             // sub rsp,28h
        0x48, 0x8B, 0xCF,                   // mov rcx,rdi
        0x49, 0x8B, 0xD5,                   // mov rdx,r13
        0x44, 0x8B, 0x47, 0x60              // mov r8d,dword ptr ds:[rdi+60]
    };
    out = EmitBytes(out, originalAndSave, sizeof(originalAndSave));
    out = EmitMovRaxImm64(out, reinterpret_cast<uintptr_t>(&LogCommandResourceWrite));

    const BYTE callAndRestore[] = {
        0xFF, 0xD0,                         // call rax
        0x48, 0x83, 0xC4, 0x28,             // add rsp,28h
        0x41, 0x5B,                         // pop r11
        0x41, 0x5A,                         // pop r10
        0x41, 0x59,                         // pop r9
        0x41, 0x58,                         // pop r8
        0x5A,                               // pop rdx
        0x59,                               // pop rcx
        0x58                                // pop rax
    };
    out = EmitBytes(out, callAndRestore, sizeof(callAndRestore));
    EmitAbsoluteJump(out, reinterpret_cast<uintptr_t>(target + patchSize));

    FlushInstructionCache(GetCurrentProcess(), stub, stubSize);
    memcpy(g_commandResourceWriteOriginalBytes, target, patchSize);
    if (!WriteAbsoluteJump(target, stub, patchSize)) {
        VirtualFree(stub, 0, MEM_RELEASE);
        Log(L"[Risen3FontHookTest] CommandResourceWrite inline hook install failed.\n");
        return false;
    }

    g_commandResourceWriteStub = stub;
    g_commandResourceWriteHookInstalled = true;
    Log(L"[Risen3FontHookTest] CommandResourceWrite hook installed at Risen3.exe+0x282F19.\n");
    return true;
}

bool InstallSubmitGlyphCommandHook(uintptr_t base)
{
    auto* target = reinterpret_cast<BYTE*>(base + kSubmitGlyphCommandRva);

    const BYTE expectedPrefix[] = {
        0x40, 0x55,                         // push rbp
        0x53,                               // push rbx
        0x41, 0x55,                         // push r13
        0x41, 0x56,                         // push r14
        0x41, 0x57,                         // push r15
        0x48, 0x8B, 0xEC                    // mov rbp,rsp
    };

    if (memcmp(target, expectedPrefix, sizeof(expectedPrefix)) != 0) {
        Log(L"[Risen3FontHookTest] SubmitGlyphCommand prefix mismatch; inline hook skipped.\n");
        return false;
    }

    constexpr size_t patchSize = sizeof(expectedPrefix);
    if (!InstallInlineHook(
            target,
            reinterpret_cast<void*>(&HookSubmitGlyphCommand),
            patchSize,
            g_submitGlyphCommandOriginalBytes,
            &g_submitGlyphCommandTrampoline)) {
        Log(L"[Risen3FontHookTest] SubmitGlyphCommand inline hook install failed.\n");
        return false;
    }

    g_originalSubmitGlyphCommand = reinterpret_cast<SubmitGlyphCommandFn>(g_submitGlyphCommandTrampoline);
    g_submitGlyphCommandHookInstalled = true;
    Log(L"[Risen3FontHookTest] SubmitGlyphCommand hook installed at Risen3.exe+0x282360.\n");
    return true;
}

bool InstallBuildUvFromFontResourceHook(uintptr_t base)
{
    auto* target = reinterpret_cast<BYTE*>(base + kBuildUvFromFontResourceRva);

    const BYTE expectedPrefix[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08,       // mov qword ptr ss:[rsp+8],rbx
        0x48, 0x89, 0x74, 0x24, 0x10,       // mov qword ptr ss:[rsp+10],rsi
        0x48, 0x89, 0x7C, 0x24, 0x18,       // mov qword ptr ss:[rsp+18],rdi
        0x55                                // push rbp
    };

    if (memcmp(target, expectedPrefix, sizeof(expectedPrefix)) != 0) {
        Log(L"[Risen3FontHookTest] BuildUV prefix mismatch; inline hook skipped.\n");
        return false;
    }

    constexpr size_t patchSize = sizeof(expectedPrefix);
    if (!InstallInlineHook(
            target,
            reinterpret_cast<void*>(&HookBuildUvFromFontResource),
            patchSize,
            g_buildUvFromFontResourceOriginalBytes,
            &g_buildUvFromFontResourceTrampoline)) {
        Log(L"[Risen3FontHookTest] BuildUV inline hook install failed.\n");
        return false;
    }

    g_originalBuildUvFromFontResource =
        reinterpret_cast<BuildUvFromFontResourceFn>(g_buildUvFromFontResourceTrampoline);
    g_buildUvFromFontResourceHookInstalled = true;
    Log(L"[Risen3FontHookTest] BuildUV hook installed at Risen3.exe+0x281810.\n");
    return true;
}

bool InstallAtlasGetterHook(uintptr_t base)
{
    auto* target = reinterpret_cast<BYTE*>(base + kAtlasGetterRva);

    const BYTE expectedPrefix[] = {
        0x48, 0x8B, 0x41, 0x38,             // mov rax,qword ptr ds:[rcx+38]
        0xC3,                               // ret
        0xCC, 0xCC, 0xCC, 0xCC,             // int3 padding
        0xCC, 0xCC, 0xCC
    };

    if (memcmp(target, expectedPrefix, sizeof(expectedPrefix)) != 0) {
        Log(L"[Risen3FontHookTest] AtlasGetter prefix mismatch; inline hook skipped.\n");
        return false;
    }

    constexpr size_t patchSize = sizeof(expectedPrefix);
    if (!InstallInlineHook(
            target,
            reinterpret_cast<void*>(&HookAtlasGetter),
            patchSize,
            g_atlasGetterOriginalBytes,
            &g_atlasGetterTrampoline)) {
        Log(L"[Risen3FontHookTest] AtlasGetter inline hook install failed.\n");
        return false;
    }

    g_originalAtlasGetter = reinterpret_cast<AtlasGetterFn>(g_atlasGetterTrampoline);
    g_atlasGetterHookInstalled = true;
    Log(L"[Risen3FontHookTest] AtlasGetter hook installed at Risen3.exe+0x78B230.\n");
    return true;
}

bool InstallBindTextureHook(uintptr_t base)
{
    auto* target = reinterpret_cast<BYTE*>(base + kBindTextureFunctionRva);

    const BYTE expectedPrefix[] = {
        0x48, 0x89, 0x5C, 0x24, 0x10,       // mov qword ptr ss:[rsp+10],rbx
        0x57,                               // push rdi
        0x48, 0x83, 0xEC, 0x20,             // sub rsp,20
        0x4D, 0x8B, 0xC8                    // mov r9,r8
    };

    if (memcmp(target, expectedPrefix, sizeof(expectedPrefix)) != 0) {
        wchar_t message[512]{};
        wsprintfW(
            message,
            L"[Risen3FontHookTest] BindTexture prefix mismatch; inline hook skipped. bytes=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
            target[0],
            target[1],
            target[2],
            target[3],
            target[4],
            target[5],
            target[6],
            target[7],
            target[8],
            target[9],
            target[10],
            target[11]);
        Log(message);
        return false;
    }

    constexpr size_t patchSize = sizeof(expectedPrefix);
    if (!InstallInlineHook(
            target,
            reinterpret_cast<void*>(&HookBindTexture),
            patchSize,
            g_bindTextureOriginalBytes,
            &g_bindTextureTrampoline)) {
        Log(L"[Risen3FontHookTest] BindTexture inline hook install failed.\n");
        return false;
    }

    g_originalBindTexture = reinterpret_cast<BindTextureFn>(g_bindTextureTrampoline);
    g_bindTextureHookInstalled = true;
    Log(L"[Risen3FontHookTest] BindTexture hook installed at Risen3.exe+0x78C840.\n");
    return true;
}

bool InstallFlushGlyphCommandsHook(uintptr_t base)
{
    auto* target = reinterpret_cast<BYTE*>(base + kFlushGlyphCommandsRva);

    const BYTE expectedPrefix[] = {
        0x40, 0x55,                         // push rbp
        0x56,                               // push rsi
        0x48, 0x8B, 0xEC,                   // mov rbp,rsp
        0x48, 0x83, 0xEC, 0x78,             // sub rsp,78
        0x48, 0x83, 0xB9, 0xF8, 0x00, 0x00, 0x00, 0x00
    };

    if (memcmp(target, expectedPrefix, sizeof(expectedPrefix)) != 0) {
        Log(L"[Risen3FontHookTest] FlushGlyphCommands prefix mismatch; inline hook skipped.\n");
        return false;
    }

    constexpr size_t patchSize = 18;
    if (!InstallInlineHook(
            target,
            reinterpret_cast<void*>(&HookFlushGlyphCommands),
            patchSize,
            g_flushGlyphCommandsOriginalBytes,
            &g_flushGlyphCommandsTrampoline)) {
        Log(L"[Risen3FontHookTest] FlushGlyphCommands inline hook install failed.\n");
        return false;
    }

    g_originalFlushGlyphCommands = reinterpret_cast<FlushGlyphCommandsFn>(g_flushGlyphCommandsTrampoline);
    g_flushGlyphCommandsHookInstalled = true;
    Log(L"[Risen3FontHookTest] FlushGlyphCommands hook installed at Risen3.exe+0x2827A0.\n");
    return true;
}

bool InstallHook()
{
    HMODULE exe = GetModuleHandleW(nullptr);
    if (!exe) {
        Log(L"[Risen3FontHookTest] GetModuleHandleW(nullptr) failed.\n");
        return false;
    }

    const uintptr_t base = reinterpret_cast<uintptr_t>(exe);
    g_vtableSlot = reinterpret_cast<uintptr_t*>(base + kFontVtableRva + kGetGlyphMetricsSlotOffset);
    g_originalSlotValue = *g_vtableSlot;
    g_drawGlyphSlot = reinterpret_cast<uintptr_t*>(base + kFontVtableRva + kDrawGlyphSlotOffset);
    g_originalDrawGlyphSlotValue = *g_drawGlyphSlot;

    if (g_originalSlotValue == reinterpret_cast<uintptr_t>(&HookGetGlyphMetrics) ||
        g_originalDrawGlyphSlotValue == reinterpret_cast<uintptr_t>(&HookDrawGlyph)) {
        Log(L"[Risen3FontHookTest] Hooks already installed; skipping duplicate install.\n");
        return true;
    }

    const uintptr_t expected = base + kExpectedGetGlyphMetricsRva;
    if (g_originalSlotValue != expected) {
        wchar_t buffer[256]{};
        wsprintfW(
            buffer,
            L"[Risen3FontHookTest] Unexpected vtable slot. got=%p expected=%p\n",
            reinterpret_cast<void*>(g_originalSlotValue),
            reinterpret_cast<void*>(expected));
        Log(buffer);
        return false;
    }

    const uintptr_t expectedDrawGlyph = base + kExpectedDrawGlyphRva;
    if (g_originalDrawGlyphSlotValue != expectedDrawGlyph) {
        wchar_t buffer[256]{};
        wsprintfW(
            buffer,
            L"[Risen3FontHookTest] Unexpected draw glyph slot. got=%p expected=%p\n",
            reinterpret_cast<void*>(g_originalDrawGlyphSlotValue),
            reinterpret_cast<void*>(expectedDrawGlyph));
        Log(buffer);
        return false;
    }

    g_originalGetGlyphMetrics = reinterpret_cast<GetGlyphMetricsFn>(g_originalSlotValue);
    g_originalDrawGlyph = reinterpret_cast<DrawGlyphFn>(g_originalDrawGlyphSlotValue);

    if (!WritePointer(g_vtableSlot, reinterpret_cast<uintptr_t>(&HookGetGlyphMetrics))) {
        Log(L"[Risen3FontHookTest] Failed to write vtable slot.\n");
        g_originalGetGlyphMetrics = nullptr;
        g_vtableSlot = nullptr;
        g_originalSlotValue = 0;
        return false;
    }

    if (!WritePointer(g_drawGlyphSlot, reinterpret_cast<uintptr_t>(&HookDrawGlyph))) {
        Log(L"[Risen3FontHookTest] Failed to write DrawGlyph vtable slot.\n");
        WritePointer(g_vtableSlot, g_originalSlotValue);
        g_originalGetGlyphMetrics = nullptr;
        g_originalDrawGlyph = nullptr;
        g_vtableSlot = nullptr;
        g_drawGlyphSlot = nullptr;
        g_originalSlotValue = 0;
        g_originalDrawGlyphSlotValue = 0;
        return false;
    }

    InstallBuildGlyphCommandHook(base);
    if (g_enableCommandWriteHook) {
        InstallCommandResourceWriteHook(base);
    } else {
        Log(L"[Risen3FontHookTest] CommandResourceWrite hook disabled by config.\n");
    }
    if (g_enableSubmitHook) {
        InstallSubmitGlyphCommandHook(base);
    } else {
        Log(L"[Risen3FontHookTest] SubmitGlyphCommand hook disabled by config.\n");
    }
    if (g_enableBuildUvHook) {
        InstallBuildUvFromFontResourceHook(base);
    } else {
        Log(L"[Risen3FontHookTest] BuildUV hook disabled by config.\n");
    }
    if (g_enableAtlasGetterHook) {
        InstallAtlasGetterHook(base);
    } else {
        Log(L"[Risen3FontHookTest] AtlasGetter hook disabled by config.\n");
    }
    if (g_enableTextureReplaceHook) {
        InstallBindTextureHook(base);
    } else {
        Log(L"[Risen3FontHookTest] BindTexture hook disabled by config.\n");
    }
    if (g_enableFlushHook) {
        InstallFlushGlyphCommandsHook(base);
    } else {
        Log(L"[Risen3FontHookTest] FlushGlyphCommands hook disabled by config.\n");
    }

    g_installed = true;
    Log(L"[Risen3FontHookTest] vtable+0x40 and +0x50 hooks installed.\n");
    return true;
}

DWORD WINAPI InitThread(void*)
{
    Sleep(1000);
    LoadConfig();
    InstallHook();

    while (g_installed) {
        Sleep(kConfigPollIntervalMs);
        ReloadConfigIfChanged();
    }

    return 0;
}

void UninstallHook()
{
    if (!g_installed || !g_vtableSlot || !g_originalSlotValue) {
        return;
    }

    WritePointer(g_vtableSlot, g_originalSlotValue);
    if (g_drawGlyphSlot && g_originalDrawGlyphSlotValue) {
        WritePointer(g_drawGlyphSlot, g_originalDrawGlyphSlotValue);
    }
    HMODULE exe = GetModuleHandleW(nullptr);
    if (g_buildGlyphCommandHookInstalled && exe) {
        RestoreInlineHook(
            reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(exe) + kBuildGlyphCommandRva),
            15,
            g_buildGlyphCommandOriginalBytes);
    }
    if (g_buildGlyphCommandTrampoline) {
        VirtualFree(g_buildGlyphCommandTrampoline, 0, MEM_RELEASE);
        g_buildGlyphCommandTrampoline = nullptr;
    }
    if (g_commandResourceWriteHookInstalled && exe) {
        RestoreInlineHook(
            reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(exe) + kCommandResourceWritePatchRva),
            13,
            g_commandResourceWriteOriginalBytes);
    }
    if (g_commandResourceWriteStub) {
        VirtualFree(g_commandResourceWriteStub, 0, MEM_RELEASE);
        g_commandResourceWriteStub = nullptr;
    }
    if (g_submitGlyphCommandHookInstalled && exe) {
        RestoreInlineHook(
            reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(exe) + kSubmitGlyphCommandRva),
            12,
            g_submitGlyphCommandOriginalBytes);
    }
    if (g_submitGlyphCommandTrampoline) {
        VirtualFree(g_submitGlyphCommandTrampoline, 0, MEM_RELEASE);
        g_submitGlyphCommandTrampoline = nullptr;
    }
    if (g_buildUvFromFontResourceHookInstalled && exe) {
        RestoreInlineHook(
            reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(exe) + kBuildUvFromFontResourceRva),
            16,
            g_buildUvFromFontResourceOriginalBytes);
    }
    if (g_buildUvFromFontResourceTrampoline) {
        VirtualFree(g_buildUvFromFontResourceTrampoline, 0, MEM_RELEASE);
        g_buildUvFromFontResourceTrampoline = nullptr;
    }
    if (g_bindTextureHookInstalled && exe) {
        RestoreInlineHook(
            reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(exe) + kBindTextureFunctionRva),
            13,
            g_bindTextureOriginalBytes);
    }
    if (g_atlasGetterHookInstalled && exe) {
        RestoreInlineHook(
            reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(exe) + kAtlasGetterRva),
            12,
            g_atlasGetterOriginalBytes);
    }
    if (g_atlasGetterTrampoline) {
        VirtualFree(g_atlasGetterTrampoline, 0, MEM_RELEASE);
        g_atlasGetterTrampoline = nullptr;
    }
    if (g_bindTextureTrampoline) {
        VirtualFree(g_bindTextureTrampoline, 0, MEM_RELEASE);
        g_bindTextureTrampoline = nullptr;
    }
    if (g_testAtlasTexture) {
        g_testAtlasTexture->Release();
        g_testAtlasTexture = nullptr;
        g_testAtlasDevice = nullptr;
    }
    for (size_t i = 0; i < kFontStateCount; ++i) {
        ReleaseShadowFontState(&g_fontStates[i]);
    }
    if (g_flushGlyphCommandsHookInstalled && exe) {
        RestoreInlineHook(
            reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(exe) + kFlushGlyphCommandsRva),
            18,
            g_flushGlyphCommandsOriginalBytes);
    }
    if (g_flushGlyphCommandsTrampoline) {
        VirtualFree(g_flushGlyphCommandsTrampoline, 0, MEM_RELEASE);
        g_flushGlyphCommandsTrampoline = nullptr;
    }
    g_originalGetGlyphMetrics = nullptr;
    g_originalDrawGlyph = nullptr;
    g_originalBuildGlyphCommand = nullptr;
    g_originalSubmitGlyphCommand = nullptr;
    g_originalBuildUvFromFontResource = nullptr;
    g_originalAtlasGetter = nullptr;
    g_originalBindTexture = nullptr;
    g_originalFlushGlyphCommands = nullptr;
    g_vtableSlot = nullptr;
    g_drawGlyphSlot = nullptr;
    g_originalSlotValue = 0;
    g_originalDrawGlyphSlotValue = 0;
    g_buildGlyphCommandHookInstalled = false;
    g_commandResourceWriteHookInstalled = false;
    g_submitGlyphCommandHookInstalled = false;
    g_buildUvFromFontResourceHookInstalled = false;
    g_atlasGetterHookInstalled = false;
    g_bindTextureHookInstalled = false;
    g_flushGlyphCommandsHookInstalled = false;
    g_installed = false;
}

} // namespace
