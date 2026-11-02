#!/usr/bin/env bash
# Downloads public STEP models used by the optional real-world tests into
# tests/data/downloads (git-ignored). Tests skip when the files are absent.
#
#   tests/data/fetch.sh
#   SC_TEST_DATA_DIR=tests/data/downloads ctest --preset dev
set -euo pipefail
dir="$(cd "$(dirname "$0")" && pwd)/downloads"
mkdir -p "$dir"

fetch() { # url file
  if [ ! -s "$dir/$2" ]; then
    echo "Downloading $2"
    curl -fsSL --retry 3 -o "$dir/$2" "$1"
  fi
}

# Classic Open CASCADE sample assembly (AP214, nested subassemblies, colors).
fetch https://raw.githubusercontent.com/tpaviot/pythonocc-demos/master/assets/models/as1-oc-214.stp as1-oc-214.stp
# Same assembly as AP203 (no colors).
fetch https://raw.githubusercontent.com/tpaviot/pythonocc-demos/master/assets/models/as1_pe_203.stp as1_pe_203.stp

echo "Test models are in $dir"
