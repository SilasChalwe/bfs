#pragma once
#include "SolarVoltageSensor.h"
#include "BatteryManager.h"
#include "DemandTracker.h"
#include "AStarPlanner.h"
#include "RecommendationEngine.h"
#include "WebSocketServer.h"
#include "PreferenceStore.h"
class ApiServer;
class EnergyManagementSystem{ public: EnergyManagementSystem(); void begin(); void loop(); SolarData solar() const{return solar_;} const BatteryData& battery() const{return battery_.data();} Appliance* loads(){return loads_;} uint8_t loadCount() const{return loadCount_;} private: SolarVoltageSensor solarVoltage_; BatteryManager battery_; DemandTracker demand_; AStarPlanner planner_; RecommendationEngine recommender_; PreferenceStore preferences_; WebSocketServer ws_; ApiServer* api_; SolarData solar_; Appliance loads_[5]; uint8_t loadCount_; unsigned long lastMs_=0; };
