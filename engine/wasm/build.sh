#!/bin/sh
# Builds the shared engine to WebAssembly for the sound designer.
# Ubuntu: apt install wasi-libc libc++-18-dev-wasm32 libc++abi-18-dev-wasm32 libclang-rt-18-dev-wasm32 lld-18
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
eng="$here/.."
out="${1:-$here/../../designer/atmos.wasm}"
clang++ --target=wasm32-wasi -std=c++20 -O3 -fno-exceptions -fno-rtti \
  -mexec-model=reactor -Wl,-z,stack-size=1048576 \
  -isystem /usr/lib/llvm-18/include/wasm32-wasi/c++/v1 -L/usr/lib/llvm-18/lib/wasm32-wasi \
  -I"$eng" -I"$eng/osp" \
  "$here/api.cpp" "$eng/atmos/Core.cpp" "$eng/atmos/Bible.cpp" "$eng/atmos/ClimateMapper.cpp" "$eng/atmos/Astro.cpp" \
  "$eng"/osp/engine/*.cpp \
  -lc++ -lc++abi -o "$out"
echo "wrote $out ($(wc -c < "$out") bytes)"
