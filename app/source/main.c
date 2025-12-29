#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <3ds.h>

#include "http.h"

const u64 SYS_TITLE_ID = 0x00040130091A8C02ULL;

inline const char* rStr(Result r) { return R_FAILED(r) ? "FAIL" : "OK"; };

Result openProcessByTitleId(u64 titleId, Handle* processHandleOut)
{
    Result r;
    u32 pidList[0x40];
    s32 processCount;
    Handle foundProcessHandle = 0;

    *processHandleOut = 0;
    
    r = svcGetProcessList(&processCount, pidList, sizeof(pidList)/sizeof(u32));
    if (R_FAILED(r))
        return r;

    for (s32 i = 0; i < processCount; i++)
    {
        Handle processHandle;
        Result res = svcOpenProcess(&processHandle, pidList[i]);
        if(R_FAILED(res))
            continue;

        u64 procTitleId;
        svcGetProcessInfo((s64*)&procTitleId, processHandle, 0x10001);
        if(procTitleId == titleId)
            foundProcessHandle = processHandle;
        else
            svcCloseHandle(processHandle);
    }

    if (foundProcessHandle == 0)
        return -1;

    *processHandleOut = foundProcessHandle;

    return 0;
}

#define PRINT_AND_EXIT_IF_R_FAILED(r, format, ...)  \
    if (R_FAILED((r)))                              \
    {                                               \
        printf(format "\n", ##__VA_ARGS__);         \
        goto failure;                               \
    }

int main()
{
    Result r;
    u32 launchPid = 0;
    u32 downloadedSize = 0;
    Handle sysProcessHandle = 0;

    gfxInitDefault();
    atexit(gfxExit);

    consoleInit(GFX_TOP, NULL);
    printf("NextProf\n\n--------------------------------\n\n");

    r = srvPmInit();
    PRINT_AND_EXIT_IF_R_FAILED(r, "Initing srv:pm failed: 0x%08lX", r);
    atexit(srvPmExit);

    r = nsInit();
    PRINT_AND_EXIT_IF_R_FAILED(r, "Initing ns failed: 0x%08lX", r);
    atexit(nsExit);

    r = httpcInit(0);
    PRINT_AND_EXIT_IF_R_FAILED(r, "Initing httpc failed: 0x%08lX", r);
    atexit(httpcExit);

    r = openProcessByTitleId(SYS_TITLE_ID, &sysProcessHandle);
    if (R_SUCCEEDED(r))
    {
        printf("Profiler is already running!\n\nPress A to stop it.\nPress START to exit.\n\n");
        
        while (aptMainLoop())
        {
            gspWaitForVBlank();
            gfxSwapBuffers();
            hidScanInput();

            u32 kDown = hidKeysDown();

            if (kDown & KEY_A)
            {
                // Note: Initing pm:app freezes the system for some reason,
                //       so we directly publish the termination notification.
                r = SRVPM_PublishToProcess(0x100, sysProcessHandle);
                PRINT_AND_EXIT_IF_R_FAILED(r, "Publishing termination notification failed: 0x%08lX", r);
                break;
            }
            if (kDown & KEY_START)
                return 0;
        }

        svcCloseHandle(sysProcessHandle);
    }

    printf("Downloading sysmodule files...\n");
    gspWaitForVBlank();
    gfxSwapBuffers();

    r = http_download(HTTP_HOST "/sysmodule/code.bin", "/luma/titles/00040130091A8C02/code.bin", &downloadedSize);
    printf("code.bin:     %s (%lu bytes)\n", rStr(r), downloadedSize);
    gspWaitForVBlank();
    gfxSwapBuffers();

    r = http_download(HTTP_HOST "/sysmodule/exheader.bin", "/luma/titles/00040130091A8C02/exheader.bin", &downloadedSize);
    printf("exheader.bin: %s (%lu bytes)\n\n", rStr(r), downloadedSize);

    printf("Press A to start profiler.\nPress START to exit.\n\n");

    while (aptMainLoop())
    {
        gspWaitForVBlank();
        gfxSwapBuffers();
        hidScanInput();

        u32 kDown = hidKeysDown();

        if (kDown & KEY_A)
        {
            r = NS_LaunchTitle(SYS_TITLE_ID, 0, &launchPid);
            PRINT_AND_EXIT_IF_R_FAILED(r, "Launching sysmodule title failed: 0x%08lX", r);
            return 0;
        }
        else if (kDown & KEY_START)
            return 0;
    }

failure:
    printf("\nPress START to exit.\n");
        
    while (aptMainLoop())
    {
        gspWaitForVBlank();
        gfxSwapBuffers();
        hidScanInput();

        u32 kDown = hidKeysDown();

        if (kDown & KEY_START)
            break;
    }

    return 1;
}
