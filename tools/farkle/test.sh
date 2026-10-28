#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
bin=.test_advisor
cli=.test_farkle_win
cache=.test_advisor_cache
trap 'rm -f "$bin" "$cli"; rm -rf "$cache"' EXIT
g++ -O3 -std=c++17 -pthread test_advisor.cpp -o "$bin"
nice -n 19 "./$bin" "$cache"
g++ -O3 -std=c++17 -pthread farkle_win.cpp -o "$cli"

# The offline queries: advice + the player's own pick, then the standalone finish / hot / expected numbers.
six="Ordinary die;Ordinary die;Ordinary die;Ordinary die;Ordinary die;Ordinary die"
out=$(nice -n 19 "./$cli" dice_game.csv --me "$six" --pos "0,3950,0" --faces "1,1,1,2,3,4" --keep "1,2,3")
grep -Fq "advice: keep 1 1 1 = 1000, throw 3 dice" <<<"$out"
grep -Fq "win: throw again" <<<"$out"
grep -Fq "finish this turn: " <<<"$out"
grep -Fq "hot dice: " <<<"$out"
grep -Fq "average points of the next throw: " <<<"$out"
grep -Fq "opponent finishes next turn: " <<<"$out"
grep -Fq "your pick: 1 1 1 = 1000, 3 dice left, bust 27.78 %, throw again" <<<"$out"
grep -Fq "mistake cost: 0.00 %" <<<"$out"

hot=$(nice -n 19 "./$cli" dice_game.csv --me "$six" --hot 2)
grep -Fq "hot dice (2 dice): 11.11 %" <<<"$hot"

exp=$(nice -n 19 "./$cli" dice_game.csv --me "$six" --expected 1)
grep -Fq "expected points (1 dice): 25.00" <<<"$exp"

finish=$(nice -n 19 "./$cli" dice_game.csv --me "$six" --target 50 --finish "0,0")
grep -Fq "finish this turn from the turn start: 96.91 %" <<<"$finish"

echo "[ok] farkle_win queries (advice, pick, finish, hot, expected)"
