#!/bin/sh
# app_scripts (sub-slice 4d): the real program, headless, driven by scripted input (EXHAUST_SCRIPT, see src/main.c), checking what
# ends up on disk: the inline title renames a note on Enter and when it loses the focus, and dragging a note onto a folder moves it.
# Usage: app_scripts.sh <exhaust-binary>
exe="$1"
[ -x "$exe" ] || { echo "usage: app_scripts.sh <exhaust>"; exit 2; }
work=$(mktemp -d "${TMPDIR:-/tmp}/exhaust_app_scripts.XXXXXX") || exit 2
trap 'rm -rf "$work"' EXIT
fail=0
export SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software
export XDG_CONFIG_HOME="$work/cfg" XDG_DATA_HOME="$work/data" XDG_CACHE_HOME="$work/cache" HOME="$work/home"
mkdir -p "$work/cfg" "$work/data" "$work/cache" "$work/home"
# the program keeps its state in XDG folders on Linux and in $HOME/Library on a Mac, so a reset clears both
reset_state() { rm -rf "$work/data" "$work/cfg" "$work/cache" "$work/home"; mkdir -p "$work/cfg" "$work/data" "$work/cache" "$work/home"; }

run() {   # name script-text open-note
    vault="$work/$1"
    rm -rf "$vault"; mkdir -p "$vault/Box"
    printf '# Welcome\n\nHello.\n' > "$vault/Home.md"
    printf 'Alpha\n' > "$vault/Alpha.md"
    printf 'Inner\n' > "$vault/Box/Inner.md"
    printf '%s\n' "$2" > "$work/$1.script"
    if [ -n "$3" ]; then export EXHAUST_OPEN="$3"; else unset EXHAUST_OPEN; fi
    if command -v timeout >/dev/null 2>&1; then lim="timeout 60"; else lim=""; fi        # macOS has no timeout
    EXHAUST_SCRIPT="$work/$1.script" $lim "$exe" "$vault" >"$work/$1.log" 2>&1
}
check() {   # description file-that-must-exist file-that-must-not
    if [ -e "$2" ] && [ ! -e "$3" ]; then echo "ok: $1"; else echo "FAILED: $1"; fail=1; fi
}

# the title: click at the end of it, type, Enter
run title_enter 'wait 800
move 420 67
down 420 67
up 420 67
wait 200
text  Two
wait 200
key enter
wait 1500
quit' Home.md
check "the title renames the note on Enter" "$work/title_enter/Home Two.md" "$work/title_enter/Home.md"

# the title: type, then click in the text instead of pressing Enter
run title_blur 'wait 800
move 420 67
down 420 67
up 420 67
wait 200
text  Three
wait 200
move 500 400
down 500 400
up 500 400
wait 1500
quit' Home.md
check "the title renames the note when it loses the focus" "$work/title_blur/Home Three.md" "$work/title_blur/Home.md"

# the title refuses the characters a file name cannot have: typing "a/b:c" gives "abc"
run title_chars 'wait 800
move 420 67
down 420 67
up 420 67
wait 200
text :x/y#z
wait 200
key enter
wait 1500
quit' Home.md
check "the title does not take the characters a file name cannot have" "$work/title_chars/Homexyz.md" "$work/title_chars/Home.md"

# dragging a note onto a folder
run drag_in 'wait 800
move 100 112
down 100 112
move 100 105
move 100 95
move 100 86
wait 100
up 100 86
wait 1500
quit' ""
check "a note dragged onto a folder moves into it" "$work/drag_in/Box/Alpha.md" "$work/drag_in/Alpha.md"

# dragging a note out of a folder, onto the empty space
run drag_out 'wait 800
move 60 85
down 60 85
up 60 85
wait 300
move 110 112
down 110 112
move 110 150
move 110 300
move 110 400
wait 100
up 110 400
wait 1500
quit' ""
check "a note dragged out of a folder onto the empty space goes to the vault folder" "$work/drag_out/Inner.md" "$work/drag_out/Box/Inner.md"

# double click on the name of a folder renames it in place; on the name of a note it does not
run folder_rename 'wait 800
move 90 87
down 90 87
up 90 87
wait 100
down 90 87
up 90 87
wait 300
text Crate
key enter
wait 1500
quit' ""
check "double clicking a folder's name renames the folder" "$work/folder_rename/Crate/Inner.md" "$work/folder_rename/Box"
run note_no_rename 'wait 800
move 90 117
down 90 117
up 90 117
wait 100
down 90 117
up 90 117
wait 300
text Zed
key enter
wait 1000
quit' ""
check "double clicking a note's name does not rename it" "$work/note_no_rename/Alpha.md" "$work/note_no_rename/Zed.md"

# a click on the empty space below the rows selects nothing, so New folder makes a top-level folder
run top_level 'wait 800
move 90 87
down 90 87
up 90 87
wait 200
move 100 500
down 100 500
up 100 500
wait 200
move 93 53
down 93 53
up 93 53
wait 1000
quit' ""
check "New folder with nothing selected makes a top-level folder" "$work/top_level/Untitled folder" "$work/top_level/Box/Untitled folder"

