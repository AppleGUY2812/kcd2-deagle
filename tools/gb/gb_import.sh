#!/usr/bin/env bash
# Imports a GameBanana Counter-Strike weapon mod for conversion: download -> extract -> find the viewmodel, its
# sequences, bones, sounds and weapon script -> guessed gun profile -> source .blend with every sequence -> preview
# contact sheet of every clip (Desktop\Deagle Progress\<slug> clips.png).
# Work goes to E:\Tools\deagle-work\guns\<slug>; the profile to guns/<slug>.json in the repo (edit it if a guess is
# wrong; a re-import writes guns/<slug>.guessed.json instead of overwriting it).
# usage: tools/gb/gb_import.sh <gamebanana mod id | local archive> [--slug name]
set -euo pipefail
here="$(cd "$(dirname "$0")/../.." && pwd)"
BLENDER='E:\Tools\blender-4.3.2-windows-x64\blender.exe'
WORK='E:\Tools\deagle-work\guns'
DESK='C:\Users\<user>\Desktop\Deagle Progress'
src="${1:?usage: gb_import.sh <gamebanana mod id | archive> [--slug name]}"
slug=""
[ "${2:-}" = "--slug" ] && slug="${3:?--slug needs a name}"
win() { wslpath -w "$1"; }
ps() { powershell.exe -NoProfile -Command "$1" | tr -d '\r'; }
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/dl"

title="" sourcejson="{}"
if [[ "$src" =~ ^[0-9]+$ ]]; then
	echo "== GameBanana mod $src"
	curl -sf "https://gamebanana.com/apiv11/Mod/$src/ProfilePage" -o "$tmp/page.json"
	read -r title url fname < <(python3 -I - "$tmp/page.json" <<'EOF'
import json, sys
d = json.load(open(sys.argv[1]))
files = [f for f in d.get("_aFiles", []) if f.get("_sFile", "").lower().endswith((".zip", ".rar", ".7z"))]
if not files:
    sys.exit("no archive in this mod")
f = max(files, key=lambda f: f.get("_nFilesize", 0))
print(d["_sName"].replace("\t", " ").replace(" ", "\x1f"), f["_sDownloadUrl"], f["_sFile"].replace(" ", "_"))
EOF
)
	title="${title//$'\x1f'/ }"
	echo "   $title"
	echo "   downloading $fname"
	curl -sfL "$url" -o "$tmp/dl/$fname"
	archive="$tmp/dl/$fname"
	sourcejson="$(python3 -I -c 'import json,sys; d=json.load(open(sys.argv[1])); print(json.dumps({"gamebanana": d["_idRow"], "url": d.get("_sProfileUrl"), "author": (d.get("_aSubmitter") or {}).get("_sName"), "game": (d.get("_aGame") or {}).get("_sName")}))' "$tmp/page.json")"
else
	archive="$(realpath "$src")"
	title="$(basename "$archive")"; title="${title%.*}"
fi
if [ -z "$slug" ]; then
	slug="$(python3 -I - "$title" <<'EOF'
import re, sys
t = sys.argv[1].lower()
t = re.sub(r"\b(insurgency:? ?sandstorm'?s?|ready or not|escape from tarkov|ismc|scp:?5k|eft|ron|\(.*?\))\b", " ", t)
t = re.sub(r"\bon (mw|bo)\w*( ?(anims|animations))?.*$", " ", t)
t = re.sub(r"\b(anims|animations|reworked|updated|update)\b", " ", t)
print(re.sub(r"[^a-z0-9]+", "_", t).strip("_")[:32] or "gun")
EOF
)"
fi
G="$WORK\\$slug"
echo "== $slug -> $G"

echo "== extract"
ps "New-Item -ItemType Directory -Force '$G\\download','$G\\src' | Out-Null; Copy-Item '$(win "$archive")' '$G\\download\\' -Force; tar -xf '$G\\download\\$(basename "$archive")' -C '$G\\src'; (Get-ChildItem -Recurse -File '$G\\src').Count.ToString() + ' files'"
python3 -I -c 'import json,sys; print(json.dumps({"title": sys.argv[1], "slug": sys.argv[2], "source": json.loads(sys.argv[3])}))' "$title" "$slug" "$sourcejson" > "$tmp/meta.json"
ps "Copy-Item '$(win "$tmp/meta.json")' '$G\\meta.json' -Force"

# Blender exits non-zero on a harmless shutdown error after background scripts: judge by the DONE line instead
blend() { ps "\$env:BLENDER_USER_SCRIPTS='E:\\Tools\\blender-user'; & '$BLENDER' -b --factory-startup -P '$(win "$1")' -- '$G\\src' '$G' 2>&1 | Select-String '$2' | % Line; exit 0"; }
echo "== inspect"
out="$(blend "$here/tools/gb/gb_inspect.py" 'VIEWMODEL|ROLES|BONES|MISSING|NO VIEWMODEL|INSPECT DONE|Traceback|Error:')"; echo "$out"
grep -q "INSPECT DONE" <<<"$out" || { echo "inspect failed" >&2; exit 1; }
echo "== preview (import every sequence, render each clip)"
out="$(blend "$here/tools/gb/gb_preview.py" 'ACTIONS|SAVED|NO ACTION|PREVIEW DONE|Error:')"; echo "$out"
grep -q "PREVIEW DONE" <<<"$out" || { echo "preview failed" >&2; exit 1; }

mkdir -p "$tmp/out"
ps "Copy-Item '$G\\preview' '$(win "$tmp/out")' -Recurse -Force; Copy-Item '$G\\profile.json' '$(win "$tmp/out")\\profile.json' -Force"
python3 "$here/tools/gb/gb_sheet.py" "$tmp/out/preview" "$tmp/out/profile.json" "$tmp/out/sheet.png"
ps "New-Item -ItemType Directory -Force '$DESK' | Out-Null; Copy-Item '$(win "$tmp/out/sheet.png")' '$DESK\\$slug clips.png' -Force"
mkdir -p "$here/guns"
dest="$here/guns/$slug.json"
[ -e "$dest" ] && dest="$here/guns/$slug.guessed.json"
cp "$tmp/out/profile.json" "$dest"
echo "== profile -> ${dest#"$here"/}"
python3 -I - "$dest" <<'EOF'
import json, sys
p = json.load(open(sys.argv[1]))
print("   clips:", ", ".join(f"{r}={c['sequence']}" for r, c in p["clips"].items()))
print("   bones:", ", ".join(f"{r}={b.get('bone') or b.get('attachment')}" for r, b in p["bones"].items()))
print("   mag", p.get("magSize"), "| shot sounds", len(p.get("shotSounds", [])), "| missing", p.get("missing_required") or "none", "| unassigned", p.get("unassigned_sequences") or "none")
EOF
echo "== sheet -> $DESK\\$slug clips.png"
echo "IMPORT OK"
