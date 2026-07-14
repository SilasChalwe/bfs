#include "INA219Sensor.h"
bool INA219Sensor::begin(TwoWire& wire) {
#ifdef ARDUINO
    if (!ina219_.begin(&wire)) return false;
    ina219_.setCalibration_32V_2A();
#endif
    return true;
}
float INA219Sensor::readBusVoltage() {
#ifdef ARDUINO
    return ina219_.getBusVoltage_V();
#else
    return 0.0f;
#endif
}
float INA219Sensor::readCurrent() {
#ifdef ARDUINO
    return ina219_.getCurrent_mA() / 1000.0f;
#else
    return 0.0f;
#endif
}
float INA219Sensor::readPower() {
#ifdef ARDUINO
    return ina219_.getPower_mW() / 1000.0f;
#else
    return readBusVoltage() * readCurrent();
#endif
}
