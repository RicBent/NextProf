#include "record.h"
#include "log.h"

#include <stdio.h>
#include <sys/stat.h>
#include <time.h>

u8 recordBuffer[RECORD_BUFFER_SIZE];
u8* recordHead = NULL;

FILE* recordFile = NULL;

void recordInit(bool toFile, bool toNetwork)
{
    recordExit();

    if (toFile && recordFile == NULL)
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
}

void recordExit()
{
    recordFlush();

    if (recordFile)
    {
        fclose(recordFile);
        recordFile = NULL;
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

    recordHead = recordBuffer;

    LOG_INFO("Flushed %lu bytes of recorded data", dataSize);
}
