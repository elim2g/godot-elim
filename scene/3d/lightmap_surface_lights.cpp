/**************************************************************************/
/*  lightmap_surface_lights.cpp                                           */
/**************************************************************************/
/* TURNT ADDITION -- not upstream Godot.                                  */
/*                                                                        */
/* Emissive surfaces as analytic area lights for LightmapGI: surfaces     */
/* whose material carries the "tnt_surface_light" meta are merged into    */
/* coplanar islands, and each island becomes one AREA_POLY light.         */
/**************************************************************************/

#include "lightmap_surface_lights.h"

#include "core/math/geometry_2d.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"

// Analytic polygonal surface lights: surfaces whose effective material
// carries the bool meta "tnt_surface_light" are merged into coplanar islands,
// and each island becomes ONE LIGHT_TYPE_AREA_POLY light carrying its polygon
// geometry — integrated in the direct pass with exact edge-sum irradiance and
// ray-estimated visibility (no centroid discretization, no Monte Carlo blotch).
static constexpr float SURFACE_LIGHT_MIN_AREA = 1e-4f; // Degenerate-sliver island cull.
// Per-island triangle cap, applied to the island's SIMPLIFIED polygon (see
// _surface_light_simplify_island) — not to the importer's raw fragment count.
// Convex islands collapse to a hull fan of a handful of triangles; a non-convex
// island is rebuilt from its boundary, which strips the T-junction slivers.
// Past the cap an island falls back to its hull fan with radiance scaled by
// island_area/hull_area: power-conserving but smeared, so a concave emitter
// leaks onto non-emissive geometry and dims. The cap sits far above any real
// face because the edge sum costs a few dozen flops per triangle and the
// batcher's cost model charges each triangle explicitly.
static constexpr uint32_t SURFACE_LIGHT_MAX_POLY_TRIS = 1024;
// Global polygon-vertex backstop against a pathological/misconfigured map
// (e.g. a texture accidentally flagged emissive across the whole level)
// exhausting VRAM. Past it, further emitter islands are dropped entirely (a
// real, logged loss of light) — on a legitimate map, raise it rather than
// accept the drop.
static constexpr uint32_t SURFACE_LIGHT_MAX_POLY_VERTS_TOTAL = 262144;
// Convex-exactness tolerance: an island whose in-plane convex hull area is
// within 1% of its member-triangle area IS its hull (the excess is weld/float
// slop), so the hull fan reproduces it exactly with minimal triangles.
static constexpr float SURFACE_LIGHT_HULL_TOLERANCE = 1.01f;
// Lambertian radiance→irradiance normalization (the exitance of a diffuse
// emitter of radiance L is πL; energies are authored as radiance).
static constexpr float SURFACE_LIGHT_ENERGY_SCALE = 1.0f / (float)Math::PI;
// Range-cull contribution threshold: the derived range solves the unshadowed
// peak contribution E·A/d² = CUTOFF. Per-texel cost is bounded by island count
// × visibility rays, so a large emitter may legitimately reach far.
static constexpr float SURFACE_LIGHT_CUTOFF = 0.005f;
static constexpr float SURFACE_LIGHT_RANGE_MAX = 512.0f;
// <ELIM> Coplanar-contact occlusion is an AREA fraction over an island's member
// triangles, not a single centroid probe (see the cull site). Only an island that
// is essentially fully pressed against solid is dropped. The sample cap bounds the
// grid queries on very large islands; islands are far below it in practice.
static constexpr float SURFACE_LIGHT_OCC_CULL_FRACTION = 0.98f;
static constexpr uint32_t SURFACE_LIGHT_OCC_MAX_SAMPLES = 256u;
// </ELIM>
// Connectivity + coplanarity merge. The importer's vertex-weld / T-junction pass
// re-triangulates one logical flat emissive brush face into dozens–hundreds of
// tiny slivers (needed to seal see-through seam cracks against neighbouring
// geometry — see surface_gatherer.cpp; must NOT be bypassed). Emitting those
// fragments as individual lights would track the raw fragment count instead of
// real face count. Fix: flood-fill each surface's emitter triangles into
// connected components gated by exact coplanarity — every sliver of one original
// planar brush face is mutually edge-adjacent and coplanar, so they collapse into ONE
// island regardless of fragmentation, recovering the true logical face. Two
// genuinely different faces/fixtures are either not edge-adjacent or (if they touch)
// disagree in normal, so they stay distinct. Light count then tracks real faces.
static constexpr float SURFACE_LIGHT_WELD_EPS = 1e-3f; // Vert quantization for edge matching; finer than welded-coincident spacing.
static constexpr float SURFACE_LIGHT_COPLANAR_DOT = 0.9995f; // Tight near-parallel gate; brush faces are exactly planar, not "roughly flat".
// Coplanar-contact occlusion cull. An emitter texture flags EVERY face of its brush
// (e.g. all six faces of a lava prism), so the underside/sides sandwiched face-to-face
// against the sealed shell or a wall emit straight into solid: wasted lights that
// still cost direct-pass visibility rays. Detect it purely
// geometrically — a solid non-emitter occluder triangle that is coincident with (SAME
// plane), anti-parallel to, and overlapping the emitter face at its centroid means the
// face is pressed flush against solid. Deliberately keyed on TRUE coincidence (offset ≈ 0
// — the flush-contact signature, only float error away): (1) a genuine gap breaks
// coincidence, so lava legitimately lighting a narrow slot is never culled; (2) it keys on
// the SAME plane, so the opposite (non-emitter) face of a thin two-sided emitter brush —
// which sits a brush-thickness behind, not on the emitter plane — is not mistaken for a
// seal (that emitter fires into open space). CONTACT_EPS is far above post-transform float
// error yet far below any real brush thickness, so both hold.
static constexpr float SURFACE_LIGHT_CONTACT_EPS = 0.02f; // m; flush-coincidence band (>> float error, << any real brush thickness/light gap).
static constexpr float SURFACE_LIGHT_CONTACT_DOT = -0.99f; // Anti-parallel gate: opposing faces point into each other (dot ≈ -1).
static constexpr float SURFACE_LIGHT_OCC_CELL = 4.0f; // m; occluder spatial-hash cell (chunked worldspawn keeps face tris well under this).


// Bake flags of a surface's effective (override-preferred) material; the values
// are LightmapperRD::SURFACE_FLAG_SKY and SURFACE_FLAG_SURFACE_LIGHT.
static constexpr uint32_t SURFACE_BAKE_FLAG_SKY = 1u;
static constexpr uint32_t SURFACE_BAKE_FLAG_SURFACE_LIGHT = 4u;

uint32_t LightmapSurfaceLights::surface_bake_flags(const Ref<Material> &p_material) {
	if (p_material.is_null()) {
		return 0;
	}
	uint32_t flags = 0;
	if (bool(p_material->get_meta(SNAME("tnt_sky"), false))) {
		flags |= SURFACE_BAKE_FLAG_SKY;
	}
	const Ref<BaseMaterial3D> bm = p_material;
	if (bool(p_material->get_meta(SNAME("tnt_surface_light"), false)) && bm.is_valid() && bm->get_feature(BaseMaterial3D::FEATURE_EMISSION)) {
		flags |= SURFACE_BAKE_FLAG_SURFACE_LIGHT;
	}
	return flags;
}

// Quantized world vertex + undirected edge key for triangle-adjacency matching.
// Snapping to SURFACE_LIGHT_WELD_EPS makes coincident (already-welded) verts hash
// identically despite post-xform float error, so shared edges are found.
struct SurfaceLightVertKey {
	int32_t x;
	int32_t y;
	int32_t z;
	bool operator==(const SurfaceLightVertKey &p_o) const { return x == p_o.x && y == p_o.y && z == p_o.z; }
	bool operator<(const SurfaceLightVertKey &p_o) const {
		if (x != p_o.x) {
			return x < p_o.x;
		}
		if (y != p_o.y) {
			return y < p_o.y;
		}
		return z < p_o.z;
	}
};

