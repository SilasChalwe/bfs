#include "EnergyManagementSystem.h"
EnergyManagementSystem ems;
void setup(){ Serial.println("Solar ESP32 Energy Management System"); ems.begin(); }
void loop(){ ems.loop(); }
