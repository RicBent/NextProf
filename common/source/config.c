#include "config.h"
#include "minIni.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <ctype.h>

#define CONFIG_HOST_DEFAULT ""
#define CONFIG_PORT_HTTP_DEFAULT 7621
#define CONFIG_PORT_UDP_DEFAULT 7622
#define CONFIG_PORT_TCP_DEFAULT 7623

Config config = {
    .network = {
        .host = CONFIG_HOST_DEFAULT,
        .portHttp = CONFIG_PORT_HTTP_DEFAULT,
        .portUdp = CONFIG_PORT_UDP_DEFAULT,
        .portTcp = CONFIG_PORT_TCP_DEFAULT,
    },
    .log = {
        .file = true,
        .udp = true,
    },
    .record = {
        .file = true,
        .tcp = true,
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

#define CHECK_READ_INT(field)                                           \
    if (strcasecmp(key, #field) == 0) {                                 \
        cfg->field = strtol(value, NULL, 10);                           \
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
        CHECK_READ_INT(portHttp)
        CHECK_READ_INT(portUdp)
        CHECK_READ_INT(portTcp)
    SECTION_END

    SECTION_START(log)
        CHECK_READ_BOOL(file)
        CHECK_READ_BOOL(udp)
    SECTION_END

    SECTION_START(record)
        CHECK_READ_BOOL(file)
        CHECK_READ_BOOL(tcp)
    SECTION_END

    return 1;
}

bool configRead()
{
    mkdir(CONFIG_DIR, 0777);

    ini_browse(configReadCallback, NULL, CONFIG_PATH);
    return config.network.host[0] != '\0';
}

bool configWriteNetworkHost(const char* host)
{
    strncpy(config.network.host, host, sizeof(config.network.host) - 1);
    config.network.host[sizeof(config.network.host) - 1] = '\0';

    return ini_puts("Network", "Host", config.network.host, CONFIG_PATH);
}
