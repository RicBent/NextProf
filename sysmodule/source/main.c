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

#define SAMPLE_CYCLE_COUNT 100000000

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

s32 waitHandlesActive = 0;

bool attached = false;

#define MAX_ATTACHED_THREADS 0x20
u32 attachedThreadIds[MAX_ATTACHED_THREADS];
size_t attachedThreadCount = 0;

bool addAttachedThread(u32 threadId)
{
    if (attachedThreadCount >= MAX_ATTACHED_THREADS)
    {
        LOG_WARNING("Maximum attached thread count reached, cannot add thread ID %lu", threadId);
        return false;
    }

    attachedThreadIds[attachedThreadCount++] = threadId;
    return true;
}

bool removeAttachedThread(u32 threadId)
{
    for (size_t i = 0; i < attachedThreadCount; i++)
    {
        if (attachedThreadIds[i] == threadId)
        {
            for (size_t j = i; j < attachedThreadCount - 1; j++)
            {
                attachedThreadIds[j] = attachedThreadIds[j + 1];
            }
            attachedThreadCount--;
            return true;
        }
    }
    return false;
}

bool isThreadAttached(u32 threadId)
{
    for (size_t i = 0; i < attachedThreadCount; i++)
    {
        if (attachedThreadIds[i] == threadId)
            return true;
    }
    return false;
}


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
    r = svcControlPerformanceCounter(&out, PERFCOUNTEROP_SET_VALUE, 0, (u64)-SAMPLE_CYCLE_COUNT);
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

    waitHandlesActive++;

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

    PMC_releaseControl();

    waitHandlesActive--;
}

void notificationsInit()
{
    Result r;
    
    r = srvEnableNotification(&handles.notification);
    TERMINATE_IF_R_FAILED(r, "Enabling notifications failed: %08X", r);

    waitHandlesActive++;

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

    waitHandlesActive--;
}

static bool terminationRequested = false;

void handleDebugNextApplication()
{
    Result r;

    LOG_INFO("Debuggee application launched");

    if (handles.debugeeProcess != 0)
    {
        svcCloseHandle(handles.debugeeProcess);
        handles.debugeeProcess = 0;
    }

    r = PMDBG_RunQueuedProcess(&handles.debugeeProcess);
    TERMINATE_IF_R_FAILED(r, "Running queued debuggee process failed: %08X", r);

    waitHandlesActive++;
}

void handleNotification()
{
    Result r;
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

void handlePerfCounterOverflow()
{
    Result r;

    LOG_TRACE("Perf counter overflow event received");

    if (!attached)
        return;

    r = svcBreakDebugProcess(handles.debugeeProcess);
    if (R_FAILED(r))
    {
        LOG_WARNING("Breaking debuggee process failed: %08X", r);
        return;
    }
}

void handleDebugeeProcessEvent()
{
    Result r;
    DebugEventInfo info;

    LOG_TRACE("Debuggee process event received");

    r = svcGetProcessDebugEvent(&info, handles.debugeeProcess);
    TERMINATE_IF_R_FAILED(r, "Getting debug event failed: %08X", r);

    if (info.type == DBGEVENT_OUTPUT_STRING)
    {
        char buffer[0x101];
        memset(buffer, 0, sizeof(buffer));

        r = svcReadProcessMemory(buffer, handles.debugeeProcess, info.output_string.string_addr, info.output_string.string_size);
        TERMINATE_IF_R_FAILED(r, "Reading debug output string failed: %08X", r);

        LOG_INFO("Debug output: %s", buffer);
    }
    else if (info.type == DBGEVENT_EXCEPTION)
    {
        if (info.exception.type == EXCEVENT_ATTACH_BREAK)
        {
            LOG_INFO("Attached");
            attached = true;
            PMC_resetInterrupt();
        }
        else if (info.exception.type == EXCEVENT_DEBUGGER_BREAK)
        {
            u32 threadId;
            ThreadContext context;

            for (size_t i = 0; i < attachedThreadCount; i++)
            {
                threadId = attachedThreadIds[i];

                r = svcGetDebugThreadContext(&context, handles.debugeeProcess, threadId, THREADCONTEXT_CONTROL_CPU_SPRS);
                TERMINATE_IF_R_FAILED(r, "Getting debug thread context failed (thread ID: %u): %08X", threadId, r);
                LOG_INFO("Thread ID %lu - pc: 0x%08X, lr: 0x%08X, sp: 0x%08X", threadId, context.cpu_registers.pc, context.cpu_registers.lr, context.cpu_registers.sp);
            }

            PMC_resetInterrupt();
        }
        else
        {
            LOG_WARNING("Unhandled debuggee process exception (type: %d)", info.exception.type);
        }
    }
    else if (info.type == DBGEVENT_EXIT_PROCESS)
    {
        LOG_INFO("Debuggee process exited");

        svcCloseHandle(handles.debugeeProcess);
        handles.debugeeProcess = 0;
        waitHandlesActive--;

        attached = false;

        r = PMDBG_LumaDebugNextApplicationByForce(true);
        TERMINATE_IF_R_FAILED(r, "Enabling Luma debug next application by force failed: %08X", r);
    }
    else if (info.type == DBGEVENT_ATTACH_THREAD)
    {
        LOG_INFO("Debuggee process thread attached (thread ID: %u)", info.thread_id);
        LOG_INFO(" Creator thread ID: %u", info.attach_thread.creator_thread_id);
        LOG_INFO(" TLS: 0x%08X", info.attach_thread.thread_local_storage);
        LOG_INFO(" Entry point: %08X", info.attach_thread.entry_point);

        addAttachedThread(info.thread_id);
    }
    else if (info.type == DBGEVENT_EXIT_THREAD)
    {
        LOG_INFO("Debuggee process thread exited (thread ID: %u)", info.thread_id);

        removeAttachedThread(info.thread_id);
    } 

    if (info.flags & 1)
    {
        r = svcContinueDebugEvent(handles.debugeeProcess, DBG_SIGNAL_SCHEDULE_EVENTS | DBG_SIGNAL_SYSCALL_EVENTS | DBG_SIGNAL_MAP_EVENTS);
        if (R_FAILED(r))
            LOG_WARNING("Continuing debug event failed: %08X", r);
    }
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

    LOG_INFO("Started, waiting for debuggee application...");

    s32 idx = 0;
    while (!terminationRequested)
    {
        LOG_TRACE("Waiting for synchronization event...");
        r = svcWaitSynchronizationN(&idx, handles.wait, waitHandlesActive, false, -1LL);
        TERMINATE_IF_R_FAILED(r, "Waiting for synchronization failed: %08X", r);

        LOG_TRACE("Synchronization event %d signaled", idx);

        switch (idx)
        {
        case 0:
            handleNotification();
            break;
        case 1:
            handlePerfCounterOverflow();
            break;
        case 2:
            handleDebugeeProcessEvent();
            break;
        default:
            LOG_ERROR("Unknown synchronization index %d", idx);
            break;
        }
    }

    return 0;
}
