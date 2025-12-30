#pragma once

#include <stdbool.h>

#define CONFIG_DIR "/nextprof"
#define CONFIG_PATH "/nextprof/config.ini"

typedef struct {
    struct {
        char host[60];
        int portHttp;
        int portUdp;
        int portTcp;
    } network;
    struct {
        bool file;
        bool udp;
    } log;
    struct {
        bool file;
        bool tcp;
    } record;
} Config;

extern Config config;

bool configRead();
bool configWriteNetworkHost(const char* host);
