#ifndef WST_PREFERENCES_H
#define WST_PREFERENCES_H

#include "WSTConfig.h"
#include <Preferences.h>

#define WST_DEFAULT_BAUD  115200
#define WST_DEFAULT_PORT  2323

class WSTPreferences : public BasePreferences {
public:
    WSTPreferences(WSTConfig& config)
        : BasePreferences(config), _wstConfig(config) {}

    void getPreferences() override {
        BasePreferences::getPreferences();
        _prefs.begin(PREF_NAMESPACE, true);
        _wstConfig.baud_rate   = _prefs.getUInt("baud_rate", WST_DEFAULT_BAUD);
        _wstConfig.telnet_port = 2323; //_prefs.getUShort("telnet_port", WST_DEFAULT_PORT);
        _prefs.end();
    }

    // Called by the AP manager's form handler after form values have been
    // copied into the FormField string buffers.  Parse them back into the
    // typed config fields before persisting.
    void putPreferences() override {
        syncFromFormBuffers();
        BasePreferences::putPreferences();
        _prefs.begin(PREF_NAMESPACE, false);
        _prefs.putUInt("baud_rate", _wstConfig.baud_rate);
        _prefs.putUShort("telnet_port", _wstConfig.telnet_port);
        _prefs.end();
    }

    void setBaudBuffer(char* buf)  { _baudBuf = buf; }
    void setPortBuffer(char* buf)  { _portBuf = buf; }

    void dumpPreferences() override {
        BasePreferences::dumpPreferences();
        Serial.printf("Pref=baud_rate: %u\n", _wstConfig.baud_rate);
        Serial.printf("Pref=telnet_port: %u\n", _wstConfig.telnet_port);
    }

    WSTConfig& wstConfig() { return _wstConfig; }

private:
    WSTConfig& _wstConfig;
    char* _baudBuf = nullptr;
    char* _portBuf = nullptr;

    void syncFromFormBuffers() {
        if (_baudBuf && _baudBuf[0]) {
            uint32_t v = strtoul(_baudBuf, nullptr, 10);
            if (v > 0) _wstConfig.baud_rate = v;
        }
        if (_portBuf && _portBuf[0]) {
            uint32_t v = strtoul(_portBuf, nullptr, 10);
            if (v > 0 && v <= 65535) _wstConfig.telnet_port = (uint16_t)v;
        }
    }
};

#endif // WST_PREFERENCES_H
