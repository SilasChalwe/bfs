#include "BatteryManager.h"
BatteryManager::BatteryManager(float c,float r,float crit):capacityWh_(c),reservePct_(r),criticalPct_(crit){}
void BatteryManager::begin(){ sensor_.begin(); }
void BatteryManager::update(float loadW){ data_.voltage=sensor_.readBusVoltage(); data_.current=sensor_.readCurrent(); data_.remainingWh=capacityWh_*(data_.soc/100.0f); if(data_.soc<=0) { data_.soc=65; data_.remainingWh=capacityWh_*0.65f; } data_.reserveWh=capacityWh_*(reservePct_/100.0f); data_.allowableDischargeW=data_.soc<=reservePct_?0:300.0f*(data_.soh/100.0f); data_.maxChargeW=250; data_.lowAlarm=data_.soc<25; data_.criticalAlarm=data_.soc<criticalPct_; data_.runtimeHours=loadW>1?data_.remainingWh/loadW:99; }
bool BatteryManager::canSupply(float watts) const { return watts<=data_.allowableDischargeW && data_.remainingWh>data_.reserveWh; }
