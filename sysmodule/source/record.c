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

u8 recordBuffer[RECORD_BUFFER_SIZE];
u8* recordHead = NULL;

FILE* recordFile = NULL;
int recordSocket = -1;

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
}

void recordExit()
{
    recordFlush();

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

void recordFlush()
{
    if (recordHead == recordBuffer)
        return;

    u32 dataSize = recordHead - recordBuffer;

    if (recordFile)
    {
        size_t written = 0;
        while (written < dataSize)
        {
            size_t chunkSize = RECORD_FILE_WRITE_CHUNK_SIZE;
            if (dataSize - written < chunkSize)
                chunkSize = dataSize - written;

            size_t result = fwrite(recordBuffer + written, 1, chunkSize, recordFile);
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
        while (sent < dataSize)
        {
            size_t chunkSize = RECORD_NETWORK_SEND_CHUNK_SIZE;
            if (dataSize - sent < chunkSize)
                chunkSize = dataSize - sent;

            ssize_t result = send(recordSocket, recordBuffer + sent, chunkSize, 0);
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

    recordHead = recordBuffer;

    LOG_INFO("Flushed %lu bytes of recorded data", dataSize);
}
