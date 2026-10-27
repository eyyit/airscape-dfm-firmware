#!/bin/sh
# SPDX-FileCopyrightText: 2026 the DFM firmware contributors
# SPDX-License-Identifier: Apache-2.0
set -eu
cd "$(dirname "$0")/.."
exec python3 tools/check_updates.py
