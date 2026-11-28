#!/bin/sh
# hunt_static.sh: a list of patterns in the sources that have been bugs in C programs like this one. It finds candidates, not bugs:
# read each hit. Usage: tests/hunt/hunt_static.sh [--strict] [repository-root]
#   without --strict the script prints its report and exits 0; with --strict it exits 1 when a hard rule of AGENTS.md is broken
#   (a network call, a GPL header, SDL_Delay in src/ui, a POSIX-only call outside src/platform).
# The report is also written to hunt_work/static_audit.txt below the current folder.
strict=0
if [ "$1" = "--strict" ]; then strict=1; shift; fi
root="${1:-.}"
cd "$root" || exit 2
mkdir -p hunt_work
out=hunt_work/static_audit.txt
: > "$out"
hard=0

say() { printf '%s\n' "$*" | tee -a "$out"; }
# section NAME HARD REGEX [DIR...]: lists lines of src/ (and include/) matching REGEX, outside third_party
section() {
    name="$1"; is_hard="$2"; re="$3"; shift 3
    dirs="${*:-src include tools}"
    say ""
    say "== $name"
    hits=$(grep -rnE --include='*.c' --include='*.h' --include='*.m' "$re" $dirs 2>/dev/null | grep -v '^third_party' | grep -vE '^[^:]*:[0-9]+:[[:space:]]*(/\*|\*|//)' )
    if [ -z "$hits" ]; then say "   (none)"; return; fi
    n=$(printf '%s\n' "$hits" | wc -l)
    say "   $n hits"
    printf '%s\n' "$hits" | head -60 | sed 's/^/   /' | tee -a "$out" >/dev/null
    printf '%s\n' "$hits" | head -60 | sed 's/^/   /'
    [ "$n" -gt 60 ] && say "   ... and $((n - 60)) more (see the whole list with: grep -rnE '$re' $dirs)"
    if [ "$is_hard" = "1" ]; then hard=$((hard + n)); fi
}

say "Exhaust static audit: candidates, not bugs. Read every hit."

