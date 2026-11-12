// Offline check of native/src/armpose_core.h: Henry's own arm joints put into the viewmodel's arm pose.
// Built and run by tests/run_all.sh:
//   g++ -O2 -std=c++17 native/test/armpose_test.cpp -o armpose_test && ./armpose_test
#include "../src/armpose_core.h"
#include <cstdio>
#include <initializer_list>

using namespace armpose;

static int fails = 0;
static void check(const char* what, bool ok, double got) {
	printf("%s %s (%.5f)\n", ok ? "PASS" : "FAIL", what, got);
	if (!ok) fails++;
}
static float dist(const QT& a, const QT& b) {
	const float d[3] = { a.tx - b.tx, a.ty - b.ty, a.tz - b.tz };
	return sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
}
static float turn(const QT& a, const QT& b) {   // angle between two rotations, degrees
	float d = fabsf(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
	if (d > 1) d = 1;
	return 2 * acosf(d) * 57.29578f;
}
static QT axis(float ax, float ay, float az, float deg, float tx, float ty, float tz) {
	const float h = deg * 0.00872665f, s = sinf(h), n = sqrtf(ax * ax + ay * ay + az * az);
	return QT{ ax / n * s, ay / n * s, az / n * s, cosf(h), tx, ty, tz };
}
static void rows(const QT& q, float* m) {       // the Matrix34 of a QuatT, by rotating the three axes
	for (int c = 0; c < 3; c++) {
		float e[3] = { 0, 0, 0 }, o[3];
		e[c] = 1;
		Rot(q, e, o);
		m[c] = o[0]; m[4 + c] = o[1]; m[8 + c] = o[2];
	}
	m[3] = q.tx; m[7] = q.ty; m[11] = q.tz;
}

int main() {
	// --- the transforms
	const QT a = axis(1, 2, 3, 40, 0.3f, -1, 2), b = axis(-2, 1, 0.5f, 110, 5, 0.2f, -3);
	const QT id = Mul(a, Inv(a));
	check("a * inverse(a) is the identity", fabsf(id.w) > 0.99999f && dist(id, QT{ 0, 0, 0, 1, 0, 0, 0 }) < 1e-5f, id.w);
	const float v[3] = { 0.4f, -0.7f, 1.1f };
	float bv[3], abv[3], direct[3];
	Rot(b, v, bv); bv[0] += b.tx; bv[1] += b.ty; bv[2] += b.tz;
	Rot(a, bv, abv); abv[0] += a.tx; abv[1] += a.ty; abv[2] += a.tz;
	const QT ab = Mul(a, b);
	Rot(ab, v, direct); direct[0] += ab.tx; direct[1] += ab.ty; direct[2] += ab.tz;
	const float e = fabsf(abv[0] - direct[0]) + fabsf(abv[1] - direct[1]) + fabsf(abv[2] - direct[2]);
	check("(a * b) moves a point like b, then a", e < 1e-5f, e);
	float m[12];
	QT back;
	bool all = true;
	float worst = 0;
	for (int k = 0; k < 4; k++) {               // the four branches of the matrix -> quaternion step
		const QT q = k == 0 ? axis(0, 0, 1, 20, 1, 2, 3) : k == 1 ? axis(1, 0, 0, 175, 1, 2, 3) : k == 2 ? axis(0, 1, 0, 175, 1, 2, 3) : axis(0, 0, 1, 175, 1, 2, 3);
		rows(q, m);
		all = FromRows(m, back) && all;
		const float t = turn(q, back) + dist(q, back);
		if (t > worst) worst = t;
	}
	check("a world matrix turns into the same transform (4 cases)", all && worst < 1e-2f, worst);
	rows(a, m);
	m[0] *= 1.1f; m[4] *= 1.1f; m[8] *= 1.1f;
	check("a scaled matrix is refused", !FromRows(m, back), 0);
	rows(a, m);
	m[2] = -m[2]; m[6] = -m[6]; m[10] = -m[10];
	check("a mirrored matrix is refused", !FromRows(m, back), 0);

	// --- the plan. Body: Hips > Spine > [LeftShoulder > LeftArm > [LeftElbow, LeftHand], Head]
	const char* names[] = { "Hips", "Spine", "LeftShoulder", "LeftArm", "LeftElbow", "LeftHand", "Head" };
	const int16_t parent[] = { -1, 0, 1, 2, 3, 3, 1 };
	// Viewmodel: root > Spine > LEFTSHOULDER > LeftArm > LeftHand, in another order, another case, no elbow
	const char* vnames[] = { "root", "LeftHand", "Spine", "LEFTSHOULDER", "LeftArm" };
	const char* roots[] = { "LeftShoulder", "RightShoulder" };
	static Plan p;
	const bool built = Build(p, names, parent, 7, vnames, 5, roots, 2);
	check("plan: built", built, p.copied);
	check("  shoulder, arm and hand copy the viewmodel's joints", p.copied == 3 && p.src[2] == 3 && p.src[3] == 4 && p.src[5] == 1, p.copied);
	check("  the elbow helper follows its parent", p.follow == 1 && p.src[4] == -2, p.src[4]);
	check("  spine and head are untouched", p.src[0] == -1 && p.src[1] == -1 && p.src[6] == -1, p.src[1]);
	const int16_t bad[] = { -1, 0, 3, 2, 3, 3, 1 };
	static Plan q;
	check("  a child listed before its parent is refused", !Build(q, names, bad, 7, vnames, 5, roots, 2), 0);
	const char* none[] = { "a", "b" };
	check("  no shared arm joint is refused", !Build(q, names, parent, 7, none, 2, roots, 2), 0);

	// --- the pose. The body stands at bw in the world, the viewmodel at vw; each has its own pose
	const QT bw = axis(0, 0, 1, 75, 100, 200, 30), vw = axis(0.2f, 0.1f, 1, -35, 100.2f, 200.1f, 31.6f);
	QT rel[7] = { axis(0, 0, 1, 5, 0, 0, 1), axis(1, 0, 0, 10, 0, 0, 0.3f), axis(0, 1, 0, 20, -0.1f, 0, 0.2f), axis(0, 1, 0, 30, -0.2f, 0, 0),
	              axis(1, 0, 0, 15, -0.25f, 0.02f, 0), axis(0, 0, 1, 40, -0.3f, 0, 0), axis(1, 0, 0, 3, 0, 0, 0.3f) }, abs[7];
	for (int i = 0; i < 7; i++) abs[i] = parent[i] < 0 ? rel[i] : Mul(abs[parent[i]], rel[i]);
	const QT elbowRel = rel[4], spine = abs[1], head = abs[6];
	const QT vabs[5] = { axis(0, 0, 1, 0, 0, 0, 0), axis(1, 1, 0, 80, 0.35f, 0.5f, -0.2f), axis(1, 0, 0, 2, 0, -0.2f, -0.3f),
	                     axis(0, 1, 1, 50, 0.1f, -0.15f, -0.25f), axis(1, 0, 1, 65, 0.25f, 0.1f, -0.3f) };
	Apply(p, abs, rel, vabs, Between(bw, vw));
	float wp = 0, wt = 0;
	const int pair[3][2] = { { 2, 3 }, { 3, 4 }, { 5, 1 } };
	for (auto& pr : pair) {                     // in the world, his joint is exactly where the viewmodel's is
		const QT mine = Mul(bw, abs[pr[0]]), theirs = Mul(vw, vabs[pr[1]]);
		if (dist(mine, theirs) > wp) wp = dist(mine, theirs);
		if (turn(mine, theirs) > wt) wt = turn(mine, theirs);
	}
	check("pose: his arm joints sit on the viewmodel's in the world (m; a float at 200 m holds 0.015 mm)", wp < 6e-5f, wp);
	check("  and turn with them (deg)", wt < 0.05f, wt);
	float wr = 0;
	for (int i : { 2, 3, 5 }) { const QT again = Mul(abs[parent[i]], rel[i]); if (dist(again, abs[i]) + turn(again, abs[i]) > wr) wr = dist(again, abs[i]) + turn(again, abs[i]); }
	check("  their parent-relative transforms agree with it", wr < 0.05f, wr);
	const QT elbow = Mul(abs[3], elbowRel);
	check("  the elbow helper kept its place on the moved upper arm", dist(elbow, abs[4]) < 1e-5f && turn(elbow, abs[4]) < 0.05f, dist(elbow, abs[4]));
	check("  spine and head did not move", dist(spine, abs[1]) == 0 && dist(head, abs[6]) == 0, dist(head, abs[6]));

	{   // twice in a frame (the camera hook repeats): the same arrays again
		QT abs2[7], rel2[7];
		memcpy(abs2, abs, sizeof(abs)); memcpy(rel2, rel, sizeof(rel));
		Apply(p, abs2, rel2, vabs, Between(bw, vw));
		float d = 0;
		for (int i = 0; i < 7; i++) d += dist(abs[i], abs2[i]) + turn(abs[i], abs2[i]) + dist(rel[i], rel2[i]) + turn(rel[i], rel2[i]);
		check("a second write in the same frame changes nothing", d < 0.05f, d);
	}
	{   // a joint the viewmodel has, hanging below one it lacks: Arm > Elbow (follows) > Strap (copied)
		const char* n2[] = { "Hips", "LeftShoulder", "LeftArm", "LeftElbow", "Strap" };
		const int16_t p2[] = { -1, 0, 1, 2, 3 };
		const char* v2[] = { "LeftShoulder", "LeftArm", "Strap" };
		static Plan q2;
		const bool ok2 = Build(q2, n2, p2, 5, v2, 3, roots, 2);
		check("plan: a copied joint below a following one", ok2 && q2.copied == 3 && q2.follow == 1 && q2.src[4] == 2, q2.copied);
		QT r2[5] = { axis(0, 0, 1, 5, 0, 0, 1), axis(0, 1, 0, 20, -0.1f, 0, 0.4f), axis(0, 1, 0, 30, -0.2f, 0, 0), axis(1, 0, 0, 15, -0.25f, 0, 0), axis(0, 0, 1, 9, -0.05f, 0, 0) }, a2[5];
		for (int i = 0; i < 5; i++) a2[i] = p2[i] < 0 ? r2[i] : Mul(a2[p2[i]], r2[i]);
		const QT elbowRel2 = r2[3];
		const QT va2[3] = { axis(0, 1, 1, 50, 0.1f, -0.15f, -0.25f), axis(1, 0, 1, 65, 0.25f, 0.1f, -0.3f), axis(1, 1, 0, 80, 0.35f, 0.5f, -0.2f) };
		Apply(q2, a2, r2, va2, Between(bw, vw));
		const QT mine = Mul(bw, a2[4]), theirs = Mul(vw, va2[2]), elbow2 = Mul(a2[2], elbowRel2), again = Mul(a2[3], r2[4]);
		check("  it sits on the viewmodel's joint in the world", dist(mine, theirs) < 6e-5f && turn(mine, theirs) < 0.05f, dist(mine, theirs));
		check("  the following joint above it kept its place on the arm", dist(elbow2, a2[3]) < 1e-5f, dist(elbow2, a2[3]));
		check("  and its parent-relative transform agrees", dist(again, a2[4]) + turn(again, a2[4]) < 0.05f, dist(again, a2[4]));
	}
	{   // 4 km from the origin, 1.6 m apart: the hand's place in the body's space, against the same sum in doubles
		const QT fb = axis(0, 0, 1, 75, -3909.372f, -4001.730f, 51.292f), fv = axis(0.2f, 0.1f, 1, -35, -3909.172f, -4001.630f, 52.892f);
		const QT x = Between(fb, fv), got = Mul(x, vabs[1]);
		// reference: world point of the viewmodel's hand, then into the body's space, all in double
		auto rotd = [](const QT& q, const double* v, double* o) {
			const double tx = 2 * (q.y * v[2] - q.z * v[1]), ty = 2 * (q.z * v[0] - q.x * v[2]), tz = 2 * (q.x * v[1] - q.y * v[0]);
			o[0] = v[0] + q.w * tx + (q.y * tz - q.z * ty); o[1] = v[1] + q.w * ty + (q.z * tx - q.x * tz); o[2] = v[2] + q.w * tz + (q.x * ty - q.y * tx);
		};
		const double hv[3] = { vabs[1].tx, vabs[1].ty, vabs[1].tz };
		double w[3], o[3];
		rotd(fv, hv, w);
		const double d[3] = { w[0] + ((double)fv.tx - fb.tx), w[1] + ((double)fv.ty - fb.ty), w[2] + ((double)fv.tz - fb.tz) };
		rotd(QT{ -fb.x, -fb.y, -fb.z, fb.w, 0, 0, 0 }, d, o);
		const double err = sqrt((o[0] - got.tx) * (o[0] - got.tx) + (o[1] - got.ty) * (o[1] - got.ty) + (o[2] - got.tz) * (o[2] - got.tz));
		check("4 km from the origin the hand lands within 0.005 mm", err < 5e-6, err);
		const QT naive = Mul(Mul(Inv(fb), fv), vabs[1]);
		const double nerr = sqrt((o[0] - naive.tx) * (o[0] - naive.tx) + (o[1] - naive.ty) * (o[1] - naive.ty) + (o[2] - naive.tz) * (o[2] - naive.tz));
		check("  (inverse(body) * vm without the subtraction would be off by more than that)", nerr > err, nerr);
	}
	check("joint id: crc32 of the lower-case name", Crc32Lower("LeftArm") == Crc32Lower("leftarm") && Crc32Lower("hips") == 0x7EE3A92Fu, Crc32Lower("hips"));
	printf("%s\n", fails ? "FAILED" : "all passed");
	return fails != 0;
}
