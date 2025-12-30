#include "log.h"
#include "config.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>

FILE* logFile = NULL;
int logSocket = -1;

#define LOG_FILE_PATH   "/nextprof/nextprof_sys.log"

void initLog()
{
    if (config.log.file && logFile == NULL)
        logFile = fopen(LOG_FILE_PATH, "w");

    if (config.log.udp && logSocket < 0)
    {
        struct addrinfo hints;
        struct addrinfo* res = NULL;
        char portStr[8];

        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_protocol = IPPROTO_UDP;

        snprintf(portStr, sizeof(portStr), "%u", config.network.portUdp);

        if (getaddrinfo(config.network.host, portStr, &hints, &res) != 0 || res == NULL)
        {
            LOG_ERROR("Failed to resolve log host");
            return;
        }

        for (struct addrinfo* rp = res; rp != NULL; rp = rp->ai_next)
        {
            logSocket = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if (logSocket < 0)
                continue;

            if (connect(logSocket, rp->ai_addr, rp->ai_addrlen) == 0)
            {
                int flags = fcntl(logSocket, F_GETFL, 0);
                if (flags >= 0)
                    fcntl(logSocket, F_SETFL, flags | O_NONBLOCK);
                break;
            }

            close(logSocket);
            logSocket = -1;
        }

        if (logSocket < 0)
            LOG_ERROR("Failed to connect log socket");

        freeaddrinfo(res);
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