section "unbounded string functions (strcpy, strcat, sprintf, vsprintf, gets, strtok)" 0 '(^|[^a-z_])(strcpy|strcat|sprintf|vsprintf|gets|strtok)[[:space:]]*\('
section "strncpy and strncat (do not always end the string)" 0 '(^|[^a-z_])(strncpy|strncat)[[:space:]]*\('
section "numbers read without a check (atoi, atol, atof, sscanf %s %d)" 0 '(^|[^a-z_])(atoi|atol|atoll|atof)[[:space:]]*\(|sscanf[[:space:]]*\([^;]*%s'
section "x = realloc(x, ...) (the old block leaks when it fails)" 0 '([A-Za-z_][A-Za-z_0-9>.-]*)[[:space:]]*=[[:space:]]*(\([^)]*\))?realloc\(\1[,)]'
section "a size computed in int (int n = strlen/size...)" 0 '(^|[^a-z_])int[[:space:]]+[a-z_]+[[:space:]]*=[[:space:]]*(strlen|ex_buffer_size|sizeof)[[:space:]]*\('
section "format strings that are not literals (printf(x), snprintf(b, n, x))" 0 '(^|[^a-z_])(printf|fprintf[[:space:]]*\([^,]*,|snprintf[[:space:]]*\([^,]*,[^,]*,|SDL_Log|SDL_snprintf[[:space:]]*\([^,]*,[^,]*,)[[:space:]]*[a-z_]+[[:space:]]*[,)]'
section "random numbers and clocks in the program (not reproducible)" 0 '(^|[^a-z_])(rand|srand|random|srandom)[[:space:]]*\('
section "alloca and variable length arrays" 0 '(^|[^a-z_])alloca[[:space:]]*\(|^[[:space:]]*(char|int|uint8_t|unsigned char|size_t)[[:space:]]+[a-z_]+\[[a-z_]+\][[:space:]]*;'
section "TODO, FIXME, XXX, HACK left in the sources" 0 'TODO|FIXME|XXX|HACK' src include tools tests
section "programs started and libraries loaded (system, popen, exec, dlopen, LoadLibrary)" 0 '(^|[^a-z_])(system|popen|execv|execl|execvp|execlp|dlopen|LoadLibrary[A-Z]?)[[:space:]]*\('
section "the network (AGENTS.md: no network by default; these must be optional and off)" 1 '(^|[^a-z_])(socket|connect|getaddrinfo|gethostbyname|WSAStartup|curl_easy_[a-z_]+|SDL_net|InternetOpen|WinHttp[A-Za-z]*|NSURLSession)[[:space:]]*\('
section "web addresses in the program (a request would be a privacy leak)" 0 'https?://[a-z0-9.-]+' src
section "SDL_Delay in the interface (AGENTS.md: never block the UI thread)" 1 'SDL_Delay[[:space:]]*\(' src/ui
section "blocking calls on the interface thread without a ui-ok comment (fopen, sqlite3_, ex_index_open, SDL_WaitThread, ex_platform_read_file)" 0 '(fopen|sqlite3_[a-z_]+|ex_index_open|SDL_WaitThread|ex_platform_read_file|ex_platform_walk)[[:space:]]*\(' src/ui
section "POSIX calls that strict C11 does not declare, outside src/platform and the files that define _POSIX_C_SOURCE" 1 '(^|[^a-z_])(localtime_r|gmtime_r|strdup|strndup|strcasecmp|strncasecmp|strtok_r|fileno|popen|setenv|unsetenv|mkstemp|realpath|getline|pread|pwrite|usleep|nanosleep|clock_gettime|gettimeofday|symlink|readlink|lstat|ftruncate|fsync|opendir|readdir)[[:space:]]*\(' src/vault src/notes src/index src/ui src/markdown src/base src/canvas src/buffer src/text src/util src/workspace src/settings src/find src/nav src/import src/export src/diagram src/graph src/image src/audio src/media src/plugins src/render src/spell src/a11y
section "Windows-only or macOS-only calls outside src/platform" 1 '(^|[^a-z_])(CreateFile[AW]?|GetLastError|MultiByteToWideChar|ShellExecute[AW]?|NSString|NSFileManager|CFStringRef)[[:space:]]*[\(*]' src/vault src/notes src/index src/ui src/markdown src/base src/canvas src/buffer src/text src/util src/workspace src/settings src/find src/nav src/import src/export src/graph src/plugins
section "assert and abort in the program (a crash is the worst way to say no)" 0 '(^|[^a-z_])(assert|abort|exit)[[:space:]]*\(' src
section "functions that return a pointer to a static buffer (not thread safe)" 0 '(^|[^a-z_])(strerror|ctime|asctime|gmtime|localtime|getpwnam|inet_ntoa|strtok)[[:space:]]*\('
section "comparisons that look at only part of a name (strncmp with a literal length)" 0 'strncmp[[:space:]]*\([^,]*,[[:space:]]*"[^"]*",[[:space:]]*[0-9]+[[:space:]]*\)' src/vault src/index src/import
section "files opened for writing outside the helpers that write atomically (fopen with w)" 0 'fopen[[:space:]]*\([^,]*,[[:space:]]*"[wa]b?\+?"' src
section "GPL or AGPL text in the vendored or own sources (AGENTS.md: never add GPL code)" 1 'GNU (Affero )?General Public License|AGPL|GPL-?[23]' src include extras
section "magic numbers for sizes of buffers on the stack larger than 64 KB" 0 '\[[[:space:]]*(6[5-9][0-9]{3}|[1-9][0-9]{5,})[[:space:]]*\]' src

say ""
say "Report written to $out"
if [ "$strict" = "1" ] && [ "$hard" -gt 0 ]; then say "$hard hits in sections that break a hard rule"; exit 1; fi
exit 0
