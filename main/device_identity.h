/* SPDX-FileCopyrightText: 2026 the DFM firmware contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#if !defined(DFM_TESTING) && __has_include("deployment.local.h")
#include "deployment.local.h"
#endif
#ifndef DFM_DEVICE_HOSTNAME
#define DFM_DEVICE_HOSTNAME "dfm-device.example.invalid"
#endif
#ifndef DFM_DEVICE_SHORTNAME
#define DFM_DEVICE_SHORTNAME "dfm-device"
#endif
#ifndef DFM_SYSLOG_ADDRESS
#define DFM_SYSLOG_ADDRESS ""
#endif
#ifndef DFM_SYSLOG_PORT
#define DFM_SYSLOG_PORT 514
#endif
#define DFM_STRINGIFY_INNER(value) #value
#define DFM_STRINGIFY(value) DFM_STRINGIFY_INNER(value)
