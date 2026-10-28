#define NOMINMAX   // farkle_core.h (through dice_logic.h) uses std::min/std::max
#include <windows.h>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include "MinHook.h"
#include "log.h"
#include "scan.h"
#include "settings.h"
#include "dice_logic.h"
#include "bestset.h"

namespace {

HMODULE g_self = nullptr;
Module g_game;
uintptr_t g_env = 0;
std::atomic<bool> g_enabled{ true }, g_logStates{ true }, g_advice{ true };
std::atomic<uintptr_t> g_badGame{ 0 };
std::deque<std::string> g_console;
std::mutex g_consoleMu;

void Queue(const std::string& s) {
	std::lock_guard<std::mutex> lk(g_consoleMu);
	g_console.push_back(s);
}

bool ReadPtr(uintptr_t at, uintptr_t& out) {
	__try { out = *(const uintptr_t*)at; return true; }
	__except (EXCEPTION_EXECUTE_HANDLER) { out = 0; return false; }
}
uintptr_t VFn(uintptr_t obj, int off) {
	uintptr_t vt;
	return ReadPtr(obj, vt) && ReadPtr(vt + off, vt) ? vt : 0;
}

struct RawDie { uintptr_t p = 0; int id = 0, state = 0, value = 0, joker = 0; float prob[6] = {}, x = 0, y = 0; };
struct RawSnapshot {
	uintptr_t game = 0, player[2] = {}, current = 0;
	int me = 0, target = 0, rolledMask = 0, heldMask = 0, banked[2] = {}, turn[2] = {}, count[2] = {};
	int human = 0, playerTurn = 0;
	RawDie dice[2][6];
};

// cachedCurrent != 0 (the frame poll): no call into the state object, which may be gone by then; the current
// player and playerTurn come from the last state change instead. Only plain guarded reads remain.
// The die view's +0x18 holds window pixels (the cursor handler writes it); the game converts them for AddDiceSelector
// as (pixel - window / 2) * 1080 / window height (docs/whgame_re/dice_minigame.md). A view never positioned (0, 0)
// gets an off-screen sentinel and the Lua side skips its marker.
HWND GameWindow() {
	static HWND w = nullptr;
	if (w && IsWindow(w)) return w;
	struct Find { DWORD pid; HWND best; } f{ GetCurrentProcessId(), nullptr };
	EnumWindows([](HWND h, LPARAM lp) -> BOOL {
		auto* f = (Find*)lp;
		DWORD pid = 0;
		GetWindowThreadProcessId(h, &pid);
		if (pid == f->pid && IsWindowVisible(h) && !GetWindow(h, GW_OWNER)) { f->best = h; return FALSE; }
		return TRUE;
	}, (LPARAM)&f);
	return w = f.best;
}
void ToUiSpace(float px, float py, float& x, float& y) {
	RECT rc{};
	HWND h = GameWindow();
	if ((px == 0 && py == 0) || !h || !GetClientRect(h, &rc) || rc.bottom <= 0) { x = y = 99999.f; return; }
	const float k = 1080.f / (float)rc.bottom;
	x = (px - rc.right * 0.5f) * k;
	y = (py - rc.bottom * 0.5f) * k;
}

bool ReadLive(uintptr_t controller, uintptr_t state, RawSnapshot& s, const char*& where, uintptr_t cachedCurrent = 0,
              int cachedTurn = 0) {
	where = "state";
	__try {
		s.game = *(uintptr_t*)(controller + 0x4a0);
		if (!s.game) { where = "C_Dice+0x4a0"; return false; }
		if (cachedCurrent) { s.current = cachedCurrent; s.playerTurn = cachedTurn; }
		else {
			uintptr_t vt = *(uintptr_t*)state;
			s.playerTurn = ((bool(__fastcall*)(uintptr_t))(*(uintptr_t*)(vt + 0xf8)))(state) ? 1 : 0;
			s.current = ((uintptr_t(__fastcall*)(uintptr_t))(*(uintptr_t*)(vt + 0x20)))(state);
		}
		where = "G players";
		s.player[0] = *(uintptr_t*)(s.game + 0x20);
		s.player[1] = *(uintptr_t*)(s.game + 0x30);
		s.target = *(int*)(s.game + 0x100);
		s.me = s.current == s.player[1] ? 1 : 0;
		for (int side = 0; side < 2; side++) {
			const uintptr_t p = s.player[side];
			if (!p) { where = "player model"; return false; }
			s.turn[side] = *(int*)(p + 0x18);
			s.banked[side] = *(int*)(p + 0x30);
			uintptr_t begin = *(uintptr_t*)(p + 0xd8), end = *(uintptr_t*)(p + 0xe0);
			if (end < begin || ((end - begin) & 15)) { where = "dice vector"; return false; }
			s.count[side] = (int)((end - begin) >> 4);
			if (s.count[side] != 6) continue;
			for (int i = 0; i < 6; i++) {
				where = "die model";
				uintptr_t d = *(uintptr_t*)(begin + i * 16);
				RawDie& out = s.dice[side][i];
				out.p = d;
				out.id = *(int*)(d + 0x28);
				out.state = *(unsigned char*)(d + 0x40);
				out.value = *(int*)(d + 0x58);
				out.joker = *(int*)(d + 0xa0) == 6;
				for (int j = 0; j < 6; j++) out.prob[j] = *(float*)(d + 0x88 + j * 4);
			}
		}
		if (s.current) s.human = *(uintptr_t*)(s.current + 0x48) == 0;
		where = "die views";
		uintptr_t side = *(uintptr_t*)(controller + 0x4b0 + s.me * 0x10);
		if (side) {
			uintptr_t begin = *(uintptr_t*)(side + 0xf0), end = *(uintptr_t*)(side + 0xf8);
			if (end >= begin && !((end - begin) & 15)) {
				for (uintptr_t v = begin; v < end; v += 16) {
					uintptr_t view = *(uintptr_t*)v, model = *(uintptr_t*)(view + 0x48);
					for (int i = 0; i < 6; i++) if (s.dice[s.me][i].p == model) {
						s.dice[s.me][i].x = *(float*)(view + 0x18);
						s.dice[s.me][i].y = *(float*)(view + 0x1c);
					}
				}
			}
		}
		for (int i = 0; i < 6; i++) {
			const RawDie& d = s.dice[s.me][i];
			if (d.state == 0 || d.state == 1) s.rolledMask |= 1 << i;
			if (d.state == 1) s.heldMask |= 1 << i;
		}
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// The __try body must not need object unwinding (C2712), so the name is read into a caller's buffer and only copied
// into a std::string outside it.
static bool ReadStateName(uintptr_t state, char* out, size_t cap) {
	__try {
		uintptr_t vt = *(uintptr_t*)state;
		const char* name = ((const char*(__fastcall*)(uintptr_t))(*(uintptr_t*)(vt + 8)))(state);
		if (!name) return false;
		size_t i = 0;
		for (; i + 1 < cap && name[i]; i++) out[i] = name[i];
		out[i] = 0;
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

std::string StateName(uintptr_t state) {
	char out[96] = {};
	if (!ReadStateName(state, out, sizeof out)) return "<read failed>";
	return out;
}

diceadvisor::Snapshot Convert(const RawSnapshot& r) {
	diceadvisor::Snapshot s;
	s.playerTurn = r.playerTurn != 0;
	s.human = r.human != 0;
	s.me = r.me;
	s.target = r.target;
	s.rolledMask = r.rolledMask;
	for (int side = 0; side < 2; side++) {
		s.banked[side] = r.banked[side];
		s.turn[side] = r.turn[side];
		s.count[side] = r.count[side];
		for (int i = 0; i < 6; i++) {
			const RawDie& a = r.dice[side][i];
			diceadvisor::DieState& b = s.dice[side][i];
			b.id = a.id; b.state = a.state; b.value = a.value; b.joker = a.joker != 0;
			ToUiSpace(a.x, a.y, b.x, b.y);
			for (int j = 0; j < 6; j++) b.p[j] = a.prob[j];
			if (side == s.me && a.state == 1) s.heldMask |= 1 << i;
		}
	}
	return s;
}

bool Valid(const diceadvisor::Snapshot& s, std::string& error) {
	if (s.target < 500 || s.target > 20000 || s.target % 50) { error = "invalid target score"; return false; }
	for (int side = 0; side < 2; side++) {
		if (s.count[side] != 6) { error = "unsupported dice count (exactly six required)"; return false; }
		if (s.banked[side] < 0 || s.banked[side] % 50 || s.turn[side] < 0 || s.turn[side] % 50) {
			error = "invalid score"; return false;
		}
		for (int i = 0; i < 6; i++) {
			const auto& d = s.dice[side][i];
			if (d.id != i || d.state < 0 || d.state > 2 || d.value < 0 || d.value > 6 || (d.value == 6 && !d.joker)) {
				error = "unexpected die id, state or face"; return false;
			}
			double sum = 0;
			for (float p : d.p) { if (!std::isfinite(p) || p < 0) { error = "invalid probability"; return false; } sum += p; }
			if (!(sum > 0.99 && sum < 1.01)) { error = "probabilities are not normalised"; return false; }
		}
	}
	return true;
}

void LogSnapshot(const RawSnapshot& r, const std::string& name) {
	if (!g_logStates.load()) return;
	Log("dice: state %s turn=%d human=%d target=%d A bank=%d turn=%d B bank=%d turn=%d rolled=0x%x",
	    name.c_str(), r.me, r.human, r.target, r.banked[0], r.turn[0], r.banked[1], r.turn[1], r.rolledMask);
	for (int side = 0; side < 2; side++) for (int i = 0; i < r.count[side] && i < 6; i++) {
		const RawDie& d = r.dice[side][i];
		Log("dice: %c%d id=%d state=%d value=%d p=%.6f,%.6f,%.6f,%.6f,%.6f,%.6f joker=%d xy=%.1f,%.1f",
		    side ? 'B' : 'A', i, d.id, d.state, d.value, d.prob[0], d.prob[1], d.prob[2], d.prob[3], d.prob[4], d.prob[5], d.joker, d.x, d.y);
	}
}

struct Matchup {
	std::array<farkle::Die, 6> dice[2];
	std::array<const farkle::Die*, 6> ptr[2];
	std::unique_ptr<farkle::CachedGame> solved;
};
enum class Action { None, Scoring, Passing };
struct Work { uint64_t generation = 0; uintptr_t game = 0; Action action = Action::None; diceadvisor::Snapshot snapshot; };
std::mutex g_workMu;
std::condition_variable g_workCv;
Work g_work;
std::unordered_map<std::string, std::unique_ptr<Matchup>> g_games;
std::string g_cacheDir;
std::atomic<uint64_t> g_generation{ 0 };
std::atomic<uint64_t> g_done{ 0 };

// The advice shown for the throw the player is currently choosing from, kept so a committed choice can be priced.
struct ThrowAdvice {
	bool valid = false;
	uintptr_t game = 0;
	int human = 0, humanBanked = 0, oppBanked = 0, turn = 0, rolledMask = 0;
	int faces[6] = {};
	farkle::Advice advice;
};
ThrowAdvice g_throw;
uintptr_t g_lastGame = 0;
int g_lastBankedTotal = 0;
double g_mistakeTotal = 0, g_pendingMistake = 0;

// Live polling: the state hook only fires on transitions, while the player picks dice inside Holding. The frame tick
// re-reads the live state when the selection or the shown faces change, so the pick's numbers appear as the player
// clicks and a transition that fired before the dice settled is corrected.
std::atomic<uintptr_t> g_liveState{ 0 }, g_liveController{ 0 };
std::atomic<bool> g_liveShowable{ false };
std::atomic<int> g_liveSig{ -1 };

int LiveSignature(const RawSnapshot& s) {
	int sig = s.me * 64 + s.heldMask;
	for (int i = 0; i < 6; i++) sig = sig * 7 + s.dice[s.me][i].value;
	return sig;
}

bool CopyDice(const diceadvisor::Snapshot& s, int side, std::array<farkle::Die, 6>& dice) {
	std::string error;
	if (!diceadvisor::MakeDice(s, side, dice, error))
		throw std::runtime_error("cannot build the dice model: " + error);
	return true;
}

double CommittedCost(const Matchup& match, const Work& w) {
	if (!g_throw.valid || g_throw.game != w.game || w.action != Action::Scoring) return 0.0;
	if (!w.snapshot.heldMask || w.snapshot.me != g_throw.human) return 0.0;
	const int h = g_throw.human;
	if (w.snapshot.banked[h] != g_throw.humanBanked || w.snapshot.banked[1 - h] != g_throw.oppBanked ||
	    w.snapshot.turn[h] != g_throw.turn) return 0.0;
	for (int i = 0; i < 6; i++) {
		const int face = w.snapshot.dice[h][i].value == 6 ? 0 : w.snapshot.dice[h][i].value;
		if (face != g_throw.faces[i]) return 0.0;   // a stale throw from an earlier roll
	}
	const farkle::Selection sel = farkle::EvaluateSelection(match.solved->game, 0,
		g_throw.humanBanked, g_throw.oppBanked, g_throw.turn, g_throw.faces, g_throw.rolledMask, w.snapshot.heldMask);
	if (!sel.valid) return std::max(g_throw.advice.winBank, g_throw.advice.winRoll);
	return farkle::MistakeCost(g_throw.advice, std::max(sel.winBank, sel.winRoll));
}

void Worker() {
	for (;;) {
		Work w;
		{
			std::unique_lock<std::mutex> lk(g_workMu);
			g_workCv.wait(lk, [] { return g_work.generation != 0; });
			w = g_work;
			g_work.generation = 0;
		}
		if (!g_enabled.load() || g_badGame.load() == w.game) continue;
		try {
			// A new game: another controller, or the cumulative banked scores dropped back to zero. (Turn points fall
			// back to zero on every bust, so only the banked pair says the scores were reset.)
			const int bankedTotal = w.snapshot.banked[0] + w.snapshot.banked[1];
			const bool freshGame = w.game != g_lastGame || (g_lastBankedTotal > 0 && bankedTotal == 0);
			g_lastGame = w.game;
			g_lastBankedTotal = bankedTotal;
			if (freshGame) {
				g_throw = ThrowAdvice{};
				g_pendingMistake = 0;
				g_mistakeTotal = 0;
			}
			// Banking: price the choice against the displayed advice; no new panel is needed on the way out.
			if (w.action == Action::Passing) {
				if (g_throw.valid && g_throw.game == w.game && !g_throw.advice.bank) {
					const double cost = farkle::MistakeCost(g_throw.advice, g_throw.advice.winBank);
					if (cost > 1e-9) {
						g_pendingMistake = cost;
						g_mistakeTotal += cost;
						Log("dice: banked against the advice, cost %.2f %% win (game total %.2f %%)", cost * 100.0, g_mistakeTotal * 100.0);
					}
				}
				continue;
			}
			const std::string key = diceadvisor::MatchupKey(w.snapshot);
			Matchup* match = nullptr;
			auto it = g_games.find(key);
			if (it == g_games.end()) {
				auto m = std::make_unique<Matchup>();
				CopyDice(w.snapshot, w.snapshot.me, m->dice[0]);
				CopyDice(w.snapshot, 1 - w.snapshot.me, m->dice[1]);
				for (int side = 0; side < 2; side++) for (int i = 0; i < 6; i++) m->ptr[side][i] = &m->dice[side][i];
				match = m.get();
				g_games.emplace(key, std::move(m));
			} else match = it->second.get();
			if (!match->solved) {
				// "Calculating..." only when a solve is actually needed; updates of the player's own pick are instant.
				Queue("dice_busy " + std::to_string(w.generation));
				match->solved = farkle::SolveCached(match->ptr[0], match->ptr[1], w.snapshot.target, g_cacheDir);
			}
			const double cost = CommittedCost(*match, w);
			farkle::Advice a = diceadvisor::AdviseSnapshot(match->solved->game, w.snapshot);
			farkle::Selection sel = diceadvisor::SelectSnapshot(match->solved->game, w.snapshot);
			if (g_generation != w.generation || !g_enabled.load() || g_badGame.load() == w.game) continue;
			g_done.store(w.generation);
			diceadvisor::Presentation p = diceadvisor::Present(match->solved->game, w.snapshot, a, sel);
			if (cost > 1e-9) {
				p.mistake = cost * 100.0;
				g_mistakeTotal += cost;
				Log("dice: choice cost %.2f %% win (game total %.2f %%)", cost * 100.0, g_mistakeTotal * 100.0);
			} else if (g_pendingMistake > 1e-9 && w.snapshot.heldMask == 0) {
				p.mistake = g_pendingMistake * 100.0;
				g_pendingMistake = 0;
			}
			if (w.action == Action::None && w.snapshot.heldMask == 0 && w.snapshot.rolledMask) {
				g_throw = ThrowAdvice{};
				g_throw.valid = true;
				g_throw.game = w.game;
				g_throw.human = w.snapshot.me;
				g_throw.humanBanked = w.snapshot.banked[w.snapshot.me];
				g_throw.oppBanked = w.snapshot.banked[1 - w.snapshot.me];
				g_throw.turn = w.snapshot.turn[w.snapshot.me];
				g_throw.rolledMask = w.snapshot.rolledMask;
				for (int i = 0; i < 6; i++)
					g_throw.faces[i] = w.snapshot.dice[w.snapshot.me][i].value == 6 ? 0 : w.snapshot.dice[w.snapshot.me][i].value;
				g_throw.advice = a;
			}
			Log("dice: advice gen=%llu keep=0x%x score=%d bank=%d roll=%.6f bankWin=%.6f held=0x%x pick=%d pscore=%d left=%d bust=%.6f proll=%.6f pbank=%.6f finish=%.6f hot=%.6f opp=%.6f tf=%d mistake=%.2f",
			    (unsigned long long)w.generation, a.keep, p.score, a.bank ? 1 : 0, a.winRoll, a.winBank,
			    w.snapshot.heldMask, p.hasPick ? 1 : 0, p.pickScore, p.pickDiceLeft, p.pickBust, p.pickWinRoll,
			    p.pickWinBank, p.finish, p.hot, p.opponentFinish, p.hasTakeFewer ? 1 : 0, p.mistake);
			char head[1024];
			std::snprintf(head, sizeof head,
				"dice_show gen:%llu keep:%d score:%d bank:%d roll:%.6f bankwin:%.6f held:%d pick:%d pscore:%d "
				"pleft:%d pbust:%.6f proll:%.6f pbank:%.6f finish:%.6f hot:%.6f opp:%.6f tpts:%.1f bpts:%.1f "
				"tf:%d tfkeep:%d tfscore:%d tfwin:%.6f tallkeep:%d tallscore:%d tallwin:%.6f mist:%.2f rolled:%d pfin:%.6f phot:%.6f ptpts:%.1f pbpts:%.1f abust:%.6f",
				(unsigned long long)w.generation, a.keep, p.score, a.bank ? 1 : 0, a.winRoll, a.winBank,
				w.snapshot.heldMask, p.hasPick ? 1 : 0, p.pickScore, p.pickDiceLeft, p.pickBust, p.pickWinRoll,
				p.pickWinBank, p.finish, p.hot, p.opponentFinish, p.throwPoints, p.bankPoints,
				p.hasTakeFewer ? 1 : 0, p.tfKeep, p.tfScore, p.tfWin, p.tfAllKeep, p.tfAllScore, p.tfAllWin,
				p.mistake, w.snapshot.rolledMask, p.pickFinish, p.pickHot, p.pickThrowPoints, p.pickBankPoints, p.bust);
			std::string cmd = head;
			for (int i = 0; i < 6; i++) {
				const auto& d = w.snapshot.dice[w.snapshot.me][i];
				char one[64];
				std::snprintf(one, sizeof one, " d%d:%.2f,%.2f,%d,%d", i, d.x, d.y, d.value, d.joker ? 1 : 0);
				cmd += one;
			}
			// The console reads "a=b" as a cvar assignment (MODLOG), so every field is key:value. The fields go over in
			// short dice_part lines and dice_show gen:<n> applies them.
			std::string fields = cmd.substr(strlen("dice_show ")), part;
			size_t at = 0;
			while (at < fields.size()) {
				size_t sp = fields.find(' ', at);
				if (sp == std::string::npos) sp = fields.size();
				const std::string tok = fields.substr(at, sp - at);
				if (!part.empty() && part.size() + 1 + tok.size() > 180) { Queue("dice_part " + part); part.clear(); }
				part += (part.empty() ? "" : " ") + tok;
				at = sp + 1;
			}
			if (!part.empty()) Queue("dice_part " + part);
			Queue("dice_show gen:" + std::to_string(w.generation));
		} catch (const std::exception& e) {
			Log("dice: solver failed: %s; advisor off for this game", e.what());
			g_badGame.store(w.game);
			if (g_generation == w.generation) Queue("dice_hide " + std::to_string(g_generation));
		}
	}
}

void Submit(uintptr_t game, const diceadvisor::Snapshot& s, Action action = Action::None) {
	{
		std::lock_guard<std::mutex> lk(g_workMu);
		g_work = Work{ ++g_generation, game, action, s };
	}
	g_workCv.notify_one();
}

// ---- the dice picker: which 6 of the player's dice to bring (tools/farkle/bestset.h). Lua lists the inventory's dice
// ("own <seq> c,w1..w6,j;..."), this worker ranks them at the lowest thread priority (about 10 s for 14 dice) and
// answers "dice_best <seq> <count per entry> <points>"; results are kept per collection for the session.
struct BestJob { unsigned long long seq = 0; std::vector<bestset::Owned> own; std::string key; };
std::mutex g_bestMu;
std::condition_variable g_bestCv;
BestJob g_bestJob;
bool g_bestPending = false;
std::unordered_map<std::string, std::string> g_bestDone;   // collection key -> "counts points"

void QueueBest(unsigned long long seq, const std::string& answer) { Queue("dice_best " + std::to_string(seq) + " " + answer); }

void BestWorker() {
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
	for (;;) {
		BestJob job;
		{
			std::unique_lock<std::mutex> lk(g_bestMu);
			g_bestCv.wait(lk, [] { return g_bestPending; });
			job = g_bestJob;
			g_bestPending = false;
		}
		std::string answer;
		try {
			const auto t0 = GetTickCount64();
			auto picks = bestset::BestSets(job.own, 4000, 1);
			if (picks.empty()) answer = "none 0";
			else {
				for (size_t i = 0; i < picks[0].count.size(); i++) answer += (i ? "," : "") + std::to_string(picks[0].count[i]);
				answer += " " + std::to_string((int)std::lround(picks[0].oneTurn));
			}
			Log("dice: best set for %zu kinds in %llu ms: %s", job.own.size(), (unsigned long long)(GetTickCount64() - t0), answer.c_str());
		} catch (const std::exception& e) {
			Log("dice: best set failed: %s", e.what());
			answer = "none 0";
		}
		{ std::lock_guard<std::mutex> lk(g_bestMu); g_bestDone[job.key] = answer; }
		QueueBest(job.seq, answer);
	}
}

// "own <seq> c,w1,w2,w3,w4,w5,w6,j;..." from Lua (main thread)
void OwnCommand(const char* rest) {
	BestJob job;
	char* end = nullptr;
	job.seq = strtoull(rest, &end, 10);
	if (!end || end == rest) return;
	while (*end == ' ') end++;
	job.key = end;
	std::string all = end, one;
	size_t at = 0;
	while (at < all.size()) {
		size_t semi = all.find(';', at);
		if (semi == std::string::npos) semi = all.size();
		one = all.substr(at, semi - at);
		at = semi + 1;
		double v[8] = {};
		int n = 0;
		const char* s = one.c_str();
		while (n < 8 && *s) { char* e = nullptr; v[n] = strtod(s, &e); if (e == s) break; n++; s = *e == ',' ? e + 1 : e; }
		if (n != 8 || v[0] < 1) continue;
		double sum = 0; for (int f = 0; f < 6; f++) sum += v[1 + f];
		if (!(sum > 0)) continue;
		bestset::Owned o;
		o.name = std::to_string(job.own.size());
		for (int f = 0; f < 6; f++) o.p[f] = v[1 + f] / sum;
		o.devil = v[7] != 0;
		o.count = (int)v[0];
		job.own.push_back(o);
	}
	{
		std::lock_guard<std::mutex> lk(g_bestMu);
		auto it = g_bestDone.find(job.key);
		if (it != g_bestDone.end()) { QueueBest(job.seq, it->second); return; }
		g_bestJob = job;
		g_bestPending = true;
	}
	g_bestCv.notify_one();
}

using StateChangeFn = uintptr_t(__fastcall*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t);
StateChangeFn g_origState = nullptr;
std::atomic<uintptr_t> g_liveCurrent{ 0 };
std::atomic<int> g_liveTurn{ 0 };
// A throw state, by its vtable (Rolling, ExtraRolling, ReRolling; docs/whgame_re/dice_minigame.md): a guarded read
// only, so it is safe on a state object the game has already freed (the old state after a transition).
bool IsRolling(uintptr_t state) {
	uintptr_t vt = 0;
	if (!state || !ReadPtr(state, vt) || !vt) return false;
	const uintptr_t rva = vt - g_game.base;
	return rva == 0x3f71a00 || rva == 0x3f702a8 || rva == 0x3f70ac8;
}
uintptr_t __fastcall HkState(uintptr_t controller, uintptr_t arg2, uintptr_t oldState, uintptr_t newState) {
	bool afterRoll = IsRolling(oldState);   // before the original: the transition may free the old state
	const uintptr_t result = g_origState(controller, arg2, oldState, newState);
	++g_generation;
	if (!newState) {   // the minigame is over: hide, and don't blame the next game on a read
		g_liveShowable.store(false);
		g_liveState.store(0);
		Queue("dice_hide " + std::to_string(g_generation));
		return result;
	}
	// The dice-picking screen is the Null state before a game starts (in game 2026-10-27/28: "state Null" when the
	// player opened the dice selection, Null + Initial when the game began). Queued last, after this transition's hide.
	struct PickerSignal {
		bool on;
		~PickerSignal() { if (on) Queue("dice_picker " + std::to_string(g_generation)); }
	} picker{ StateName(newState).find("Null") != std::string::npos };
	RawSnapshot raw;
	const char* where = "unknown";
	if (!ReadLive(controller, newState, raw, where)) {
		Log("dice: read failed at %s; advisor off for this game", where);
		g_badGame.store(controller);
		g_liveShowable.store(false);
		g_liveState.store(0);
		Queue("dice_hide " + std::to_string(g_generation));
		return result;
	}
	g_badGame.store(0);   // the next state may be readable again (a bad read at a menu transition, say)
	const std::string name = StateName(newState);
	LogSnapshot(raw, name);
	const bool holding = name.find("Holding") != std::string::npos;
	const bool scoring = name.find("Scoring") != std::string::npos;
	const bool passing = name.find("Passing") != std::string::npos;
	// PlayerTurn is where the player chooses after a throw (Rolling -> PlayerTurn) and returns to after each die click
	// (PlayerTurn -> Holding -> PlayerTurn): the panel's main state (in game 2026-10-27: v1 showed nothing without it)
	const bool turnState = name.find("PlayerTurn") != std::string::npos;
	static std::string lastName;   // main thread only; backs up the vtable check with the name logged for the old state
	afterRoll = afterRoll || lastName.find("Rolling") != std::string::npos;
	lastName = name;
	// Show while the human is choosing: the result of a throw (Holding) and the committed selection (Scoring). Hide
	// during throws, the opponent's turn, the bank transition and any other phase. A Holding state reached without a
	// throw is the start of a turn, where the dice values are stale, so it only gets a panel once the player has picks.
	if (!raw.human || IsRolling(newState) || passing || (!holding && !scoring && !turnState) || !raw.rolledMask ||
	    (!scoring && !afterRoll && raw.heldMask == 0 && !g_liveShowable.load())) {
		g_liveShowable.store(false);
		g_liveState.store(0);
		Queue("dice_hide " + std::to_string(g_generation));
		if (passing && raw.human && g_enabled.load()) {
			diceadvisor::Snapshot s = Convert(raw);
			Submit(controller, s, Action::Passing);
		}
		return result;
	}
	diceadvisor::Snapshot s = Convert(raw);
	std::string error;
	if (!Valid(s, error)) {
		Log("dice: read failed at validation (%s); advisor off for this game", error.c_str());
		g_badGame.store(controller);
		g_liveShowable.store(false);
		g_liveState.store(0);
		Queue("dice_hide " + std::to_string(g_generation));
		return result;
	}
	g_liveController.store(controller);
	g_liveState.store(newState);
	g_liveShowable.store(true);
	g_liveSig.store(LiveSignature(raw));
	g_liveCurrent.store(raw.current);
	g_liveTurn.store(raw.playerTurn);
	if (g_enabled.load()) Submit(controller, s, scoring ? Action::Scoring : Action::None);
	else Queue("dice_hide " + std::to_string(g_generation));
	return result;
}

// Re-read the live selection while the player clicks dice inside Holding/Scoring (no state transition fires there).
void PollLive() {
	if (!g_enabled.load() || !g_liveShowable.load()) return;
	const uintptr_t controller = g_liveController.load(), state = g_liveState.load();
	if (!controller || !state) return;
	RawSnapshot raw;
	const char* where = "poll";
	if (!ReadLive(controller, state, raw, where, g_liveCurrent.load(), g_liveTurn.load()) || !raw.human ||
	    IsRolling(state) || !raw.rolledMask) {
		g_liveShowable.store(false);
		return;
	}
	const int sig = LiveSignature(raw);
	if (sig == g_liveSig.load()) return;
	g_liveSig.store(sig);
	diceadvisor::Snapshot s = Convert(raw);
	std::string error;
	if (!Valid(s, error)) {
		Log("dice: selection poll failed at validation (%s)", error.c_str());
		return;
	}
	Submit(controller, s);
}

constexpr uintptr_t kStateRva = 0x2e907d0;
const unsigned char kStateBytes[] = { 0x48,0x8b,0xc4,0x48,0x89,0x58,0x10,0x4c,0x89,0x40,0x18,0x55,0x56,0x57,0x41,0x54 };
bool InstallStateHook() {
	uintptr_t at = g_game.base + kStateRva;
	unsigned char got[sizeof kStateBytes] = {};
	__try { memcpy(got, (const void*)at, sizeof got); }
	__except (EXCEPTION_EXECUTE_HANDLER) { Log("dice: read failed at state hook; nothing hooked"); return false; }
	if (memcmp(got, kStateBytes, sizeof got)) {
		Log("dice: state hook bytes differ from WHGame 1.5.6; nothing hooked");
		return false;
	}
	return MH_CreateHook((void*)at, (void*)&HkState, (void**)&g_origState) == MH_OK && MH_EnableHook((void*)at) == MH_OK;
}

using ExecFn = void(__fastcall*)(uintptr_t, const char*, bool, bool);
void RunConsole(const char* cmd) {
	uintptr_t con;
	if (!ReadPtr(g_env + 0xa8, con) || !con) return;
	if (auto exec = (ExecFn)VFn(con, 0x118)) exec(con, cmd, true, false);
}
bool SafeConsole(const char* cmd) {
	__try { RunConsole(cmd); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

constexpr uintptr_t kRvaSystemVt = 0x3dc2858, kSlotUpdate = 0x28, kRvaUpdate = 0x6678c4;
const unsigned char kUpdateBytes[] = { 0x48,0x8b,0xc4,0x48,0x89,0x58,0x10,0x48,0x89,0x70,0x18,0x48,0x89,0x78,0x20,0x55 };
using UpdateFn = uintptr_t(__fastcall*)(void*, unsigned, int);
UpdateFn g_origUpdate = nullptr;
std::atomic<DWORD> g_mainTid{ 0 };
uintptr_t __fastcall HkUpdate(void* sys, unsigned flags, int pause) {
	const uintptr_t r = g_origUpdate(sys, flags, pause);
	DWORD want = 0;
	const DWORD me = GetCurrentThreadId();
	if (g_mainTid.compare_exchange_strong(want, me)) Log("dice: frame tick on thread %lu", me);
	if ((want == 0 || want == me) && g_env) {
		std::deque<std::string> run;
		{ std::lock_guard<std::mutex> lk(g_consoleMu); run.swap(g_console); }
		for (const auto& c : run) SafeConsole(c.c_str());
		static unsigned tick = 0;
		if (g_liveShowable.load() && ++tick % 12 == 0) {
			try { PollLive(); }
			catch (...) { g_liveShowable.store(false); }
		}
	}
	return r;
}
bool PatchUpdate() {
	void** slot = (void**)(g_game.base + kRvaSystemVt + kSlotUpdate);
	uintptr_t cur = 0;
	unsigned char head[sizeof kUpdateBytes] = {};
	if (!ReadPtr((uintptr_t)slot, cur)) return false;
	bool ours = cur == g_game.base + kRvaUpdate;
	bool foreign = cur && (cur < g_game.base || cur >= g_game.base + g_game.size);
	if (!ours && !foreign) return false;
	if (ours) {
		__try { memcpy(head, (const void*)cur, sizeof head); } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
		if (memcmp(head, kUpdateBytes, sizeof head)) return false;
	}
	DWORD old = 0;
	if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return false;
	g_origUpdate = (UpdateFn)cur;
	InterlockedExchangePointer(slot, (void*)&HkUpdate);
	VirtualProtect(slot, sizeof(void*), old, &old);
	return true;
}

using LogHandlerFn = int(__fastcall*)(void*, uintptr_t);
using GetParamStrFn = bool(__fastcall*)(uintptr_t, int, const char**);
LogHandlerFn g_origLog = nullptr;
GetParamStrFn g_getParamStr = nullptr;
const char* LuaString(uintptr_t pH) {
	__try { const char* s = nullptr; return g_getParamStr && g_getParamStr(pH, 1, &s) ? s : nullptr; }
	__except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}
void Hello() {
	std::vector<std::string> saved = SettingsReplay();
	std::lock_guard<std::mutex> lk(g_consoleMu);
	g_console.clear();
	for (auto& c : saved) g_console.push_back(c);
	g_console.push_back("dice_native_ok");
}
void Command(const char* line) {
	char cmd[32] = {};
	if (sscanf_s(line, "%31s", cmd, (unsigned)sizeof cmd) != 1) return;
	const char* rest = line + strlen(cmd);
	while (*rest == ' ') rest++;
	if (!strcmp(cmd, "hello")) Hello();
	else if (!strcmp(cmd, "advisor")) g_enabled.store(atoi(rest) != 0);
	else if (!strcmp(cmd, "log")) g_logStates.store(atoi(rest) != 0);
	else if (!strcmp(cmd, "own")) OwnCommand(rest);
	else if (!strcmp(cmd, "set")) {
		char key[96] = {};
		if (sscanf_s(rest, "%95s", key, (unsigned)sizeof key) == 1) {
			const char* value = rest + strlen(key);
			while (*value == ' ') value++;
			SettingsSave(key, value);
		}
	}
}
int __fastcall HkLog(void* self, uintptr_t pH) {
	const char* s = LuaString(pH);
	if (s && !strncmp(s, "[DICE]", 6)) {
		Command(s + 6 + (s[6] == ' ' ? 1 : 0));
		if (auto end = (int(__fastcall*)(uintptr_t))VFn(pH, 0x58)) return end(pH);
	}
	return g_origLog(self, pH);
}

bool ValidEnv(uintptr_t env) {
	uintptr_t rnd, vt;
	if (!ReadPtr(env + 0x108, rnd) || !rnd || !ReadPtr(rnd, vt)) return false;
	return RttiName(g_game, vt).find("Renderer") != std::string::npos;
}
uintptr_t FindEnv() {
	uintptr_t cands[2] = {};
	if (uintptr_t a = FindUnique(g_game, "48 8D 05 ?? ?? ?? ?? 48 89 0F 4C 8D 67 28 48 8D 0D ?? ?? ?? ?? 48 89 47 20 48 89 4F 08")) cands[0] = RipTarget(a, 3, 7);
	if (uintptr_t b = FindUnique(g_game, "48 8B 51 08 48 8B C8 FF D2 48 8B 0D ?? ?? ?? ?? 48 85 C9")) cands[1] = RipTarget(b + 9, 3, 7);
	for (uintptr_t c : cands) {
		if (!c) continue;
		if (ValidEnv(c)) return c;
		uintptr_t d = 0;
		if (ReadPtr(c, d) && d && ValidEnv(d)) return d;
	}
	return 0;
}

DWORD WINAPI Init(LPVOID) {
	Log("dice advisor v1 loaded (WHGame 1.5.6)");
	for (int i = 0; i < 600 && !GetModuleHandleA("WHGame.dll"); i++) Sleep(100);
	g_game = GetModule("WHGame.dll");
	if (!g_game.base) { Log("dice: WHGame.dll not found"); return 0; }
	if (MH_Initialize() != MH_OK) { Log("dice: MinHook init failed"); return 0; }
	uintptr_t gp = FindUnique(g_game, "48 89 5C 24 08 57 48 83 EC ?? 48 8B 01 49 8B F8 4C 8D 44 24 20 C7 44 24 20 05 00 00 00 FF 50 38 32 DB 84 C0 74 11");
	uintptr_t lh = FindLogAlwaysHandler(g_game);
	g_getParamStr = (GetParamStrFn)gp;
	if (gp && lh && MH_CreateHook((void*)lh, (void*)&HkLog, (void**)&g_origLog) == MH_OK && MH_EnableHook((void*)lh) == MH_OK) Log("dice: Lua channel installed");
	else Log("dice: Lua channel FAILED");
	if (!InstallStateHook()) Log("dice: state hook unavailable; advisor off");
	else Log("dice: state hook installed; live state logging on by default");
	for (int i = 0; i < 240 && !g_env; i++) { g_env = FindEnv(); if (!g_env) Sleep(500); }
	if (!PatchUpdate()) Log("dice: no frame tick; Lua replies are idle");
	char path[MAX_PATH] = {};
	GetModuleFileNameA(g_self, path, MAX_PATH);
	std::filesystem::path plugin(path);
	g_cacheDir = (plugin.parent_path() / "dice_cache").string();
	std::thread(Worker).detach();
	std::thread(BestWorker).detach();
	return 0;
}
}

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID) {
	if (reason == DLL_PROCESS_ATTACH) {
		g_self = h;
		DisableThreadLibraryCalls(h);
		LogInit(h);
		SettingsInit(h, "dice_settings.txt", "dice_set");
		CreateThread(nullptr, 0, Init, nullptr, 0, nullptr);
	}
	return TRUE;
}
