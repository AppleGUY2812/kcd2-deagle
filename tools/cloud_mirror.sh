#!/usr/bin/env bash
# Code-only mirror of this repo for Claude Code cloud sessions (Claude 5, 2026-10-15): the committed HEAD minus
# binary assets (assets/, src/Objects/, *.pyc) is copied into ~/kcd2-guns-mirror and pushed to the private
# git@github.com:binkybarnes/kcd2-guns.git (main). The full history can't go: it holds a 106 MB file (GitHub's limit is
# 100 MB) and third-party model sources. Cloud work comes back with `tools/cloud_mirror.sh import <branch>`: the
# branch's diff against the mirror commit it started from, applied here with --3way (commit it yourself).
#   tools/cloud_mirror.sh push            mirror HEAD and push
#   tools/cloud_mirror.sh import <branch> apply a cloud branch's changes to this working tree
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
mirror="$HOME/kcd2-guns-mirror"
remote="git@github.com:binkybarnes/kcd2-guns.git"
exclude='^(assets/|src/Objects/)|\.pyc$'

case "${1:-push}" in
push)
	if [ ! -d "$mirror/.git" ]; then
		git init -q -b main "$mirror"
		git -C "$mirror" remote add origin "$remote"
	fi
	head=$(git -C "$repo" rev-parse --short HEAD)
	find "$mirror" -mindepth 1 -maxdepth 1 ! -name .git -exec rm -rf {} +
	git -C "$repo" ls-files -z | tr '\0' '\n' | grep -v -E "$exclude" | while IFS= read -r f; do
		[ -f "$repo/$f" ] || continue   # submodules (native/third_party/minhook) and deleted files
		mkdir -p "$mirror/$(dirname "$f")"
		git -C "$repo" show "HEAD:$f" > "$mirror/$f"
	done
	git -C "$mirror" add -A
	if git -C "$mirror" diff --cached --quiet; then echo "mirror already at $head"; else
		git -C "$mirror" commit -q -m "mirror of $head"
	fi
	git -C "$mirror" push -q origin main
	echo "pushed mirror of $head ($(du -sh --exclude=.git "$mirror" | cut -f1))"
	;;
import)
	branch="${2:?branch name}"
	git -C "$mirror" fetch -q origin "$branch"
	base=$(git -C "$mirror" merge-base main "origin/$branch")
	git -C "$mirror" diff --binary "$base" "origin/$branch" | git -C "$repo" apply --3way
	echo "applied origin/$branch (since $(git -C "$mirror" log -1 --format=%s "$base")); review and commit your own hunks"
	;;
*) echo "usage: $0 push | import <branch>"; exit 1 ;;
esac
