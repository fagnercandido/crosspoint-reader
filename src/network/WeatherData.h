#pragma once
#if defined(FSC_WEATHER_DASHBOARD)

#include <cstdint>
#include <string>

// Shared weather model + fetch/cache used by the sleep "Dashboard" mode.
struct WeatherData {
  static constexpr int FORECAST_DAYS = 5;

  bool valid = false;
  float tempC = 0, feelsC = 0, tMaxC = 0, tMinC = 0, windKmh = 0;
  int humidity = 0, rainProb = 0, weatherCode = 0;
  int wYear = 0, wMonth = 1, wDay = 1, wHour = 0, wMin = 0;  // local time from the API

  // 5-day outlook (index 0 == today). wday: 0=Sun .. 6=Sat.
  bool haveForecast = false;
  int fcHiC[FORECAST_DAYS] = {0};
  int fcLoC[FORECAST_DAYS] = {0};
  int fcCode[FORECAST_DAYS] = {0};
  int fcWday[FORECAST_DAYS] = {0};

  long fetchEpoch = 0;  // for the freshness throttle
  std::string condition;
};

namespace WeatherStore {
const char* codeToText(int code);
const char* dayName(int y, int m, int d);  // "Wednesday"
const char* dayShort(int wdayIndex);       // "WED" (0=Sun..6=Sat)
int weekdayIndex(int y, int m, int d);     // 0=Sun..6=Sat
const char* monthName(int m);              // "July"
bool fetch(WeatherData& out);              // blocking HTTPS GET (needs WiFi); also sets the ESP clock
bool loadCache(WeatherData& out);
void saveCache(const WeatherData& d);
bool isFresh(const WeatherData& d, int maxAgeMinutes);
}  // namespace WeatherStore

#endif  // FSC_WEATHER_DASHBOARD