struct SurfaceLightEdgeKey {
	SurfaceLightVertKey a; // Canonical order (a <= b) so edge (u,v) == (v,u).
	SurfaceLightVertKey b;
	bool operator==(const SurfaceLightEdgeKey &p_o) const { return a == p_o.a && b == p_o.b; }
};

struct SurfaceLightEdgeKeyHasher {
	static uint32_t hash(const SurfaceLightEdgeKey &p_k) {
		uint32_t h = hash_murmur3_one_32((uint32_t)p_k.a.x);
		h = hash_murmur3_one_32((uint32_t)p_k.a.y, h);
		h = hash_murmur3_one_32((uint32_t)p_k.a.z, h);
		h = hash_murmur3_one_32((uint32_t)p_k.b.x, h);
		h = hash_murmur3_one_32((uint32_t)p_k.b.y, h);
		h = hash_murmur3_one_32((uint32_t)p_k.b.z, h);
		return hash_fmix32(h);
	}
};

// One emitter triangle (world space) plus a connected coplanar component (a logical
// face) accumulated over its member triangles, and a cell accumulator for retiling an
// oversized component. Area-weighted centroid/normal + summed area are all an area
// patch needs; tris keeps the member list for the oversized-face grid pass.
struct SurfaceLightTri {
	Vector3 v[3];
	Vector3 normal; // Vertex normal: authoritatively out of the emitting face.
	Vector3 centroid;
	float area = 0.0f;
};

// One merged coplanar island (a logical emitter face) accumulated over its member
// triangles. Held until the adaptive subdivide pass so the effective patch area can
// be derived from the WHOLE map's emissive area before any island is tiled. Member
// triangles are kept for the oversized-island retile path.
struct SurfaceLightIsland {
	Vector3 pos_sum; // Area-weighted centroid accumulator.
	Vector3 normal_sum; // Area-weighted normal accumulator.
	float area = 0.0f;
	Color color;
	float energy = 0.0f;
	int32_t group = -1; // Emitter fixture, see the grouping pass.
	LocalVector<SurfaceLightTri> tris;
};

// One solid (non-emitter, non-sky) world-space occluder triangle for the coplanar-contact
// cull. normal is the vertex normal (authoritatively outward), used for the anti-parallel
// gate against an emitter face pressed against it.
struct SurfaceLightOccTri {
	Vector3 v0;
	Vector3 v1;
	Vector3 v2;
	Vector3 normal;
};

static SurfaceLightEdgeKey _surface_light_edge_key(const Vector3 &p_a, const Vector3 &p_b) {
	const float inv = 1.0f / SURFACE_LIGHT_WELD_EPS;
	const SurfaceLightVertKey ka{ (int32_t)Math::round(p_a.x * inv), (int32_t)Math::round(p_a.y * inv), (int32_t)Math::round(p_a.z * inv) };
	const SurfaceLightVertKey kb{ (int32_t)Math::round(p_b.x * inv), (int32_t)Math::round(p_b.y * inv), (int32_t)Math::round(p_b.z * inv) };
	SurfaceLightEdgeKey e;
	if (kb < ka) {
		e.a = kb;
		e.b = ka;
	} else {
		e.a = ka;
		e.b = kb;
	}
	return e;
}

// Union-find with path halving over the per-surface triangle parent array.
static int32_t _surface_light_uf_find(int32_t *p_parent, int32_t p_i) {
	while (p_parent[p_i] != p_i) {
		p_parent[p_i] = p_parent[p_parent[p_i]];
		p_i = p_parent[p_i];
	}
	return p_i;
}

// Occluder spatial-hash cell for a world point (see SURFACE_LIGHT_OCC_CELL).
static _FORCE_INLINE_ Vector3i _surface_light_occ_cell(const Vector3 &p_p) {
	const float inv = 1.0f / SURFACE_LIGHT_OCC_CELL;
	return Vector3i((int32_t)Math::floor(p_p.x * inv), (int32_t)Math::floor(p_p.y * inv), (int32_t)Math::floor(p_p.z * inv));
}

// Insert an occluder triangle index into every grid cell its AABB spans, so a query at
// any point the triangle covers finds it in a single cell lookup. Worldspawn chunking
// bounds face size, so a triangle spans only a handful of cells; the guard is a hard
// backstop against a pathological giant tri (falls back to a single centroid cell, a
// conservative miss — it can only fail to cull, never cull wrongly).
static void _surface_light_occ_insert(HashMap<Vector3i, LocalVector<int32_t>> &r_grid, int32_t p_idx, const Vector3 &p_a, const Vector3 &p_b, const Vector3 &p_c) {
	const Vector3 mn(MIN(p_a.x, MIN(p_b.x, p_c.x)), MIN(p_a.y, MIN(p_b.y, p_c.y)), MIN(p_a.z, MIN(p_b.z, p_c.z)));
	const Vector3 mx(MAX(p_a.x, MAX(p_b.x, p_c.x)), MAX(p_a.y, MAX(p_b.y, p_c.y)), MAX(p_a.z, MAX(p_b.z, p_c.z)));
	const Vector3i cmn = _surface_light_occ_cell(mn);
	const Vector3i cmx = _surface_light_occ_cell(mx);
	if ((int64_t)(cmx.x - cmn.x + 1) * (int64_t)(cmx.y - cmn.y + 1) * (int64_t)(cmx.z - cmn.z + 1) > 4096) {
		r_grid[_surface_light_occ_cell((p_a + p_b + p_c) * (1.0f / 3.0f))].push_back(p_idx);
		return;
	}
	for (int32_t cx = cmn.x; cx <= cmx.x; cx++) {
		for (int32_t cy = cmn.y; cy <= cmx.y; cy++) {
			for (int32_t cz = cmn.z; cz <= cmx.z; cz++) {
				r_grid[Vector3i(cx, cy, cz)].push_back(p_idx);
			}
		}
	}
}

// Winding-agnostic point-in-triangle in the triangle's own plane (p is assumed already
// near-coplanar with a/b/c). Inside iff p sits on the same side of all three edges.
static bool _surface_light_point_in_tri(const Vector3 &p_p, const Vector3 &p_a, const Vector3 &p_b, const Vector3 &p_c, const Vector3 &p_n) {
	const float s0 = (p_b - p_a).cross(p_p - p_a).dot(p_n);
	const float s1 = (p_c - p_b).cross(p_p - p_b).dot(p_n);
	const float s2 = (p_a - p_c).cross(p_p - p_c).dot(p_n);
	return (s0 >= -CMP_EPSILON && s1 >= -CMP_EPSILON && s2 >= -CMP_EPSILON) ||
			(s0 <= CMP_EPSILON && s1 <= CMP_EPSILON && s2 <= CMP_EPSILON);
}

// True if an emitter face centroid sits in face-to-face contact with solid: a solid
// occluder triangle that is anti-parallel, near-coincident (within CONTACT_EPS along the
// normal), and contains the point in-plane. Detects an emitter jammed against the sealed
// shell / a wall — it emits straight into solid. See the SURFACE_LIGHT_CONTACT_* comment.
static bool _surface_light_test_occluded(const HashMap<Vector3i, LocalVector<int32_t>> &p_grid, const LocalVector<SurfaceLightOccTri> &p_occ, const Vector3 &p_point, const Vector3 &p_normal) {
	const HashMap<Vector3i, LocalVector<int32_t>>::ConstIterator it = p_grid.find(_surface_light_occ_cell(p_point));
	if (it == p_grid.end()) {
		return false;
	}
	const LocalVector<int32_t> &bucket = it->value;
	for (uint32_t k = 0; k < bucket.size(); k++) {
		const SurfaceLightOccTri &t = p_occ[bucket[k]];
		if (t.normal.dot(p_normal) > SURFACE_LIGHT_CONTACT_DOT) {
			continue; // Not an opposing (anti-parallel) face.
		}
		if (Math::abs((p_point - t.v0).dot(t.normal)) > SURFACE_LIGHT_CONTACT_EPS) {
			continue; // A real gap separates the faces — not contact.
		}
		if (_surface_light_point_in_tri(p_point, t.v0, t.v1, t.v2, t.normal)) {
			return true;
		}
	}
	return false;
}

