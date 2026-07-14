#pragma once
#include "Appliance.h"
class PreferenceStore{ public: void begin(); void load(Appliance* loads,uint8_t count); void save(const Appliance* loads,uint8_t count); };
