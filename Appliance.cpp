#include "Appliance.h"
Appliance::Appliance(uint8_t id,const char* n,uint8_t r,uint8_t addr,float surge,uint8_t pri,float pref,float imp,bool mand):priority(pri),userPreference(pref),importance(imp),mandatory(mand),sensor(addr),id_(id),relayPin_(r),name_(n),startupSurgeW_(surge){}
void Appliance::begin(){ pinMode(relayPin_,OUTPUT); setRelay(false); sensor.begin(); }
void Appliance::setRelay(bool on){ on_=on; digitalWrite(relayPin_, on?HIGH:LOW); }
void Appliance::update(float dtHours){ voltage_=sensor.readBusVoltage(); current_=sensor.readCurrent(); power_=sensor.readPower(); if(on_){ energyConsumedWh_+=power_*dtHours; runtime+=static_cast<uint32_t>(dtHours*3600.0f); } }
