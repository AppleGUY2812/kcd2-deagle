#pragma once
// Henry's own arms in the gun viewmodel's arm pose: the arithmetic, free of the game (tested by
// native/test/armpose_test.cpp). armpose.cpp reads and writes the two characters' pose arrays with it.
//
// The viewmodel plays the fitted arm clips on Henry's arm joints (same names, same bind poses). Every frame each of
// his arm joints that the viewmodel has too gets the viewmodel joint's place in the world; a joint below it that the
// viewmodel lacks (an elbow helper, a strap) keeps its place on its parent.
#include <cmath>
#include <cstdint>
#include <cstring>

namespace armpose {

struct QT { float x, y, z, w, tx, ty, tz; };   // the engine's QuatT: quaternion x y z w, then the translation
static_assert(sizeof(QT) == 0x1c, "QuatT is 0x1c bytes");

constexpr int kMaxJoints = 768;                 // the game's own limit for one skeleton

inline void Rot(const QT& q, const float* v, float* o) {
	const float tx = 2 * (q.y * v[2] - q.z * v[1]), ty = 2 * (q.z * v[0] - q.x * v[2]), tz = 2 * (q.x * v[1] - q.y * v[0]);
	o[0] = v[0] + q.w * tx + (q.y * tz - q.z * ty);
	o[1] = v[1] + q.w * ty + (q.z * tx - q.x * tz);
	o[2] = v[2] + q.w * tz + (q.x * ty - q.y * tx);
}

inline QT Mul(const QT& a, const QT& b) {       // b first, then a (a child's b on its parent's a)
	QT r;
	r.x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y;
	r.y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x;
	r.z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w;
	r.w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z;
	const float n = sqrtf(r.x * r.x + r.y * r.y + r.z * r.z + r.w * r.w);
	if (n > 0) { r.x /= n; r.y /= n; r.z /= n; r.w /= n; }
	float t[3]; const float bt[3] = { b.tx, b.ty, b.tz };
	Rot(a, bt, t);
	r.tx = t[0] + a.tx; r.ty = t[1] + a.ty; r.tz = t[2] + a.tz;
	return r;
}

inline QT Inv(const QT& a) {
	QT r{ -a.x, -a.y, -a.z, a.w, 0, 0, 0 };
	float t[3]; const float at[3] = { -a.tx, -a.ty, -a.tz };
	Rot(r, at, t);
	r.tx = t[0]; r.ty = t[1]; r.tz = t[2];
	return r;
}

// The viewmodel's transform seen from the body: inverse(body) * vm, with the two world positions subtracted first.
// Far from the world's origin (Kuttenberg is thousands of metres out) a float holds a position to about 0.25 mm;
// the difference of two nearby positions is exact, so nothing is lost beyond what the two matrices already lost.
inline QT Between(const QT& body, const QT& vm) {
	const QT bi{ -body.x, -body.y, -body.z, body.w, 0, 0, 0 };
	QT r = Mul(bi, QT{ vm.x, vm.y, vm.z, vm.w, 0, 0, 0 });
	const float d[3] = { vm.tx - body.tx, vm.ty - body.ty, vm.tz - body.tz };
	float t[3];
	Rot(bi, d, t);
	r.tx = t[0]; r.ty = t[1]; r.tz = t[2];
	return r;
}

// A Matrix34 as the plugin holds it (m[row * 4 + col], translation in m[3], m[7], m[11]). False when it is not a
// plain rotation (a scaled or sheared entity): the pose would not carry over.
inline bool FromRows(const float* m, QT& o) {
	for (int c = 0; c < 3; c++) {
		const float n = m[c] * m[c] + m[4 + c] * m[4 + c] + m[8 + c] * m[8 + c];
		if (!(fabsf(n - 1) < 5e-3f)) return false;
	}
	const float m00 = m[0], m01 = m[1], m02 = m[2], m10 = m[4], m11 = m[5], m12 = m[6], m20 = m[8], m21 = m[9], m22 = m[10];
	const float det = m00 * (m11 * m22 - m12 * m21) - m01 * (m10 * m22 - m12 * m20) + m02 * (m10 * m21 - m11 * m20);
	if (!(fabsf(det - 1) < 1e-2f)) return false;   // a mirror is no rotation either
	const float tr = m00 + m11 + m22;
	if (tr > 0) { const float s = sqrtf(tr + 1) * 2; o.w = s / 4; o.x = (m21 - m12) / s; o.y = (m02 - m20) / s; o.z = (m10 - m01) / s; }
	else if (m00 > m11 && m00 > m22) { const float s = sqrtf(1 + m00 - m11 - m22) * 2; o.w = (m21 - m12) / s; o.x = s / 4; o.y = (m01 + m10) / s; o.z = (m02 + m20) / s; }
	else if (m11 > m22) { const float s = sqrtf(1 + m11 - m00 - m22) * 2; o.w = (m02 - m20) / s; o.x = (m01 + m10) / s; o.y = s / 4; o.z = (m12 + m21) / s; }
	else { const float s = sqrtf(1 + m22 - m00 - m11) * 2; o.w = (m10 - m01) / s; o.x = (m02 + m20) / s; o.y = (m12 + m21) / s; o.z = s / 4; }
	const float n = sqrtf(o.x * o.x + o.y * o.y + o.z * o.z + o.w * o.w);
	o.x /= n; o.y /= n; o.z /= n; o.w /= n;
	o.tx = m[3]; o.ty = m[7]; o.tz = m[11];
	return true;
}

inline bool Finite(const QT& q) {
	return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w) && std::isfinite(q.tx) &&
	       std::isfinite(q.ty) && std::isfinite(q.tz);
}

