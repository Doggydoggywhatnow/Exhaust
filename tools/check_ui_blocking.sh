#!/bin/sh
# The interface thread must never wait for disk, the index or a search (AGENTS.md: at most 16 ms). Those run on worker threads
# (src/ui/service.c, the indexer, the notes writer). This lists calls that block, in the sources the interface thread runs.
# A line that really is safe can say so with a trailing comment: /* ui-ok: why */
cd "$(dirname "$0")/.." || exit 2
pattern='SDL_Delay|usleep|[^_a-z]sleep\(|SDL_WaitThread|ex_search_run|ex_index_sync|ex_links_unlinked|ex_props_rename_everywhere|ex_props_retype_everywhere|ex_rename_links|ex_platform_walk|ex_index_candidates|sqlite3_|ex_vault_rescan[^;]*for'
bad=$(grep -nE "$pattern" src/ui/*.c src/main.c src/workspace/*.c 2>/dev/null | grep -v '^src/ui/service.c' | grep -v 'ui-ok')
if [ -n "$bad" ]; then
    echo "blocking calls on the interface thread:"
    echo "$bad"
    exit 1
fi
echo ok
