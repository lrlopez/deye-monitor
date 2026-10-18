#pragma once
#include "Arduino.h"
class Preferences { public:
  bool begin(const char*, bool = false) { return true; } void end() {}
  String getString(const char*, const String& d = "") { return d; }
  uint32_t getULong(const char*, uint32_t d = 0) { return d; } uint16_t getUShort(const char*, uint16_t d = 0) { return d; }
  uint8_t getUChar(const char*, uint8_t d = 0) { return d; } bool getBool(const char*, bool d = false) { return d; }
  size_t putString(const char*, const char*) { return 0; } size_t putULong(const char*, uint32_t) { return 0; }
  size_t putUShort(const char*, uint16_t) { return 0; } size_t putUChar(const char*, uint8_t) { return 0; } size_t putBool(const char*, bool) { return 0; }
};
