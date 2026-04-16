#ifndef WST_ACCESS_POINT_MANAGER_H
#define WST_ACCESS_POINT_MANAGER_H

#include "WSTPreferences.h"
#include <BaseAccessPointManager.h>

class WSTAccessPointManager : public BaseAccessPointManager {
public:
    WSTAccessPointManager(WSTPreferences& prefs)
        : BaseAccessPointManager(prefs), _wstPrefs(prefs) {}

protected:
    void initializeFormFields() override {
        BaseAccessPointManager::initializeFormFields();

        snprintf(_baudBuf, sizeof(_baudBuf), "%u", _wstPrefs.wstConfig().baud_rate);
        snprintf(_portBuf, sizeof(_portBuf), "%u", _wstPrefs.wstConfig().telnet_port);

        // Let the preferences object know where the form buffers live
        // so it can parse them back to numeric fields on save
        _wstPrefs.setBaudBuffer(_baudBuf);
        _wstPrefs.setPortBuffer(_portBuf);

        _formFields.push_back(FormField{
            "BaudRateInput", "Baud Rate", false, PREF_STRING,
            { .str_pref = _baudBuf }, nullptr, 0
        });

        _formFields.push_back(FormField{
            "TelnetPortInput", "Telnet Port", false, PREF_STRING,
            { .str_pref = _portBuf }, nullptr, 0
        });
    }

private:
    WSTPreferences& _wstPrefs;
    char _baudBuf[MAX_PREF_STRING_LEN];
    char _portBuf[MAX_PREF_STRING_LEN];
};

#endif // WST_ACCESS_POINT_MANAGER_H
