# Pressure fields

The SDP810-500Pa is one differential-pressure sensor. Its digital response carries
one signed differential-pressure word, one temperature word and one scale word,
with CRCs. It does not publish independent P1 and P2 absolute/static pressures.
Manufacturer: https://sensirion.com/file/datasheet_sdp800-d/

The values in this independent firmware are explicit processing stages:

- sensor_dp_pa / hvac_filter_sensor_pressure_pa: signed pressure counts / scale,
  before our smoothing/offset/orientation. This differential reading already
  measures the pressure difference between the two physical ports.
- last_good.filter_pa: exponential smoothing state, before rounding.
- raw_dp_pa: stock-compatible smoothing and whole-Pa rounding before offset.
- canonical_dp_pa: rounded pressure plus configured additive offset.
- cor_dp_pa / hvac_filter_pressure_pa: canonical multiplied by orientation.
- processing_difference_pa: cor_dp_pa minus sensor_dp_pa. This shows the numerical
  processing effect, not a physical second sensor or the two port pressures.

Current top-level readings are null when invalid/stale; last_good remains diagnostic.
Pressure measurements under different zones/blower conditions are not directly
comparable as filter loading. No fixed dirty-filter threshold is inferred.
