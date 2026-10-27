// Runs the inventory search matcher (search.as) in Node: query grammar, spelling normalisation, category matching
// and the score ordering. Pure code, no game needed.
const fs = require("fs");
const path = require("path");
const src = fs.readFileSync(path.join(__dirname, "search.as"), "utf8");
const S = new Function(src + "\nreturn {SB_Score, SB_Parse, SB_MatchItem, SB_MatchBool, SB_Norm, SB_MIN};")();

let fails = 0;
let total = 0;
const check = (ok, msg) => { total++; if (!ok) { fails++; console.log("FAIL " + msg); } };
const hit = (q, name, cat = "Common") => S.SB_MatchItem(S.SB_Parse(q), name, cat) > S.SB_MIN / 2;
const bool = (q, name, cat = "Common") => S.SB_MatchBool(S.SB_Parse(q), name, cat);
const score = (q, name, cat = "Common") => S.SB_MatchItem(S.SB_Parse(q), name, cat);

// --- the user's example: item name and category, "or" between alternatives
check(hit("brunswick helmet or brunswick armor", "Brunswick Helmet", "Head"), "name match, first alternative");
check(hit("brunswick helmet or brunswick armor", "Brunswick Armour", "Body"), "armour spelled the American way");
check(hit("brunswick helmet or brunswick armor", "Brunswick Helmet", "Head"), "second alternative still matches");
check(!hit("brunswick helmet or brunswick armor", "Brunswick Gloves", "Arms"), "gloves match neither alternative");
check(!hit("brunswick helmet", "Brunswick Boots", "Legs"), "every term in a group must match");

// --- categories are searchable, in the game's own words
check(hit("head", "Cuman Cap", "Head"), "category text matches");
check(hit("legs", "Ragged Hose", "Legs"), "legs category");
check(!hit("legs", "Cuman Cap", "Head"), "wrong category does not match");

// --- fuzzy, not just substring
check(hit("lngswrd", "Long sword"), "subsequence match");
check(hit("brnswck", "Brunswick Helmet", "Head"), "subsequence across a name");
check(!hit("sword", "Long bow"), "no characters in order, no match");
check(hit("SWORD", "long SWORD"), "case-insensitive");

// --- spelling normalisation both ways
check(hit("armor", "Plate Armour"), "armor finds armour");
check(hit("armour", "Plate Armor"), "armour finds armor");
check(hit("defence", "Defense potion"), "defence finds defense");

// --- grammar edge cases
check(S.SB_Parse("brunswick helmet or brunswick armor").length === 2, "two groups");
check(S.SB_Parse("a b c").length === 1, "no 'or' is one group");
check(S.SB_Parse("or").length === 0, "a lone 'or' is empty");
check(S.SB_Parse("  ").length === 0, "blank query is empty");
check(hit("", "anything"), "empty query matches everything");
check(hit("helmet brunswick", "Brunswick Helmet", "Head"), "term order does not matter");
check(hit("brunswick or armour", "Plate Armour"), "or with one term per side");

// --- ordering: a contiguous, word-start match beats a scattered one
check(score("sword", "Long sword") > score("sword", "Silver ornate war dirk"), "sword ranks the plain name higher");
check(score("head", "Head") > score("head", "Hooded chainmail"), "word start beats scattered");
check(score("brunswick", "Brunswick Helmet") > 0, "a real match scores above zero");
check(score("brunswick", "Brunswick Helmet") > score("brunswick", "Bronze vase, kiln, sword, iron, nails, wire, comb, knife, shield"), "short exact beats scattered");

// --- a name that only matches through the category still shows
check(hit("body", "Padded gambeson", "Body"), "category-only match");
check(hit("jewellery", "Silver ring", "Jewellery"), "attachment category is named Jewellery");

// --- the pass's cheap matcher answers exactly what the scoring one does (every case above, both ways)
const cases = [
	["brunswick helmet or brunswick armor", "Brunswick Helmet", "Head"],
	["brunswick helmet or brunswick armor", "Brunswick Armor", "Body"],
	["brunswick helmet or brunswick armor", "Brunswick Gloves", "Arms"],
	["head", "Cuman Cap", "Head"],
	["legs", "Ragged Hose", "Legs"],
	["lngswrd", "Long sword", "Common"],
	["brnswck", "Brunswick Helmet", "Head"],
	["sword", "Long bow", "Common"],
	["SWORD", "long SWORD", "Common"],
	["armor", "Plate Armour", "Body"],
	["armour", "Plate Armor", "Body"],
	["defence", "Defense potion", "Common"],
	["", "anything", "Common"],
	["helmet brunswick", "Brunswick Helmet", "Head"],
	["brunswick or armour", "Plate Armour", "Body"],
	["body", "Padded gambeson", "Body"],
	["jewellery", "Silver ring", "Jewellery"],
	["a b or c", "Carb", "Common"],
	["x", "Brunswick Helmet", "Head"],
	["our", "Armour", "Body"],
];
for (const [q, name, cat] of cases) check(bool(q, name, cat) === hit(q, name, cat), `pass matcher agrees on ${JSON.stringify(q)} / ${name} / ${cat}`);
check(bool("head", "Cuman Cap", "Head"), "the pass finds a category match");
check(bool("lngswrd", "Long sword"), "the pass matches a subsequence");
check(!bool("sword", "Long bow"), "the pass rejects a non-subsequence");
check(bool("brunswick helmet or brunswick armor", "Brunswick Armor", "Body"), "the pass reads both alternatives");
// a repeated term and a query with only separators must not upset the dedupe
check(bool("sword or sword", "Long sword"), "the same term in two groups");
check(bool(" or ", "Long sword"), "a query that is only 'or' restricts nothing (like the empty query)");

console.log(fails ? `${fails} failures` : `search ok: ${total} checks, grammar, spelling, categories, order, the pass's matcher`);
process.exit(fails ? 1 : 0);
