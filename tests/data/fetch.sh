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

# NIST MBE PMI validation and conformance test models (AP242 with semantic and
# graphical PMI and saved views). US-government works, public domain. Mirrored
# in the cadmpeg corpus (www.nist.gov downloads are often blocked by proxies);
# pinned to a commit so the files cannot change under the tests.
nist="https://raw.githubusercontent.com/cadmpeg/cadmpeg/feb97ae81e8e4088d6014ee5e18bb140504cfa89/corpus/nist-pmi/step"
for model in nist_ctc_01_asme1_ap242-e1 nist_ctc_02_asme1_ap242-e2 nist_ctc_03_asme1_ap242-e2 \
             nist_ctc_04_asme1_ap242-e1 nist_ctc_05_asme1_ap242-e1 nist_ftc_06_asme1_ap242-e2 \
             nist_ftc_07_asme1_ap242-e2 nist_ftc_10_asme1_ap242-e2; do
  fetch "$nist/$model.stp" "$model.stp"
done

echo "Test models are in $dir"
