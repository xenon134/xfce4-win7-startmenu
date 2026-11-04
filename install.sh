#!/usr/bin/env bash
#
# Install the patched Whisker Menu for the current user, without root.
#
# Xfce Panel loads user plugin modules from
#   ${XDG_DATA_HOME:-~/.local/share}/../lib/<multiarch>/xfce4/panel/plugins
# before the system copies.  The matching .desktop file goes in
#   ${XDG_DATA_HOME:-~/.local/share}/xfce4/panel/plugins
# so the panel picks up this build instead of /usr/lib/.../libwhiskermenu.so.
#
#   ./install.sh            install (build first if out/ is missing)
#   ./install.sh --restart  install and restart the panel if DISPLAY is set
#
set -euo pipefail

HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
RESTART=0
if [ "${1:-}" = "--restart" ]; then
    RESTART=1
fi

LIBDIR="$(pkg-config --variable=libdir libxfce4panel-2.0 2>/dev/null || true)"
PREFIX="$(pkg-config --variable=prefix libxfce4panel-2.0 2>/dev/null || true)"
if [ -z "$LIBDIR" ] || [ -z "$PREFIX" ]; then
    # The runtime package does not ship a .pc file. Derive the multiarch libdir
    # the same way the panel does on Debian/Ubuntu.
    MULTIARCH=""
    if command -v gcc >/dev/null; then
        MULTIARCH="$(gcc -print-multiarch 2>/dev/null || true)"
    fi
    if [ -z "$MULTIARCH" ]; then
        MULTIARCH="$(uname -m)-linux-gnu"
    fi
    LIBDIR="/usr/lib/$MULTIARCH"
    PREFIX="/usr"
fi

LIB_SUFFIX="${LIBDIR#"$PREFIX"}"
DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"
USER_PREFIX="${DATA_HOME%/share}"
PLUGIN_LIBDIR="$USER_PREFIX$LIB_SUFFIX/xfce4/panel/plugins"
PLUGIN_DATADIR="$DATA_HOME/xfce4/panel/plugins"
SO="$HERE/out/libwhiskermenu.so"

if [ ! -f "$SO" ]; then
    echo ">> out/libwhiskermenu.so not found, building first"
    "$HERE/build.sh"
fi

DESKTOP="/usr/share/xfce4/panel/plugins/whiskermenu.desktop"
if [ ! -f "$DESKTOP" ]; then
    echo "error: missing $DESKTOP" >&2
    exit 1
fi

install -d "$PLUGIN_LIBDIR" "$PLUGIN_DATADIR"
# Replace atomically so a running panel keeps its mapped copy until restart.
install -m 755 "$SO" "$PLUGIN_LIBDIR/.libwhiskermenu.so.tmp"
mv -f "$PLUGIN_LIBDIR/.libwhiskermenu.so.tmp" "$PLUGIN_LIBDIR/libwhiskermenu.so"
install -m 644 "$DESKTOP" "$PLUGIN_DATADIR/whiskermenu.desktop"

echo "installed plugin:   $PLUGIN_LIBDIR/libwhiskermenu.so"
echo "installed metadata: $PLUGIN_DATADIR/whiskermenu.desktop"
echo
echo "The user copy takes precedence over /usr/lib/.../libwhiskermenu.so."

if [ "$RESTART" -eq 1 ]; then
    if [ -n "${DISPLAY:-}" ]; then
        xfce4-panel -r
        echo "panel restarted"
    else
        echo "DISPLAY is not set; restart the panel manually: xfce4-panel -r"
    fi
else
    echo "Restart the panel to load it: xfce4-panel -r"
fi

echo
echo "Background darkness (0.0 transparent .. 1.0 opaque, default 0.8):"
echo "  xfconf-query -c xfce4-panel -p /plugins/plugin-11/background-alpha -t double -s 0.8"
echo "If the property does not exist yet, add -n to create it."
