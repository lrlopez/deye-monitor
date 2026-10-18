#pragma once
#include "Arduino.h"
#define WL_CONNECTED 3
#define WL_DISCONNECTED 6
#define WIFI_SCAN_RUNNING (-1)
#define WIFI_AUTH_OPEN 0
struct IPAddressMock { std::string toString() const { return "192.168.1.77"; } };
struct WiFiMock { void disconnect() {} void begin(const char*, const char*) {}
int scanComplete() { return 5; } void scanDelete() {} int scanNetworks(bool, bool) { return -1; }
  int RSSI(int i) { static int r[] = {-48, -63, -71, -80, -88}; return r[i % 5]; }
  std::string SSID(int i) { static const char* n[] = {"MiRed_5G", "MiRed", "Vecino_2.4", "HUAWEI-Fibra", "ONO4521"}; return n[i % 5]; }
  int encryptionType(int i) { return i == 3 ? 0 : 3; } IPAddressMock localIP() { return {}; } int state = 3; int rssi = -52; int status() { return state; } int RSSI() { return rssi; } };
extern WiFiMock WiFi;
class WiFiClient { public: void stop() {} };
