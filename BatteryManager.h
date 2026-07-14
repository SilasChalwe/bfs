#pragma once
#include "INA219Sensor.h"
#include "SystemTypes.h"
class BatteryManager { public: BatteryManager(float capacityWh=1200,float reservePct=25,float criticalPct=15); void begin(); void update(float loadW); const BatteryData& data() const {return data_;} bool canSupply(float watts) const; private: INA219Sensor sensor_{0x45}; BatteryData data_; float capacityWh_,reservePct_,criticalPct_; };
