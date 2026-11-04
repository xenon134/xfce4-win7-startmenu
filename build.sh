#!/usr/bin/env bash
#
# Build the patched whisker menu in a throwaway podman container.
# The plugin lands in ./out/libwhiskermenu.so.
#
#   ./build.sh          incremental build
#   ./build.sh clean    remove build artifacts first
#   ./build.sh image    force-rebuild the dependency image

set -euo pipefail

HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
IMAGE="xfce4-whiskermenu-build:2.10"

command -v podman >/dev/null || { echo "error: podman missing" >&2; exit 1; }

export TMPDIR=/mnt/podman-store/tmp
mkdir -p "$TMPDIR"

case "${1:-}" in
    clean)
        rm -rf "$HERE/build"
        echo "cleaned"
        exit 0
        ;;
    image)
        podman rmi -f "$IMAGE" 2>/dev/null || true
        ;;
esac

if ! podman image exists "$IMAGE"; then
    echo ">> building dependency image (first run only)"
    podman build --network=host -t "$IMAGE" "$HERE"
fi

mkdir -p "$HERE/out"
podman run --rm --userns=keep-id --network=host \
    -v "$HERE:/src" -w /src \
    "$IMAGE" \
    bash -euc '
        if [ ! -d build ]; then
            meson setup build --prefix=/usr --libdir=lib/x86_64-linux-gnu
        fi
        ninja -C build
    '

cp -f "$HERE/build/panel-plugin/libwhiskermenu.so" "$HERE/out/libwhiskermenu.so"
echo
echo "built: $HERE/out/libwhiskermenu.so"
file "$HERE/out/libwhiskermenu.so"
