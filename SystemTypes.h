#pragma once
#include "ArduinoStubs.h"

constexpr uint8_t MAX_APPLIANCES = 8;
constexpr uint8_t MAX_RECOMMENDATIONS = 8;
constexpr uint8_t MAX_TEXT = 160;

struct SolarData { float voltage=0, current=0, power=0, energyTodayWh=0; };
struct BatteryData { float voltage=0, current=0, soc=0, soh=100, remainingWh=0, reserveWh=0, allowableDischargeW=0, maxChargeW=0, runtimeHours=0; bool lowAlarm=false, criticalAlarm=false; };
struct PlannerDecision { uint8_t applianceId=0; bool enabled=false; char explanation[MAX_TEXT]{}; };
struct Recommendation { char text[MAX_TEXT]{}; };
