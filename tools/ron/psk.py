# ActorX .psk / .pskx reader (CUE4Parse's export of Ready or Not meshes), plain Python, usable inside Blender too.
#   psk.read(path) -> dict: points (cm, Unreal axes), wedges [(point, u, v, material)], faces [(w0, w1, w2, material,
#   smoothing)], materials [names], bones [(name, parent, (qx, qy, qz, qw), (tx, ty, tz) cm)] (parent-relative),
#   weights [(weight, point, bone)], extra uv sets, vertex colours when present.
# CUE4Parse writes ActorX Y-mirrored (right-handed): every point and bone translation has Y negated and every rotation
# is (x, -y, z, w) against the game's own skeleton (checked: 133 / 133 arm bones, 16 / 16 gun bones vs the uasset
# reference pose, Grizzly MK V 2026-10-18). read() undoes it, so everything comes back in Unreal's axes; the mirror
# also reverses triangle winding, so a front face is (w0, w2, w1) in Unreal axes.
# Chunks: 20-byte id, int type flags, int item size, int count. Read-only; the meshes stay in E:/Tools/deagle-work.
import struct


def _chunks(d):
    o, ch = 0, {}
    while o + 32 <= len(d):
        cid = d[o:o + 20].split(b"\0")[0].decode("latin-1")
        _, size, cnt = struct.unpack_from("<iii", d, o + 20)
        o += 32
        ch[cid] = (o, size, cnt)
        o += size * cnt
    return ch


def read(path):
    d = open(path, "rb").read()
    ch = _chunks(d)
    out = {"chunks": sorted(ch)}
    po, _, pc = ch["PNTS0000"]
    out["points"] = [(x, -y, z) for x, y, z in (struct.unpack_from("<fff", d, po + 12 * i) for i in range(pc))]
    wo, ws, wc = ch["VTXW0000"]
    wed = []
    for i in range(wc):
        b = wo + ws * i
        pi = struct.unpack_from("<I", d, b)[0] if pc > 65536 else struct.unpack_from("<H", d, b)[0]
        u, v = struct.unpack_from("<ff", d, b + 4)
        mat = d[b + 12]
        wed.append((pi, u, v, mat))
    out["wedges"] = wed
    fk = "FACE3200" if "FACE3200" in ch else "FACE0000"
    fo, fs, fc = ch[fk]
    faces = []
    for i in range(fc):
        b = fo + fs * i
        if fk == "FACE3200":
            w = struct.unpack_from("<III", d, b); mat, aux = d[b + 12], d[b + 13]
        else:
            w = struct.unpack_from("<HHH", d, b); mat, aux = d[b + 6], d[b + 7]
        faces.append((w[0], w[1], w[2], mat, aux))
    out["faces"] = faces
    mats = []
    if "MATT0000" in ch:
        mo, ms, mc = ch["MATT0000"]
        for i in range(mc):
            mats.append(d[mo + ms * i:mo + ms * i + 64].split(b"\0")[0].decode("latin-1"))
    out["materials"] = mats
    bones = []
    for key in ("REFSKELT", "REFSKEL0"):
        if key in ch:
            bo, bs, bc = ch[key]
            for i in range(bc):
                b = bo + bs * i
                name = d[b:b + 64].split(b"\0")[0].decode("latin-1")
                _, _, parent = struct.unpack_from("<iii", d, b + 64)
                qx, qy, qz, qw = struct.unpack_from("<ffff", d, b + 76)
                tx, ty, tz = struct.unpack_from("<fff", d, b + 92)
                bones.append((name, parent if i else -1, (qx, -qy, qz, qw), (tx, -ty, tz)))
            break
    out["bones"] = bones
    weights = []
    if "RAWWEIGHTS" in ch:
        ro, rs, rc = ch["RAWWEIGHTS"]
        for i in range(rc):
            w, p, b = struct.unpack_from("<fii", d, ro + rs * i)
            weights.append((w, p, b))
    out["weights"] = weights
    extra = []
    for k in sorted(ch):
        if k.startswith("EXTRAUVS"):
            eo, es, ec = ch[k]
            extra.append([struct.unpack_from("<ff", d, eo + es * i) for i in range(ec)])
    out["extra_uvs"] = extra
    if "VERTEXCOLOR" in ch:
        co, cs, cc = ch["VERTEXCOLOR"]
        out["colors"] = [tuple(d[co + cs * i:co + cs * i + 4]) for i in range(cc)]
    return out


if __name__ == "__main__":
    import sys
    m = read(sys.argv[1])
    print(sys.argv[1].split("/")[-1], "chunks", m["chunks"])
    print(" ", len(m["points"]), "points,", len(m["wedges"]), "wedges,", len(m["faces"]), "faces,", len(m["bones"]), "bones,",
          len(m["weights"]), "weights, materials", m["materials"])
    xs = [p[0] for p in m["points"]]; ys = [p[1] for p in m["points"]]; zs = [p[2] for p in m["points"]]
    print("  bounds cm x %.1f..%.1f y %.1f..%.1f z %.1f..%.1f" % (min(xs), max(xs), min(ys), max(ys), min(zs), max(zs)))
    if len(sys.argv) > 2:
        for i, (n, p, q, t) in enumerate(m["bones"]):
            print("   ", i, n, "parent", p, "t", tuple(round(x, 2) for x in t))
