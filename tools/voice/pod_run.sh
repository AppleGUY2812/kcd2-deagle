#!/usr/bin/env bash
# Runs ON the rented GPU pod (image runpod/pytorch:2.8.0-py3.11-cuda12.8.1-cudnn-devel-ubuntu22.04, L40S 48 GB).
# Clones KCD2 voices with Fish Audio S2 Pro, then Fish Drama 3, for every line of every voice.
# Input  /workspace/in/<voice>/: pilot_lines.json, ref_calm.wav/.txt, ref_excited.wav/.txt (sent by pilot.sh)
#        /workspace/in/READY: every voice is uploaded. Install + downloads run before it, so setup overlaps the upload.
# Output /workspace/out/<model>/<voice>/<line id>_<take>.wav, run.log, DONE when finished
# S2 Pro runs first: Fish lists Drama 3 at "at least 48 GB", exactly this card, so an out-of-memory there can't cost
# the S2 results. Each model loads once per line (the CLI); ~30-60 s a load, fine for a pilot.
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
	echo "== installed $(date -u +%T)"
fi
cd "$FS" || exit 1
export HF_HUB_ENABLE_HF_TRANSFER=1
hf download fishaudio/s2-pro --local-dir checkpoints/s2-pro >/dev/null || { echo "s2-pro download failed"; exit 1; }
echo "== s2-pro downloaded $(date -u +%T)"
# Drama 3 downloads in the background while S2 Pro generates
(hf download fishaudio/drama-3 --local-dir checkpoints/drama-3 >/dev/null && echo "== drama-3 downloaded $(date -u +%T)" && touch "$OUT/.drama_ready") &
dl=$!
until [ -f "$IN/READY" ]; do sleep 5; done
echo "== inputs ready: $(cd "$IN" && ls -d -- */ | tr -d / | tr '\n' ' ')"

gen() { # gen <model dir> <model name>
	local ck=$1 m=$2 n=0 fail=0 v dst ref t0
	for v in "$IN"/*/; do
		v=$(basename "$v"); dst=$OUT/$m/$v; mkdir -p "$dst"
		while IFS=$'\t' read -r id kind text; do
			ref=excited; [ "$kind" = dialog ] && ref=calm
			[ "$kind" = heldout ] && { case "$id" in *excited*) ref=excited ;; *) ref=calm ;; esac; }
			t0=$SECONDS
			if python fish_speech/models/text2semantic/inference.py --checkpoint-path "$ck" \
				--text "$text" --prompt-audio "$IN/$v/ref_$ref.wav" --prompt-text "$(cat "$IN/$v/ref_$ref.txt")" \
				--num-samples "$TAKES" --output "$dst/$id.wav" --output-dir "$dst/codes" >"$dst/$id.log" 2>&1; then
				n=$((n + 1)); echo "  $m $v $id ok $((SECONDS - t0)) s"
			else
				fail=$((fail + 1)); echo "  $m $v $id FAILED $((SECONDS - t0)) s: $(tail -2 "$dst/$id.log" | tr '\n' ' ')"
			fi
		done < <(python -c 'import json, sys
for l in json.load(open(sys.argv[1]))["lines"]: print(l["id"], l["kind"], l["text"], sep="\t")' "$IN/$v/pilot_lines.json")
	done
	echo "== $m: $n ok, $fail failed $(date -u +%T)"
}

gen checkpoints/s2-pro s2pro
wait "$dl"
if [ -f "$OUT/.drama_ready" ]; then gen checkpoints/drama-3 drama3; else echo "== drama-3 download failed, skipped"; fi
nvidia-smi --query-gpu=memory.used --format=csv,noheader
echo "== end $(date -u +%T)"; touch "$OUT/DONE"
