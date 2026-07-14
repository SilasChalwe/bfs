#pragma once
#include "SystemTypes.h"
#include "Appliance.h"
class ConstraintGuard { public: float availablePower(const SolarData& s,const BatteryData& b) const; bool canRun(const Appliance& a,const SolarData& s,const BatteryData& b,float existingLoad) const; };