// <ELIM> Island polygon simplification: boundary extraction + retriangulation.
//
// The importer's weld / T-junction pass shatters one logical flat emissive face
// into dozens-to-hundreds of slivers (needed to seal see-through seam cracks —
// see surface_gatherer.cpp). Those slivers are the island's INTERIOR
// tessellation, not its shape: every interior edge is shared by two members and
// contributes nothing, so the island is fully described by its boundary loops.
// Extracting that boundary, dropping the collinear T-junction vertices strung
// along it, and re-triangulating reproduces the island EXACTLY with a handful of
// triangles. That is what makes the triangle cap stop binding: a concave
// (coplanar-merged multi-brush) emitter no longer has to choose between hundreds
// of shader triangles and a convex hull that smears its light over geometry that
// is not emissive at all.
//
// Every step is exact or bails. Non-manifold winding, unclosed loops, more than
// one outer loop, un-bridgeable holes, and any area mismatch against the member
// triangles all return false, and the caller keeps its existing behaviour rather
// than emitting geometry that is merely plausible.
static constexpr float SURFACE_LIGHT_COLLINEAR_EPS = 1e-3f; // m; boundary deviation treated as collinear (matches the weld grid).
static constexpr uint64_t SURFACE_LIGHT_BRIDGE_WORK_LIMIT = 4000000; // Hole-bridge search product guard; simplified loops are orders of magnitude under.
static constexpr uint32_t SURFACE_LIGHT_MAX_BOUNDARY_VERTS = 512; // Ear-clip input bound; a simplified contour is a face's real corner count.

// Quantized 2D vertex key for boundary extraction, at the same weld granularity
// as the 3D edge matching that built the island.
struct SurfaceLightVert2Key {
	int32_t x;
	int32_t y;
	bool operator==(const SurfaceLightVert2Key &p_o) const { return x == p_o.x && y == p_o.y; }
};

struct SurfaceLightVert2KeyHasher {
	static uint32_t hash(const SurfaceLightVert2Key &p_k) {
		uint32_t h = hash_murmur3_one_32((uint32_t)p_k.x);
		h = hash_murmur3_one_32((uint32_t)p_k.y, h);
		return hash_fmix32(h);
	}
};

static double _surface_light_loop_area(const LocalVector<Vector2> &p_loop) {
	double a = 0.0;
	const uint32_t n = p_loop.size();
	for (uint32_t i = 0; i < n; i++) {
		a += (double)p_loop[i].cross(p_loop[(i + 1) % n]);
	}
	return a * 0.5;
}

// Drop boundary vertices sitting on the straight line between their neighbours —
// the T-junction fragmentation leaves long runs of them, and they are the bulk of
// the triangle count the cap was fighting.
static void _surface_light_drop_collinear(LocalVector<Vector2> &r_loop) {
	bool changed = true;
	while (changed && r_loop.size() > 3) {
		changed = false;
		for (uint32_t i = 0; i < r_loop.size() && r_loop.size() > 3;) {
			const Vector2 prev = r_loop[(i + r_loop.size() - 1) % r_loop.size()];
			const Vector2 cur = r_loop[i];
			const Vector2 next = r_loop[(i + 1) % r_loop.size()];
			const Vector2 seg = next - prev;
			const float seg_len = seg.length();
			// A backtracking spike (next == prev) is not collinear slop: removing
			// its tip would change the shape, so the zero-length case keeps it.
			const float dev = seg_len > CMP_EPSILON ? Math::abs(seg.cross(cur - prev)) / seg_len : cur.distance_to(prev);
			if (dev <= SURFACE_LIGHT_COLLINEAR_EPS) {
				r_loop.remove_at(i);
				changed = true;
			} else {
				i++;
			}
		}
	}
}

// True when the candidate bridge a-b properly crosses any loop edge. Edges that
// merely share an endpoint with the bridge are not crossings.
static bool _surface_light_bridge_blocked(const Vector2 &p_a, const Vector2 &p_b, const LocalVector<LocalVector<Vector2>> &p_loops) {
	for (uint32_t l = 0; l < p_loops.size(); l++) {
		const LocalVector<Vector2> &loop = p_loops[l];
		const uint32_t n = loop.size();
		for (uint32_t i = 0; i < n; i++) {
			const Vector2 &e0 = loop[i];
			const Vector2 &e1 = loop[(i + 1) % n];
			if (e0.is_equal_approx(p_a) || e0.is_equal_approx(p_b) || e1.is_equal_approx(p_a) || e1.is_equal_approx(p_b)) {
				continue;
			}
			Vector2 hit;
			if (Geometry2D::segment_intersects_segment(p_a, p_b, e0, e1, &hit)) {
				return true;
			}
		}
	}
	return false;
}

