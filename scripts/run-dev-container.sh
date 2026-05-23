#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE_TAG="${DEV_CONTAINER_IMAGE:-lofibox-zero/dev-container:trixie}"
BUILD_TYPE="${DEV_CONTAINER_BUILD_TYPE:-Debug}"
BUILD_VOLUME_NAME="${DEV_CONTAINER_BUILD_VOLUME:-lofibox-zero-dev-build}"
CONTAINER_BUILD_ROOT="${DEV_CONTAINER_BUILD_ROOT:-/container-build}"
CONTAINER_BUILD_DIR="${DEV_CONTAINER_BUILD_DIR:-$CONTAINER_BUILD_ROOT/device}"
MODE="${DEV_CONTAINER_MODE:-build}"
SKIP_IMAGE_BUILD="${DEV_CONTAINER_SKIP_BUILD:-0}"
PLATFORM="${DEV_CONTAINER_PLATFORM:-}"
MEDIA_ROOT_HOST="${DEV_CONTAINER_MEDIA_ROOT_HOST:-${LOFIBOX_MEDIA_ROOT:-}}"
MEDIA_ROOT_CONTAINER="${DEV_CONTAINER_MEDIA_ROOT_CONTAINER:-/media-library}"
RUNTIME_ROOT_HOST="${DEV_CONTAINER_RUNTIME_ROOT_HOST:-$ROOT_DIR/.tmp/dev-container-runtime}"
RUNTIME_STATE_CONTAINER="${DEV_CONTAINER_RUNTIME_STATE_CONTAINER:-/runtime/state}"
RUNTIME_CACHE_CONTAINER="${DEV_CONTAINER_RUNTIME_CACHE_CONTAINER:-/runtime/cache}"

require_command() {
  local command_name="$1"
  if command -v "$command_name" >/dev/null 2>&1; then
    return 0
  fi

  printf 'Missing required command: %s\n' "$command_name" >&2
  if [[ "$command_name" == "docker" ]]; then
    printf 'This workflow expects Docker to be available inside WSL.\n' >&2
    printf 'Enable Docker Desktop WSL integration for your distro, then rerun the script.\n' >&2
  fi
  exit 1
}

ensure_docker_available() {
  if docker version >/dev/null 2>&1; then
    return 0
  fi

  printf 'Docker is installed but not usable from the current environment.\n' >&2
  printf 'On Windows + WSL, enable Docker Desktop WSL integration for your distro first.\n' >&2
  exit 1
}

print_step() {
  local message="$1"
  printf '\n==> %s\n' "$message"
}

device_args=()
while (( $# > 0 )); do
  case "$1" in
    --shell)
      MODE="shell"
      shift
      ;;
    --device)
      MODE="device"
      shift
      ;;
    --skip-image-build)
      SKIP_IMAGE_BUILD=1
      shift
      ;;
    --)
      shift
      device_args=("$@")
      break
      ;;
    *)
      device_args+=("$1")
      shift
      ;;
  esac
done

require_command docker
ensure_docker_available

mkdir -p "$RUNTIME_ROOT_HOST"

if (( ! SKIP_IMAGE_BUILD )); then
  "$ROOT_DIR/scripts/build-dev-container.sh"
fi

for environment_name in ACOUSTID_API_KEY ACOUSTID_CLIENT_KEY FPCALC_PATH; do
  if [[ -n "${!environment_name:-}" ]]; then
    GUI_ARGS+=(-e "${environment_name}=${!environment_name}")
  fi
done

RUN_ARGS=(
  run
  --rm
  -v "$ROOT_DIR:/workspace/LoFiBox-Zero"
  -v "$BUILD_VOLUME_NAME:$CONTAINER_BUILD_ROOT"
  --mount "type=bind,source=$RUNTIME_ROOT_HOST,target=/runtime,readonly=false"
  -w /workspace/LoFiBox-Zero
  -e "LOFIBOX_CONTAINER_BUILD_TYPE=$BUILD_TYPE"
  -e "LOFIBOX_CONTAINER_BUILD_DIR=$CONTAINER_BUILD_DIR"
  -e "DEV_CONTAINER_MODE=$MODE"
  -e "XDG_STATE_HOME=$RUNTIME_STATE_CONTAINER"
  -e "XDG_CACHE_HOME=$RUNTIME_CACHE_CONTAINER"
  -e "LOFIBOX_RUNTIME_LOG_PATH=$RUNTIME_STATE_CONTAINER/lofibox-zero/runtime.log"
)

if [[ -n "$MEDIA_ROOT_HOST" && -d "$MEDIA_ROOT_HOST" ]]; then
  RUN_ARGS+=(
    --mount "type=bind,source=$MEDIA_ROOT_HOST,target=$MEDIA_ROOT_CONTAINER"
    -e "LOFIBOX_MEDIA_ROOT=$MEDIA_ROOT_CONTAINER"
  )
fi

if [[ "$MODE" == "shell" ]]; then
  RUN_ARGS+=(-it)
elif [[ -t 0 && -t 1 ]]; then
  RUN_ARGS+=(-it)
fi

if command -v id >/dev/null 2>&1 && [[ "$ROOT_DIR" != /mnt/* ]]; then
  RUN_ARGS+=(--user "$(id -u):$(id -g)")
fi

if [[ -n "$PLATFORM" ]]; then
  RUN_ARGS+=(--platform "$PLATFORM")
fi

print_step "Launching development container in $MODE mode"

if [[ "$MODE" == "shell" ]]; then
  docker "${RUN_ARGS[@]}" "$IMAGE_TAG" bash
  exit 0
fi

docker "${RUN_ARGS[@]}" "$IMAGE_TAG" bash -lc '
  set -euo pipefail
  cmake -S . -B "$LOFIBOX_CONTAINER_BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE="$LOFIBOX_CONTAINER_BUILD_TYPE" \
    -DLOFIBOX_BUILD_DEVICE=ON \
    -DLOFIBOX_BUILD_WAYLAND=OFF \
    -DBUILD_TESTING=OFF
  cmake --build "$LOFIBOX_CONTAINER_BUILD_DIR" --target lofibox_zero_device --config "$LOFIBOX_CONTAINER_BUILD_TYPE"
  if [[ "${DEV_CONTAINER_MODE:-build}" == "build" ]]; then
    exit 0
  fi
  exec "$LOFIBOX_CONTAINER_BUILD_DIR/lofibox_zero_device" "$@"
' bash "${device_args[@]}"
