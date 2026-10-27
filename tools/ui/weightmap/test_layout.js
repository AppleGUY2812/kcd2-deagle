// Runs the weight map's treemap code (the pure part of common.as) in Node and checks it:
// every box inside its rectangle, no overlaps, areas proportional to weight. Optional: --dump <json> writes a
// mock inventory layout for tools/ui/weightmap/mock_render.py.
const fs = require("fs");
const path = require("path");
const src = fs.readFileSync(path.join(__dirname, "common.as"), "utf8");
const pick = (name) => {
	const m = src.match(new RegExp(`function ${name}\\([^)]*\\)\\n\\{[\\s\\S]*?\\n\\}\\n`));
	if (!m) throw new Error("missing " + name);
	return m[0];
};
const code = ["WM_Worst", "WM_LayRow", "WM_Squarify", "WM_ByV"].map(pick).join("\n");
const WM = new Function(code + "\nreturn {WM_Squarify, WM_ByV};")();

let fails = 0;
const check = (ok, msg) => { if (!ok) { fails++; console.log("FAIL " + msg); } };

function run(vals, R) {
	const nodes = vals.map((v) => ({ v })).sort(WM.WM_ByV);
	WM.WM_Squarify(nodes, R.x, R.y, R.w, R.h);
	const total = vals.reduce((a, b) => a + b, 0), eps = 1e-6 * (R.w + R.h);
	nodes.forEach((n, i) => {
		check(n.x >= R.x - eps && n.y >= R.y - eps && n.x + n.w <= R.x + R.w + eps && n.y + n.h <= R.y + R.h + eps, `box ${i} outside`);
		check(Math.abs(n.w * n.h - (n.v / total) * R.w * R.h) < 1e-6 * R.w * R.h, `box ${i} area`);
		for (let j = 0; j < i; j++) {
			const m = nodes[j];
			const ox = Math.min(n.x + n.w, m.x + m.w) - Math.max(n.x, m.x);
			const oy = Math.min(n.y + n.h, m.y + m.h) - Math.max(n.y, m.y);
			check(!(ox > eps && oy > eps), `boxes ${i},${j} overlap`);
		}
	});
	return nodes;
}

let seed = 7;
const rnd = () => ((seed = (seed * 16807) % 2147483647) / 2147483647);
for (let t = 0; t < 300; t++) {
	const n = 1 + Math.floor(rnd() * 120);
	const vals = Array.from({ length: n }, () => Math.pow(rnd(), 3) * 20 + 0.01);
	run(vals, { x: rnd() * 100, y: rnd() * 100, w: 50 + rnd() * 1800, h: 50 + rnd() * 1500 });
}
run([5], { x: 0, y: 0, w: 1526, h: 1480 });
run([1, 1, 1, 1], { x: 0, y: 0, w: 400, h: 100 });
console.log(fails ? `${fails} failures` : "layout ok: 302 cases, inside, no overlap, area matches weight");

if (process.argv[2] === "--layout") {
	// Lays out a mock scene exactly like WM_Build (heading row, dark ground, every item its own box, group label
	// rows, scaled by k) for mock_render.py. In: {R:{x,y,w,h}, k, items:[[group, name, kgEach, count, icon, cat]]}.
	const S = JSON.parse(fs.readFileSync(process.argv[3], "utf8")), k = S.k, groups = [];
	for (const [g, name, w, n = 1, icon, cat = [...name.split(" ").pop()].reduce((a, ch) => a + ch.charCodeAt(0), 0)] of S.items) {
		const v = w * n;
		if (v <= 0) continue;
		(groups[g] ||= { g, v: 0, kids: [] }).kids.push({ v, name, n, each: w, icon, cat, g });
		groups[g].v += v;
	}
	const used = groups.filter(Boolean).sort(WM.WM_ByV);
	const shown = used.reduce((a, g) => a + g.v, 0);
	const hh = Math.round(46 * 1.45 * k), top = S.R.y + hh + Math.round(4 * k), edge = Math.round(3 * k), lh = Math.round(30 * 1.45 * k);
	WM.WM_Squarify(used, S.R.x + edge, top + edge, S.R.w - 2 * edge, S.R.y + S.R.h - top - 2 * edge);
	const single = used.length === 1;
	for (const gr of used) {
		let ix = gr.x + edge, iy = gr.y + edge, iw = gr.w - 2 * edge, ih = gr.h - 2 * edge;
		gr.label = false;
		if (!single && ih > lh * 3 && iw > 120 * k) { gr.label = true; gr.lx = ix; gr.ly = iy; gr.lw = iw; iy += lh; ih -= lh; }
		gr.kids.sort(WM.WM_ByV);
		WM.WM_Squarify(gr.kids, ix, iy, iw, ih);
	}
	fs.writeFileSync(process.argv[4], JSON.stringify({ ...S, hh, top, single, shown, groups: used }));
}
process.exit(fails ? 1 : 0);