// Rebuild an island's polygon from its boundary. On success fills r_tris_2d with
// flat triangle triples in the island's (t_axis, b_axis) frame, relative to the
// island centroid, wound CCW (geometric normal along the island normal). Returns
// false when the member triangles are not a clean tessellation of a single
// region.
static bool _surface_light_simplify_island(const LocalVector<SurfaceLightTri> &p_tris, const Vector3 &p_centroid, const Vector3 &p_t_axis, const Vector3 &p_b_axis, float p_island_area, LocalVector<Vector2> &r_tris_2d) {
	r_tris_2d.clear();

	// Unique 2D vertices + CCW index triangles.
	const float inv_weld = 1.0f / SURFACE_LIGHT_WELD_EPS;
	HashMap<SurfaceLightVert2Key, int32_t, SurfaceLightVert2KeyHasher> vert_ids;
	LocalVector<Vector2> verts;
	LocalVector<int32_t> tri_idx;
	for (uint32_t m = 0; m < p_tris.size(); m++) {
		int32_t id[3];
		for (int k = 0; k < 3; k++) {
			const Vector3 rel = p_tris[m].v[k] - p_centroid;
			const Vector2 p(rel.dot(p_t_axis), rel.dot(p_b_axis));
			const SurfaceLightVert2Key key{ (int32_t)Math::round(p.x * inv_weld), (int32_t)Math::round(p.y * inv_weld) };
			HashMap<SurfaceLightVert2Key, int32_t, SurfaceLightVert2KeyHasher>::Iterator it = vert_ids.find(key);
			if (it == vert_ids.end()) {
				id[k] = (int32_t)verts.size();
				verts.push_back(p);
				vert_ids.insert(key, id[k]);
			} else {
				id[k] = it->value;
			}
		}
		if (id[0] == id[1] || id[1] == id[2] || id[0] == id[2]) {
			// Collapsed by the weld: its edges cancel in place, so it neither
			// bounds the island nor carries area.
			continue;
		}
		const float cr = (verts[id[1]] - verts[id[0]]).cross(verts[id[2]] - verts[id[0]]);
		if (Math::abs(cr) < CMP_EPSILON) {
			return false; // Three distinct but collinear verts: no usable winding.
		}
		if (cr < 0.0f) {
			SWAP(id[1], id[2]);
		}
		tri_idx.push_back(id[0]);
		tri_idx.push_back(id[1]);
		tri_idx.push_back(id[2]);
	}
	if (tri_idx.size() < 3) {
		return false;
	}

	// Directed edge set. A clean tessellation uses every directed edge at most
	// once; an interior edge appears once in each direction, a boundary edge in
	// only one.
	HashMap<uint64_t, int32_t> edge_set;
	for (uint32_t e = 0; e < tri_idx.size(); e += 3) {
		for (int k = 0; k < 3; k++) {
			const uint64_t key = ((uint64_t)(uint32_t)tri_idx[e + k] << 32) | (uint64_t)(uint32_t)tri_idx[e + (k + 1) % 3];
			if (edge_set.has(key)) {
				return false; // Overlapping or duplicated winding.
			}
			edge_set.insert(key, 1);
		}
	}

	HashMap<int32_t, LocalVector<int32_t>> next_from;
	uint32_t boundary_edges = 0;
	for (const KeyValue<uint64_t, int32_t> &kv : edge_set) {
		const uint32_t a = (uint32_t)(kv.key >> 32);
		const uint32_t b = (uint32_t)(kv.key & 0xffffffffULL);
		if (edge_set.has(((uint64_t)b << 32) | (uint64_t)a)) {
			continue; // Interior.
		}
		next_from[(int32_t)a].push_back((int32_t)b);
		boundary_edges++;
	}
	if (boundary_edges < 3) {
		return false;
	}

	// Chain boundary edges into closed loops.
	LocalVector<LocalVector<Vector2>> loops;
	uint32_t consumed = 0;
	for (KeyValue<int32_t, LocalVector<int32_t>> &kv : next_from) {
		while (!kv.value.is_empty()) {
			const int32_t start = kv.key;
			int32_t cur = start;
			LocalVector<int32_t> loop_idx;
			bool closed = false;
			while (loop_idx.size() <= boundary_edges) {
				LocalVector<int32_t> *outs = next_from.getptr(cur);
				if (!outs || outs->is_empty()) {
					break;
				}
				const int32_t nxt = (*outs)[outs->size() - 1];
				outs->remove_at(outs->size() - 1);
				consumed++;
				loop_idx.push_back(cur);
				cur = nxt;
				if (cur == start) {
					closed = true;
					break;
				}
			}
			if (!closed || loop_idx.size() < 3) {
				return false;
			}
			LocalVector<Vector2> loop;
			loop.resize(loop_idx.size());
			for (uint32_t i = 0; i < loop_idx.size(); i++) {
				loop[i] = verts[loop_idx[i]];
			}
			_surface_light_drop_collinear(loop);
			if (loop.size() >= 3) {
				loops.push_back(loop);
			}
		}
	}
	if (consumed != boundary_edges || loops.is_empty()) {
		return false;
	}

	// Exactly one outer loop; the rest are holes (a pillar standing in a lava
	// pool). Degenerate zero-area loops are dropped.
	LocalVector<Vector2> outer;
	LocalVector<LocalVector<Vector2>> holes;
	for (uint32_t l = 0; l < loops.size(); l++) {
		const double a = _surface_light_loop_area(loops[l]);
		if (a > (double)SURFACE_LIGHT_MIN_AREA) {
			if (!outer.is_empty()) {
				return false;
			}
			outer = loops[l];
		} else if (a < -(double)SURFACE_LIGHT_MIN_AREA) {
			// Kept CW, i.e. opposite the outer contour: traversed that way the hole
			// boundary keeps the island's material on the same side the outer
			// traversal does, which is what the bridge splice below needs.
			holes.push_back(loops[l]);
		}
	}
	if (outer.size() < 3) {
		return false;
	}

	// Splice each hole into the outer contour with a zero-width bridge (the
	// shortest hole->outer vertex pair whose connecting segment crosses nothing),
	// producing one simple polygon the ear clipper can handle.
	while (!holes.is_empty()) {
		uint32_t hole_verts = 0;
		for (uint32_t h = 0; h < holes.size(); h++) {
			hole_verts += holes[h].size();
		}
		if ((uint64_t)hole_verts * (uint64_t)outer.size() > SURFACE_LIGHT_BRIDGE_WORK_LIMIT) {
			return false;
		}
		LocalVector<LocalVector<Vector2>> obstacles;
		obstacles.push_back(outer);
		for (uint32_t h = 0; h < holes.size(); h++) {
			obstacles.push_back(holes[h]);
		}
		uint32_t best_h = 0;
		uint32_t best_hi = 0;
		uint32_t best_oi = 0;
		float best_d = 0.0f;
		bool found = false;
		for (uint32_t h = 0; h < holes.size(); h++) {
			for (uint32_t hi = 0; hi < holes[h].size(); hi++) {
				for (uint32_t oi = 0; oi < outer.size(); oi++) {
					const float d = outer[oi].distance_squared_to(holes[h][hi]);
					if (found && d >= best_d) {
						continue;
					}
					if (_surface_light_bridge_blocked(outer[oi], holes[h][hi], obstacles)) {
						continue;
					}
					best_h = h;
					best_hi = hi;
					best_oi = oi;
					best_d = d;
					found = true;
				}
			}
		}
		if (!found) {
			return false;
		}
		const LocalVector<Vector2> hole = holes[best_h];
		LocalVector<Vector2> merged;
		merged.reserve(outer.size() + hole.size() + 2);
		for (uint32_t i = 0; i <= best_oi; i++) {
			merged.push_back(outer[i]);
		}
		for (uint32_t i = 0; i < hole.size(); i++) {
			merged.push_back(hole[(best_hi + i) % hole.size()]);
		}
		merged.push_back(hole[best_hi]);
		for (uint32_t i = best_oi; i < outer.size(); i++) {
			merged.push_back(outer[i]);
		}
		outer = merged;
		holes.remove_at(best_h);
	}

	if (outer.size() > SURFACE_LIGHT_MAX_BOUNDARY_VERTS) {
		return false; // Ear clipping is worst-case cubic; don't stall the bake on a pathological contour.
	}

	Vector<Vector2> poly;
	poly.resize((int)outer.size());
	for (uint32_t i = 0; i < outer.size(); i++) {
		poly.write[(int)i] = outer[i];
	}
	const Vector<int> idx = Geometry2D::triangulate_polygon(poly);
	if (idx.size() < 3 || idx.size() % 3 != 0) {
		return false;
	}

	// The simplification is only worth trusting if it reproduces the island's
	// area. A self-intersecting loop from a pinched boundary or a bad bridge
	// shows up here and falls back.
	double tri_area_sum = 0.0;
	for (int t = 0; t < idx.size(); t += 3) {
		const Vector2 &a = poly[idx[t + 0]];
		const Vector2 &b = poly[idx[t + 1]];
		const Vector2 &c = poly[idx[t + 2]];
		tri_area_sum += 0.5 * (double)Math::abs((b - a).cross(c - a));
	}
	if (Math::abs(tri_area_sum - (double)p_island_area) > (double)p_island_area * ((double)SURFACE_LIGHT_HULL_TOLERANCE - 1.0)) {
		return false;
	}

	for (int t = 0; t < idx.size(); t += 3) {
		Vector2 tv[3] = { poly[idx[t + 0]], poly[idx[t + 1]], poly[idx[t + 2]] };
		const float cr = (tv[1] - tv[0]).cross(tv[2] - tv[0]);
		if (0.5f * Math::abs(cr) < SURFACE_LIGHT_MIN_AREA) {
			continue; // Bridge sliver: no area to emit, no CDF weight to carry.
		}
		if (cr < 0.0f) {
			SWAP(tv[1], tv[2]);
		}
		for (int k = 0; k < 3; k++) {
			r_tris_2d.push_back(tv[k]);
		}
	}
	return r_tris_2d.size() >= 3;
}
// </ELIM>