# the sidebars are resized by dragging the strip at their edge, as far as the window allows
run sidebars 'wait 800
move 307 300
down 307 300
move 400 300
move 500 300
up 500 300
wait 300
shot SHOT1
move 797 300
down 797 300
move 600 300
move 20 300
up 20 300
wait 400
shot SHOT2
quit' ""
sed "s|SHOT1|$work/sb1.ppm|; s|SHOT2|$work/sb2.ppm|" "$work/sidebars.script" > "$work/sidebars.script.tmp" && mv "$work/sidebars.script.tmp" "$work/sidebars.script"      # no sed -i: the macOS sed wants a suffix after it
run sidebars "$(cat "$work/sidebars.script")" ""
widths=$(python3 - "$work/sb1.ppm" "$work/sb2.ppm" <<'PY'
import sys
def edge_x(path, y=300):
    d=open(path,'rb').read(); parts=d.split(b'\n',3); w,h=map(int,parts[1].split()); px=parts[3]
    row=px[y*w*3:(y+1)*w*3]
    return w,row
w,row=edge_x(sys.argv[1])
# the first frame: the dark sidebar background to the right of the ribbon line ends where the note column starts; count columns equal to the sidebar color
side=row[(100)*3:(100)*3+3]
n=100
while n<w and row[n*3:n*3+3]==side: n+=1
w2,row2=edge_x(sys.argv[2])
col=row2[(w2-100)*3:(w2-100)*3+3]
m=w2-100
while m>0 and row2[(m-1)*3:(m-1)*3+3]==col: m-=1
print(n, w2-m)
PY
)
set -- $widths
if [ -n "$1" ] && [ "$1" -gt 400 ] 2>/dev/null; then echo "ok: the left sidebar was dragged wider ($1 px)"; else echo "FAILED: the left sidebar was not resized ($widths)"; fail=1; fi
if [ -n "$2" ] && [ "$2" -gt 300 ] 2>/dev/null; then echo "ok: the right sidebar was dragged wider ($2 px)"; else echo "FAILED: the right sidebar was not resized ($widths)"; fail=1; fi

# the pane header (5d): the graph is opened after a note, back goes to the note, and the history is in the session; after a restart forward works
run history_walk 'wait 1000
move 21 53
down 21 53
up 21 53
wait 1000
move 326 50
down 326 50
up 326 50
wait 800
quit' Home.md
hsession=$(find "$work" -name session.ini -newer "$work/history_walk.script" | head -1)
if [ -n "$hsession" ] && grep -q "history_pos = 0" "$hsession" && grep -q "history_0 = Home.md|note|" "$hsession" && grep -q "history_1 = |graph|" "$hsession"; then echo "ok: back in the pane header walked from the graph to the note, and the history is saved"; else echo "FAILED: the pane history (back from the graph)"; fail=1; fi
vault="$work/history_walk"
printf '%s\n' 'wait 1000
move 352 50
down 352 50
up 352 50
wait 800
quit' > "$work/history_walk2.script"
if command -v timeout >/dev/null 2>&1; then lim="timeout 60"; else lim=""; fi
unset EXHAUST_OPEN
EXHAUST_SCRIPT="$work/history_walk2.script" $lim "$exe" "$vault" >"$work/history_walk2.log" 2>&1
if [ -n "$hsession" ] && grep -q "history_pos = 1" "$hsession"; then echo "ok: after a restart forward in the pane header walked on to the graph (the history was restored)"; else echo "FAILED: the pane history after a restart"; fail=1; fi

# the pane menu (5d): the three-dot button of the header; its entries do what they say
run menu_right 'wait 1000
move 777 50
down 777 50
up 777 50
wait 1000
move 790 80
down 790 80
up 790 80
wait 2000
quit' Home.md
msession=$(find "$work" -name session.ini -newer "$work/menu_right.script" | head -1)
groups=$(grep -c '^\[group\.' "$msession" 2>/dev/null)
if [ "${groups:-0}" -ge 2 ] 2>/dev/null; then echo "ok: Open to the right in the pane menu made a second pane"; else echo "FAILED: the pane menu's Open to the right ($groups panes)"; fail=1; fi
run menu_bookmark 'wait 1000
move 777 50
down 777 50
up 777 50
wait 400
move 790 210
down 790 210
up 790 210
wait 1000
quit' Home.md
if grep -rqs "Home.md" "$work/menu_bookmark/.exhaust/bookmarks.ini"; then echo "ok: Bookmark in the pane menu bookmarked the note"; else echo "FAILED: the pane menu's Bookmark"; fail=1; fi
run menu_close 'wait 1000
move 777 50
down 777 50
up 777 50
wait 1000
move 790 316
down 790 316
up 790 316
wait 2000
quit' Home.md
csession=$(find "$work" -name session.ini -newer "$work/menu_close.script" | head -1)
if [ -n "$csession" ] && ! grep -q "path = Home.md" "$csession"; then echo "ok: Close in the pane menu closed the tab"; else echo "FAILED: the pane menu's Close"; fail=1; fi

# a note in the right sidebar (5e): moved there from the pane menu, edited there and saved; after a restart it is still there and moves back
run sidebar_edit 'wait 1000
move 777 50
down 777 50
up 777 50
wait 800
move 790 277
down 790 277
up 790 277
wait 1200
move 900 400
down 900 400
up 900 400
text  Typed here
wait 2500
quit' Home.md
if grep -q "Typed here" "$work/sidebar_edit/Home.md"; then echo "ok: a note moved to the right sidebar takes typing and is saved to its file"; else echo "FAILED: typing in a note in the right sidebar"; fail=1; fi
ssession=$(find "$work" -name session.ini -newer "$work/sidebar_edit.script" | head -1)
rg=$(grep "^right_group = " "$ssession" 2>/dev/null | sed 's/.*= *//')
if [ -n "$rg" ] && sed -n "/^\[group\.$rg\]/,/^\[/p" "$ssession" | grep -q "^tabs = [0-9]"; then echo "ok: the right sidebar's group of tabs is in the session with its tab"; else echo "FAILED: the sidebar group in the session ($rg)"; fail=1; fi
vault="$work/sidebar_edit"
printf '%s\n' 'wait 1500
move 1083 86
down 1083 86
up 1083 86
wait 800
move 955 286
down 955 286
up 955 286
wait 1500
quit' > "$work/sidebar_back.script"
unset EXHAUST_OPEN
EXHAUST_SCRIPT="$work/sidebar_back.script" $lim "$exe" "$vault" >"$work/sidebar_back.log" 2>&1
if [ -n "$rg" ] && sed -n "/^\[group\.$rg\]/,/^\[/p" "$ssession" | grep -q "^tabs = *$"; then echo "ok: after a restart the note was still in the sidebar and moved back to the main area"; else echo "FAILED: moving the note back to the main area"; fail=1; fi

