#pragma once
#include "SystemTypes.h"
class RecommendationEngine{ public: uint8_t generate(const SolarData& s,const BatteryData& b,float load,Recommendation* out,uint8_t maxOut); };