// Shared surface-light island collector: gather emitter triangles from the given
// meshes, merge coplanar islands (union-find over welded edges gated by exact
// coplanarity), then extract each island's analytic polygon (convex hull fan when
// exact, raw member triangles otherwise). One light per island. Frame-agnostic:
// islands come out in whatever frame the input xforms are in. Used by both the
// bake prepare path and the GDScript debug hook so the visualizer is exactly
// faithful to the bake.
// <ELIM> Convex-merge fallback for islands the boundary rebuild cannot handle.
//
// _surface_light_simplify_island is exact-or-nothing: a pinched boundary, a hole it
// cannot bridge, or an area mismatch drops the island to EMIT_RAW_MEMBERS. Measured
// on dfwc2017-6 (test/manual/diag_surface_light_complexity.gd), that is where the
// triangle count actually lives — the raw-member islands hold over half the map's
// polygon triangles, and the worst ships 695 triangles for a 63-corner face. The
// bake charges 2 trace-equivalents per triangle, so this is bake throughput, not
// just memory. Note the 512-vertex contour cap is NOT what binds (worst case
// measured: 298) and the collinear epsilon is already doing its job — neither is
// worth touching.
//
// This is the monotone fallback. It never rebuilds a global contour, so holes and
// pinch vertices cannot defeat it: it grows maximal CONVEX regions out of the member
// triangles and fans each one. If nothing merges it emits exactly the raw members,
// so it can never be worse than the path it replaces.
//
// A region is accepted only when the convex hull of its accumulated vertices has the
// same area as the sum of the member triangles inside it. That single test proves
// convex, gap-free and non-overlapping simultaneously, so the emitted area is
// preserved by construction rather than by a tolerance check after the fact.
static constexpr uint32_t SURFACE_LIGHT_MERGE_MAX_REGION_VERTS = 64; // Bounds per-candidate hull cost.
static constexpr double SURFACE_LIGHT_MERGE_AREA_REL_EPS = 1e-4;

static bool _surface_light_convex_merge_island(const LocalVector<SurfaceLightTri> &p_tris,
		const Vector3 &p_centroid, const Vector3 &p_t_axis, const Vector3 &p_b_axis,
		const Vector3 &p_normal, LocalVector<Vector2> &r_tris_2d) {
	using EdgeMap = HashMap<SurfaceLightEdgeKey, Vector2i, SurfaceLightEdgeKeyHasher>;

	// Project into the island frame, winding-normalized CCW, dropping the same
	// degenerate slivers the raw-member emit path drops.
	LocalVector<Vector2> tv;
	LocalVector<float> tri_area;
	LocalVector<uint32_t> src_index;
	for (uint32_t m = 0; m < p_tris.size(); m++) {
		const SurfaceLightTri &tri = p_tris[m];
		if (tri.area < SURFACE_LIGHT_MIN_AREA) {
			continue;
		}
		const bool flip = (tri.v[1] - tri.v[0]).cross(tri.v[2] - tri.v[0]).dot(p_normal) < 0.0f;
		const Vector3 src[3] = { tri.v[0], flip ? tri.v[2] : tri.v[1], flip ? tri.v[1] : tri.v[2] };
		for (int k = 0; k < 3; k++) {
			const Vector3 rel = src[k] - p_centroid;
			tv.push_back(Vector2(rel.dot(p_t_axis), rel.dot(p_b_axis)));
		}
		tri_area.push_back(tri.area);
		src_index.push_back(m);
	}

	const uint32_t n = tri_area.size();
	if (n < 2) {
		return false;
	}

	// Edge -> the (up to two) member triangles carrying it, at the same weld
	// granularity the island was built with.
	EdgeMap edge_tris;
	for (uint32_t t = 0; t < n; t++) {
		const SurfaceLightTri &tri = p_tris[src_index[t]];
		for (int k = 0; k < 3; k++) {
			const SurfaceLightEdgeKey ek = _surface_light_edge_key(tri.v[k], tri.v[(k + 1) % 3]);
			EdgeMap::Iterator it = edge_tris.find(ek);
			if (it == edge_tris.end()) {
				edge_tris.insert(ek, Vector2i((int)t, -1));
			} else if (it->value.y < 0) {
				it->value.y = (int)t;
			}
		}
	}

	LocalVector<bool> used;
	used.resize(n);
	for (uint32_t i = 0; i < n; i++) {
		used[i] = false;
	}

	r_tris_2d.clear();
	uint32_t emitted_tris = 0;
	LocalVector<uint32_t> frontier;
	Vector<Point2> cand;

	for (uint32_t seed = 0; seed < n; seed++) {
		if (used[seed]) {
			continue;
		}
		used[seed] = true;

		// Region state: its convex hull (CCW, last == first, matching
		// Geometry2D::convex_hull) plus the summed area of its members.
		Vector<Point2> hull;
		hull.resize(4);
		hull.write[0] = tv[seed * 3 + 0];
		hull.write[1] = tv[seed * 3 + 1];
		hull.write[2] = tv[seed * 3 + 2];
		hull.write[3] = tv[seed * 3 + 0];
		double region_area = (double)tri_area[seed];

		frontier.clear();
		frontier.push_back(seed);

		while (!frontier.is_empty()) {
			const uint32_t cur = frontier[frontier.size() - 1];
			frontier.resize(frontier.size() - 1);

			const SurfaceLightTri &cur_tri = p_tris[src_index[cur]];
			for (int k = 0; k < 3; k++) {
				EdgeMap::Iterator it = edge_tris.find(_surface_light_edge_key(cur_tri.v[k], cur_tri.v[(k + 1) % 3]));
				if (it == edge_tris.end()) {
					continue;
				}
				for (int slot = 0; slot < 2; slot++) {
					const int cand_t = (slot == 0) ? it->value.x : it->value.y;
					if (cand_t < 0 || used[(uint32_t)cand_t]) {
						continue;
					}
					if ((uint32_t)(hull.size() - 1) >= SURFACE_LIGHT_MERGE_MAX_REGION_VERTS) {
						continue;
					}

					cand.resize(0);
					for (int h = 0; h < hull.size() - 1; h++) {
						cand.push_back(hull[h]);
					}
					for (int kk = 0; kk < 3; kk++) {
						cand.push_back(tv[(uint32_t)cand_t * 3 + kk]);
					}

					const Vector<Point2> merged = Geometry2D::convex_hull(cand);
					const int mn = merged.size() - 1;
					if (mn < 3) {
						continue;
					}
					double merged_area = 0.0;
					for (int h = 0; h < mn; h++) {
						merged_area += 0.5 * (double)merged[h].cross(merged[h + 1]);
					}
					const double want = region_area + (double)tri_area[cand_t];
					if (Math::abs(merged_area - want) > want * SURFACE_LIGHT_MERGE_AREA_REL_EPS) {
						continue; // Union is not exactly the hull: concave, gapped or overlapping.
					}

					used[(uint32_t)cand_t] = true;
					hull = merged;
					region_area = want;
					frontier.push_back((uint32_t)cand_t);
				}
			}
		}

		// convex_hull is CCW in this frame, so the fan is wound along the island
		// normal like every other emit path.
		const int hn = hull.size() - 1;
		for (int h = 1; h < hn - 1; h++) {
			r_tris_2d.push_back(hull[0]);
			r_tris_2d.push_back(hull[h]);
			r_tris_2d.push_back(hull[h + 1]);
			emitted_tris++;
		}
	}

	// Only worth taking if it actually reduced the count; equal means nothing merged.
	return emitted_tris > 0 && emitted_tris < n;
}
// </ELIM>

