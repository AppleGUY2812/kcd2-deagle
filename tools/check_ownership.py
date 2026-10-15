#!/usr/bin/env python3
"""CLAUDE.md "Who edits what" must match the repo: every file / folder it names exists, and every script in
src/Scripts/Deagle/ has an owner there. Four Claude sessions use the list to know whose code is whose; a stale one
misleads all of them (docs/code_navigation.md).
Names in backticks: with a "/" they are repo paths (globs allowed); a bare file name (x.lua, x.md, x.xml, x.py) must
exist somewhere in the repo; anything else (Deagle.U, fx) is a code name and is skipped.
usage: check_ownership.py <repo>   (run by tests/run_all.sh)"""
import glob, os, re, subprocess, sys

repo = sys.argv[1] if len(sys.argv) > 1 else '.'
text = open(os.path.join(repo, 'CLAUDE.md'), encoding='utf-8').read()
m = re.search(r'## Who edits what.*?(?=\n## |\Z)', text, re.S)
if not m:
    print('CLAUDE.md has no "Who edits what" section'); sys.exit(1)
sec = m.group(0)
files = subprocess.run(['git', '-C', repo, 'ls-files'], capture_output=True, text=True).stdout.split('\n')
base = {}
for f in files:
    base.setdefault(os.path.basename(f), []).append(f)
SCRIPTS = 'src/Scripts/Deagle/'
bad, named = [], set()
for tok in re.findall(r'`([^`\s]+)`', sec):
    if '/' in tok:
        cands = [tok, SCRIPTS + tok]
        hit = [c for c in cands if glob.glob(os.path.join(repo, c.rstrip('/')))]
        if not hit:
            bad.append(tok)
        else:
            named.update(os.path.relpath(p, repo) for p in glob.glob(os.path.join(repo, hit[0].rstrip('/'))))
    elif re.search(r'\.(lua|py|md|xml|cpp|h|ps1|sh)$', tok):
        if tok not in base:
            bad.append(tok)
        named.update(base.get(tok, []))
orphans = [f for f in files if f.startswith(SCRIPTS) and f.endswith('.lua') and f.count('/') == 3 and f not in named]
if bad:
    print('CLAUDE.md "Who edits what" names what no longer exists:', ', '.join(bad))
if orphans:
    print('scripts with no owner in CLAUDE.md "Who edits what":', ', '.join(orphans))
if bad or orphans:
    sys.exit(1)
print('ownership map OK: %d names checked' % len(named))
