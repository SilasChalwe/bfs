#pragma once
#include "Appliance.h"
class DemandTracker { public: float currentUsage(Appliance* loads,uint8_t count) const; };