void LightmapSurfaceLights::collect(const Vector<SurfaceLightMeshInput> &p_inputs, float p_pos_bias, Vector<SurfaceLightIslandLight> &r_islands, bool p_capture_geometry) {
	// PHASE 1: gather emitter triangles per surface, merge into coplanar islands, and
	// accumulate the map-wide emissive area (drives the adaptive tile size below). Solid
	// (non-emitter, non-sky) triangles are indexed into a spatial hash in the same pass so
	// PHASE 2 can cull emitter faces jammed face-to-face against solid.
	LocalVector<SurfaceLightIsland> islands;
	double total_area = 0.0;
	LocalVector<SurfaceLightOccTri> occ_tris;
	HashMap<Vector3i, LocalVector<int32_t>> occ_grid;

	for (int input_idx = 0; input_idx < p_inputs.size(); input_idx++) {
		const SurfaceLightMeshInput &in = p_inputs[input_idx];
		if (in.mesh.is_null()) {
			continue;
		}
		const Basis normal_xform = in.xform.basis.inverse().transposed();

		for (int i = 0; i < in.mesh->get_surface_count(); i++) {
			if (in.mesh->surface_get_primitive_type(i) != Mesh::PRIMITIVE_TRIANGLES) {
				continue;
			}

			// Effective (override-preferred) material. Sky faces are holes to the
			// environment: they neither emit nor occlude, so skip them entirely.
			Ref<Material> mat = in.mesh->surface_get_material(i);
			Ref<Material> eff_mat = (i < in.overrides.size() && in.overrides[i].is_valid()) ? in.overrides[i] : mat;
			const uint32_t bake_flags = surface_bake_flags(eff_mat);
			if (bake_flags & SURFACE_BAKE_FLAG_SKY) {
				continue;
			}
			// Everything that is neither sky nor an emitter is a solid occluder.
			const bool is_emitter = (bake_flags & SURFACE_BAKE_FLAG_SURFACE_LIGHT) != 0;

			// Positions + vertex normals feed both paths (the emitter merge and the solid
			// occluder index), so fetch them once, then branch.
			Vector<Vector3> vertices;
			Vector<Vector3> normals;
			Vector<int> index;
			if (i < (int)in.surfaces.size() && !in.surfaces[i].vertices.is_empty()) {
				vertices = in.surfaces[i].vertices;
				normals = in.surfaces[i].normals;
				index = in.surfaces[i].index;
			} else {
				Array a = in.mesh->surface_get_arrays(i);
				vertices = a[Mesh::ARRAY_VERTEX];
				normals = a[Mesh::ARRAY_NORMAL];
				index = a[Mesh::ARRAY_INDEX];
			}
			if (vertices.is_empty() || normals.is_empty()) {
				continue;
			}
			const Vector3 *vr = vertices.ptr();
			const Vector3 *nr = normals.ptr();
			const int *ir = index.size() ? index.ptr() : nullptr;
			const int facecount = ir ? (index.size() / 3) : (vertices.size() / 3);

			if (!is_emitter) {
				// Solid occluder: index its world-space triangles so PHASE 2 can detect an
				// emitter face pressed face-to-face against it.
				for (int j = 0; j < facecount; j++) {
					uint32_t vidx[3];
					for (int k = 0; k < 3; k++) {
						vidx[k] = ir ? (uint32_t)ir[j * 3 + k] : (uint32_t)(j * 3 + k);
					}
					SurfaceLightOccTri ot;
					ot.v0 = in.xform.xform(vr[vidx[0]]);
					ot.v1 = in.xform.xform(vr[vidx[1]]);
					ot.v2 = in.xform.xform(vr[vidx[2]]);
					if (0.5f * (ot.v1 - ot.v0).cross(ot.v2 - ot.v0).length() < SURFACE_LIGHT_MIN_AREA) {
						continue; // Degenerate sliver: no meaningful facing/coverage.
					}
					ot.normal = normal_xform.xform(nr[vidx[0]]).normalized();
					const int32_t oi = (int32_t)occ_tris.size();
					occ_tris.push_back(ot);
					_surface_light_occ_insert(occ_grid, oi, ot.v0, ot.v1, ot.v2);
				}
				continue;
			}

			// Emission colour (linear) + energy come from the emitter material.
			const Ref<BaseMaterial3D> bm = eff_mat;
			const Color island_color = bm->get_emission().srgb_to_linear();
			const float island_energy = bm->get_emission_energy_multiplier();

			// Collect this surface's emitter triangles in the input frame. Drop
			// degenerate slivers up front so they can't seed spurious edges.
			LocalVector<SurfaceLightTri> tris;
			for (int j = 0; j < facecount; j++) {
				uint32_t vidx[3];
				for (int k = 0; k < 3; k++) {
					vidx[k] = ir ? (uint32_t)ir[j * 3 + k] : (uint32_t)(j * 3 + k);
				}
				SurfaceLightTri tri;
				tri.v[0] = in.xform.xform(vr[vidx[0]]);
				tri.v[1] = in.xform.xform(vr[vidx[1]]);
				tri.v[2] = in.xform.xform(vr[vidx[2]]);
				tri.normal = normal_xform.xform(nr[vidx[0]]).normalized();
				tri.area = 0.5f * (tri.v[1] - tri.v[0]).cross(tri.v[2] - tri.v[0]).length();
				tri.centroid = (tri.v[0] + tri.v[1] + tri.v[2]) * (1.0f / 3.0f);
				if (tri.area >= SURFACE_LIGHT_MIN_AREA) {
					tris.push_back(tri);
				}
			}
			if (tris.is_empty()) {
				continue;
			}

			// Union-find over welded edges, unioning two edge-adjacent triangles only
			// when their face normals are near-parallel. PER SURFACE, so distinct faces
			// / fixtures never cross-merge. Every sliver of one planar brush face
			// collapses into ONE island regardless of tjunc fragmentation.
			const int32_t tri_count = (int32_t)tris.size();
			LocalVector<int32_t> parent;
			parent.resize(tri_count);
			for (int32_t t = 0; t < tri_count; t++) {
				parent[t] = t;
			}
			HashMap<SurfaceLightEdgeKey, int32_t, SurfaceLightEdgeKeyHasher> edge_owner;
			for (int32_t t = 0; t < tri_count; t++) {
				const SurfaceLightTri &tri = tris[t];
				for (int e = 0; e < 3; e++) {
					const SurfaceLightEdgeKey ek = _surface_light_edge_key(tri.v[e], tri.v[(e + 1) % 3]);
					HashMap<SurfaceLightEdgeKey, int32_t, SurfaceLightEdgeKeyHasher>::Iterator it = edge_owner.find(ek);
					if (it == edge_owner.end()) {
						edge_owner.insert(ek, t);
					} else if (tris[it->value].normal.dot(tri.normal) > SURFACE_LIGHT_COPLANAR_DOT) {
						const int32_t ra = _surface_light_uf_find(parent.ptr(), t);
						const int32_t rb = _surface_light_uf_find(parent.ptr(), it->value);
						if (ra != rb) {
							parent[ra] = rb;
						}
					}
				}
			}

			// Fold each component root into a (globally indexed) island: summed area,
			// area-weighted centroid + normal, and the member triangles for retiling.
			HashMap<int32_t, int32_t> root_to_island;
			for (int32_t t = 0; t < tri_count; t++) {
				const int32_t root = _surface_light_uf_find(parent.ptr(), t);
				int32_t ii;
				HashMap<int32_t, int32_t>::Iterator it = root_to_island.find(root);
				if (it == root_to_island.end()) {
					ii = (int32_t)islands.size();
					islands.push_back(SurfaceLightIsland());
					islands[ii].color = island_color;
					islands[ii].energy = island_energy;
					root_to_island.insert(root, ii);
				} else {
					ii = it->value;
				}
				SurfaceLightIsland &isl = islands[ii];
				const SurfaceLightTri &tri = tris[t];
				isl.area += tri.area;
				isl.pos_sum += tri.centroid * tri.area;
				isl.normal_sum += tri.normal * tri.area;
				isl.tris.push_back(tri);
				total_area += (double)tri.area;
			}
		}
	}

	if (islands.is_empty()) {
		return;
	}

	// Emitter fixtures: islands sharing a welded vertex with identical emission are
	// one fixture. Across surfaces on purpose: chunking splits one strip over meshes.
	{
		LocalVector<int32_t> group_parent;
		group_parent.resize(islands.size());
		for (uint32_t ii = 0; ii < islands.size(); ii++) {
			group_parent[ii] = (int32_t)ii;
		}
		HashMap<SurfaceLightEdgeKey, int32_t, SurfaceLightEdgeKeyHasher> vert_owner;
		for (uint32_t ii = 0; ii < islands.size(); ii++) {
			for (const SurfaceLightTri &tri : islands[ii].tris) {
				for (int k = 0; k < 3; k++) {
					const SurfaceLightEdgeKey vk = _surface_light_edge_key(tri.v[k], tri.v[k]);
					HashMap<SurfaceLightEdgeKey, int32_t, SurfaceLightEdgeKeyHasher>::Iterator it = vert_owner.find(vk);
					if (it == vert_owner.end()) {
						vert_owner.insert(vk, (int32_t)ii);
						continue;
					}
					const SurfaceLightIsland &other = islands[it->value];
					if (other.color != islands[ii].color || other.energy != islands[ii].energy) {
						continue;
					}
					const int32_t ra = _surface_light_uf_find(group_parent.ptr(), (int32_t)ii);
					const int32_t rb = _surface_light_uf_find(group_parent.ptr(), it->value);
					if (ra != rb) {
						group_parent[ra] = rb;
					}
				}
			}
		}
		for (uint32_t ii = 0; ii < islands.size(); ii++) {
			islands[ii].group = _surface_light_uf_find(group_parent.ptr(), (int32_t)ii);
		}
	}

	// PHASE 2: one analytic polygon light per island. Convex islands (brush
	// faces, however T-junction-fragmented) collapse to their in-plane convex
	// hull fan — exact and minimal. Non-convex islands (coplanar-merged
	// multi-brush faces) keep their raw member triangles under the per-island
	// cap; past it they fall back to the hull fan with radiance scaled to
	// conserve emitted power.
	bool global_cap_warned = false;
	uint32_t occluded_islands = 0;
	uint32_t hull_fallback_islands = 0;
	uint32_t total_poly_verts = 0;
	uint32_t simplified_islands = 0;
	uint32_t simplify_failed_islands = 0;
	uint32_t simplified_tris_in = 0;
	uint32_t simplified_tris_out = 0;
	uint32_t merged_islands = 0;
	uint32_t merged_tris_in = 0;
	uint32_t merged_tris_out = 0;

	for (uint32_t ci = 0; ci < islands.size(); ci++) {
		const SurfaceLightIsland &isl = islands[ci];
		if (isl.area < SURFACE_LIGHT_MIN_AREA) {
			continue;
		}
		if (total_poly_verts >= SURFACE_LIGHT_MAX_POLY_VERTS_TOTAL) {
			if (!global_cap_warned) {
				WARN_PRINT(vformat("LightmapGI: global surface-light polygon-vertex cap (%d) reached; further emitter faces will not emit as area lights. Raise SURFACE_LIGHT_MAX_POLY_VERTS_TOTAL if this map is legitimate.", (int)SURFACE_LIGHT_MAX_POLY_VERTS_TOTAL));
				global_cap_warned = true;
			}
			break;
		}
		const Vector3 cnormal = isl.normal_sum.normalized();
		const Vector3 island_centroid = isl.pos_sum / isl.area;

		SurfaceLightIslandLight light;
		light.normal = cnormal;
		light.pos = island_centroid + cnormal * p_pos_bias;
		light.area = isl.area;
		light.color = isl.color;
		light.energy = isl.energy;
		light.component = (int32_t)ci;
		light.group = isl.group;
		// <ELIM> Occluded = the fraction of the island's AREA pressed face-to-face
		// against solid, not a single centroid probe. v1 tested one point per PATCH
		// (dozens per island), so a point sample was a local decision. v2 emits ONE
		// light per island, so the same test let a single buried sample cull a
		// map-spanning emitter: a lava pool with any brush standing in it went
		// completely dark while a neighbouring pool of the same texture lit fine.
		// Cull only when the island is essentially fully buried.
		// light.occluded = _surface_light_test_occluded(occ_grid, occ_tris, island_centroid, cnormal);
		{
			const uint32_t occ_tri_count = isl.tris.size();
			const uint32_t occ_stride = MAX(1u, occ_tri_count / SURFACE_LIGHT_OCC_MAX_SAMPLES);
			float occ_area = 0.0f;
			float occ_tested_area = 0.0f;
			for (uint32_t m = 0; m < occ_tri_count; m += occ_stride) {
				const SurfaceLightTri &occ_tri = isl.tris[m];
				const Vector3 occ_c = (occ_tri.v[0] + occ_tri.v[1] + occ_tri.v[2]) / 3.0f;
				occ_tested_area += occ_tri.area;
				if (_surface_light_test_occluded(occ_grid, occ_tris, occ_c, cnormal)) {
					occ_area += occ_tri.area;
				}
			}
			light.occluded = occ_tested_area > 0.0f && (occ_area / occ_tested_area) >= SURFACE_LIGHT_OCC_CULL_FRACTION;
		}
		// </ELIM>

		// In-plane convex hull over all member vertices. The island plane is
		// the area-weighted mean plane through the centroid (members are
		// coplanar to within SURFACE_LIGHT_COPLANAR_DOT, so reconstruction
		// error is float-level slop, absorbed by the bias offset).
		const Vector3 t_axis = (Math::abs(cnormal.y) < 0.777f ? Vector3(0, 1, 0) : Vector3(1, 0, 0)).cross(cnormal).normalized();
		const Vector3 b_axis = cnormal.cross(t_axis);
		Vector<Point2> pts2;
		pts2.resize((int)isl.tris.size() * 3);
		for (uint32_t m = 0; m < isl.tris.size(); m++) {
			for (int k = 0; k < 3; k++) {
				const Vector3 rel = isl.tris[m].v[k] - island_centroid;
				pts2.write[(int)m * 3 + k] = Point2(rel.dot(t_axis), rel.dot(b_axis));
			}
		}
		const Vector<Point2> hull = Geometry2D::convex_hull(pts2); // CCW; last == first.
		const int hull_n = hull.size() - 1; // Unique hull vertices.
		double hull_area = 0.0;
		for (int h = 0; h < hull_n; h++) {
			hull_area += 0.5 * (double)hull[h].cross(hull[h + 1]);
		}
		const bool convex_exact = hull_n >= 3 && hull_area <= (double)isl.area * (double)SURFACE_LIGHT_HULL_TOLERANCE;

		// <ELIM> A non-convex island is rebuilt from its boundary BEFORE the cap
		// is consulted, so the cap sees the island's real shape complexity rather
		// than the importer's T-junction fragmentation. The hull fallback below is
		// then the pathological-map backstop it was always meant to be, not the
		// routine outcome for every coplanar-merged multi-brush face.
		LocalVector<Vector2> simple_tris_2d;
		bool simplified = false;
		if (!convex_exact) {
			simplified = _surface_light_simplify_island(isl.tris, island_centroid, t_axis, b_axis, isl.area, simple_tris_2d);
			if (simplified) {
				simplified_islands++;
				simplified_tris_in += isl.tris.size();
				simplified_tris_out += (uint32_t)(simple_tris_2d.size() / 3);
			} else {
				simplify_failed_islands++;
				// <ELIM> Boundary rebuild bailed. Fall to the monotone convex
				// merge rather than straight to raw member triangles.
				simplified = _surface_light_convex_merge_island(isl.tris, island_centroid, t_axis, b_axis, cnormal, simple_tris_2d);
				if (simplified) {
					merged_islands++;
					merged_tris_in += isl.tris.size();
					merged_tris_out += (uint32_t)(simple_tris_2d.size() / 3);
				}
				// </ELIM>
			}
		}
		const uint32_t emit_tri_count = simplified ? (uint32_t)(simple_tris_2d.size() / 3) : isl.tris.size();
		const bool over_cap = emit_tri_count > SURFACE_LIGHT_MAX_POLY_TRIS;

		enum EmitMode {
			EMIT_HULL_FAN,
			EMIT_SIMPLIFIED,
			EMIT_RAW_MEMBERS,
		};
		EmitMode emit_mode;
		if (convex_exact && hull_n >= 3) {
			emit_mode = EMIT_HULL_FAN; // Exact and minimal.
		} else if (simplified && !over_cap) {
			emit_mode = EMIT_SIMPLIFIED; // Exact.
		} else if (!over_cap) {
			emit_mode = EMIT_RAW_MEMBERS; // Exact.
		} else if (hull_n >= 3) {
			// Last resort: radiance spread over the larger hull conserves the
			// island's emitted power but smears it geometrically.
			light.radiance_scale = (float)((double)isl.area / MAX(hull_area, 1e-6));
			hull_fallback_islands++;
			emit_mode = EMIT_HULL_FAN;
		} else {
			emit_mode = EMIT_RAW_MEMBERS; // No usable hull; exactness beats the cap.
		}
		// </ELIM>

		if (emit_mode == EMIT_HULL_FAN) {
			// convex_hull output is CCW in the (t_axis, b_axis) frame, so fan
			// triangles are already wound with their geometric normal along
			// cnormal.
			for (int h = 1; h < hull_n - 1; h++) {
				const Point2 h2[3] = { hull[0], hull[h], hull[h + 1] };
				for (int k = 0; k < 3; k++) {
					const Vector3 v3 = island_centroid + t_axis * h2[k].x + b_axis * h2[k].y;
					light.poly_verts.push_back(v3 + cnormal * p_pos_bias);
					if (p_capture_geometry) {
						light.dbg_verts.push_back(v3);
					}
				}
			}
		} else if (emit_mode == EMIT_SIMPLIFIED) {
			// Boundary-rebuilt triangles, already CCW in the (t_axis, b_axis)
			// frame and therefore wound along cnormal.
			for (uint32_t m = 0; m < simple_tris_2d.size(); m++) {
				const Vector3 v3 = island_centroid + t_axis * simple_tris_2d[m].x + b_axis * simple_tris_2d[m].y;
				light.poly_verts.push_back(v3 + cnormal * p_pos_bias);
				if (p_capture_geometry) {
					light.dbg_verts.push_back(v3);
				}
			}
		} else {
			// Raw member triangles. Winding-normalize each against the island
			// normal — the cross product's sign depends on source winding (Godot
			// fronts wind clockwise) and only the vertex normal is authoritative.
			for (uint32_t m = 0; m < isl.tris.size(); m++) {
				const SurfaceLightTri &tri = isl.tris[m];
				if (tri.area < SURFACE_LIGHT_MIN_AREA) {
					continue;
				}
				const bool flip = (tri.v[1] - tri.v[0]).cross(tri.v[2] - tri.v[0]).dot(cnormal) < 0.0f;
				const Vector3 tv[3] = { tri.v[0], flip ? tri.v[2] : tri.v[1], flip ? tri.v[1] : tri.v[2] };
				for (int k = 0; k < 3; k++) {
					light.poly_verts.push_back(tv[k] + cnormal * p_pos_bias);
					if (p_capture_geometry) {
						light.dbg_verts.push_back(tv[k]);
					}
				}
			}
		}

		if (light.poly_verts.is_empty()) {
			continue;
		}
		for (int k = 0; k < light.poly_verts.size(); k++) {
			light.bounding_radius = MAX(light.bounding_radius, light.pos.distance_to(light.poly_verts[k]));
		}
		total_poly_verts += (uint32_t)light.poly_verts.size();
		if (light.occluded) {
			occluded_islands++;
		}
		r_islands.push_back(light);
	}

	// Diagnostics (bake only; the debug hook may run per-frame so it stays quiet).
	if (!p_capture_geometry) {
		print_line(vformat("LightmapGI: surface-light collector — %d island light(s), total emissive area %.1f m^2, %d polygon vert(s), %d hull-fallback island(s), %d occluded island(s) culled.",
				(int)r_islands.size(), total_area, (int)total_poly_verts, (int)hull_fallback_islands, (int)occluded_islands));
		// <ELIM> Emitter fixtures (islands grouped for emitter-arc selection).
		HashSet<int32_t> fixtures;
		int multi_face = 0;
		HashMap<int32_t, int> fixture_faces;
		for (const SurfaceLightIslandLight &isl : r_islands) {
			fixtures.insert(isl.group);
			fixture_faces[isl.group] += 1;
		}
		for (const KeyValue<int32_t, int> &kv : fixture_faces) {
			if (kv.value > 1) {
				multi_face++;
			}
		}
		print_line(vformat("LightmapGI: emitter fixtures — %d island(s) in %d fixture(s), %d of them with more than one island.", (int)r_islands.size(), fixtures.size(), multi_face));
		// </ELIM>
		print_line(vformat("LightmapGI: surface-light simplification — %d non-convex island(s) rebuilt from their boundary (%d member tri(s) -> %d exact tri(s)), %d could not be rebuilt.",
				(int)simplified_islands, (int)simplified_tris_in, (int)simplified_tris_out, (int)simplify_failed_islands));
		// <ELIM> Convex-merge rescue of the islands the boundary rebuild bailed on.
		if (simplify_failed_islands > 0) {
			print_line(vformat("LightmapGI: surface-light convex merge — %d of %d un-rebuildable island(s) merged into convex regions (%d member tri(s) -> %d fan tri(s)); %d still emit raw members.",
					(int)merged_islands, (int)simplify_failed_islands, (int)merged_tris_in, (int)merged_tris_out,
					(int)(simplify_failed_islands - merged_islands)));
		}
		// </ELIM>
		if (hull_fallback_islands > 0) {
			WARN_PRINT(vformat("LightmapGI: %d emitter island(s) still exceed %d triangles after boundary simplification and fell back to their convex hull — power-conserving, but the light is smeared over non-emissive geometry and the emitter itself is dimmed by island_area/hull_area. Raise SURFACE_LIGHT_MAX_POLY_TRIS.", (int)hull_fallback_islands, (int)SURFACE_LIGHT_MAX_POLY_TRIS));
		}
	}
}

