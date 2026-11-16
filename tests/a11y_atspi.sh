#!/bin/sh
# a11y_atspi (sub-slice 7h, Linux): Exhaust with the real AT-SPI adapter and a client that reads it as a screen reader does. Needs dbus, at-spi2-core and
# the AT-SPI client library; the CI job installs them. Usage: a11y_atspi.sh <exhaust> <client>
exe="$1"; client="$2"
[ -x "$exe" ] && [ -x "$client" ] || { echo "usage: a11y_atspi.sh <exhaust> <client>"; exit 2; }
if [ -z "$EXHAUST_ATSPI_INNER" ]; then
    command -v dbus-run-session >/dev/null 2>&1 || { echo "dbus-run-session is missing"; exit 1; }
    EXHAUST_ATSPI_INNER=1 exec dbus-run-session -- sh "$0" "$@"
fi
work=$(mktemp -d "${TMPDIR:-/tmp}/exhaust_atspi.XXXXXX") || exit 2
cleanup() { kill $exhaust_pid $launcher_pid 2>/dev/null; rm -rf "$work"; }
trap cleanup EXIT
export XDG_CONFIG_HOME="$work/cfg" XDG_DATA_HOME="$work/data" XDG_CACHE_HOME="$work/cache" HOME="$work/home"
mkdir -p "$work/cfg" "$work/data" "$work/cache" "$work/home" "$work/vault/.exhaust"
printf '# Welcome\n\nHello.\n' > "$work/vault/Home.md"
launcher=""
for p in /usr/libexec/at-spi-bus-launcher /usr/lib/at-spi2-core/at-spi-bus-launcher /usr/lib/x86_64-linux-gnu/at-spi2-core/at-spi-bus-launcher /usr/lib/at-spi2/at-spi-bus-launcher; do [ -x "$p" ] && launcher="$p" && break; done
[ -n "$launcher" ] || { echo "at-spi-bus-launcher was not found (install at-spi2-core)"; exit 1; }
"$launcher" --launch-immediately >"$work/launcher.log" 2>&1 &
launcher_pid=$!
n=0; until dbus-send --session --print-reply --dest=org.freedesktop.DBus /org/freedesktop/DBus org.freedesktop.DBus.NameHasOwner string:org.a11y.Bus 2>/dev/null | grep -q "boolean true"; do n=$((n+1)); [ $n -ge 50 ] && { echo "the accessibility bus did not start"; cat "$work/launcher.log"; exit 1; }; sleep 0.2; done
# a screen reader is on: this is what Orca does
dbus-send --session --print-reply --dest=org.a11y.Bus /org/a11y/bus org.freedesktop.DBus.Properties.Set string:org.a11y.Status string:IsEnabled variant:boolean:true >/dev/null 2>&1
dbus-send --session --print-reply --dest=org.a11y.Bus /org/a11y/bus org.freedesktop.DBus.Properties.Set string:org.a11y.Status string:ScreenReaderEnabled variant:boolean:true >/dev/null 2>&1
printf 'wait 90000\nquit\n' > "$work/idle.script"
SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software EXHAUST_A11Y_ADAPTER=1 EXHAUST_OPEN=Home.md EXHAUST_SCRIPT="$work/idle.script" "$exe" "$work/vault" >"$work/exhaust.log" 2>&1 &
exhaust_pid=$!
timeout 120 "$client"
rc=$?
if [ $rc -ne 0 ]; then echo "--- exhaust log"; cat "$work/exhaust.log"; echo "--- launcher log"; cat "$work/launcher.log"; fi
exit $rc
