#include "config.h"
#include "minIni.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <ctype.h>

#define CONFIG_PROFILE_INSTRUCTION_INTERVAL_MIN 0x1000

Config config = {
    .network = {
        .host = "",
        .portHttp = 7621,
        .portUdp = 7622,
        .portTcp = 7623,
    },
    .log = {
        .file = true,
        .udp = true,
    },
    .record = {
        .file = true,
        .tcp = true,
        .threaded = false,
    },
    .profile = {
        .instructionInterval = 0x100000,
        .stackSize = 0,
        .maxThreads = 0,
    },
};

#define SECTION_START(s)                                                \
    if (strcasecmp(section, #s) == 0) {                                 \
        typeof(config.s)* cfg = &config.s;

#define SECTION_END return 1; }

#define CHECK_READ_STRING(field)                                        \
    if (strcasecmp(key, #field) == 0) {                                 \
        strncpy(cfg->field, value, sizeof(cfg->field) - 1);             \
        cfg->field[sizeof(cfg->field) - 1] = '\0';                      \
        return 1;                                                       \
    }

#define CHECK_READ_S32(field)                                           \
    if (strcasecmp(key, #field) == 0) {                                 \
        int base = (strncasecmp(value, "0x", 2) == 0) ? 16 : 10;        \
        cfg->field = strtol(value, NULL, base);                         \
        return 1;                                                       \
    }

#define CHECK_READ_U32(field)                                           \
    if (strcasecmp(key, #field) == 0) {                                 \
        int base = (strncasecmp(value, "0x", 2) == 0) ? 16 : 10;        \
        cfg->field = strtoul(value, NULL, base);                        \
        return 1;                                                       \
    }

#define CHECK_READ_S64(field)                                           \
    if (strcasecmp(key, #field) == 0) {                                 \
        int base = (strncasecmp(value, "0x", 2) == 0) ? 16 : 10;        \
        cfg->field = strtoll(value, NULL, base);                        \
        return 1;                                                       \
    }

#define CHECK_READ_U64(field)                                           \
    if (strcasecmp(key, #field) == 0) {                                 \
        int base = (strncasecmp(value, "0x", 2) == 0) ? 16 : 10;        \
        cfg->field = strtoull(value, NULL, base);                       \
        return 1;                                                       \
    }

#define CHECK_READ_BOOL(field)                                          \
    if (strcasecmp(key, #field) == 0) {                                 \
        char upper = toupper((unsigned char)value[0]);                  \
        cfg->field = (upper == 'T' || upper == '1' || upper == 'Y');    \
        return 1;                                                       \
    }

int configReadCallback(const char* section, const char* key, const char* value, void* userData)
{
    SECTION_START(network)
        CHECK_READ_STRING(host)
        CHECK_READ_S32(portHttp)
        CHECK_READ_S32(portUdp)
        CHECK_READ_S32(portTcp)
    SECTION_END

    SECTION_START(log)
        CHECK_READ_BOOL(file)
        CHECK_READ_BOOL(udp)
    SECTION_END

    SECTION_START(record)
        CHECK_READ_BOOL(file)
        CHECK_READ_BOOL(tcp)
        CHECK_READ_BOOL(threaded)
    SECTION_END

    SECTION_START(profile)
        CHECK_READ_S64(instructionInterval)
        CHECK_READ_U32(stackSize)
        CHECK_READ_U32(maxThreads)
    SECTION_END

    return 1;
}

bool configRead()
{
    mkdir(CONFIG_DIR, 0777);

    ini_browse(configReadCallback, NULL, CONFIG_PATH);
    
    if (config.profile.instructionInterval < CONFIG_PROFILE_INSTRUCTION_INTERVAL_MIN)
        config.profile.instructionInterval = CONFIG_PROFILE_INSTRUCTION_INTERVAL_MIN;

    return config.network.host[0] != '\0';
}

bool configWriteNetworkHost(const char* host)
{
    strncpy(config.network.host, host, sizeof(config.network.host) - 1);
    config.network.host[sizeof(config.network.host) - 1] = '\0';

    return ini_puts("Network", "Host", config.network.host, CONFIG_PATH);
}
