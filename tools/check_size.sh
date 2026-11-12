#!/bin/sh
# Fails if the installed core program is larger than the size budget (25 MB).
# Usage: tools/check_size.sh <install-dir>
set -eu
dir="${1:?usage: check_size.sh <install-dir>}"
limit_kb=25600
used_kb=$(du -sk "$dir" | cut -f1)
echo "install size: ${used_kb} KB (limit ${limit_kb} KB)"
[ "$used_kb" -le "$limit_kb" ]
