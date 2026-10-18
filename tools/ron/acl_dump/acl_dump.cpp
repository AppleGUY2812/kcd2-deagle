// acl_dump: decompress one ACL compressed_tracks buffer (Unreal's ACL plugin: an AnimSequence's CompressedByteStream,
// ACL 2.0 format = version 7) at every sample into raw floats, with the ACL 2.1 library (nfrechette/acl, header only).
// Ready or Not's DLC3 Grizzly clips are ACL; CUE4Parse 1.2.2 can't read them without its native ACL build.
//   acl_dump <in.acl> <out.bin>
// out.bin: uint32 tracks, uint32 samples, float sample_rate, float duration, then per sample, per track 10 floats:
// rotation x y z w, translation x y z (Unreal units, cm), scale x y z. Track i maps to a skeleton bone through the
// sequence's CompressedTrackToSkeletonMapTable (tools/ron/acl_decode.py). Read-only, private use.
#include <acl/core/compressed_tracks.h>
#include <acl/decompression/decompress.h>
#include <cstdio>
#include <cstdlib>
#include <vector>

struct Settings : acl::default_transform_decompression_settings {
	static constexpr acl::compressed_tracks_version16 version_supported() { return acl::compressed_tracks_version16::any; }
};

struct Writer : acl::track_writer {
	float* out;
	void RTM_SIMD_CALL write_rotation(uint32_t i, rtm::quatf_arg0 q) {
		float* o = out + i * 10;
		o[0] = rtm::quat_get_x(q); o[1] = rtm::quat_get_y(q); o[2] = rtm::quat_get_z(q); o[3] = rtm::quat_get_w(q);
	}
	void RTM_SIMD_CALL write_translation(uint32_t i, rtm::vector4f_arg0 t) {
		float* o = out + i * 10 + 4;
		o[0] = rtm::vector_get_x(t); o[1] = rtm::vector_get_y(t); o[2] = rtm::vector_get_z(t);
	}
	void RTM_SIMD_CALL write_scale(uint32_t i, rtm::vector4f_arg0 s) {
		float* o = out + i * 10 + 7;
		o[0] = rtm::vector_get_x(s); o[1] = rtm::vector_get_y(s); o[2] = rtm::vector_get_z(s);
	}
};

int main(int argc, char** argv) {
	if (argc < 3) { std::fprintf(stderr, "usage: acl_dump <in.acl> <out.bin>\n"); return 2; }
	FILE* f = std::fopen(argv[1], "rb");
	if (!f) { std::fprintf(stderr, "can't open %s\n", argv[1]); return 1; }
	std::fseek(f, 0, SEEK_END); long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
	// ACL wants its buffer 16-byte aligned
	void* buf = _aligned_malloc(n, 16);
	if (std::fread(buf, 1, n, f) != size_t(n)) { std::fprintf(stderr, "short read\n"); return 1; }
	std::fclose(f);
	const acl::compressed_tracks* tracks = acl::make_compressed_tracks(buf);
	if (!tracks) { std::fprintf(stderr, "not an ACL compressed_tracks buffer\n"); return 1; }
	acl::error_result err = tracks->is_valid(true);
	if (err.any()) { std::fprintf(stderr, "invalid: %s\n", err.c_str()); return 1; }
	if (tracks->get_track_type() != acl::track_type8::qvvf) { std::fprintf(stderr, "not transform tracks\n"); return 1; }
	acl::decompression_context<Settings> ctx;
	if (!ctx.initialize(*tracks)) { std::fprintf(stderr, "can't initialize (version %u)\n", unsigned(tracks->get_version())); return 1; }
	const uint32_t nt = tracks->get_num_tracks(), ns = tracks->get_num_samples_per_track();
	const float rate = tracks->get_sample_rate(), dur = tracks->get_finite_duration();
	std::vector<float> out(size_t(ns) * nt * 10);
	Writer w;
	for (uint32_t s = 0; s < ns; s++) {
		ctx.seek(rate > 0 ? float(s) / rate : 0.0f, acl::sample_rounding_policy::nearest);
		w.out = out.data() + size_t(s) * nt * 10;
		ctx.decompress_tracks(w);
	}
	FILE* o = std::fopen(argv[2], "wb");
	if (!o) { std::fprintf(stderr, "can't write %s\n", argv[2]); return 1; }
	std::fwrite(&nt, 4, 1, o); std::fwrite(&ns, 4, 1, o); std::fwrite(&rate, 4, 1, o); std::fwrite(&dur, 4, 1, o);
	std::fwrite(out.data(), 4, out.size(), o);
	std::fclose(o);
	std::printf("%u tracks, %u samples at %g Hz (%.3f s), ACL version %u\n", nt, ns, rate, dur, unsigned(tracks->get_version()));
	_aligned_free(buf);
	return 0;
}
