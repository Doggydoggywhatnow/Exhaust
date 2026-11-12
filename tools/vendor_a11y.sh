#!/bin/sh
# Re-vendors the Rust crates AccessKit needs, filtered to the platforms Exhaust
# ships for, so the repository does not carry import libraries and bindings for
# targets nobody builds. Needs cargo, jq and network access; the normal build
# needs none of them offline except cargo (see CMakeLists.txt).
#
#   tools/vendor_a11y.sh
#
# What it does:
#  1. `cargo vendor` everything in Cargo.lock into third_party/accesskit-c/vendor.
#  2. For each shipping target (below) asks `cargo metadata --filter-platform` which
#     crates, and which of their features, the build really uses; the union is "needed".
#  3. Crates that are not needed keep only their Cargo.toml (cargo must still be able
#     to read every crate in the lock file, but never builds them on our targets);
#     their sources are replaced by an empty library.
#  4. In the Windows API crates (`windows`, `windows-sys`) the folders of Cargo
#     features that are not enabled are deleted, and in `linux-raw-sys` the folders of
#     CPU architectures Exhaust does not ship.
#  5. Checks the result with an offline, locked `cargo metadata` and `cargo check`.
set -eu

TARGETS="x86_64-unknown-linux-gnu aarch64-unknown-linux-gnu x86_64-pc-windows-msvc aarch64-pc-windows-msvc x86_64-apple-darwin aarch64-apple-darwin"
KEEP_ARCH="x86_64 aarch64 x32 x86 arm"   # linux-raw-sys folders kept (x86 and arm are cheap and rustix wants them listed)

root=$(cd "$(dirname "$0")/.." && pwd)
dir="$root/third_party/accesskit-c"
cd "$dir"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

echo "== vendoring"
rm -rf vendor .cargo
mkdir .cargo
cargo vendor --locked vendor > "$tmp/vendor.out"
# source replacement for offline builds
cat > .cargo/config.toml <<'CFG'
[source.crates-io]
replace-with = "vendored-sources"

[source.vendored-sources]
directory = "vendor"

[net]
offline = true
CFG
# cargo metadata needs the network config to be absent for this step: use the output of cargo vendor's lock

echo "== which crates and features are needed"
mv .cargo/config.toml "$tmp/config.toml"
: > "$tmp/needed.txt"
for t in $TARGETS; do
  cargo metadata --format-version 1 --filter-platform "$t" --locked > "$tmp/meta.json"
  jq -r '. as $m | $m.resolve.nodes[] | . as $n | ($m.packages[] | select(.id == $n.id)) as $p |
         "\($p.name) \($p.version) \(($n.features // []) | join(","))"' "$tmp/meta.json" >> "$tmp/needed.txt"
done
mv "$tmp/config.toml" .cargo/config.toml
sort -u "$tmp/needed.txt" > "$tmp/needed.sorted"
# features of a crate: the union over targets
awk '{ key = $1 " " $2; if (!(key in f)) f[key] = $3; else f[key] = f[key] "," $3 }
     END { for (k in f) print k, f[k] }' "$tmp/needed.sorted" > "$tmp/needed.txt"

field() { sed -n "s/^$2 = \"\(.*\)\"$/\1/p" "$1/Cargo.toml" | head -1; }

echo "== pruning"
for crate in vendor/*; do
  name=$(field "$crate" name)
  version=$(field "$crate" version)
  line=$(grep "^$name $version " "$tmp/needed.txt" || true)
  if [ -z "$line" ]; then
    # not built on any shipping target: keep the manifest, drop everything else
    pkg=$(sed -n 's/.*"package":"\([0-9a-f]*\)".*/\1/p' "$crate/.cargo-checksum.json")
    find "$crate" -mindepth 1 -maxdepth 1 ! -name Cargo.toml -exec rm -rf {} +
    mkdir "$crate/src"
    : > "$crate/src/lib.rs"
    b=$(sed -n 's/^build = "\(.*\)"$/\1/p' "$crate/Cargo.toml" | head -1)
    if [ -n "$b" ]; then mkdir -p "$(dirname "$crate/$b")"; : > "$crate/$b"; fi
    printf '{"files":{},"package":"%s"}\n' "$pkg" > "$crate/.cargo-checksum.json"
    continue
  fi
  feats=$(echo "$line" | cut -d' ' -f3)
  # (tests, examples and docs are kept: some crates include_str! them into their sources)
  case "$name" in
  windows|windows-sys)
    # delete the folder of every API area whose Cargo feature is off (Win32_Foundation -> src/Windows/Win32/Foundation)
    ( cd "$crate/src/Windows" && find . -mindepth 1 -type d | sort | while read -r d; do
        [ -d "$d" ] || continue
        f=$(echo "$d" | sed 's|^\./||; s|/|_|g')
        case ",$feats," in *",$f,"*) ;; *) rm -rf "$d" ;; esac
      done )
    ;;
  linux-raw-sys)
    for a in "$crate"/src/*/; do
      a=$(basename "$a")
      case " $KEEP_ARCH " in *" $a "*) ;; *) case "$a" in aarch64|x86_64|x86|arm|x32) ;; *) [ -d "$crate/src/$a" ] && rm -rf "$crate/src/$a" ;; esac ;; esac
    done
    ;;
  esac
  # the checksum file lists every file; cargo only checks it when it unpacks from a registry,
  # so an empty list is the honest description of a pruned directory
  pkg=$(sed -n 's/.*"package":"\([0-9a-f]*\)".*/\1/p' "$crate/.cargo-checksum.json")
  printf '{"files":{},"package":"%s"}\n' "$pkg" > "$crate/.cargo-checksum.json"
done

echo "== checking offline"
cargo metadata --format-version 1 --offline --locked > /dev/null
for t in $TARGETS; do
  if rustup target list --installed 2>/dev/null | grep -qx "$t"; then
    cargo check --release --offline --locked --target "$t" 2>&1 | tail -2
  else
    echo "(target $t is not installed here; not checked)"
  fi
done
du -sh vendor
