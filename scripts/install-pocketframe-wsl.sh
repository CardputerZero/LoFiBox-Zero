#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${LOFIBOX_POCKETFRAME_BUILD_DIR:-$ROOT_DIR/build/pocketframe-wsl}"
INSTALL_PREFIX="${LOFIBOX_POCKETFRAME_INSTALL_PREFIX:-$HOME/.local/lofibox-zero}"
BIN_DIR="$INSTALL_PREFIX/bin"
ASSET_DIR="$INSTALL_PREFIX/assets"
STATE_DIR="${LOFIBOX_POCKETFRAME_STATE_DIR:-$HOME/.local/state/lofibox-zero}"
CACHE_DIR="${LOFIBOX_POCKETFRAME_CACHE_DIR:-$HOME/.cache/lofibox-zero}"

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE="${LOFIBOX_POCKETFRAME_BUILD_TYPE:-Debug}" \
  -DLOFIBOX_BUILD_X11=ON \
  -DLOFIBOX_BUILD_DEVICE=OFF \
  -DLOFIBOX_BUILD_WAYLAND=OFF \
  -DBUILD_TESTING=OFF
cmake --build "$BUILD_DIR" --target lofibox_zero_x11 -j "${LOFIBOX_POCKETFRAME_BUILD_JOBS:-4}"

mkdir -p "$BIN_DIR" "$ASSET_DIR" "$HOME/.local/bin" "$STATE_DIR" "$CACHE_DIR"
cp "$BUILD_DIR/lofibox-x11" "$BIN_DIR/lofibox-zero-x11.new"
mv -f "$BIN_DIR/lofibox-zero-x11.new" "$BIN_DIR/lofibox-zero-x11"
rm -rf "$ASSET_DIR/ui"
cp -R "$ROOT_DIR/assets/ui" "$ASSET_DIR/ui"

cat > "$HOME/.local/bin/lofibox-zero" <<EOF
#!/usr/bin/env bash
set -euo pipefail
export LOFIBOX_ASSET_DIR="$ASSET_DIR"
export XDG_STATE_HOME="$STATE_DIR"
export XDG_CACHE_HOME="$CACHE_DIR"
exec "$BIN_DIR/lofibox-zero-x11" "\$@"
EOF
chmod +x "$HOME/.local/bin/lofibox-zero" "$BIN_DIR/lofibox-zero-x11"

printf 'LoFiBox Zero installed for PocketFrame/VNC.\n'
printf '  Launcher: %s\n' "$HOME/.local/bin/lofibox-zero"
printf '  Binary:   %s\n' "$BIN_DIR/lofibox-zero-x11"
printf '  Assets:   %s\n' "$ASSET_DIR"