# a reading view of a note shows an edit made in an editor of the same note elsewhere (5e): reading on the left, the same note editing on the right
run reading_refresh "wait 1200
move 751 50
down 751 50
up 751 50
wait 600
move 777 50
down 777 50
up 777 50
wait 600
move 790 80
down 790 80
up 790 80
wait 1200
shot $work/read_before.ppm
move 650 300
down 650 300
up 650 300
text  REFRESHED
wait 1500
shot $work/read_after.ppm
quit" Home.md
changed=$(python3 - "$work/read_before.ppm" "$work/read_after.ppm" <<'PY'
import sys
def load(path):
    d=open(path,'rb').read(); parts=d.split(b'\n',3); w,h=map(int,parts[1].split()); return w,h,parts[3]
try:
    w,h,a=load(sys.argv[1]); w2,h2,b=load(sys.argv[2])
    n=0
    for y in range(215,255):                       # the reading page on the left, where the text of the note is
        for x in range(320,540):
            i=(y*w+x)*3
            if a[i:i+3]!=b[i:i+3]: n+=1
    print(n)
except Exception:
    print(-1)
PY
)
if [ "${changed:--1}" -gt 30 ] 2>/dev/null; then echo "ok: the reading view showed the edit made in the editor of the same note ($changed pixels changed)"; else echo "FAILED: the reading view did not show the edit ($changed)"; fail=1; fi

# a table command from the slash menu (5f): with the caret in the last cell, "Table: Add row below" adds an empty row under it, and the typed text lands in it
vault="$work/table_slash"; rm -rf "$vault"; mkdir -p "$vault/.exhaust"
printf '[features]\nslash_commands = true\n' > "$vault/.exhaust/settings.ini"
printf '# Tbl\n\n| Name | Qty |\n| :--- | --: |\n| a    | 1   |\n| bb   | 22  |\n\nafter\n' > "$vault/Table.md"
printf '%s\n' 'wait 1000
move 404 311
down 404 311
up 404 311
wait 300
text /
text tableaddrowbelow
wait 400
key enter
wait 600
text X
wait 2200
quit' > "$work/table_slash.script"
export EXHAUST_OPEN=Table.md
EXHAUST_SCRIPT="$work/table_slash.script" $lim "$exe" "$vault" >"$work/table_slash.log" 2>&1
unset EXHAUST_OPEN
if grep -qF '|      |    X |' "$vault/Table.md" && grep -qF '| bb   | 22  |' "$vault/Table.md" && grep -q '^after$' "$vault/Table.md" && ! grep -q '/table' "$vault/Table.md"; then echo "ok: the slash menu's Table: Add row below added a row and the typing went into it"; else echo "FAILED: table command from the slash menu"; cat "$vault/Table.md"; fail=1; fi

# the default note to open (7a, [files] default_file): a chosen note opens at start instead of the last session; `empty` starts with no note; a note that is not there gives the empty tab
vault="$work/default_file"; rm -rf "$vault"; mkdir -p "$vault/.exhaust"
printf '# Welcome\n' > "$vault/Home.md"; printf 'Alpha\n' > "$vault/Alpha.md"
printf 'wait 600\nmove 21 604\ndown 21 604\nup 21 604\nwait 1500\nquit\n' > "$work/default_file.script"       # the click on the sidebar button changes the layout, so the session is saved
unset EXHAUST_OPEN
printf '[files]\ndefault_file = note\ndefault_file_note = Alpha.md\n' > "$vault/.exhaust/settings.ini"
EXHAUST_SCRIPT="$work/default_file.script" $lim "$exe" "$vault" >"$work/default_file.log" 2>&1
dsession=$(find "$work" -name session.ini -newer "$work/default_file.script" | head -1)
if [ -n "$dsession" ] && grep -q "Alpha.md" "$dsession"; then echo "ok: default_file = note opens the chosen note at start"; else echo "FAILED: default_file = note"; fail=1; fi
printf '[files]\ndefault_file = empty\n' > "$vault/.exhaust/settings.ini"
export EXHAUST_OPEN=Home.md
EXHAUST_SCRIPT="$work/default_file.script" $lim "$exe" "$vault" >"$work/default_file2.log" 2>&1
unset EXHAUST_OPEN
grep -q "Home.md" "$dsession" || { echo "FAILED: the earlier session did not hold Home.md (the check below means nothing)"; fail=1; }
EXHAUST_SCRIPT="$work/default_file.script" $lim "$exe" "$vault" >"$work/default_file3.log" 2>&1
if ! grep -q "Home.md" "$dsession" && ! grep -q "Alpha.md" "$dsession"; then echo "ok: default_file = empty starts without the last session's notes"; else echo "FAILED: default_file = empty"; fail=1; fi

