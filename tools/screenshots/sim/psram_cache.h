#pragma once
#include "data_store.h"
class PsramCache { public:
  static PsramCache& instance() { static PsramCache c; return c; }
  bool getDaily(uint32_t dep, DailyRecord& out);
  const HourlyRecord* getHourly(uint32_t dep);
  uint32_t getOldestDailyEpoch() const { return 0; }
  bool dayHasData(uint32_t) const { return true; }
};
#define Cache PsramCache::instance()
