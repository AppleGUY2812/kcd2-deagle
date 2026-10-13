#!/usr/bin/env bash
# Pre-render Ready or Not's MK V first-person shot ("1P Grizzly Fire") with Ready or Not's own FMOD engine
# (tools/ron/FmodRender) for every KCD2 place class, 8 takes each, into assets/build/sounds/mk5/<class>/shot-N.wav
# (44.1 kHz 16-bit stereo, 3.95 s, limiter + reverb already in; rendered with the final fader at 0.5 so indoor shots do not overflow). See docs/mk5_sound_spec.md.
set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
exe=/mnt/e/Tools/deagle-work/ron/fmodrender/bin/FmodRender.exe
banks='C:\Program Files (x86)\Steam\steamapps\common\Ready Or Not\ReadyOrNot\Content\FMOD\Desktop'
raw=/mnt/e/Tools/deagle-work/ron/fmodrender/sets; rawwin='E:\Tools\deagle-work\ron\fmodrender\sets'
mkdir -p "$raw"
# class | IsOutside | snapshots
classes=(
  "in_small|0|Reverb_IN_Small01"            "in_small_wood|0|Reverb_IN_Small01;Reverb Materials/Reverb_WOOD"
  "in_mid|0|Reverb_IN_Mid01"                "in_mid_wood|0|Reverb_IN_Mid01;Reverb Materials/Reverb_WOOD"
  "in_hall|0|Reverb_IN_Hall01"              "in_hall_wood|0|Reverb_IN_Hall01;Reverb Materials/Reverb_WOOD"
  "in_corridor|0|Reverb_IN_Corridor01"      "in_corridor_wood|0|Reverb_IN_Corridor01;Reverb Materials/Reverb_WOOD"
  "in_cave|0|Reverb_OUT_Cave01"             "in_cave_stone|0|Reverb_OUT_Cave01;Reverb Materials/Reverb_CONCRETE"
  "out_forest|1|Reverb_OUT_Forest01"        "out_street|1|Reverb_OUT_Street01"
  "out_covered|1|Reverb_IN_Mid01;Reverb Materials/Reverb_CONCRETE"
)
for c in "${classes[@]}"; do
  IFS='|' read -r name io snaps <<< "$c"
  args=(); IFS=';' read -ra sl <<< "$snaps"; for s in "${sl[@]}"; do args+=("snapshot:/$s"); done
  more=(); for k in 2 3 4 5 6 7 8; do more+=(+ 4 "IsOutside=$io"); done
  WSLENV=FMOD_MASTER_GAIN FMOD_MASTER_GAIN=0.5 "$exe" render "$banks" "$rawwin\\$name.wav" "event:/Weapons/Grizzly/1P Grizzly Fire" 4 "IsOutside=$io" "${args[@]}" "${more[@]}" | grep -E "ERROR|wrote" || true
done
~/.venvs/deagle-audio/bin/python - "$raw" "$repo/assets/build/sounds/mk5" <<'EOF'
import sys, os, wave, glob; sys.path.insert(0, os.path.join(os.path.dirname(sys.argv[2]), "../../../tools/sound"))
import numpy as np
from measure4 import rd, SR, onset
src, dst = sys.argv[1], sys.argv[2]
for f in sorted(glob.glob(f"{src}/*.wav")):
    name = os.path.basename(f)[:-4]; x = rd(f); a = onset(x); os.makedirs(f"{dst}/{name}", exist_ok=True)
    for k in range(8):
        s = a + k * int(4 * SR); y = x[max(s - 32, 0): s - 32 + int(3.95 * SR)].copy(); fl = int(.05 * SR); y[-fl:] *= np.linspace(1, 0, fl)[:, None]
        w = wave.open(f"{dst}/{name}/shot-{k + 1}.wav", "wb"); w.setnchannels(2); w.setsampwidth(2); w.setframerate(SR)
        w.writeframes((np.clip(y, -1, 1) * 32767).astype(np.int16).tobytes()); w.close()
    print(name, "8 takes")
EOF
