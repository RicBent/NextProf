#include "log.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

FILE* logFile = NULL;
int logSocket = -1;


void initLog(bool file, bool udp)
{
    if (file && logFile == NULL)
    {
        logFile = fopen(LOG_FILE_PATH, "w");
    }

    if (udp && logSocket < 0)
    {
        logSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        LOG_INFO("Log socket created: %d", logSocket);

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(LOG_SERVER_PORT);
        addr.sin_addr.s_addr = inet_addr(LOG_SERVER_IP);

        if (connect(logSocket, (struct sockaddr*)&addr, sizeof(addr)) < 0)
        {
            LOG_ERROR("Failed to connect log socket");
            close(logSocket);
            logSocket = -1;
        }
    }
}

void exitLog()
{
    if (logFile)
    {
        fclose(logFile);
        logFile = NULL;
    }

    if (logSocket >= 0)
    {
        close(logSocket);
        logSocket = -1;
    }
}

void logImpl(const char* type, const char* file, int line, const char* format, ...)
{
    char buffer[1024];
    int offset;

    offset = snprintf(buffer, sizeof(buffer), "[%s] %s:%d: ", type, file, line);

    if (offset < 0 || offset >= sizeof(buffer))
        offset = sizeof(buffer) - 1;

    va_list args;
    va_start(args, format);
    offset += vsnprintf(buffer + offset, sizeof(buffer) - offset, format, args);
    va_end(args);

    if (offset >= sizeof(buffer))
        offset = sizeof(buffer) - 1;

    buffer[offset++] = '\n';

    if (logFile)
    {
        fwrite(buffer, 1, offset, logFile);
        fflush(logFile);
    }

    if (logSocket >= 0)
    {
        send(logSocket, buffer, offset, 0);
    }
}
