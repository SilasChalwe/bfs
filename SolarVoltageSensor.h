#pragma once
#include "ArduinoStubs.h"
class SolarVoltageSensor {
public:
    SolarVoltageSensor(uint8_t adcPin=35, float r1=100000.0f, float r2=10000.0f, float referenceVoltage=3.3f, uint16_t adcResolution=4095, float calibrationFactor=1.0f);
    void begin();
    float readVoltage();
    float rawADC();
    float filteredVoltage();
private:
    uint8_t adcPin_;
    float r1_, r2_, referenceVoltage_, calibrationFactor_, samples_[10]{};
    uint16_t adcResolution_;
    uint8_t index_=0, count_=0;
};
