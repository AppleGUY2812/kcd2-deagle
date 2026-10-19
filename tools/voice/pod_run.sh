#!/usr/bin/env bash
# Runs ON the rented GPU pod (image runpod/pytorch:2.8.0-py3.11-cuda12.8.1-cudnn-devel-ubuntu22.04, L40S 48 GB).
# Clones a KCD2 voice with Fish Audio S2 Pro, then Fish Drama 3, for every line in pilot_lines.json.
# Input  /workspace/in:  pilot_lines.json, <voice>_ref_calm.wav/.txt, <voice>_ref_excited.wav/.txt (sent by pilot.sh)
# Output /workspace/out: <model>/<line id>_<take>.wav, run.log, DONE when finished
# S2 Pro runs first: Fish lists Drama 3 at "at least 48 GB", exactly this card, so an out-of-memory there can't cost
# the S2 results. Each model loads once per line (the CLI); ~30-60 s a load, fine for a 12-line pilot.
set -uo pipefail
IN=/workspace/in OUT=/workspace/out FS=/workspace/fish-speech TAKES=${TAKES:-2}
mkdir -p "$OUT"
exec > >(tee -a "$OUT/run.log") 2>&1
echo "== start $(date -u +%T)"; nvidia-smi --query-gpu=name,memory.total --format=csv,noheader

if [ ! -f "$FS/.installed" ]; then
	apt-get update -qq && apt-get install -y -qq portaudio19-dev libsox-dev ffmpeg >/dev/null
	git clone --depth 1 https://github.com/fishaudio/fish-speech.git "$FS"
	cd "$FS" || exit 1
	git log -1 --format='fish-speech %h %cs'
	pip install -q --extra-index-url https://download.pytorch.org/whl/cu128 -e ".[cu128]" && pip install -q -U "huggingface_hub[cli]" hf_transfer || exit 1
	touch .installed
fi
cd "$FS" || exit 1
export HF_HUB_ENABLE_HF_TRANSFER=1
hf download fishaudio/s2-pro --local-dir checkpoints/s2-pro >/dev/null || { echo "s2-pro download failed"; exit 1; }
echo "== s2-pro downloaded $(date -u +%T)"
# Drama 3 downloads in the background while S2 Pro generates
(hf download fishaudio/drama-3 --local-dir checkpoints/drama-3 >/dev/null && echo "== drama-3 downloaded $(date -u +%T)" && touch "$OUT/.drama_ready") &
dl=$!

gen() { # gen <model dir> <out dir>
	local ck=$1 dst=$OUT/$2 n=0 fail=0
	mkdir -p "$dst"
	while IFS=$'\t' read -r id kind text; do
		local ref=excited; [ "$kind" = dialog ] && ref=calm
		[ "$kind" = heldout ] && { case "$id" in *excited*) ref=excited ;; *) ref=calm ;; esac; }
		local voice; voice=$(python -c 'import json;print(json.load(open("'"$IN"'/pilot_lines.json"))["slug"])')
		local t0=$SECONDS
		if python fish_speech/models/text2semantic/inference.py --checkpoint-path "$ck" \
			--text "$text" --prompt-audio "$IN/${voice}_ref_$ref.wav" --prompt-text "$(cat "$IN/${voice}_ref_$ref.txt")" \
			--num-samples "$TAKES" --output "$dst/$id.wav" --output-dir "$dst/codes" >"$dst/$id.log" 2>&1; then
			n=$((n + 1)); echo "  $2 $id ok $((SECONDS - t0)) s"
		else
			fail=$((fail + 1)); echo "  $2 $id FAILED $((SECONDS - t0)) s: $(tail -2 "$dst/$id.log" | tr '\n' ' ')"
		fi
	done < <(python -c 'import json
for l in json.load(open("'"$IN"'/pilot_lines.json"))["lines"]: print(l["id"], l["kind"], l["text"], sep="\t")')
	echo "== $2: $n ok, $fail failed $(date -u +%T)"
}

gen checkpoints/s2-pro s2pro
wait "$dl"
if [ -f "$OUT/.drama_ready" ]; then gen checkpoints/drama-3 drama3; else echo "== drama-3 download failed, skipped"; fi
nvidia-smi --query-gpu=memory.used --format=csv,noheader
echo "== end $(date -u +%T)"; touch "$OUT/DONE"