# hotkeys (7c): the default shortcut of Open settings (Ctrl+,) opens the settings tab; with a hotkeys.ini that gives the command another shortcut
# the old one does nothing and the new one works; the Hotkeys page's file is the one the program reads
vault="$work/hotkeys_run"; rm -rf "$vault"; mkdir -p "$vault/.exhaust"
printf '# Welcome\n' > "$vault/Home.md"
printf 'wait 600\nkey mod+comma\nwait 600\nmove 21 604\ndown 21 604\nup 21 604\nwait 1500\nquit\n' > "$work/hk_default.script"
printf 'wait 600\nkey mod+comma\nwait 300\nkey ctrl+alt+j\nwait 600\nmove 21 604\ndown 21 604\nup 21 604\nwait 1500\nquit\n' > "$work/hk_custom.script"
export EXHAUST_OPEN=Home.md
EXHAUST_SCRIPT="$work/hk_default.script" $lim "$exe" "$vault" >"$work/hk_default.log" 2>&1
hsession=$(find "$work" -name session.ini -newer "$work/hk_default.script" | head -1)
if [ -n "$hsession" ] && grep -q "view = settings" "$hsession"; then echo "ok: Ctrl+, opens the settings (the default shortcut)"; else echo "FAILED: the default shortcut Ctrl+,"; fail=1; fi
reset_state
printf '[hotkeys]\napp.open-settings = Ctrl+Alt+J\n' > "$vault/.exhaust/hotkeys.ini"
printf 'wait 600\nkey mod+comma\nwait 600\nmove 21 604\ndown 21 604\nup 21 604\nwait 1500\nquit\n' > "$work/hk_old.script"
EXHAUST_SCRIPT="$work/hk_old.script" $lim "$exe" "$vault" >"$work/hk_old.log" 2>&1
hsession=$(find "$work" -name session.ini -newer "$work/hk_old.script" | head -1)
if [ -n "$hsession" ] && ! grep -q "view = settings" "$hsession"; then echo "ok: a shortcut that was taken away does nothing"; else echo "FAILED: the old shortcut still works"; fail=1; fi
reset_state
EXHAUST_SCRIPT="$work/hk_custom.script" $lim "$exe" "$vault" >"$work/hk_custom.log" 2>&1
hsession=$(find "$work" -name session.ini -newer "$work/hk_custom.script" | head -1)
if [ -n "$hsession" ] && grep -q "view = settings" "$hsession"; then echo "ok: the shortcut from hotkeys.ini opens the settings"; else echo "FAILED: the shortcut from hotkeys.ini"; fail=1; fi
unset EXHAUST_OPEN

# file recovery (7e): the first save of a note keeps the text it had on disk; the history lists it, shows the changes, and Restore puts it back
# as an edit of the note (which the autosave then writes), after keeping the text that was there as one more snapshot
vault="$work/history_run"; rm -rf "$vault"; mkdir -p "$vault/.exhaust"
printf '# Welcome\n\nHello.\n' > "$vault/Home.md"
printf '[hotkeys]\nfile-recovery.open-local-history = Ctrl+Alt+H\n' > "$vault/.exhaust/hotkeys.ini"
printf 'wait 800\nmove 420 250\ndown 420 250\nup 420 250\nwait 200\ntext  typed more\nwait 2500\nquit\n' > "$work/hist_type.script"
printf 'wait 800\nkey ctrl+alt+h\nwait 800\nkey enter\nwait 1200\nmove 560 20\ndown 560 20\nup 560 20\nwait 2500\nquit\n' > "$work/hist_restore.script"
reset_state
export EXHAUST_OPEN=Home.md
EXHAUST_SCRIPT="$work/hist_type.script" $lim "$exe" "$vault" >"$work/hist_type.log" 2>&1
hfirst=$(find "$work" -name 'Home.md.*.snap' | head -1)
if grep -q "typed more" "$vault/Home.md" && [ -n "$hfirst" ] && [ "$(cat "$hfirst")" = "$(printf '# Welcome\n\nHello.')" ]; then echo "ok: the first save kept the text the note had before"; else echo "FAILED: no snapshot of the text before the first save"; fail=1; fi
EXHAUST_SCRIPT="$work/hist_restore.script" $lim "$exe" "$vault" >"$work/hist_restore.log" 2>&1
if [ "$(cat "$vault/Home.md")" = "$(printf '# Welcome\n\nHello.')" ]; then echo "ok: Restore put the earlier version back"; else echo "FAILED: Restore did not put the earlier version back"; fail=1; fi
if [ -n "$(find "$work" -name 'Home.md.*.snap' -exec grep -l "typed more" {} + 2>/dev/null)" ]; then echo "ok: the text that was replaced is in the history"; else echo "FAILED: the text a restore replaced was not kept"; fail=1; fi
unset EXHAUST_OPEN

# workspaces (7e): a layout saved under a name comes back after the tabs changed, and it is in workspaces.ini
vault="$work/layout_run"; rm -rf "$vault"; mkdir -p "$vault/.exhaust"
printf '# Welcome\n' > "$vault/Home.md"
printf '[features]\nworkspaces = true\n' > "$vault/.exhaust/settings.ini"
printf '[hotkeys]\nworkspaces.save-workspace-layout = Ctrl+Alt+W\nworkspaces.load-workspace-layout = Ctrl+Alt+L\n' > "$vault/.exhaust/hotkeys.ini"
printf 'wait 800\nkey ctrl+alt+w\nwait 500\ntext Writing\nwait 200\nkey enter\nwait 600\nmove 405 15\ndown 405 15\nup 405 15\nwait 500\nkey ctrl+alt+l\nwait 500\nkey enter\nwait 800\nmove 21 604\ndown 21 604\nup 21 604\nwait 1500\nquit\n' > "$work/layout.script"
printf 'wait 800\nmove 405 15\ndown 405 15\nup 405 15\nwait 500\nmove 21 604\ndown 21 604\nup 21 604\nwait 1500\nquit\n' > "$work/layout_control.script"
reset_state       # only this vault has a session folder, so the one find picks is the right one
export EXHAUST_OPEN=Home.md
EXHAUST_SCRIPT="$work/layout_control.script" $lim "$exe" "$vault" >"$work/layout_control.log" 2>&1
lsession=$(find "$work" -name session.ini | head -1)
lcontrol=$(grep -c '^\[tab\.' "$lsession" 2>/dev/null)
reset_state
EXHAUST_SCRIPT="$work/layout.script" $lim "$exe" "$vault" >"$work/layout.log" 2>&1
lsession=$(find "$work" -name session.ini | head -1)
ltabs=$(grep -c '^\[tab\.' "$lsession" 2>/dev/null)
if [ "$lcontrol" = "2" ] && [ "$ltabs" = "1" ]; then echo "ok: loading a saved layout brought back its one tab"; else echo "FAILED: loading a saved layout (tabs without a load: $lcontrol, with one: $ltabs)"; fail=1; fi
if grep -q '^\[workspace.Writing\]' "$vault/.exhaust/workspaces.ini" 2>/dev/null; then echo "ok: the layout is in workspaces.ini"; else echo "FAILED: workspaces.ini has no layout"; fail=1; fi
reset_state
unset EXHAUST_OPEN

