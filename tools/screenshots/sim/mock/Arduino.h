#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include <time.h>
#include <string>
#include <algorithm>
#include <freertos/semphr.h>
class String : public std::string { public:
  String() {} String(const char* s) : std::string(s ? s : "") {} String(const std::string& s) : std::string(s) {}
  bool equals(const char* s) const { return *this == s; }
  bool isEmpty() const { return empty(); } void remove(size_t i, size_t n = 1) { erase(i, n); }
  String& operator+=(char c) { push_back(c); return *this; } String& operator+=(const char* s) { append(s); return *this; }
};
static inline void delay(uint32_t) {}
struct EspMock { void restart() {} } ;
extern EspMock ESP;
#define LV_FONT_UNUSED
extern uint32_t g_now_ms;
static inline uint32_t millis() { return g_now_ms; }
static inline bool getLocalTime(struct tm* ti, uint32_t = 0) { time_t t = 1760700000 + g_now_ms/1000; localtime_r(&t, ti); return true; }
#define PROGMEM
using std::min; using std::max;
class Print { public:
  virtual ~Print() {}
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t* b, size_t n) { for (size_t i = 0; i < n; i++) write(b[i]); return n; }
  size_t print(const char*) { return 0; } size_t println(const char*) { return 0; } size_t println() { return 0; }
};
class Stream : public Print { public: virtual int available() = 0; virtual int read() = 0; virtual int peek() = 0; };
