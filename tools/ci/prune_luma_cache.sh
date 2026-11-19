#!/usr/bin/env bash
# prune_luma_cache.sh CACHE STARTED
# Drops the entries of a row generator cache (tools/lumacore/instrument.py, NEBULA_LUMA_CACHE) that this build did not
# use: an entry's manifest.json gets the current time when a build restores or writes it, so one older than STARTED (a
# Unix time taken before the build) was not used. What CI keeps between runs is then one build's worth, not every
# version ever cached.
set -euo pipefail
cache=$1
started=$2
[ -d "$cache" ] || exit 0
kept=0
dropped=0
while IFS= read -r -d '' manifest; do
  if [ "$(date -r "$manifest" +%s)" -lt "$started" ]; then
    rm -rf "$(dirname "$manifest")"
    dropped=$((dropped + 1))
  else
    kept=$((kept + 1))
  fi
done < <(find "$cache" -name manifest.json -print0)
# Half-written entries of a build that stopped.
find "$cache" -maxdepth 2 -name '*.tmp' -exec rm -rf {} + 2>/dev/null || true
echo "Row generator cache: $kept entries kept, $dropped unused dropped."
