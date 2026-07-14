#pragma once
#include "INA219Sensor.h"
class Appliance {
public:
    Appliance(uint8_t id,const char* name,uint8_t relayPin,uint8_t ina219Address,float surgeW,uint8_t priority,float pref,float importance,bool mandatory=false);
    void begin(); void update(float dtHours); void setRelay(bool on); bool isOn() const { return on_; }
    float voltage() const { return voltage_; } float current() const { return current_; } float power() const { return power_; } float energyConsumed() const { return energyConsumedWh_; }
    float startupSurge() const { return startupSurgeW_; } uint8_t id() const { return id_; } const char* name() const { return name_; }
    uint8_t priority; float userPreference; float importance; bool mandatory; bool locked=false; uint32_t runtime=0; float estimatedRemainingRuntime=0;
    INA219Sensor sensor;
private:
    uint8_t id_, relayPin_; const char* name_; bool on_=false; float voltage_=0,current_=0,power_=0,energyConsumedWh_=0,startupSurgeW_;
};
