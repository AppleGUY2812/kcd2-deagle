#!/usr/bin/env bash
# Every suite; exits non-zero if any fails (run before committing). usage: tests/run_all.sh   (from anywhere)
cd "$(dirname "$0")/.." || exit 1
fail=0
luajit tests/run_flows.lua | grep -q "TOTAL ERRORS:	0$" || { echo "FAIL run_flows"; fail=1; }
luajit tests/run_transitions.lua | tail -1 | grep -q "script errors: 0" && luajit tests/run_transitions.lua | tail -1 | awk '{split($1,a,"/"); exit !(a[1]==a[2])}' || { echo "FAIL run_transitions"; fail=1; }
luajit tests/run_fallback.lua | grep -q "FALLBACK ERRORS:	0" || { echo "FAIL run_fallback"; fail=1; }
for g in deagle glock_17 ron_grizzly; do luajit tests/run_gun.lua "$g" >/dev/null || { echo "FAIL run_gun $g"; fail=1; }; done
luajit tests/run_ballistics.lua >/dev/null || { echo "FAIL run_ballistics"; fail=1; }
luajit tests/run_penetration.lua >/dev/null || { echo "FAIL run_penetration"; fail=1; }
luajit tests/run_hits.lua >/dev/null || { echo "FAIL run_hits"; fail=1; }
luajit tests/run_killcam.lua >/dev/null || { echo "FAIL run_killcam"; fail=1; }
luajit tests/run_round_contract.lua >/dev/null || { echo "FAIL run_round_contract"; fail=1; }
# the same contract on the native sim (native/sim, Claude 5), built here when its sources changed (~1.5 s); without g++
# it fails unless SKIP_NATIVE=1 says so on purpose: never a silent skip
if command -v g++ >/dev/null; then
  so=native/build/sim/libdeaglesim.so
  mkdir -p native/build/sim
  if [ ! -f "$so" ] || [ native/sim/sim.cpp -nt "$so" ] || [ native/sim/sim.h -nt "$so" ]; then
    g++ -O2 -std=c++17 -Wall -Wextra -Werror -shared -fPIC native/sim/sim.cpp -o "$so" || { echo "FAIL native sim build"; fail=1; rm -f "$so"; }
  fi
  [ -f "$so" ] && { luajit tests/run_round_contract.lua --native "$so" >/dev/null || { echo "FAIL run_round_contract --native"; fail=1; }; }
  # and ballistics.lua's native round path (cfg.roundSim "native", Claude 1) through the DN binding's stand-in
  [ -f "$so" ] && { luajit tests/run_round_contract.lua --dn "$so" >/dev/null || { echo "FAIL run_round_contract --dn"; fail=1; }; }
  # step 7: the static / dynamic split (one static cast per ~50 m chord, dynamic-only per step)
  { g++ -O2 -std=c++17 -Wall -Wextra -Werror native/test/sim_split_test.cpp native/sim/sim.cpp -o native/build/test/sim_split_test &&
    native/build/test/sim_split_test >/dev/null; } || { echo "FAIL sim_split_test"; fail=1; }
  # Ready or Not's recoil in the plugin (native/src/ronvm.cpp, Claude 4) against its decompiled numbers
  mkdir -p native/build/test
  { g++ -O2 -std=c++17 -Wall -Wextra -Werror native/test/ronvm_test.cpp native/src/ronvm.cpp -o native/build/test/ronvm_test &&
    native/build/test/ronvm_test >/dev/null; } || { echo "FAIL ronvm_test"; fail=1; }
  # the view lean (native/src/lean.cpp, Claude 4)
  { g++ -O2 -std=c++17 -Wall -Wextra -Werror native/test/lean_test.cpp native/src/lean.cpp -o native/build/test/lean_test &&
    native/build/test/lean_test >/dev/null; } || { echo "FAIL lean_test"; fail=1; }
  # "let me in early" for the save-load / fast-travel readiness gates (native/src/readiness_early.h)
  { g++ -O2 -std=c++17 -Wall -Wextra -Werror native/test/readiness_early_test.cpp -o native/build/test/readiness_early_test &&
    native/build/test/readiness_early_test >/dev/null; } || { echo "FAIL readiness_early_test"; fail=1; }
  # the concept graph's find-by-name without the deque (native/src/conceptbfs_core.h) against the game's algorithm
  { g++ -O2 -std=c++17 -Wall -Wextra -Werror native/test/conceptbfs_test.cpp -o native/build/test/conceptbfs_test &&
    native/build/test/conceptbfs_test >/dev/null; } || { echo "FAIL conceptbfs_test"; fail=1; }
  # Storm's rule pass with remembered condition sets (native/src/stormmemo_core.h) against the game's rule walk
  { g++ -O2 -std=c++17 -Wall -Wextra -Werror native/test/stormmemo_test.cpp -o native/build/test/stormmemo_test &&
    native/build/test/stormmemo_test >/dev/null; } || { echo "FAIL stormmemo_test"; fail=1; }
  # the quest build's rttr registry sorted insert (native/src/questfast_sort.h) against std::stable_sort
  { g++ -O2 -std=c++17 -Wall -Wextra -Werror native/test/questfast_sort_test.cpp -o native/build/test/questfast_sort_test &&
    native/build/test/questfast_sort_test >/dev/null; } || { echo "FAIL questfast_sort_test"; fail=1; }
