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

exit $fail