# themes (7d): the theme file named in the settings colours the window, and a change to the file shows without a restart
vault="$work/theme_live"; rm -rf "$vault"; mkdir -p "$vault/.exhaust/themes"
printf '# Welcome\n' > "$vault/Home.md"
printf '[appearance]\ntheme = Rose\n' > "$vault/.exhaust/settings.ini"
printf '[theme]\nname = Rose\nbase = light\n[colors]\nbackground = #ffe0e6\n' > "$vault/.exhaust/themes/rose.theme"
printf 'wait 1500\nshot %s\nwait 2200\nshot %s\nquit\n' "$work/theme_a.ppm" "$work/theme_b.ppm" > "$work/theme_live.script"
( n=0; until [ -s "$work/theme_a.ppm" ] || [ $n -ge 100 ]; do sleep 0.1; n=$((n+1)); done; sleep 0.4; printf '[theme]\nname = Rose\nbase = light\n[colors]\nbackground = #e0ffe6\n' > "$vault/.exhaust/themes/rose.theme.tmp" && mv "$vault/.exhaust/themes/rose.theme.tmp" "$vault/.exhaust/themes/rose.theme" ) &
export EXHAUST_OPEN=Home.md
EXHAUST_SCRIPT="$work/theme_live.script" $lim "$exe" "$vault" >"$work/theme_live.log" 2>&1
unset EXHAUST_OPEN
wait
tcolors=$(python3 - "$work/theme_a.ppm" "$work/theme_b.ppm" <<'PY'
import sys
def px(path, x, y):
    d=open(path,'rb').read(); parts=d.split(b'\n',3); w,h=map(int,parts[1].split()); i=(y*w+x)*3; return tuple(parts[3][i:i+3])
try:
    print(px(sys.argv[1],600,500), px(sys.argv[2],600,500))
except Exception:
    print("none")
PY
)
if [ "$tcolors" = "(255, 224, 230) (224, 255, 230)" ]; then echo "ok: a theme file colours the window and a change to it shows within a second or two"; else echo "FAILED: the theme file's colours ($tcolors)"; fail=1; fi