elif [ "${SKIP_NATIVE:-}" = 1 ]; then echo "SKIP run_round_contract --native (SKIP_NATIVE=1)"
else echo "FAIL run_round_contract --native: no g++ (SKIP_NATIVE=1 skips it on purpose)"; fail=1; fi
luajit tests/run_accuracy.lua >/dev/null || { echo "FAIL run_accuracy"; fail=1; }
luajit tests/run_motion.lua >/dev/null || { echo "FAIL run_motion"; fail=1; }
luajit tests/run_settings.lua >/dev/null || { echo "FAIL run_settings"; fail=1; }
luajit tests/run_env.lua >/dev/null || { echo "FAIL run_env"; fail=1; }
luajit tests/run_flykeys.lua >/dev/null || { echo "FAIL run_flykeys"; fail=1; }
luajit tests/run_lean.lua >/dev/null || { echo "FAIL run_lean"; fail=1; }
luajit tests/run_flight.lua >/dev/null || { echo "FAIL run_flight"; fail=1; }
luajit tests/run_skin.lua >/dev/null || { echo "FAIL run_skin"; fail=1; }
luajit tests/run_invnative.lua >/dev/null || { echo "FAIL run_invnative"; fail=1; }
luajit tests/run_dummy.lua >/dev/null || { echo "FAIL run_dummy"; fail=1; }
luajit tests/run_layers.lua >/dev/null || { echo "FAIL run_layers"; fail=1; }
luajit inventory/tests/run_inventory.lua >/dev/null || { echo "FAIL run_inventory"; fail=1; }
luajit tests/run_perf.lua >/dev/null || { echo "FAIL run_perf"; fail=1; }
luajit tests/run_effects.lua >/dev/null || { echo "FAIL run_effects"; fail=1; }
python3 tests/test_profile_guess.py >/dev/null || { echo "FAIL test_profile_guess"; fail=1; }
python3 tests/test_loadtime.py >/dev/null || { echo "FAIL test_loadtime"; fail=1; }
python3 tests/check_joint_names.py >/dev/null || { echo "FAIL check_joint_names"; fail=1; }
python3 tools/armswap/check_chr.py --quiet >/dev/null || { echo "FAIL check_chr"; fail=1; }
python3 tools/check_native.py . >/dev/null || { echo "FAIL check_native"; fail=1; }
python3 tools/check_fx.py . >/dev/null || { echo "FAIL check_fx"; fail=1; }
python3 tools/fx_hold.py --check >/dev/null || { echo "FAIL fx_hold"; fail=1; }
python3 tools/check_ownership.py . || { echo "FAIL check_ownership"; fail=1; }
[ $fail = 0 ] && echo "ALL SUITES PASS"
exit $fail
