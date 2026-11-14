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

exit $fail
