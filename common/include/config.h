#pragma once

#include <stdbool.h>

typedef struct {
    struct {
        char host[60];
        int portHttp;
        int portUdp;
    } network;
    struct {
        bool file;
        bool udp;
    } log;
} Config;

extern Config config;

bool configRead();
bool configWriteNetworkHost(const char* host);
