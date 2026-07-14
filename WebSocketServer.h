#pragma once
#include "SystemTypes.h"
class WebSocketServer{ public: void begin(); void loop(); void broadcastSensorUpdate(const SolarData&,const BatteryData&); void broadcastDecision(const PlannerDecision&); void broadcastRecommendation(const Recommendation&); };
