#ifndef WST_PREFERENCES_H
#define WST_PREFERENCES_H

#include "WSTConfig.h"
#include <Preferences.h>

#define WST_DEFAULT_BAUD  115200
#define WST_DEFAULT_PORT  23

class WSTPreferences : public BasePreferences {
public:
    WSTPreferences(WSTConfig& config)
        : BasePreferences(config), _wstConfig(config) {}

    void getPreferences() override {
        BasePreferences::getPreferences();
        _prefs.begin(PREF_NAMESPACE, true);
        _wstConfig.baud_rate   = _prefs.getUInt("baud_rate", WST_DEFAULT_BAUD);
        _wstConfig.telnet_port = _prefs.getUShort("telnet_port", WST_DEFAULT_PORT);
        _prefs.end();
    }

    void putPreferences() override {
        BasePreferences::putPreferences();
        _prefs.begin(PREF_NAMESPACE, false);
        _prefs.putUInt("baud_rate", _wstConfig.baud_rate);
        _prefs.putUShort("telnet_port", _wstConfig.telnet_port);
        _prefs.end();
    }

    void dumpPreferences() override {
        BasePreferences::dumpPreferences();
        Serial.printf("Pref=baud_rate: %u\n", _wstConfig.baud_rate);
        Serial.printf("Pref=telnet_port: %u\n", _wstConfig.telnet_port);
    }

    WSTConfig& wstConfig() { return _wstConfig; }

private:
    WSTConfig& _wstConfig;
};

#endif // WST_PREFERENCES_H
