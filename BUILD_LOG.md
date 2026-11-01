# BUILD_LOG.md — crosspoint-reader fork

One row per build (runbook Phase 5). **Flashing is done by the human.** Device = Xteink X3/X4 (ESP32-C3). **Not** an X4 Pro. Release profile = `env:gh_release`.

## Rescue binary (Phase 1)
- Stock `firmware-*.bin` on the SD card: **CONFIRMED present** by Fagner. Filename/version: _pending._

## Feature flags
- `FSC_SLEEP_NOW_READING` — sleep "Now Reading" screen (cover + title/author + battery, full refresh).
- `FSC_WEATHER_DASHBOARD` — weather dashboard screen (Open-Meteo), entry via Settings → System → Weather.
- `env:fsc` = gh_release + both flags. Location hardcoded (Lisboa). Layout = v1 (enrichment to the target mockup pending).

## Builds

| Date | Branch | Commit | Flags / env | firmware.bin | Flash | RAM | Flashed? | Result |
|------|--------|--------|-------------|--------------|-------|-----|----------|--------|
| 2026-09-03 | develop | 25bbfae7 | baseline, env:default | 5429968 | 82.6% | 17.1% | no | SUCCESS |
| 2026-09-04 | chore/proof-build | 25bbfae7+ | 1.5.0-fsc.0, env:gh_release | 5386048 | 82.0% | 17.1% | yes | SUCCESS (proof) |
| 2026-09-04 | feat/sleep-now-reading | 25bbfae7+ | sleep v1, env:fsc_sleep | 5388960 | 82.0% | 17.1% | yes | SUCCESS (ghosted; fixed in v1.1) |
| 2026-09-04 | feat/sleep-now-reading | 25bbfae7+ | sleep v1.1 + weather v1, env:fsc | 5397120 | 82.1% (5,383,403) | 17.1% (56,140) | no | SUCCESS (both features; +~11 KB) |
| 2026-09-04 | feat/sleep-now-reading | 25bbfae7+ | sleep Dashboard (weather+reading) + Now Reading, env:fsc | 5399920 | 82.2% (5,386,203) | 17.1% (56,140) | no | SUCCESS (blind, first-try; standalone Weather screen removed) |

(+ = uncommitted working-tree changes)

## 2026-09-04 — Dashboard sleep: layout redesign v2 + 5-day forecast + reading progress
- Fix: eliminated overlaps (temp/condition, divider/date, tile labels/borders) using
  runtime text measurement (getTextWidth) + real line heights (getLineHeight); y = top-of-text.
- New: 5-day forecast row (Open-Meteo daily weather_code + max/min, forecast_days=5) fills the
  previously-empty lower half. WeatherData cache format bumped 'FSCW' -> 'FSCX'.
- New: Reading section now shows book cover thumbnail + progress bar + book % (read from
  <book>/progress.bin via Epub::calculateProgress) + 3 stat tiles (PROGRESSO / CAPÍTULO / PÁGINA).
- Note: max font asset is 18px (NOTOSERIF_18); a giant temperature would need a new font asset.
- Note: reading TIME + STREAK tiles deferred — they need a persistent reading-time tracker
  (hooks in EpubReaderActivity), planned as the next step. Current 3 tiles use existing data only.
- Files: src/network/WeatherData.{h,cpp}, src/activities/boot_sleep/SleepActivity.cpp
- Build: run gerar-weather.sh (env:fsc) -> dist/firmware-fsc.bin
