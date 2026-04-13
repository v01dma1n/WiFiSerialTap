#ifndef WST_CONFIG_H
#define WST_CONFIG_H

#include "ESP32WiFi.h"

struct WSTConfig : public BaseConfig {
    uint32_t baud_rate;
    uint16_t telnet_port;
};

#endif // WST_CONFIG_H
