#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
DEBUG_SANITIZE=1 ./build.sh
printf '\nDebug usage (three terminals; close old versions first):\n'
printf '  ASAN_OPTIONS=detect_leaks=0 ./server 2>&1 | tee server.log\n'
printf '  ASAN_OPTIONS=detect_leaks=0 ./client 2>&1 | tee client1.log\n'
printf '  ASAN_OPTIONS=detect_leaks=0 ./client 2>&1 | tee client2.log\n'
printf 'If a process reports an AddressSanitizer error, copy its stack trace.\n'
