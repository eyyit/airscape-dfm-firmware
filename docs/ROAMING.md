# Weak-signal roaming (0.3.2-dev)

Default automatic policy, no AP-specific hard pin or new credentials:
- Link RSSI <= -75 dBm continuously for 60 seconds; connected for at least 120 seconds.
- Background asynchronous same-SSID scan, active dwell 20-40 ms/channel.
- At most one scan per 300 seconds; 5 second scan watchdog, release scan records.
- Reconnect only if another WPA2-or-better matching AP is >= -78 dBm and at least
  12 dB stronger than the current AP, rechecked after scanning.
- Reconnection uses the existing all-channel signal-sorted selection. The candidate
  is not hard-pinned, so fallback remains possible if it disappears.
- No scan starts during OTA upload, selected update, or pending verification.
  An in-progress scan is stopped when upload/activation is observed by the
  network worker (up to its 500 ms polling interval). Scanning is not atomic
  with upload start; an already-started scan can briefly overlap that transition.
- No forced reconnect just because RSSI is weak: there must be a better AP.
- Worker priority remains 4, below acquisition 10 and metrics 8. All sensor
  reads remain in the sensor worker. Radio scans and AP changes can briefly
  interrupt delivery even though sensor acquisition continues.

Status JSON exposes policy and scan/attempt/error counters. Metrics exposes
hvac_filter_wifi_roam_scans_total, hvac_filter_wifi_roam_attempts_total and
hvac_filter_wifi_roam_scan_errors_total. Logs record actual BSSID changes and
stronger candidate/current signal. Counters are per boot. These are fixed
conservative policy constants in network.c; no persistent configuration schema
or calibration is changed. The policy does not guarantee a stronger AP exists,
or that RSSI alone predicts low interference or packet delivery.

Host tests cover time hysteresis, cooldown, OTA guard/abort, missing/stale scan,
scan failure/timeout, SSID/authentication, same BSSID, gain and signal thresholds,
and signal recovery while scanning. RF roaming still requires hardware testing.

ESP-IDF reference:
https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32c6/api-guides/wifi.html
