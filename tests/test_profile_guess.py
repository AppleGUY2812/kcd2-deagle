# usage: python3 tests/test_profile_guess.py   (no Blender needed)
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools", "gb"))
from profile_guess import guess_roles, guess_bones, match_sound, guess_mag_size, soundscripts, shot_sounds

def eq(label, got, want):
    ok = got == want
    print(("PASS " if ok else "FAIL ") + label + ("" if ok else f"\n   got  {got}\n   want {want}"))
    return ok

names = lambda ns: [{"name": n} for n in ns]
ok = all([
    eq("deagle (MWII names)", guess_roles(names(["idle1", "draw", "shoot1", "shoot2", "shoot_empty", "reload", "reload_empty", "inspect"])),
       {"idle": "idle1", "draw": "draw", "shoot_empty": "shoot_empty", "shoot": "shoot1", "reload_empty": "reload_empty", "reload": "reload", "inspect": "inspect"}),
    eq("glock (prefixed names, burst)", guess_roles(names(["glock_draw", "glock_fireburst1", "glock_fireburst2", "glock_firelast", "glock_firesingle", "glock_idle", "glock_reload", "inspect", "reload_empty"])),
       {"idle": "glock_idle", "draw": "glock_draw", "shoot_empty": "glock_firelast", "shoot": "glock_firesingle", "reload_empty": "reload_empty", "reload": "glock_reload", "inspect": "inspect", "shoot_alt": "glock_fireburst1"}),
    eq("stock CS activities", guess_roles([{"name": "idle", "activity": "ACT_VM_IDLE"}, {"name": "fire", "activity": "ACT_VM_PRIMARYATTACK"}, {"name": "draw", "activity": "ACT_VM_DRAW"}, {"name": "reload", "activity": "ACT_VM_RELOAD"}]),
       {"idle": "idle", "draw": "draw", "shoot": "fire", "reload": "reload"}),
    eq("CoD bones", {k: v["bone"] for k, v in guess_bones(["tag_view", "tag_weapon", "j_slide", "j_slidelock", "j_mag1", "j_mag2", "j_ammo_01", "tag_flash", "tag_brass"], []).items()},
       {"camera": "tag_view", "gun": "tag_weapon", "slide": "j_slide", "slide_stop": "j_slidelock", "mag": "j_mag1", "mag2": "j_mag2", "rounds": "j_ammo_01", "muzzle": "tag_flash", "brass": "tag_brass"}),
    eq("soundscript match", match_sound("DECHO.trmagrel", ["A/sound/weapons/decho/decho-1.wav", "A/sound/weapons/decho/p25_reload_magrelease.wav"],
                                        soundscripts('"DECHO.trmagrel" { "wave" ")weapons/decho/p25_reload_magrelease.wav" }')), "A/sound/weapons/decho/p25_reload_magrelease.wav"),
    eq("no wrong fuzzy match", match_sound("DECHO.trmagrel", ["A/sound/weapons/decho/decho-1.wav"]), None),
    eq("shot sounds", shot_sounds({"x.single": ["weapons/x/s1.wav"]}, ["m/sound/weapons/x/s1.wav"]), ["m/sound/weapons/x/s1.wav"]),
    eq("mag sizes", [guess_mag_size(t) for t in ("Glock 17 on MWII", "AKM & AK Alpha", "DEAGLE on MWII", "P90 on MW22")], [17, 30, 7, 50]),
])
sys.exit(0 if ok else 1)
