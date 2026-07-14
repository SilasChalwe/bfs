#pragma once
#include "ArduinoStubs.h"
#ifdef ARDUINO
#include <Adafruit_INA219.h>
#endif
class INA219Sensor {
public:
    explicit INA219Sensor(uint8_t address=0x40): address_(address)
#ifdef ARDUINO
    , ina219_(address)
#endif
    {}
    bool begin(TwoWire& wire=Wire);
    float readBusVoltage();
    float readCurrent();
    float readPower();
    uint8_t address() const { return address_; }
private:
    uint8_t address_;
#ifdef ARDUINO
    Adafruit_INA219 ina219_;
#endif
};
