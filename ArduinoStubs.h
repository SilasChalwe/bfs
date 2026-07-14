#pragma once
#ifndef ARDUINO
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
using String = std::string;
#define HIGH 0x1
#define LOW 0x0
#define OUTPUT 0x1
#define INPUT 0x0
inline void pinMode(uint8_t, uint8_t) {}
inline void digitalWrite(uint8_t, uint8_t) {}
inline int analogRead(uint8_t) { return 0; }
inline void analogReadResolution(uint8_t) {}
inline unsigned long millis() { static unsigned long t=0; return t+=1000; }
class TwoWire {};
static TwoWire Wire;
class Print { public: template<typename T> void println(const T&){} template<typename T> void print(const T&){} };
static Print Serial;
#else
#include <Arduino.h>
#include <Wire.h>
#endif
