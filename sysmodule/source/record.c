#include "record.h"
#include "log.h"
#include "config.h"

#include <stdio.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <netdb.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define RECORD_BUFFER_SIZE (0x100000)
#define RECORD_FILE_WRITE_CHUNK_SIZE (0x4000)
#define RECORD_NETWORK_SEND_CHUNK_SIZE (0x1000)

u8 recordBuffer[RECORD_BUFFER_SIZE];

u8* recordBase = NULL;
u8* recordHead = NULL;
u8* recordEnd = NULL;

Thread recordThread = NULL;
LightEvent recordThreadFlushRequestEvent;
LightEvent recordThreadFlushDoneEvent;
volatile bool recordThreadShouldExit = false;
u8* recordThreadFlushBase = NULL;
u32 recordThreadFlushSize = 0;

FILE* recordFile = NULL;
int recordSocket = -1;

void recordThreadFunc(void* arg);

void recordInit()
{
    recordExit();

    if (config.record.file && recordFile == NULL)
    {
        mkdir("/nextprof", 0777);

        time_t unixTime = time(NULL);
        struct tm* time = localtime(&unixTime);

        char recordFilePath[128];
        snprintf(recordFilePath, sizeof(recordFilePath), "/nextprof/%04d-%02d-%02d_%02d-%02d-%02d.bin",
                 time->tm_year + 1900, time->tm_mon + 1, time->tm_mday,
                 time->tm_hour, time->tm_min, time->tm_sec);

        recordFile = fopen(recordFilePath, "wb");
        if (recordFile == NULL)
            LOG_ERROR("Failed to open record file: %s", recordFilePath);
        else
            LOG_INFO("Recording to file: %s", recordFilePath);
    }

    if (config.record.tcp && recordSocket < 0)
    {
        struct addrinfo hints;
        struct addrinfo* res = NULL;
        char portStr[8];

        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        snprintf(portStr, sizeof(portStr), "%u", config.network.portTcp);

        if (getaddrinfo(config.network.host, portStr, &hints, &res) != 0 || res == NULL)
        {
            LOG_ERROR("Failed to resolve record host");
            return;
        }

        LOG_INFO("Connecting to record host: %s:%s", config.network.host, portStr);
        for (struct addrinfo* rp = res; rp != NULL; rp = rp->ai_next)
        {
            recordSocket = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if (recordSocket < 0)
                continue;       

            if (connect(recordSocket, rp->ai_addr, rp->ai_addrlen) == 0)
            {
                LOG_INFO("Connected to record host: %s:%s", config.network.host, portStr);
                break;
            }

            close(recordSocket);
            recordSocket = -1;
        }

        if (recordSocket < 0)
            LOG_ERROR("Failed to connect record socket");

        freeaddrinfo(res);
    }

    if (config.record.threaded)
    {
        LightEvent_Init(&recordThreadFlushRequestEvent, RESET_ONESHOT);
        LightEvent_Init(&recordThreadFlushDoneEvent, RESET_ONESHOT);
        LightEvent_Signal(&recordThreadFlushDoneEvent);
        recordThreadShouldExit = false;

        s32 priority = 0x30;
        svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
        priority -= 1;
        priority = priority < 0x18 ? 0x18 : priority;
        priority = priority > 0x3F ? 0x3F : priority;

        // TODO: Use core_id 2 or 3 on New3DS?
        const s32 coreId = 3;

        recordThread = threadCreate(recordThreadFunc, NULL, 0x2000, priority, coreId, false);

        recordBase = recordBuffer;
        recordHead = recordBase;
        recordEnd = recordBase + (RECORD_BUFFER_SIZE / 2);
    }
    else 
    {
        recordBase = recordBuffer;
        recordHead = recordBase;
        recordEnd = recordBase + RECORD_BUFFER_SIZE;
    }
}

void recordExit()
{
    recordFlush();

    if (recordThread)
    {
        // Wait for last flush to complete
        LightEvent_Wait(&recordThreadFlushDoneEvent);

        // Signal thread to exit
        recordThreadShouldExit = true;
        LightEvent_Signal(&recordThreadFlushRequestEvent);

        // Wait for thread to exit
        LightEvent_Wait(&recordThreadFlushDoneEvent);
        threadJoin(recordThread, U64_MAX);

        recordThread = NULL;
    }

    if (recordFile)
    {
        fclose(recordFile);
        recordFile = NULL;
    }

    if (recordSocket >= 0)
    {
        close(recordSocket);
        recordSocket = -1;
    }
}

void recordFlushData(u8* data, u32 size)
{
    if (recordFile)
    {
        size_t written = 0;
        while (written < size)
        {
            size_t chunkSize = RECORD_FILE_WRITE_CHUNK_SIZE;
            if (size - written < chunkSize)
                chunkSize = size - written;
            size_t result = fwrite(data + written, 1, chunkSize, recordFile);
            if (result != chunkSize)
            {
                LOG_ERROR("Failed to write to record file");
                fclose(recordFile);
                recordFile = NULL;
                break;
            }

            written += chunkSize;
        }

        fflush(recordFile);
    }

    if (recordSocket >= 0)
    {
        size_t sent = 0;
        while (sent < size)
        {
            size_t chunkSize = RECORD_NETWORK_SEND_CHUNK_SIZE;
            if (size - sent < chunkSize)
                chunkSize = size - sent;

            ssize_t result = send(recordSocket, data + sent, chunkSize, 0);
            if (result < 0)
            {
                LOG_ERROR("Failed to send to record socket");
                close(recordSocket);
                recordSocket = -1;
                break;
            }

            sent += result;
        }
    }

    LOG_TRACE("Flushed %u bytes of recorded data", size);
}

void recordFlush()
{
    if (recordHead == recordBase)
        return;

    if (!recordThread)
    {
        recordFlushData(recordBase, recordHead - recordBase);
        recordHead = recordBase;
        return;
    }
    
    // Wait for previous flush to complete
    LOG_TRACE("Waiting for previous record flush to complete...");
    LightEvent_Wait(&recordThreadFlushDoneEvent);

    // Swap buffers
    LOG_TRACE("Swapping record buffers and signaling...");
    recordThreadFlushBase = recordBase;
    recordThreadFlushSize = recordHead - recordBase;
    recordBase = (recordBase == recordBuffer) ? (recordBuffer + (RECORD_BUFFER_SIZE / 2)) : recordBuffer;
    recordHead = recordBase;
    recordEnd = recordBase + (RECORD_BUFFER_SIZE / 2);

    // Request new flush
    LightEvent_Signal(&recordThreadFlushRequestEvent);
}

void recordThreadFunc(void* arg)
{
    while (true)
    {
		LightEvent_Wait(&recordThreadFlushRequestEvent);
        LOG_TRACE("Record thread: Woke up for flush request");
		if (recordThreadShouldExit)
        {
            LightEvent_Signal(&recordThreadFlushDoneEvent);
            break;
        }

        LOG_TRACE("Record thread: Flushing %u bytes", recordThreadFlushSize);
        recordFlushData(recordThreadFlushBase, recordThreadFlushSize);
        LOG_TRACE("Record thread: Flush complete");
        LightEvent_Signal(&recordThreadFlushDoneEvent);
	}
}
