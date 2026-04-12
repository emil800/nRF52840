#!/usr/bin/env bash
# Build this Zephyr app and copy zephyr.uf2 to the Seeed XIAO UF2 bootloader volume.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

BOARD="${BOARD:-xiao_ble/nrf52840}"
BUILD_DIR="${BUILD_DIR:-${ROOT}/build}"
UF2_VOLUME="${UF2_VOLUME:-/Volumes/XIAO-SENSE}"

PRISTINE=false
BUILD_ONLY=false

usage() {
	cat <<'EOF'
Usage: build_flash.sh [--pristine] [--build-only]

  --pristine, -p   Run a pristine west build
  --build-only, -b Only build; do not copy UF2

Environment:
  BOARD         west board id (default: xiao_ble/nrf52840)
  BUILD_DIR     west build directory (default: <repo>/build)
  UF2_VOLUME    mount path of the UF2 drive (default: /Volumes/XIAO-SENSE)
  ZEPHYR_BASE   Zephyr tree; if unset and ~/ncs/zephyr exists, it is set automatically
EOF
}

while [[ $# -gt 0 ]]; do
	case "$1" in
	--pristine | -p)
		PRISTINE=true
		shift
		;;
	--build-only | -b)
		BUILD_ONLY=true
		shift
		;;
	-h | --help)
		usage
		exit 0
		;;
	*)
		echo "Unknown option: $1" >&2
		usage >&2
		exit 1
		;;
	esac
done

if [[ -z "${ZEPHYR_BASE:-}" ]] && [[ -d "${HOME}/ncs/zephyr" ]]; then
	export ZEPHYR_BASE="${HOME}/ncs/zephyr"
fi

if ! command -v west >/dev/null 2>&1; then
	echo "error: west not in PATH. Open an nRF Connect SDK / Zephyr shell first." >&2
	exit 1
fi

if $PRISTINE; then
	west build --pristine -b "$BOARD" -d "$BUILD_DIR" .
else
	west build -b "$BOARD" -d "$BUILD_DIR" .
fi

resolve_uf2() {
	local c
	for c in \
		"${BUILD_DIR}/nRF52840/zephyr/zephyr.uf2" \
		"${BUILD_DIR}/zephyr/zephyr.uf2"; do
		if [[ -f "$c" ]]; then
			echo "$c"
			return 0
		fi
	done
	return 1
}

if $BUILD_ONLY; then
	if UF2="$(resolve_uf2)"; then
		echo "Built UF2: $UF2"
	else
		echo "Build finished but no zephyr.uf2 found under ${BUILD_DIR}." >&2
		exit 1
	fi
	exit 0
fi

if ! UF2="$(resolve_uf2)"; then
	echo "error: zephyr.uf2 not found under ${BUILD_DIR} (sysbuild vs single-image layout?)." >&2
	exit 1
fi

if [[ ! -d "$UF2_VOLUME" ]]; then
	echo "error: UF2 volume not mounted: $UF2_VOLUME" >&2
	echo "Double-tap RESET on the XIAO until the drive appears, then re-run." >&2
	echo "If your Mac shows a different volume name, set UF2_VOLUME and try again." >&2
	exit 1
fi

echo "Copying $(basename "$UF2") to $UF2_VOLUME ..."
cp "$UF2" "$UF2_VOLUME/"
echo "Done."
