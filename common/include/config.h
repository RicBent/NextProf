#pragma once

#include <3ds/types.h>

#define CONFIG_DIR "/nextprof"
#define CONFIG_PATH "/nextprof/config.ini"

typedef struct {
    struct {
        char host[60];
        s32 portHttp;
        s32 portUdp;
        s32 portTcp;
    } network;
    struct {
        bool file;
        bool udp;
    } log;
    struct {
        bool file;
        bool tcp;
        bool threaded;
    } record;
    struct {
        s64 instructionInterval;
        u32 stackSize;
        u32 maxThreads;
    } profile;
} Config;

extern Config config;

bool configRead();
bool configWriteNetworkHost(const char* host);
