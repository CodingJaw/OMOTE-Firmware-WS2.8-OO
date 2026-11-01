#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
compiler="${CXX:-c++}"
output_dir="$(mktemp -d)"
trap 'rm -rf "$output_dir"' EXIT
for name in path_policy generation_selection boot_record manifest_index; do
  echo "[config-v2] compiling $name"
  "$compiler" -std=c++17 -O2 -Wall -Wextra -Werror -pedantic \
    -o "$output_dir/$name" \
    "Platformio/tools/config-v2-tests/${name}_test.cpp"
  echo "[config-v2] running $name"
  "$output_dir/$name"
done
