#!/usr/bin/env bash
# Download the OpenAIR impulse responses test 4 uses (about 155 MB) and unpack the WAVs.
# openair.hosted.york.ac.uk itself is down ("Account Suspended"); the files are still on webfiles.york.ac.uk.
set -euo pipefail
dest="${OPENAIR:-/mnt/e/Tools/deagle-work/sound/openair}"
mkdir -p "$dest/zips"
for n in koli-national-park-summer maes-howe falkland-palace-bottle-dungeon terrys-typing-room \
         york-guildhall-council-chamber elveden-hall-suffolk-england trollers-gill tyndall-bruce-monument \
         creswell-crags gill-heads-mine; do
  [ -s "$dest/zips/$n.zip" ] || curl -sSL -m 900 -o "$dest/zips/$n.zip" "https://webfiles.york.ac.uk/OPENAIR/IRs/$n/$n.zip"
  unzip -oq "$dest/zips/$n.zip" -d "$dest" -x '*.jpg' '*.mp3' '*.png' || true
done
find "$dest" -name '*.wav' | wc -l
