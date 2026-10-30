// Checks dict.as (the cheap Dictionary.Remove) against the game's own Dictionary (decompiled from ItemTransfer.gfx):
// random Add / Remove / Replace / AddOrReplace / GetByKey / Contain / Set / Clear sequences must leave the same array
// and answer every lookup the same. Also times removing many items from a big list, old vs new.
const fs = require("fs");
const path = require("path");

function makeDictionary() {
	function Dictionary(propertyName) {
		this.m_FilteredA = this.m_StackedA = this.m_Array = [];
		this.m_Object = {};
		this.m_propertyName = propertyName;
	}
	const P = Dictionary.prototype;
	P.Set = function (d) { this.m_FilteredA = this.m_StackedA = this.m_Array = d.GetArray(); this.constructObject(); };
	P.Add = function (value) {
		const k = value[this.m_propertyName];
		if (k == undefined) return false;
		if (this.m_Object[k] != undefined) return false;
		this.m_Object[k] = this.m_Array.push(value) - 1;
		return true;
	};
	P.AddOrReplace = function (value) {
		const k = value[this.m_propertyName];
		if (k == undefined) return false;
		if (this.m_Object[k] != undefined) return this.Replace(value);
		return this.Add(value);
	};
	P.Remove = function (key) {
		if (key == undefined) return false;
		if (this.m_Object[key] == undefined) return false;
		this.m_Array.splice(this.m_Object[key], 1);
		this.constructObject();
		return true;
	};
	P.Clear = function () { this.m_Array = []; this.m_Object = {}; };
	P.Contain = function (key) { return this.m_Object[key] != undefined; };
	P.Replace = function (value) {
		const k = value[this.m_propertyName];
		if (k == undefined || this.m_Object[k] == undefined) return false;
		this.m_Array[this.m_Object[k]] = value;
		return true;
	};
	P.GetByKey = function (key) {
		if (key == undefined || this.m_Object[key] == undefined) return undefined;
		return this.m_Array[this.m_Object[key]];
	};
	P.GetArray = function () { return this.m_Array.concat(); };
	P.constructObject = function () {
		this.m_Object = {};
		for (let i = 0; i < this.m_Array.length; i++) this.m_Object[this.m_Array[i][this.m_propertyName]] = i;
	};
	return Dictionary;
}

const src = fs.readFileSync(path.join(__dirname, "dict.as"), "utf8");
const Old = makeDictionary();
const New = makeDictionary();
new Function("_global", src + "\nDX_Install();")({ Dictionary: New });

let fails = 0;
const check = (ok, msg) => { if (!ok) { fails++; if (fails < 10) console.log("FAIL " + msg); } };
let seed = 12345;
const rnd = (n) => { seed = (seed * 1103515245 + 12345) & 0x7fffffff; return seed % n; };

for (let round = 0; round < 300; round++) {
	const a = new Old("Id"), b = new New("Id");
	let nextId = 1;
	const live = [];
	const steps = 50 + rnd(400);
	for (let s = 0; s < steps; s++) {
		const op = rnd(100);
		if (op < 35) {
			const v = { Id: "i" + nextId++, n: s };
			check(a.Add(v) === b.Add(v), "Add result");
			live.push(v.Id);
		} else if (op < 75) {
			const k = live.length && rnd(5) ? live[rnd(live.length)] : "i" + (nextId + 3);
			check(a.Remove(k) === b.Remove(k), "Remove result " + k);
			const at = live.indexOf(k);
			if (at >= 0) live.splice(at, 1);
		} else if (op < 82) {
			const k = live.length ? live[rnd(live.length)] : "none";
			const v = { Id: k, n: -s };
			check(a.Replace(v) === b.Replace(v), "Replace result");
		} else if (op < 87) {
			const k = rnd(2) && live.length ? live[rnd(live.length)] : "i" + nextId++;
			const v = { Id: k, n: s * 3 };
			check(a.AddOrReplace(v) === b.AddOrReplace(v), "AddOrReplace result");
			if (live.indexOf(k) < 0) live.push(k);
		} else if (op < 89) {
			a.Clear(); b.Clear(); live.length = 0;
		} else if (op < 90) {
			const src2 = new Old("Id");
			for (let i = 0; i < 20; i++) { const v = { Id: "s" + round + "_" + s + "_" + i }; src2.Add(v); }
			a.Set(src2); b.Set(src2);
			live.length = 0; src2.m_Array.forEach((v) => live.push(v.Id));
		}
		// every lookup must agree after every step
		check(a.m_Array.length === b.m_Array.length, "length");
		for (let i = 0; i < a.m_Array.length; i++) check(a.m_Array[i] === b.m_Array[i], "array order at " + i);
		for (let i = 0; i < live.length; i++) {
			check(a.GetByKey(live[i]) === b.GetByKey(live[i]), "GetByKey " + live[i]);
			check(a.Contain(live[i]) === b.Contain(live[i]), "Contain " + live[i]);
		}
		check(b.GetByKey("nope") === undefined && a.GetByKey("nope") === undefined, "missing key");
	}
}

// timing: a 1558-item list, remove 600 one by one (the chest screen's burst), old vs new
function timeIt(D) {
	const d = new D("Id");
	for (let i = 0; i < 1558; i++) d.Add({ Id: "k" + i });
	const t0 = process.hrtime.bigint();
	for (let i = 0; i < 600; i++) d.Remove("k" + ((i * 7) % 1558));
	d.Add({ Id: "after" });
	return Number(process.hrtime.bigint() - t0) / 1e6;
}
const to = timeIt(Old), tn = timeIt(New);
if (fails) { console.log(fails + " failures"); process.exit(1); }
console.log(`dict ok: 300 random sequences agree with the game's Dictionary; 600 removals from 1558 items: ` +
	`old ${to.toFixed(1)} ms, new ${tn.toFixed(1)} ms in node (AS2 in game is ~100x slower)`);
