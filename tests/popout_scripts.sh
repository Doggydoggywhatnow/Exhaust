#!/bin/sh
# popout_windows (sub-slice 7g): the real program, headless, with a pop-out window: "Open in new window" opens a second window with the same
# note, an edit made in the second window shows in the first, the close button of the second window returns its tab, a window left open comes
# back with the next start, and a new window has the size of a new window (7f).
# The script command `window N` sends the following events to the N-th pop-out (0 is the main window) and takes `shot` from it.
# Usage: popout_scripts.sh <exhaust-binary>
exe="$1"
[ -x "$exe" ] || { echo "usage: popout_scripts.sh <exhaust>"; exit 2; }
work=$(mktemp -d "${TMPDIR:-/tmp}/exhaust_popout.XXXXXX") || exit 2
trap 'rm -rf "$work"' EXIT
fail=0
export SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software
export XDG_CONFIG_HOME="$work/cfg" XDG_DATA_HOME="$work/data" XDG_CACHE_HOME="$work/cache" HOME="$work/home"
reset_state() { rm -rf "$work/data" "$work/cfg" "$work/cache" "$work/home"; mkdir -p "$work/cfg" "$work/data" "$work/cache" "$work/home"; }
if command -v timeout >/dev/null 2>&1; then lim="timeout 90"; else lim=""; fi
reset_state

# a picture's size, and how many pixels of one picture differ from the other in a rectangle
px() {   # file: "WxH"
python3 - "$1" <<'PY'
import sys
d=open(sys.argv[1],'rb').read(); parts=d.split(b'\n',3); w,h=map(int,parts[1].split()); print("%dx%d" % (w,h))
PY
}
diffcount() {   # fileA fileB x0 y0 x1 y1
python3 - "$@" <<'PY'
import sys
def load(p):
    d=open(p,'rb').read(); parts=d.split(b'\n',3); w,h=map(int,parts[1].split()); return w,h,parts[3]
w,h,a=load(sys.argv[1]); w2,h2,b=load(sys.argv[2])
x0,y0,x1,y1=map(int,sys.argv[3:7])
n=0
for y in range(y0,min(y1,h,h2)):
    for x in range(x0,min(x1,w,w2)):
        if a[(y*w+x)*3:(y*w+x)*3+3]!=b[(y*w2+x)*3:(y*w2+x)*3+3]: n+=1
print(n)
PY
}
windows() { grep -c '^\[window\.' "$1" 2>/dev/null; }
tabs() { grep -c '^\[tab\.' "$1" 2>/dev/null; }

vault="$work/pop_run"; rm -rf "$vault"; mkdir -p "$vault/.exhaust"
printf '# Welcome\n\nHello.\n' > "$vault/Home.md"
printf '# Other\n' > "$vault/Other.md"
export EXHAUST_OPEN=Home.md

# open the note to the right, then the right pane in a new window; type in the new window; look at both
cat > "$work/pop_edit.script" <<SCRIPT
wait 800
move 777 49
down 777 49
up 777 49
wait 500
move 875 81
down 875 81
up 875 81
wait 800
shot $work/pop_before.ppm
move 777 49
down 777 49
up 777 49
wait 500
move 887 111
down 887 111
up 887 111
wait 1500
window 1
shot $work/pop_win.ppm
move 300 300
down 300 300
up 300 300
text  typed in the second window
wait 2500
window 0
shot $work/pop_after.ppm
quit
SCRIPT
EXHAUST_SCRIPT="$work/pop_edit.script" $lim "$exe" "$vault" >"$work/pop_edit.log" 2>&1
session=$(find "$work" -name session.ini | head -1)
if [ "$(windows "$session")" = "2" ]; then echo "ok: Open in new window made a second window"; else echo "FAILED: no second window in the session ($(windows "$session") windows)"; fail=1; fi
size=$(px "$work/pop_win.ppm" 2>/dev/null)
if [ "$size" = "1100x750" ] && grep -q '^width = 1100' "$session"; then echo "ok: the new window has the size of a new window, not the 800 by 600 of the workspace model"; else echo "FAILED: the size of the new window ($size)"; fail=1; fi
if grep -q "typed in the second window" "$vault/Home.md"; then echo "ok: text typed in the second window went into the note"; else echo "FAILED: nothing typed in the second window reached the note"; fail=1; fi
changed=$(diffcount "$work/pop_before.ppm" "$work/pop_after.ppm" 311 80 560 400)
if [ "${changed:-0}" -gt 100 ] 2>/dev/null; then echo "ok: the edit made in the second window shows in the first ($changed pixels changed)"; else echo "FAILED: the first window did not show the edit ($changed)"; fail=1; fi

# the second window is still open when the program ends: it comes back with the next start, at the size it had
cat > "$work/pop_restore.script" <<SCRIPT
wait 1500
window 1
shot $work/pop_restored.ppm
window 0
shot $work/pop_main.ppm
quit
SCRIPT
EXHAUST_SCRIPT="$work/pop_restore.script" $lim "$exe" "$vault" >"$work/pop_restore.log" 2>&1
rsize=$(px "$work/pop_restored.ppm" 2>/dev/null)
if [ "$rsize" = "1100x750" ] && [ "$(windows "$session")" = "2" ]; then echo "ok: the session restored both windows"; else echo "FAILED: the second window did not come back ($rsize, $(windows "$session") windows)"; fail=1; fi
bg=$(python3 - "$work/pop_restored.ppm" <<'PY'
import sys
d=open(sys.argv[1],'rb').read(); parts=d.split(b'\n',3); w,h=map(int,parts[1].split()); px=parts[3]
n=0
for y in range(80,300):
    for x in range(10,400):
        if px[(y*w+x)*3:(y*w+x)*3+3]!=px[(400*w+600)*3:(400*w+600)*3+3]: n+=1
print(n)
PY
)
if [ "${bg:-0}" -gt 200 ] 2>/dev/null; then echo "ok: the restored window shows the note"; else echo "FAILED: the restored window is empty ($bg)"; fail=1; fi

# the close button of the second window gives its tab back to the main window
cat > "$work/pop_close.script" <<SCRIPT
wait 1500
closewindow 1
wait 1500
move 21 604
down 21 604
up 21 604
wait 1500
quit
SCRIPT
EXHAUST_SCRIPT="$work/pop_close.script" $lim "$exe" "$vault" >"$work/pop_close.log" 2>&1
if [ "$(windows "$session")" = "1" ] && [ "$(tabs "$session")" = "2" ]; then echo "ok: closing the second window returned its tab to the main window"; else echo "FAILED: closing the second window ($(windows "$session") windows, $(tabs "$session") tabs)"; fail=1; fi

# a tab dragged out of the window has a window of its own
reset_state
rm -f "$vault/.exhaust/settings.ini"
cat > "$work/pop_drag.script" <<SCRIPT
wait 800
move 343 15
down 343 15
move 200 300
move 150 300
up 150 300
wait 1500
move 21 604
down 21 604
up 21 604
wait 1500
quit
SCRIPT
EXHAUST_SCRIPT="$work/pop_drag.script" $lim "$exe" "$vault" >"$work/pop_drag.log" 2>&1
session=$(find "$work" -name session.ini | head -1)
if [ "$(windows "$session")" = "2" ]; then echo "ok: a tab dragged out of the window opened a window"; else echo "FAILED: dragging a tab out ($(windows "$session") windows)"; fail=1; fi
unset EXHAUST_OPEN
exit $fail
