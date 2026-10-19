#!/bin/sh
# install-ki.sh - make the TOPS-10 6.03 KI10 disks (images/ki_dskb0..3.rp3).
#
# Richard Cornwell publishes a pre-built system tape (kidp_system.tap, fetched as
# images/ki-system.tap by `./fetch tops10-603-ki`) and the scripts that restore it to
# an empty set of RP03 packs (restore.ini and refresh.do, in tops603.zip).  They drive
# the simulator's console with `expect`/`send`, which wants a pdp10-ki built from
# Cornwell's own tree (https://github.com/rcornwell/sims, commit be8cd4e9 tested):
#
#     git clone https://github.com/rcornwell/sims && make -C sims pdp10-ki
#     SIMH_KI=sims/BIN/pdp10-ki ./pdp10/install-ki.sh
#
# (open-simh's pdp10-ki ran the script out of step at the BACKUP prompt.)  About
# twenty minutes; the packs are 80 MB each.
set -eu
HERE="$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)"
IMG="$HERE/images"
SIM="${SIMH_KI:-pdp10-ki}"
ZIP_URL=https://sky-visions.com/dec/tops603/tops603.zip
ZIP_SHA=fb6e72b7a2a43c5dc34dbb0c1a40f3d0a8b58ccddf526e1d88c06bb450e98448
[ -f "$IMG/ki-system.tap" ] || { echo "run ./fetch tops10-603-ki first" >&2; exit 1; }
if [ -f "$IMG/ki_dskb0.rp3" ]; then echo "the KI10 disks exist (images/ki_dskb0.rp3)"; exit 0; fi
work="$(mktemp -d "${TMPDIR:-/tmp}/prebsd-ki.XXXXXX")"
trap 'rm -rf "$work"' EXIT
zip="$IMG/tops603.zip"
if [ ! -f "$zip" ]; then
    curl -L --retry 3 -o "$zip" "$ZIP_URL"
fi
[ "$(sha256sum "$zip" | cut -d' ' -f1)" = "$ZIP_SHA" ] || { echo "tops603.zip checksum differs" >&2; exit 1; }
mkdir -p "$work/tapes" "$work/rdsk"
unzip -j -o "$zip" tops603/restore.ini tops603/refresh.do -d "$work" >/dev/null
ln -s "$IMG/ki-system.tap" "$work/tapes/kidp_system.tap"
cd "$work"
# the restore ends by booting the tape again: wait for the line BOOTS (the boot prompt;
# in the file listings BOOTS is followed by a file type) that follows the boot blocks being
# written, then stop the simulator
(tail -f /dev/null | "$SIM" restore.ini ki dp > restore.log 2>&1) &
sim=$!
tries=0
until grep -q '^BOOTS.$' restore.log 2>/dev/null; do
    sleep 10; tries=$((tries + 1))
    [ $tries -lt 360 ] || { echo "the restore did not finish (see $work/restore.log)" >&2; trap - EXIT; exit 1; }
done
sleep 5
pkill -P $sim 2>/dev/null || true
kill $sim 2>/dev/null || true
for n in 0 1 2 3; do mv "rdsk/ki_dskb$n.rp3" "$IMG/ki_dskb$n.rp3"; done
echo "made images/ki_dskb0..3.rp3"
