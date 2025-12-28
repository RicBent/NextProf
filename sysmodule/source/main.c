#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#include <3ds.h>

#include "csvc.h"
#include "log.h"


#define TRY(r, label, format, ...)          \
    if (R_FAILED((r)))                      \
    {                                       \
        LOG_ERROR(format, ##__VA_ARGS__);   \
        goto label;                         \
    }

#define TERMINATE_IF_R_FAILED(r, format, ...)    \
    if (R_FAILED((r)))                      \
    {                                       \
        LOG_ERROR(format, ##__VA_ARGS__);   \
        exit(-1);                           \
    }


const u64 SYS_TITLE_ID      = 0x00040130091A8C02ULL;


#define SOC_ALIGN       0x1000
#define SOC_BUFFERSIZE  0x100000
u32* socBuf = NULL;


Result requestExit()
{
    LOG_INFO("Requesting exit");
    return PMAPP_TerminateTitle(SYS_TITLE_ID, 1000000000LL);
}


Result openProcessByName(const char *name, Handle *h)
{
    u32 pidList[0x40];
    s32 processCount;
    svcGetProcessList(&processCount, pidList, 0x40);
    Handle dstProcessHandle = 0;

    for(s32 i = 0; i < processCount; i++)
    {
        Handle processHandle;
        Result res = svcOpenProcess(&processHandle, pidList[i]);
        if(R_FAILED(res))
            continue;

        char procName[8] = {0};
        svcGetProcessInfo((s64*)procName, processHandle, 0x10000);
        if(strncmp(procName, name, 8) == 0)
            dstProcessHandle = processHandle;
        else
            svcCloseHandle(processHandle);
    }

    if(dstProcessHandle == 0)
        return -1;

    *h = dstProcessHandle;
    return 0;
}

union 
{
    struct
    {
        Handle debugeeProcessHandle;
        Handle perfCounterOverflowEvent;
    };
    Handle wait[2];
} handles;


void PMC_aquireControl()
{
    Result r;
    u64 out;

    r = svcControlPerformanceCounter(&out, PERFCOUNTEROP_ENABLE, 0, 0);
    TERMINATE_IF_R_FAILED(r, "PMC aquire control failed");
}

void PMC_releaseControl()
{
    Result r;
    u64 out;

    r = svcControlPerformanceCounter(&out, PERFCOUNTEROP_DISABLE, 0, 0);
    TERMINATE_IF_R_FAILED(r, "PMC release control failed");
}

void PMC_useVirtualCounter(bool enable)
{
    Result r;
    u64 out;

    r = svcControlPerformanceCounter(&out, PERFCOUNTEROP_SET_VIRTUAL_COUNTER_ENABLED, enable ? 1 : 0, 0);
    TERMINATE_IF_R_FAILED(r, "PMC use virtual counter failed");
}

void PMC_reset()
{
    Result r;
    u64 out;

    r = svcControlPerformanceCounter(&out, PERFCOUNTEROP_RESET, 0xFFFFFFFF, 0xFFFFFFFF);
    TERMINATE_IF_R_FAILED(r, "PMC reset failed");
}

void PMC_resetInterrupt()
{
    Result r;
    u64 out;

    PMC_reset();
    r = svcControlPerformanceCounter(&out, PERFCOUNTEROP_SET_VALUE, 0, (u64)-100000000);
    TERMINATE_IF_R_FAILED(r, "PMC reset interrupt failed");
}

void PMC_setInterrupt()
{
    Result r;
    u64 out;

    r = svcClearEvent(handles.perfCounterOverflowEvent);
    TERMINATE_IF_R_FAILED(r, "Clearing perf counter overflow event failed");

    r = svcControlPerformanceCounter(&out, PERFCOUNTEROP_SET_EVENT, 0, PERFCOUNTEREVT_CORE_CYCLE_COUNT);
    TERMINATE_IF_R_FAILED(r, "PMC set interrupt failed");
}

void socShutdown()
{
    socExit();
    free(socBuf);
    socBuf = NULL;
}

int main()
{
    Result r;

    initLog(true, false);
    atexit(exitLog);

    socBuf = memalign(SOC_ALIGN, SOC_BUFFERSIZE);
    if (socBuf == NULL)
    {
        LOG_ERROR("Allocating soc buffer failed");
        return -1;
    }
    if ((r = socInit(socBuf, SOC_BUFFERSIZE)) < 0)
    {
        LOG_ERROR("Initing soc failed: %08X", r);
        free(socBuf);
        return -1;
    }
    atexit(socShutdown);

    initLog(false, true);

    if ((r = nsInit()) < 0)
    {
        LOG_ERROR("Initing ns failed: %08X", r);
        return -1;
    }
    atexit(nsExit);

    if ((r = pmAppInit()) < 0)
    {
        LOG_ERROR("Initing pm:app failed: %08X", r);
        return -1;
    }
    atexit(pmAppExit);

    if ((r = pmDbgInit()) < 0)
    {
        LOG_ERROR("Initing pm:dbg failed: %08X", r);
        return -1;
    }
    atexit(pmDbgExit);

    r = svcCreateEvent(&handles.perfCounterOverflowEvent, RESET_ONESHOT);
    TERMINATE_IF_R_FAILED(r, "Creating perf counter overflow event failed: %08X", r);
    r = svcClearEvent(handles.perfCounterOverflowEvent);
    TERMINATE_IF_R_FAILED(r, "Clearing perf counter overflow event failed: %08X", r);

    PMC_aquireControl();
    atexit(PMC_releaseControl);

    PMC_useVirtualCounter(false);

    r = svcBindInterrupt(0x78, handles.perfCounterOverflowEvent, 0, false);
    TERMINATE_IF_R_FAILED(r, "Binding perf counter interrupt failed: %08X", r);

    PMC_setInterrupt();
    PMC_resetInterrupt();

    while (true)
    {
        LOG_INFO("Waiting for perf counter overflow event...");
        r = svcWaitSynchronization(handles.perfCounterOverflowEvent, -1LL);
        TERMINATE_IF_R_FAILED(r, "Waiting for perf counter overflow event failed: %08X", r);
        LOG_INFO("Perf counter overflow event signaled!");
        PMC_resetInterrupt();
    }

    r = svcUnbindInterrupt(0x78, handles.perfCounterOverflowEvent);
    TERMINATE_IF_R_FAILED(r, "Unbinding perf counter interrupt failed: %08X", r);

    return 0;
}
