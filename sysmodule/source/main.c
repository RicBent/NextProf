#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#include <3ds.h>

#include "config.h"
#include "log.h"
#include "record.h"
#include "luma.h"


#define TERMINATE_IF_R_FAILED(r, format, ...)   \
    if (R_FAILED((r)))                          \
    {                                           \
        LOG_ERROR(format, ##__VA_ARGS__);       \
        exit(-1);                               \
    }



#define SAMPLE_CYCLE_COUNT 10000000

#define SOC_ALIGN       0x1000
#define SOC_BUFFERSIZE  0x100000
u32* socBuf = NULL;

const char* processExitReasons[] = {
    "exit",
    "terminate",
    "debug terminate",
};

const char* threadExitReasons[] = {
    "exit",
    "terminate",
    "process exit",
    "process terminate",
};

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
        Handle debuggeeProcess;
    };
    Handle wait[3];
} handles;

s32 waitHandlesActive = 0;

bool attached = false;

#define MAX_ATTACHED_THREADS 0x20

typedef struct
{
    u32 id;
    u32 startAddress;
    u32 stackTop;
} AttachedThread;

AttachedThread attachedThreads[MAX_ATTACHED_THREADS];
size_t attachedThreadCount = 0;

bool addAttachedThread(u32 threadId, u32 pc, u32 sp)
{
    if (attachedThreadCount >= MAX_ATTACHED_THREADS)
    {
        LOG_WARNING("Maximum attached thread count reached, cannot add thread ID %lu", threadId);
        return false;
    }

    attachedThreads[attachedThreadCount++] = (AttachedThread){
        .id = threadId,
        .startAddress = pc,
        .stackTop = sp
    };

    return true;
}

bool removeAttachedThread(u32 threadId)
{
    for (size_t i = 0; i < attachedThreadCount; i++)
    {
        if (attachedThreads[i].id == threadId)
        {
            for (size_t j = i; j < attachedThreadCount - 1; j++)
            {
                attachedThreads[j] = attachedThreads[j + 1];
            }
            attachedThreadCount--;
            return true;
        }
    }
    return false;
}

AttachedThread* getAttachedThread(u32 threadId)
{
    for (size_t i = 0; i < attachedThreadCount; i++)
    {
        if (attachedThreads[i].id == threadId)
            return &attachedThreads[i];
    }
    return NULL;
}

inline bool isThreadAttached(u32 threadId)
{
    return getAttachedThread(threadId) != NULL;
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

    if (handles.debuggeeProcess != 0)
    {
        svcCloseHandle(handles.debuggeeProcess);
        handles.debuggeeProcess = 0;
    }

    r = PMDBG_RunQueuedProcess(&handles.debuggeeProcess);
    TERMINATE_IF_R_FAILED(r, "Running queued debuggee process failed: %08X", r);

    waitHandlesActive++;
}

void handleNotification()
{
    Result r;
    u32 notificationId = 0;

    r = srvReceiveNotification(&notificationId);
    TERMINATE_IF_R_FAILED(r, "Receiving notification failed: %08X", r);

    LOG_TRACE("Received notification 0x%08X", notificationId);

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

    r = svcBreakDebugProcess(handles.debuggeeProcess);
    if (R_FAILED(r))
    {
        LOG_WARNING("Breaking debuggee process failed: %08X", r);
        return;
    }
}

u32 stackBuffer[0x4000];