# the shell (7f): zoom from the keyboard is written to settings.ini and changes the window at once; show_ribbon and ribbon_items hide the ribbon or some of
# its buttons; high_contrast = on gives the high contrast colours whatever theme is named; window_width and window_height give the size of the window
shellpx() {   # file x y: the pixel as "r,g,b" and the size of the picture as "WxH" when x is -1
python3 - "$1" "$2" "$3" <<'PY'
import sys
d=open(sys.argv[1],'rb').read(); parts=d.split(b'\n',3); w,h=map(int,parts[1].split()); x=int(sys.argv[2]); y=int(sys.argv[3])
if x < 0: print("%dx%d" % (w,h))
else:
    i=(y*w+x)*3; print("%d,%d,%d" % tuple(parts[3][i:i+3]))
PY
}
shellrow() {  # file y: the first x (from 3) whose pixel differs from the one at x=2, and, with a band, how many pixels in x 5..40 and y 10..130 differ from the background
python3 - "$1" "$2" <<'PY'
import sys
d=open(sys.argv[1],'rb').read(); parts=d.split(b'\n',3); w,h=map(int,parts[1].split()); y=int(sys.argv[2]); px=parts[3]
def at(x,yy): i=(yy*w+x)*3; return px[i:i+3]
bg=at(2,y); first=-1
for x in range(3,w):
    if at(x,y)!=bg: first=x; break
bg2=at(2,300); top=sum(1 for yy in range(10,130) for x in range(5,40) if at(x,yy)!=bg2); bottom=sum(1 for yy in range(680,720) for x in range(5,40) if at(x,yy)!=bg2)
print(first, top, bottom)
PY
}
vault="$work/shell_run"; rm -rf "$vault"; mkdir -p "$vault/.exhaust/themes"
printf '# Welcome\n' > "$vault/Home.md"
printf '[theme]\nname = Rose\nbase = light\n[colors]\nbackground = #ffe0e6\n' > "$vault/.exhaust/themes/rose.theme"
reset_state
export EXHAUST_OPEN=Home.md
printf 'wait 1200\nshot %s\nquit\n' "$work/shell_base.ppm" > "$work/shell_base.script"
EXHAUST_SCRIPT="$work/shell_base.script" $lim "$exe" "$vault" >"$work/shell_base.log" 2>&1
printf 'wait 800\nkey mod+=\nwait 300\nkey mod+=\nwait 1200\nshot %s\nquit\n' "$work/shell_zoom.ppm" > "$work/shell_zoom.script"
EXHAUST_SCRIPT="$work/shell_zoom.script" $lim "$exe" "$vault" >"$work/shell_zoom.log" 2>&1
set -- $(shellrow "$work/shell_base.ppm" 300); base_first=$1; base_top=$2; base_bottom=$3
set -- $(shellrow "$work/shell_zoom.ppm" 300); zoom_first=$1
if grep -q "zoom_percent = 125" "$vault/.exhaust/settings.ini" 2>/dev/null && [ "$zoom_first" -gt "$((base_first + 5))" ] 2>/dev/null; then echo "ok: Ctrl+= twice zoomed to 125 percent, written to settings.ini and drawn (ribbon edge at $base_first, then $zoom_first)"; else echo "FAILED: zoom from the keyboard (file: $(grep zoom "$vault/.exhaust/settings.ini" 2>/dev/null), ribbon edge $base_first then $zoom_first)"; fail=1; fi
rm -f "$vault/.exhaust/settings.ini"
printf '[interface]\nshow_ribbon = false\n' > "$vault/.exhaust/settings.ini"
EXHAUST_SCRIPT="$work/shell_base.script" $lim "$exe" "$vault" >"$work/shell_noribbon.log" 2>&1
set -- $(shellrow "$work/shell_base.ppm" 300); nr_first=$1
if [ "$nr_first" -gt 150 ] 2>/dev/null && [ "$base_first" -lt 60 ]; then echo "ok: show_ribbon = false hides the ribbon (the first edge moved from $base_first to $nr_first)"; else echo "FAILED: show_ribbon = false (edge $base_first, then $nr_first)"; fail=1; fi
printf '[interface]\nribbon_items = app.open-settings\n' > "$vault/.exhaust/settings.ini"
EXHAUST_SCRIPT="$work/shell_base.script" $lim "$exe" "$vault" >"$work/shell_items.log" 2>&1
set -- $(shellrow "$work/shell_base.ppm" 300); it_first=$1; it_top=$2; it_bottom=$3
if [ "$base_top" -gt 100 ] && [ "$it_top" -lt 5 ] && [ "$it_bottom" -gt 20 ] 2>/dev/null; then echo "ok: ribbon_items = app.open-settings keeps the settings button and drops the others ($base_top then $it_top pixels at the top, $it_bottom at the bottom)"; else echo "FAILED: ribbon_items (top $base_top then $it_top, bottom $it_bottom)"; fail=1; fi
printf '[appearance]\nhigh_contrast = on\ncolor_scheme = dark\ntheme = Rose\n' > "$vault/.exhaust/settings.ini"
printf 'wait 1500\nshot %s\nquit\n' "$work/shell_hc.ppm" > "$work/shell_hc.script"
EXHAUST_SCRIPT="$work/shell_hc.script" $lim "$exe" "$vault" >"$work/shell_hc.log" 2>&1
hcpx=$(shellpx "$work/shell_hc.ppm" 600 500)
printf '[appearance]\ncolor_scheme = dark\ntheme = Rose\n' > "$vault/.exhaust/settings.ini"
EXHAUST_SCRIPT="$work/shell_hc.script" $lim "$exe" "$vault" >"$work/shell_nohc.log" 2>&1
nohcpx=$(shellpx "$work/shell_hc.ppm" 600 500)
if [ "$hcpx" = "0,0,0" ] && [ "$nohcpx" = "255,224,230" ]; then echo "ok: high_contrast = on gives the high contrast colours and not the named theme's ($hcpx; without it $nohcpx)"; else echo "FAILED: high contrast ($hcpx, without it $nohcpx)"; fail=1; fi
printf '[appearance]\nwindow_width = 900\nwindow_height = 600\n' > "$vault/.exhaust/settings.ini"
EXHAUST_SCRIPT="$work/shell_base.script" $lim "$exe" "$vault" >"$work/shell_size.log" 2>&1
szpx=$(shellpx "$work/shell_base.ppm" -1 0)
if [ "$szpx" = "900x600" ]; then echo "ok: window_width and window_height give the size of the window"; else echo "FAILED: the window size ($szpx)"; fail=1; fi
unset EXHAUST_OPEN
reset_state

