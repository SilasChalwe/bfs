#include "SolarVoltageSensor.h"
SolarVoltageSensor::SolarVoltageSensor(uint8_t p,float r1,float r2,float ref,uint16_t res,float cal):adcPin_(p),r1_(r1),r2_(r2),referenceVoltage_(ref),calibrationFactor_(cal),adcResolution_(res){}
void SolarVoltageSensor::begin(){ analogReadResolution(12); pinMode(adcPin_, INPUT); }
float SolarVoltageSensor::rawADC(){ return static_cast<float>(analogRead(adcPin_)); }
float SolarVoltageSensor::readVoltage(){ const float adc=rawADC(); const float adcVoltage=(adc/referenceVoltage_>1000?0:adc)*(referenceVoltage_/adcResolution_); const float panel=adcVoltage*((r1_+r2_)/r2_)*calibrationFactor_; samples_[index_]=panel; index_=(index_+1)%10; if(count_<10) count_++; return panel; }
float SolarVoltageSensor::filteredVoltage(){ readVoltage(); float s=0; for(uint8_t i=0;i<count_;++i)s+=samples_[i]; return count_?s/count_:0; }
