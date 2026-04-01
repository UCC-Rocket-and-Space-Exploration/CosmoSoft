#!/usr/bin/env bash
# Clears build/ and runs a clean CMake configure + build (fixes stale cache after moving the repo).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
rm -rf "${ROOT}/build"
cmake -S "${ROOT}" -B "${ROOT}/build"
cmake --build "${ROOT}/build"
echo "OK: fresh configure + build in ${ROOT}/build"