void handleDebuggeeProcessEvent()
{
    Result r;
    DebugEventInfo info;

    LOG_TRACE("Debuggee process event received");

    r = svcGetProcessDebugEvent(&info, handles.debuggeeProcess);
    TERMINATE_IF_R_FAILED(r, "Getting debug event failed: %08X", r);

    if (info.type == DBGEVENT_OUTPUT_STRING)
    {
        char buffer[0x101];
        memset(buffer, 0, sizeof(buffer));

        r = svcReadProcessMemory(buffer, handles.debuggeeProcess, info.output_string.string_addr, info.output_string.string_size);
        TERMINATE_IF_R_FAILED(r, "Reading debug output string failed: %08X", r);

        LOG_INFO("Debug output: %s", buffer);
    }
    else if (info.type == DBGEVENT_EXCEPTION)
    {
        if (info.exception.type == EXCEVENT_ATTACH_BREAK)
        {
            LOG_INFO("Debuggee process attach break");
            
            attached = true;
            recordInit();

            PMC_resetInterrupt();
        }
        else if (info.exception.type == EXCEVENT_DEBUGGER_BREAK)
        {
            u32 threadId;
            u32 stackSize;
            ThreadContext context;

            for (size_t i = 0; i < attachedThreadCount; i++)
            {
                threadId = attachedThreads[i].id;

                r = svcGetDebugThreadContext(&context, handles.debuggeeProcess, threadId, THREADCONTEXT_CONTROL_CPU_SPRS);
                TERMINATE_IF_R_FAILED(r, "Getting debug thread context failed (thread ID: %u): %08X", threadId, r);
                LOG_TRACE("Thread ID %lu - pc: 0x%08X, lr: 0x%08X, sp: 0x%08X", threadId, context.cpu_registers.pc, context.cpu_registers.lr, context.cpu_registers.sp);

                stackSize = attachedThreads[i].stackTop - context.cpu_registers.sp;
                if (stackSize > sizeof(stackBuffer))
                    stackSize = sizeof(stackBuffer);
                if (stackSize > config.profile.stackSize)
                    stackSize = config.profile.stackSize & ~3;
                
                if (stackSize > 0)
                {
                    r = svcReadProcessMemory(stackBuffer, handles.debuggeeProcess, context.cpu_registers.sp, stackSize);
                    TERMINATE_IF_R_FAILED(r, "Reading debug thread stack failed: 0x%08X", r);
                }

                recordEnsureSpace(sizeof(u32) * 5 + stackSize);

                recordHeader(RECORD_HEADER_SAMPLE);
                recordU32(threadId);
                recordU32(context.cpu_registers.pc);
                recordU32(context.cpu_registers.lr);
                recordU32(stackSize);
                if (stackSize > 0)
                    recordData(stackBuffer, stackSize);
            }

            PMC_resetInterrupt();
        }
        else
        {
            LOG_WARNING("Unhandled debuggee process exception (type: %d)", info.exception.type);
        }
    }
    else if (info.type == DBGEVENT_ATTACH_PROCESS)
    {
        LOG_INFO("Debuggee process attached (process ID: %u)", info.attach_process.process_id);
        LOG_INFO(" Program ID: %016lX", info.attach_process.program_id);
        LOG_INFO(" Name: %.8s", info.attach_process.process_name);
    }
    else if (info.type == DBGEVENT_EXIT_PROCESS)
    {
        LOG_INFO("Debuggee process exited (reason: %s)", processExitReasons[info.exit_process.reason]);

        svcCloseHandle(handles.debuggeeProcess);
        handles.debuggeeProcess = 0;
        waitHandlesActive--;

        recordExit();
        attached = false;
        attachedThreadCount = 0;

        r = PMDBG_LumaDebugNextApplicationByForce(true);
        TERMINATE_IF_R_FAILED(r, "Enabling Luma debug next application by force failed: %08X", r);

        LOG_INFO("Detached, waiting for debuggee application...");

        // Do not continue even though the flag is set, the process is gone.
        return;
    }
    else if (info.type == DBGEVENT_ATTACH_THREAD)
    {
        ThreadContext context;

        LOG_INFO("Debuggee process thread attached (thread ID: %u)", info.thread_id);
        LOG_INFO(" Creator thread ID: %u", info.attach_thread.creator_thread_id);
        LOG_INFO(" TLS: 0x%08X", info.attach_thread.thread_local_storage);
        // info.attached_thread.entry_point is always 0x100000?

        r = svcGetDebugThreadContext(&context, handles.debuggeeProcess, info.thread_id, THREADCONTEXT_CONTROL_CPU_SPRS | THREADCONTEXT_CONTROL_CPU_GPRS);
        TERMINATE_IF_R_FAILED(r, "Getting debug thread context failed: %08X", r);

        LOG_INFO(" Thread pc: 0x%08X", context.cpu_registers.pc);
        LOG_INFO(" Thread sp: 0x%08X", context.cpu_registers.sp);

        addAttachedThread(info.thread_id, context.cpu_registers.pc, context.cpu_registers.sp);
    }
    else if (info.type == DBGEVENT_EXIT_THREAD)
    {
        LOG_INFO("Debuggee process thread exited (thread ID: %u, reason: %s)", info.thread_id, threadExitReasons[info.exit_thread.reason]);

        removeAttachedThread(info.thread_id);
    } 

    if (info.flags & 1)
    {
        r = svcContinueDebugEvent(handles.debuggeeProcess, DBG_SIGNAL_SCHEDULE_EVENTS | DBG_SIGNAL_SYSCALL_EVENTS | DBG_SIGNAL_MAP_EVENTS);
        if (R_FAILED(r))
            LOG_WARNING("Continuing debug event failed: %08X", r);
    }
}

int main()
{
    Result r;

    configRead();

    socBuf = memalign(SOC_ALIGN, SOC_BUFFERSIZE);
    if (socBuf == NULL)
        return -1;
    if ((r = socInit(socBuf, SOC_BUFFERSIZE)) < 0)
    {
        free(socBuf);
        return -1;
    }
    atexit(socShutdown);

    initLog(true, false);
    atexit(exitLog);

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

    atexit(recordExit);

    r = PMDBG_LumaDebugNextApplicationByForce(true);
    TERMINATE_IF_R_FAILED(r, "Enabling Luma debug next application by force failed: %08X", r);

    LOG_INFO("Started, waiting for debuggee application...");
    LOG_INFO("STACK SIZE LIMIT: %u bytes", config.profile.stackSize);

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
            handleDebuggeeProcessEvent();
            break;
        default:
            LOG_ERROR("Unknown synchronization index %d", idx);
            break;
        }
    }

    LOG_INFO("Termination requested, exiting...");

    return 0;
}
