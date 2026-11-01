#include "WeatherData.h"
#if defined(FSC_WEATHER_DASHBOARD)

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <SecureHttpClient.h>
#include <WiFi.h>
#include <sys/time.h>
#include <time.h>

#include <cmath>
#include <cstdio>

namespace {
constexpr char WEATHER_LAT[] = "38.7223";  // Lisboa (TODO: configurable)
constexpr char WEATHER_LON[] = "-9.1393";
constexpr char CACHE_PATH[] = "/.crosspoint/weather.bin";
constexpr uint32_t CACHE_MAGIC = 0x46534358;  // 'FSCX' (bumped: now carries the 5-day forecast)

struct CachePod {
  uint32_t magic;
  float tempC, feelsC, tMaxC, tMinC, windKmh;
  int32_t humidity, rainProb, weatherCode;
  int32_t wYear, wMonth, wDay, wHour, wMin;
  int32_t haveForecast;
  int32_t fcHiC[WeatherData::FORECAST_DAYS];
  int32_t fcLoC[WeatherData::FORECAST_DAYS];
  int32_t fcCode[WeatherData::FORECAST_DAYS];
  int32_t fcWday[WeatherData::FORECAST_DAYS];
  int64_t fetchEpoch;
};

const char* MONTHS[] = {"January", "February", "March",     "April",   "May",      "June",
                        "July",    "August",   "September", "October", "November", "December"};
const char* DAYS[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
const char* DAYS3[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

long daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  const long era = (y >= 0 ? y : y - 399) / 400;
  const int yoe = static_cast<int>(y - era * 400);
  const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097L + doe - 719468L;
}

int zellerWeekday(int y, int m, int d) {  // 0=Sun..6=Sat
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  int yy = y;
  if (m < 3) yy -= 1;
  const int w = (yy + yy / 4 - yy / 100 + yy / 400 + t[(m >= 1 && m <= 12) ? m - 1 : 0] + d) % 7;
  return (w % 7 + 7) % 7;
}
}  // namespace

namespace WeatherStore {

const char* codeToText(int code) {
  if (code == 0) return "Clear";
  if (code == 1) return "Mostly clear";
  if (code == 2) return "Partly cloudy";
  if (code == 3) return "Overcast";
  if (code == 45 || code == 48) return "Fog";
  if (code >= 51 && code <= 57) return "Drizzle";
  if (code >= 61 && code <= 67) return "Rain";
  if (code >= 71 && code <= 77) return "Snow";
  if (code >= 80 && code <= 82) return "Showers";
  if (code >= 85 && code <= 86) return "Snow showers";
  if (code >= 95) return "Thunderstorm";
  return "--";
}

const char* monthName(int m) { return (m >= 1 && m <= 12) ? MONTHS[m - 1] : "--"; }

int weekdayIndex(int y, int m, int d) { return zellerWeekday(y, m, d); }

const char* dayName(int y, int m, int d) { return DAYS[weekdayIndex(y, m, d)]; }

const char* dayShort(int idx) { return (idx >= 0 && idx < 7) ? DAYS3[idx] : "--"; }

bool fetch(WeatherData& out) {
  if (WiFi.status() != WL_CONNECTED) return false;

  std::string url = std::string("https://api.open-meteo.com/v1/forecast?latitude=") + WEATHER_LAT +
                    "&longitude=" + WEATHER_LON +
                    "&current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,"
                    "wind_speed_10m,precipitation_probability"
                    "&daily=weather_code,temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=5";

  freeink::SecureHttpClient http;
  http.setTimeout(8000);
  http.setInsecure();
  http.setUserAgent("CrossPoint-ESP32-" CROSSPOINT_VERSION);
  http.setFollowRedirects(3);
  if (!http.begin(url)) return false;

  std::string body;
  const int status = http.GET(
      [&body](const uint8_t* data, size_t len) {
        body.append(reinterpret_cast<const char*>(data), len);
        return true;
      },
      []() { return false; });
  http.end();
  if (status != 200) return false;

  JsonDocument doc;
  if (deserializeJson(doc, body)) return false;

  JsonObject cur = doc["current"];
  out.tempC = cur["temperature_2m"] | 0.0f;
  out.feelsC = cur["apparent_temperature"] | 0.0f;
  out.humidity = cur["relative_humidity_2m"] | 0;
  out.weatherCode = cur["weather_code"] | 0;
  out.windKmh = cur["wind_speed_10m"] | 0.0f;
  out.rainProb = cur["precipitation_probability"] | 0;
  out.condition = codeToText(out.weatherCode);

  // 5-day daily outlook.
  JsonArray dMax = doc["daily"]["temperature_2m_max"].as<JsonArray>();
  JsonArray dMin = doc["daily"]["temperature_2m_min"].as<JsonArray>();
  JsonArray dCode = doc["daily"]["weather_code"].as<JsonArray>();
  JsonArray dTime = doc["daily"]["time"].as<JsonArray>();
  out.haveForecast = false;
  for (int i = 0; i < WeatherData::FORECAST_DAYS; ++i) {
    out.fcHiC[i] = static_cast<int>(std::lroundf(dMax[i] | 0.0f));
    out.fcLoC[i] = static_cast<int>(std::lroundf(dMin[i] | 0.0f));
    out.fcCode[i] = dCode[i] | 0;
    const char* ds = dTime[i] | "";
    int yy = 0, mm = 1, dd = 1;
    if (std::sscanf(ds, "%d-%d-%d", &yy, &mm, &dd) == 3) out.fcWday[i] = zellerWeekday(yy, mm, dd);
  }
  if (!dMax.isNull() && dMax.size() >= 1) out.haveForecast = true;
  out.tMaxC = static_cast<float>(out.fcHiC[0]);  // today's high/low mirror day 0
  out.tMinC = static_cast<float>(out.fcLoC[0]);

  const char* t = cur["time"] | "";
  if (std::sscanf(t, "%d-%d-%dT%d:%d", &out.wYear, &out.wMonth, &out.wDay, &out.wHour, &out.wMin) == 5) {
    out.fetchEpoch = daysFromCivil(out.wYear, out.wMonth, out.wDay) * 86400L + out.wHour * 3600L + out.wMin * 60L;
    struct timeval tv;
    tv.tv_sec = static_cast<time_t>(out.fetchEpoch);
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);  // keeps time across deep sleep for the freshness throttle
  }
  out.valid = true;
  return true;
}

void saveCache(const WeatherData& d) {
  CachePod p{};
  p.magic = CACHE_MAGIC;
  p.tempC = d.tempC; p.feelsC = d.feelsC; p.tMaxC = d.tMaxC; p.tMinC = d.tMinC; p.windKmh = d.windKmh;
  p.humidity = d.humidity; p.rainProb = d.rainProb; p.weatherCode = d.weatherCode;
  p.wYear = d.wYear; p.wMonth = d.wMonth; p.wDay = d.wDay; p.wHour = d.wHour; p.wMin = d.wMin;
  p.haveForecast = d.haveForecast ? 1 : 0;
  for (int i = 0; i < WeatherData::FORECAST_DAYS; ++i) {
    p.fcHiC[i] = d.fcHiC[i]; p.fcLoC[i] = d.fcLoC[i]; p.fcCode[i] = d.fcCode[i]; p.fcWday[i] = d.fcWday[i];
  }
  p.fetchEpoch = d.fetchEpoch;
  HalFile f;
  if (Storage.openFileForWrite("WTR", CACHE_PATH, f)) {
    f.write(reinterpret_cast<const uint8_t*>(&p), sizeof(p));
    f.flush();
  }
}

bool loadCache(WeatherData& out) {
  HalFile f;
  if (!Storage.openFileForRead("WTR", CACHE_PATH, f)) return false;
  CachePod p{};
  if (f.read(reinterpret_cast<uint8_t*>(&p), sizeof(p)) != static_cast<int>(sizeof(p)) || p.magic != CACHE_MAGIC)
    return false;
  out.tempC = p.tempC; out.feelsC = p.feelsC; out.tMaxC = p.tMaxC; out.tMinC = p.tMinC; out.windKmh = p.windKmh;
  out.humidity = p.humidity; out.rainProb = p.rainProb; out.weatherCode = p.weatherCode;
  out.wYear = p.wYear; out.wMonth = p.wMonth; out.wDay = p.wDay; out.wHour = p.wHour; out.wMin = p.wMin;
  out.haveForecast = p.haveForecast != 0;
  for (int i = 0; i < WeatherData::FORECAST_DAYS; ++i) {
    out.fcHiC[i] = p.fcHiC[i]; out.fcLoC[i] = p.fcLoC[i]; out.fcCode[i] = p.fcCode[i]; out.fcWday[i] = p.fcWday[i];
  }
  out.fetchEpoch = p.fetchEpoch;
  out.condition = codeToText(out.weatherCode);
  out.valid = true;
  return true;
}

bool isFresh(const WeatherData& d, int maxAgeMinutes) {
  if (!d.valid) return false;
  const time_t now = time(nullptr);
  if (now < 1700000000) return false;  // ESP clock not set (e.g. after a full power-off)
  return static_cast<long>(now) - d.fetchEpoch < static_cast<long>(maxAgeMinutes) * 60;
}

}  // namespace WeatherStore
#endif  // FSC_WEATHER_DASHBOARD