# the canvas (6a): a .canvas file opens as a board; a double click on empty space makes a text card, typing fills it, Escape ends the edit, and the autosave writes
# the file with the new card and every key the file had; a new canvas from the command palette is written when something is added; the view is kept in the session
canvas_vault() {   # name: a vault with Board.canvas (one card, a key Exhaust does not know) and a note
    vault="$work/$1"; rm -rf "$vault"; mkdir -p "$vault"
    printf '# Welcome\n' > "$vault/Home.md"
    printf '{\n\t"nodes":[\n\t\t{"id":"a1","type":"text","text":"First","x":-100,"y":-40,"width":250,"height":80,"zIndex":3}\n\t],\n\t"edges":[],\n\t"meta":{"keep":[1,2.50]}\n}' > "$vault/Board.canvas"
}
reset_state
canvas_vault canvas_edit
printf 'wait 1500\nmove 400 160\ndown 400 160\nup 400 160\nmove 400 160\ndown 400 160\nup 400 160\nwait 400\ntext Hello canvas\nwait 300\nkey escape\nwait 2200\nquit\n' > "$work/canvas_edit.script"
EXHAUST_OPEN=Board.canvas EXHAUST_SCRIPT="$work/canvas_edit.script" $lim "$exe" "$vault" >"$work/canvas_edit.log" 2>&1
if grep -q '"text":"Hello canvas"' "$vault/Board.canvas" && grep -q '"zIndex":3' "$vault/Board.canvas" && grep -q '"meta":{"keep":\[1,2.50\]}' "$vault/Board.canvas" && grep -q '"text":"First"' "$vault/Board.canvas"; then echo "ok: a double click made a text card, the text typed in it was saved, and the keys the file had were kept"; else echo "FAILED: the canvas edit ($(cat "$vault/Board.canvas" | tr '\n\t' '  '))"; fail=1; fi
# the view was written to the session
if [ -n "$(find "$work" -name session.ini -newer "$work/canvas_edit.script" -exec grep -l 'view\.Board\.canvas' {} + 2>/dev/null)" ]; then echo "ok: the view of the canvas is in the session"; else echo "FAILED: the view of the canvas is not in the session"; fail=1; fi
# a canvas that is not valid JSON is never changed, whatever is done in it
reset_state
canvas_vault canvas_broken
printf '{"nodes": [ not json' > "$vault/Board.canvas"
printf 'wait 1500\nmove 400 160\ndown 400 160\nup 400 160\nmove 400 160\ndown 400 160\nup 400 160\nwait 300\ntext x\nkey delete\nkey mod+a\nwait 2200\nquit\n' > "$work/canvas_broken.script"
EXHAUST_OPEN=Board.canvas EXHAUST_SCRIPT="$work/canvas_broken.script" $lim "$exe" "$vault" >"$work/canvas_broken.log" 2>&1
if [ "$(cat "$vault/Board.canvas")" = '{"nodes": [ not json' ]; then echo "ok: a canvas that is not JSON is left as it was"; else echo "FAILED: a broken canvas was changed ($(cat "$vault/Board.canvas"))"; fail=1; fi
# a new canvas from the command palette: nothing is written until something is added
reset_state
canvas_vault canvas_new
printf 'wait 1200\nkey mod+p\nwait 400\ntext create new canvas\nwait 400\nkey enter\nwait 1200\nmove 400 160\ndown 400 160\nup 400 160\nmove 400 160\ndown 400 160\nup 400 160\nwait 400\ntext Brand new\nwait 300\nkey escape\nwait 2200\nquit\n' > "$work/canvas_new.script"
EXHAUST_OPEN=Home.md EXHAUST_SCRIPT="$work/canvas_new.script" $lim "$exe" "$vault" >"$work/canvas_new.log" 2>&1
if [ -f "$vault/Untitled.canvas" ] && grep -q '"text":"Brand new"' "$vault/Untitled.canvas" && grep -q '"edges":\[\]' "$vault/Untitled.canvas"; then echo "ok: Create new canvas made Untitled.canvas, written when the first card was added"; else echo "FAILED: the new canvas ($(ls "$vault"); $(cat "$vault/Untitled.canvas" 2>/dev/null | tr '\n\t' '  '))"; fail=1; fi
reset_state

# bases (6b-iii): a .base file opens as a table and is never rewritten by opening it (comments, unknown keys and odd spacing stay byte for byte); Create new base
# from the command palette makes Untitled.base with one table view; a base block in a note is shown in reading mode without changing the note
bases_vault() {   # name: a vault with Notes.base (a comment, an unknown key) and a few notes with properties
    vault="$work/$1"; rm -rf "$vault"; mkdir -p "$vault"
    printf -- '---\nstatus: Done\nprice: 3\n---\n# One\n' > "$vault/One.md"
    printf -- '---\nstatus: Planned\nprice: 4\n---\n# Two\n' > "$vault/Two.md"
    printf '# a comment\nfilters:   file.ext == "md"\nviews:\n  - {type: table, name: "All  notes", order: [file.name, status, price]}\nkeep: [1,   2]\n' > "$vault/Notes.base"
    printf 'Text\n\n```base\nviews:\n  - type: list\n    order: [file.name]\n```\n' > "$vault/Embed.md"
}
reset_state
bases_vault bases_open
cp "$vault/Notes.base" "$work/bases_open.orig"; cp "$vault/Embed.md" "$work/bases_embed.orig"
printf 'wait 2500\nmove 400 160\nwheel 400 300 -3\nwait 500\nquit\n' > "$work/bases_open.script"
EXHAUST_OPEN=Notes.base EXHAUST_SCRIPT="$work/bases_open.script" $lim "$exe" "$vault" >"$work/bases_open.log" 2>&1
if cmp -s "$vault/Notes.base" "$work/bases_open.orig"; then echo "ok: a base that was only opened and scrolled is byte for byte as it was"; else echo "FAILED: opening a base changed it ($(cat "$vault/Notes.base"))"; fail=1; fi
reset_state
bases_vault bases_embed
printf 'wait 2500\nquit\n' > "$work/bases_embed.script"
EXHAUST_OPEN=Embed.md EXHAUST_SCRIPT="$work/bases_embed.script" $lim "$exe" "$vault" >"$work/bases_embed.log" 2>&1
if cmp -s "$vault/Embed.md" "$work/bases_embed.orig"; then echo "ok: a note with a base block is not changed by showing it"; else echo "FAILED: a note with a base block was changed"; fail=1; fi
reset_state
bases_vault bases_new
printf 'wait 1200\nkey mod+p\nwait 400\ntext create new base\nwait 400\nkey enter\nwait 1500\nquit\n' > "$work/bases_new.script"
EXHAUST_OPEN=One.md EXHAUST_SCRIPT="$work/bases_new.script" $lim "$exe" "$vault" >"$work/bases_new.log" 2>&1
if [ -f "$vault/Untitled.base" ] && grep -q 'type: table' "$vault/Untitled.base"; then echo "ok: Create new base made Untitled.base with a table view"; else echo "FAILED: the new base ($(ls "$vault"); $(cat "$vault/Untitled.base" 2>/dev/null | tr '\n' ' '))"; fail=1; fi
reset_state

