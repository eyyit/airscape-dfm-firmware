/* SPDX-FileCopyrightText: 2026 the DFM firmware contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { DFM_OK, DFM_I2C, DFM_CRC, DFM_SCALE, DFM_NOT_READY } dfm_error_t;
typedef struct {
    uint32_t schema, interval_ms, timeout_ms, recovery_failures, backoff_ms, stale_ms;
    float alpha, offset_pa, setpoint_pa, hysteresis_pa;
    int32_t orientation;
    bool power_save;
} dfm_config_t;
typedef struct {
    int16_t counts, temperature_counts, scale;
    float sensor_pa, temperature_c;
} dfm_frame_t;
typedef struct {
    bool valid, have_sample, filter_seeded, high_static;
    dfm_error_t error;
    uint8_t last_frame[9];
    uint8_t crc_valid_mask;
    dfm_frame_t frame;
    float filter_pa, raw_pa, canonical_pa, corrected_pa;
    uint64_t acquired_us, attempted_us, successes, errors[3], recoveries;
    uint32_t consecutive_errors, duration_us, max_duration_us;
    uint64_t missed_deadlines;
} dfm_state_t;
dfm_config_t dfm_defaults(void);
bool dfm_config_valid(const dfm_config_t *config);
bool dfm_config_equal(const dfm_config_t *a, const dfm_config_t *b);
uint8_t dfm_crc(const uint8_t *data, size_t length);
dfm_error_t dfm_decode(const uint8_t bytes[9], dfm_frame_t *frame, uint8_t *crc_mask);
void dfm_accept(dfm_state_t *state, const dfm_config_t *config,
                const uint8_t bytes[9], uint64_t now_us);
void dfm_fail(dfm_state_t *state, dfm_error_t error, uint64_t now_us);
bool dfm_fresh(const dfm_state_t *state, const dfm_config_t *config, uint64_t now_us);
/* Returns false on overflow, never exposes a truncated Prometheus response. */
bool dfm_metrics(char *buffer, size_t capacity, const dfm_state_t *state,
                 const dfm_config_t *config, uint64_t now_us, size_t *length);
