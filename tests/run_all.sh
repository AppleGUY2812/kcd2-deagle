#!/usr/bin/env bash
# Every suite; exits non-zero if any fails (run before committing). usage: tests/run_all.sh   (from anywhere)
cd "$(dirname "$0")/.." || exit 1
fail=0
luajit tests/run_flows.lua | grep -q "TOTAL ERRORS:	0$" || { echo "FAIL run_flows"; fail=1; }
luajit tests/run_transitions.lua | tail -1 | grep -q "script errors: 0" && luajit tests/run_transitions.lua | tail -1 | awk '{split($1,a,"/"); exit !(a[1]==a[2])}' || { echo "FAIL run_transitions"; fail=1; }
luajit tests/run_fallback.lua | grep -q "FALLBACK ERRORS:	0" || { echo "FAIL run_fallback"; fail=1; }
for g in deagle glock_17; do luajit tests/run_gun.lua "$g" >/dev/null || { echo "FAIL run_gun $g"; fail=1; }; done
luajit tests/run_ballistics.lua >/dev/null || { echo "FAIL run_ballistics"; fail=1; }
luajit tests/run_penetration.lua >/dev/null || { echo "FAIL run_penetration"; fail=1; }
luajit tests/run_hits.lua >/dev/null || { echo "FAIL run_hits"; fail=1; }
luajit tests/run_accuracy.lua >/dev/null || { echo "FAIL run_accuracy"; fail=1; }
luajit tests/run_motion.lua >/dev/null || { echo "FAIL run_motion"; fail=1; }
luajit tests/run_settings.lua >/dev/null || { echo "FAIL run_settings"; fail=1; }
luajit tests/run_env.lua >/dev/null || { echo "FAIL run_env"; fail=1; }
python3 tests/test_profile_guess.py >/dev/null || { echo "FAIL test_profile_guess"; fail=1; }
python3 tools/check_native.py . >/dev/null || { echo "FAIL check_native"; fail=1; }
python3 tools/check_fx.py . >/dev/null || { echo "FAIL check_fx"; fail=1; }
[ $fail = 0 ] && echo "ALL SUITES PASS"
exit $fail