// Each island becomes ONE analytic AREA_POLY light carrying its polygon. The
// range solves the peak unshadowed contribution E·A/d² = CUTOFF for d, plus the
// island bounding radius so the centroid distance test is conservative for texels
// near a big island's edge. Islands flagged occluded (face-to-face contact with
// solid) emit into solid and are skipped. Submitted grouped by fixture: the direct
// pass sums a fixture's lights as a contiguous run.
int LightmapSurfaceLights::add_to_lightmapper(const Ref<Lightmapper> &p_lightmapper, const Vector<SurfaceLightIslandLight> &p_islands) {
	LocalVector<uint64_t> island_order; // (group << 32) | island index
	island_order.resize(p_islands.size());
	for (int i = 0; i < p_islands.size(); i++) {
		island_order[i] = ((uint64_t)MAX(p_islands[i].group, 0) << 32) | (uint64_t)i;
	}
	island_order.sort();
	int emitted_islands = 0;
	for (const uint64_t order_key : island_order) {
		const int i = (int)(order_key & 0xFFFFFFFFu);
		const SurfaceLightIslandLight &sl = p_islands[i];
		if (sl.occluded) {
			continue;
		}
		const float e_eff = sl.energy * SURFACE_LIGHT_ENERGY_SCALE * sl.radiance_scale;
		const float peak = e_eff * MAX(sl.color.r, MAX(sl.color.g, sl.color.b));
		const float range = CLAMP(Math::sqrt(MAX(0.0f, peak * sl.area / SURFACE_LIGHT_CUTOFF)), 1.0f, SURFACE_LIGHT_RANGE_MAX) + sl.bounding_radius;
		p_lightmapper->add_area_poly_light(vformat("surface_light_island_%d", i), true, sl.pos, sl.normal, sl.color, e_eff, 1.0f, sl.area, range, sl.poly_verts, (uint32_t)MAX(sl.group, 0));
		emitted_islands++;
	}
	if (emitted_islands > 0) {
		print_verbose(vformat("LightmapGI: added %d analytic surface-light island(s).", emitted_islands));
	}
	return emitted_islands;
}
