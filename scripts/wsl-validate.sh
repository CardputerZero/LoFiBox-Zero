#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_TYPE="${BUILD_TYPE:-Debug}"
TEST_BUILD_DIR="${WSL_TEST_BUILD_DIR:-$ROOT_DIR/build/wsl/test}"
DEVICE_BUILD_DIR="${WSL_DEVICE_BUILD_DIR:-$ROOT_DIR/build/wsl/device}"
WAYLAND_BUILD_DIR="${WSL_WAYLAND_BUILD_DIR:-$ROOT_DIR/build/wsl/wayland}"

require_command() {
  local command_name="$1"
  if command -v "$command_name" >/dev/null 2>&1; then
    return 0
  fi

  printf 'Missing required command: %s\n' "$command_name" >&2
  printf 'Install the Linux build toolchain first, for example:\n' >&2
  printf '  sudo apt update && sudo apt install -y build-essential cmake ninja-build pkg-config git\n' >&2
  exit 1
}

print_step() {
  local message="$1"
  printf '\n==> %s\n' "$message"
}

GENERATOR_ARGS=()
if command -v ninja >/dev/null 2>&1; then
  GENERATOR_ARGS=(-G Ninja)
fi

require_command cmake
require_command ctest
require_command git
require_command wayland-scanner

if command -v pkg-config >/dev/null 2>&1; then
  REQUIRED_PKGCONFIG_MODULES=(xkbcommon wayland-client wayland-protocols)

  MISSING_MODULES=()
  for module_name in "${REQUIRED_PKGCONFIG_MODULES[@]}"; do
    if ! pkg-config --exists "$module_name"; then
      MISSING_MODULES+=("$module_name")
    fi
  done

  if (( ${#MISSING_MODULES[@]} > 0 )); then
    printf 'Missing required Linux development packages for the Linux device target.\n' >&2
    printf 'Missing pkg-config modules: %s\n' "${MISSING_MODULES[*]}" >&2
    printf 'On Ubuntu/WSL, install the packages documented in README.md before rerunning this script.\n' >&2
    exit 1
  fi
fi

print_step "Configuring shared Linux tests into $TEST_BUILD_DIR"
cmake -S "$ROOT_DIR" -B "$TEST_BUILD_DIR" "${GENERATOR_ARGS[@]}" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DLOFIBOX_BUILD_DEVICE=OFF \
  -DLOFIBOX_BUILD_WAYLAND=OFF

print_step "Building shared Linux smoke test"
cmake --build "$TEST_BUILD_DIR" --target lofibox_app_smoke --config "$BUILD_TYPE"

print_step "Running shared Linux smoke test"
ctest --test-dir "$TEST_BUILD_DIR" --build-config "$BUILD_TYPE" --output-on-failure -R '^lofibox_app_smoke$'

print_step "Configuring Linux device target into $DEVICE_BUILD_DIR"
cmake -S "$ROOT_DIR" -B "$DEVICE_BUILD_DIR" "${GENERATOR_ARGS[@]}" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DLOFIBOX_BUILD_DEVICE=ON \
  -DLOFIBOX_BUILD_WAYLAND=OFF \
  -DBUILD_TESTING=OFF

print_step "Building Linux device target"
cmake --build "$DEVICE_BUILD_DIR" --target lofibox_zero_device --config "$BUILD_TYPE"

print_step "Configuring Linux Wayland target into $WAYLAND_BUILD_DIR"
cmake -S "$ROOT_DIR" -B "$WAYLAND_BUILD_DIR" "${GENERATOR_ARGS[@]}" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DLOFIBOX_BUILD_DEVICE=OFF \
  -DLOFIBOX_BUILD_X11=OFF \
  -DLOFIBOX_BUILD_WAYLAND=ON \
  -DBUILD_TESTING=OFF

print_step "Building Linux Wayland target"
cmake --build "$WAYLAND_BUILD_DIR" --target lofibox_zero_wayland --config "$BUILD_TYPE"

printf '\nWSL validation completed successfully.\n'
printf '  Tests build dir: %s\n' "$TEST_BUILD_DIR"
printf '  Device build dir: %s\n' "$DEVICE_BUILD_DIR"
printf '  Wayland build dir: %s\n' "$WAYLAND_BUILD_DIR"
