#include "PreferenceStore.h"
#ifdef ARDUINO
#include <Preferences.h>
static Preferences prefs;
#endif
void PreferenceStore::begin(){
#ifdef ARDUINO
    prefs.begin("ems-pref", false);
#endif
}
void PreferenceStore::load(Appliance* loads,uint8_t count){
#ifdef ARDUINO
    for(uint8_t i=0;i<count;i++){ char key[8]; snprintf(key,sizeof(key),"p%u",loads[i].id()); loads[i].userPreference=prefs.getFloat(key,loads[i].userPreference); }
#else
    (void)loads; (void)count;
#endif
}
void PreferenceStore::save(const Appliance* loads,uint8_t count){
#ifdef ARDUINO
    for(uint8_t i=0;i<count;i++){ char key[8]; snprintf(key,sizeof(key),"p%u",loads[i].id()); prefs.putFloat(key,loads[i].userPreference); }
#else
    (void)loads; (void)count;
#endif
}