# pictures (6c-i): a picture file opens in a tab of its own, is read and decoded on the service thread and is never changed; a file that is not a picture
# shows a message and the program goes on
images_dir=$(dirname "$0")/data/images
reset_state
vault="$work/images_open"; rm -rf "$vault"; mkdir -p "$vault"
printf '# Welcome\n' > "$vault/Home.md"
cp "$images_dir/pattern.png" "$vault/pic.png"; cp "$images_dir/pattern.avif" "$vault/pic.avif"; cp "$images_dir/shapes.svg" "$vault/logo.svg"
head -c 3000 /dev/urandom > "$vault/broken.png" 2>/dev/null || printf 'not a picture at all' > "$vault/broken.png"
cp "$vault/pic.png" "$work/pic.orig"; cp "$vault/broken.png" "$work/broken.orig"
printf 'wait 1500\nwheel 600 300 -2\nwait 800\nquit\n' > "$work/images_open.script"
for f in pic.png pic.avif logo.svg broken.png; do
    EXHAUST_OPEN=$f EXHAUST_SCRIPT="$work/images_open.script" $lim "$exe" "$vault" >"$work/images_open.log" 2>&1
    rc=$?
    if [ $rc -eq 0 ]; then echo "ok: $f opened in a tab and the program ended normally"; else echo "FAILED: opening $f ended with status $rc"; fail=1; fi
done
if cmp -s "$vault/pic.png" "$work/pic.orig" && cmp -s "$vault/broken.png" "$work/broken.orig"; then echo "ok: opening pictures does not change them"; else echo "FAILED: a picture file was changed by opening it"; fail=1; fi
if [ -n "$(find "$work" -name session.ini -exec grep -l 'image' {} + 2>/dev/null)" ]; then echo "ok: the picture tab is in the session"; else echo "FAILED: the picture tab is not in the session"; fail=1; fi
reset_state

# audio (6c-ii): an audio file opens in a tab, loads on the service thread, plays with Space on the dummy output driver and is never changed; a format the
# built-in decoders do not take says so
audio_dir=$(dirname "$0")/data/audio
reset_state
vault="$work/audio_open"; rm -rf "$vault"; mkdir -p "$vault"
printf '# Welcome\n' > "$vault/Home.md"
cp "$audio_dir/tone.flac" "$vault/a.flac"; cp "$audio_dir/tone.mp3" "$vault/a.mp3"; cp "$audio_dir/tone.ogg" "$vault/a.ogg"; cp "$audio_dir/tone16.wav" "$vault/a.wav"; printf 'not really audio' > "$vault/a.m4a"
cp "$vault/a.flac" "$work/a.flac.orig"
printf 'wait 1500\nkey space\nwait 500\nkey space\nwait 300\nquit\n' > "$work/audio_open.script"
for f in a.flac a.mp3 a.ogg a.wav a.m4a; do
    SDL_AUDIODRIVER=dummy EXHAUST_OPEN=$f EXHAUST_SCRIPT="$work/audio_open.script" $lim "$exe" "$vault" >"$work/audio_open.log" 2>&1
    rc=$?
    if [ $rc -eq 0 ]; then echo "ok: $f opened in a tab, was played and paused, and the program ended normally"; else echo "FAILED: opening $f ended with status $rc"; fail=1; fi
done
if cmp -s "$vault/a.flac" "$work/a.flac.orig"; then echo "ok: playing an audio file does not change it"; else echo "FAILED: an audio file was changed"; fail=1; fi
reset_state

# dropping a file into a note (formats.md, "Dropping files into a note"): the file from outside is copied into the vault, never replacing one, and a link is
# written in the note where it was dropped; with the attachment folder set the copy goes there; the dropped file is only read
reset_state
mkdir -p "$work/dropsrc"
printf 'PNGDATA-1234567890' > "$work/dropsrc/pic.png"; printf 'plain' > "$work/dropsrc/readme.txt"
run drop_attach "wait 1000
drop $work/dropsrc/pic.png 500 300
wait 1500
drop $work/dropsrc/pic.png 500 300
wait 1500
drop $work/dropsrc/readme.txt 500 300
wait 3000
quit" Home.md
if cmp -s "$work/dropsrc/pic.png" "$work/drop_attach/pic.png" && cmp -s "$work/dropsrc/pic.png" "$work/drop_attach/pic 2.png" && [ -e "$work/drop_attach/readme.txt" ]; then echo "ok: dropped files are copied into the vault and never replace each other"; else echo "FAILED: the dropped files were not copied (see $work/drop_attach.log)"; fail=1; fi
if grep -q '!\[\[pic.png\]\]' "$work/drop_attach/Home.md" && grep -q '!\[\[pic 2.png\]\]' "$work/drop_attach/Home.md" && grep -q '\[\[readme.txt\]\]' "$work/drop_attach/Home.md" && grep -q '^# Welcome' "$work/drop_attach/Home.md" && grep -q 'Hello\.' "$work/drop_attach/Home.md"; then echo "ok: the links are in the note and the rest of the note is as it was"; else echo "FAILED: the links are not in the note: $(cat "$work/drop_attach/Home.md")"; fail=1; fi
if [ "$(cat "$work/dropsrc/pic.png")" = "PNGDATA-1234567890" ]; then echo "ok: the dropped file itself is untouched"; else echo "FAILED: the dropped file was changed"; fail=1; fi
reset_state
run drop_folder "wait 100
quit" Home.md
vault="$work/drop_folder"
mkdir -p "$vault/.exhaust"
printf '[files]\nattachment_location = folder\nattachment_folder = Files/Pics\n' > "$vault/.exhaust/settings.ini"
printf 'wait 1000\ndrop %s/dropsrc/pic.png 500 300\nwait 3500\nquit\n' "$work" > "$work/drop_folder2.script"
rm -f "$vault/pic.png"
EXHAUST_OPEN=Home.md EXHAUST_SCRIPT="$work/drop_folder2.script" $lim "$exe" "$vault" >"$work/drop_folder2.log" 2>&1
if cmp -s "$work/dropsrc/pic.png" "$vault/Files/Pics/pic.png" && grep -q '!\[\[Files/Pics/pic.png\]\]' "$vault/Home.md"; then echo "ok: with an attachment folder the copy goes there and the link says where"; else echo "FAILED: the attachment folder was not used: $(cat "$vault/Home.md")"; fail=1; fi
reset_state

exit $fail
