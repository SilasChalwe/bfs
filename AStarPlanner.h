#pragma once
#include "SystemTypes.h"
#include "Appliance.h"
#include "ConstraintGuard.h"
class AStarPlanner { public: uint8_t plan(Appliance* loads,uint8_t count,const SolarData& s,const BatteryData& b,PlannerDecision* out,uint8_t maxOut); private: float cost(const Appliance& a,bool on,const SolarData& s,const BatteryData& b) const; };
