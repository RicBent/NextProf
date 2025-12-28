#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#include <3ds.h>

#include "log.h"
#include "luma.h"


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

void socShutdown()
{
    socExit();
    free(socBuf);
    socBuf = NULL;
}

union 
{
    struct
    {
        Handle notification;
        Handle perfCounterOverflowEvent;
        Handle debugeeProcess;
    };
    Handle wait[3];
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

void PMC_init()
{
    Result r;

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
}

void PMC_exit()
{
    Result r;

    r = svcUnbindInterrupt(0x78, handles.perfCounterOverflowEvent);
    TERMINATE_IF_R_FAILED(r, "Unbinding perf counter interrupt failed: %08X", r);
}

void notificationsInit()
{
    Result r;
    
    r = srvEnableNotification(&handles.notification);
    TERMINATE_IF_R_FAILED(r, "Enabling notifications failed: %08X", r);

    r = srvSubscribe(0x1000);   // Luma: Next application debugged by force
    TERMINATE_IF_R_FAILED(r, "Subscribing to notification 0x1000 failed: %08X", r);
}

void notificationsExit()
{
    Result r;

    r = srvUnsubscribe(0x1000);
    if (R_FAILED(r))
        LOG_ERROR("Unsubscribing from notification 0x1000 failed: %08X", r);
    svcCloseHandle(handles.notification);
}

static bool terminationRequested = false;

void handleDebugNextApplication()
{
    LOG_INFO("Debuggee application launched");
}

void handlePerfCounterOverflow()
{
    LOG_INFO("Perf counter overflow event received");
    PMC_resetInterrupt();
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

    notificationsInit();
    atexit(notificationsExit);

    PMC_init();
    atexit(PMC_exit);

    r = PMDBG_LumaDebugNextApplicationByForce(true);
    TERMINATE_IF_R_FAILED(r, "Enabling Luma debug next application by force failed: %08X", r);

    s32 idx = 0;
    while (!terminationRequested)
    {
        LOG_INFO("Waiting for synchronization event...");
        r = svcWaitSynchronizationN(&idx, handles.wait, 2, false, -1LL);
        TERMINATE_IF_R_FAILED(r, "Waiting for synchronization failed: %08X", r);

        LOG_INFO("Synchronization event %d signaled", idx);

        if (idx == 0)
        {
            u32 notificationId = 0;
            r = srvReceiveNotification(&notificationId);
            TERMINATE_IF_R_FAILED(r, "Receiving notification failed: %08X", r);

            LOG_INFO("Received notification 0x%08X", notificationId);

            if (notificationId == 0x100)
                terminationRequested = true;
            else if (notificationId == 0x1000)
                handleDebugNextApplication();
            else
                LOG_ERROR("Unknown notification ID 0x%08X", notificationId);
        }
        else if (idx == 1)
            handlePerfCounterOverflow();
        else 
            LOG_ERROR("Unknown synchronization index %d", idx);
    }

    return 0;
}
