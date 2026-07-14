#include "ConstraintGuard.h"
float ConstraintGuard::availablePower(const SolarData& s,const BatteryData& b) const { return s.power + b.allowableDischargeW; }
bool ConstraintGuard::canRun(const Appliance& a,const SolarData& s,const BatteryData& b,float existingLoad) const { float need=existingLoad+a.startupSurge(); return need<=availablePower(s,b) && (s.power>=need || b.remainingWh>b.reserveWh); }