inline bool SameName(const char* a, const char* b) {   // the game compares joint names without case
	for (;; a++, b++) {
		char x = *a, y = *b;
		if (x >= 'A' && x <= 'Z') x += 32;
		if (y >= 'A' && y <= 'Z') y += 32;
		if (x != y) return false;
		if (!x) return true;
	}
}

// Which of the body's joints take the viewmodel's pose. src[i] >= 0: joint i copies viewmodel joint src[i];
// src[i] == -2: it hangs below a copied joint and follows its parent; -1: untouched.
struct Plan {
	int n = 0, copied = 0, follow = 0;
	int16_t src[kMaxJoints];
	int16_t parent[kMaxJoints];
};

// roots: the joints where the arms start (they and everything below them). False when the skeleton is not in
// parent-before-child order, is too large, or shares no arm joint with the viewmodel.
inline bool Build(Plan& p, const char* const* names, const int16_t* parent, int n, const char* const* vmNames, int vn,
                  const char* const* roots, int nroots) {
	p.n = p.copied = p.follow = 0;
	if (n <= 0 || n > kMaxJoints || vn <= 0 || vn > kMaxJoints) return false;
	for (int i = 0; i < n; i++) {
		if (parent[i] >= i || parent[i] < -1) return false;
		bool in = parent[i] >= 0 && p.src[parent[i]] != -1;
		for (int r = 0; !in && r < nroots; r++) in = SameName(names[i], roots[r]);
		p.parent[i] = parent[i];
		p.src[i] = -1;
		if (!in) continue;
		p.src[i] = -2;
		for (int j = 0; j < vn; j++) if (SameName(names[i], vmNames[j])) { p.src[i] = (int16_t)j; break; }
		if (p.src[i] >= 0) p.copied++; else p.follow++;
	}
	p.n = n;
	return p.copied > 0;
}

// The body's pose arrays (model space absolute, parent-relative) with the viewmodel's arm pose.
// vmToBody = Between(body's world transform, viewmodel's world transform).
inline void Apply(const Plan& p, QT* abs, QT* rel, const QT* vmAbs, const QT& vmToBody) {
	for (int i = 0; i < p.n; i++) {
		const int s = p.src[i], par = p.parent[i];
		if (s >= 0) {
			abs[i] = Mul(vmToBody, vmAbs[s]);
			if (par >= 0) rel[i] = Mul(Inv(abs[par]), abs[i]);
		} else if (s == -2 && par >= 0) abs[i] = Mul(abs[par], rel[i]);
	}
}

inline uint32_t Crc32Lower(const char* s) {     // the joint id the game keeps next to each name
	uint32_t c = 0xFFFFFFFFu;
	for (; *s; s++) {
		unsigned char ch = (unsigned char)*s;
		if (ch >= 'A' && ch <= 'Z') ch += 32;
		c ^= ch;
		for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
	}
	return ~c;
}

}  // namespace armpose
