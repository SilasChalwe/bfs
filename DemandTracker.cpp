#include "DemandTracker.h"
float DemandTracker::currentUsage(Appliance* loads,uint8_t count) const { float total=0; for(uint8_t i=0;i<count;i++) if(loads[i].isOn()) total+=loads[i].power(); return total; }
