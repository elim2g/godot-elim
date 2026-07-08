#[versions]

primary = "#define MODE_DIRECT_LIGHT";
secondary = "#define MODE_BOUNCE_LIGHT";
dilate = "#define MODE_DILATE";
unocclude = "#define MODE_UNOCCLUDE";
light_probes = "#define MODE_LIGHT_PROBES";
denoise = "#define MODE_DENOISE";
pack_coeffs = "#define MODE_PACK_L1_COEFFS";

#[compute]

#version 450

#VERSION_DEFINES

#extension GL_EXT_samplerless_texture_functions : enable

// One 2D local group focusing in one layer at a time, though all
// in parallel (no barriers) makes more sense than a 3D local group
// as this can take more advantage of the cache for each group.

#ifdef MODE_LIGHT_PROBES

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

#else

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

#endif

#include "lm_common_inc.glsl"

#ifdef MODE_LIGHT_PROBES

layout(set = 1, binding = 0, std430) restrict buffer LightProbeData {
	vec4 data[];
}
light_probes;

layout(set = 1, binding = 1) uniform texture2DArray source_light;
layout(set = 1, binding = 2) uniform texture2D environment;
#endif

#ifdef MODE_UNOCCLUDE

layout(rgba32f, set = 1, binding = 0) uniform restrict image2DArray position;
layout(rgba32f, set = 1, binding = 1) uniform restrict image2DArray unocclude;

#endif

#if defined(MODE_DIRECT_LIGHT) || defined(MODE_BOUNCE_LIGHT)

// <ELIM> Dropped `writeonly`: the direct pass now reads-modifies-writes this
// image to accumulate across light batches (TDR guard, see MODE_DIRECT_LIGHT).
// layout(rgba16f, set = 1, binding = 0) uniform restrict writeonly image2DArray dest_light;
layout(rgba16f, set = 1, binding = 0) uniform restrict image2DArray dest_light;
// </ELIM>
layout(set = 1, binding = 1) uniform texture2DArray source_light;
layout(set = 1, binding = 2) uniform texture2DArray source_position;
layout(set = 1, binding = 3) uniform texture2DArray source_normal;
layout(rgba16f, set = 1, binding = 4) uniform restrict image2DArray accum_light;

#endif

#if defined(MODE_DIRECT_LIGHT) && defined(USE_SHADOWMASK)
// <ELIM> Dropped `writeonly`: read-modify-write (max) to accumulate the
// directional shadowmask across light batches (TDR guard).
// layout(rgba8, set = 1, binding = 5) uniform restrict writeonly image2DArray shadowmask;
layout(rgba8, set = 1, binding = 5) uniform restrict image2DArray shadowmask;
// </ELIM>
#elif defined(MODE_BOUNCE_LIGHT)
layout(set = 1, binding = 5) uniform texture2D environment;
#endif

#if defined(MODE_DILATE) || defined(MODE_DENOISE) || defined(MODE_PACK_L1_COEFFS)
layout(rgba16f, set = 1, binding = 0) uniform restrict writeonly image2DArray dest_light;
layout(set = 1, binding = 1) uniform texture2DArray source_light;
#endif

#ifdef MODE_DENOISE
layout(set = 1, binding = 2) uniform texture2DArray source_normal;
layout(set = 1, binding = 3) uniform texture2DArray unocclude_mask;
layout(set = 1, binding = 4) uniform DenoiseParams {
	float spatial_bandwidth;
	float light_bandwidth;
	float albedo_bandwidth;
	float normal_bandwidth;

	int half_search_window;
	float filter_strength;
	uint slice_count;
}
denoise_params;
#endif

layout(push_constant, std430) uniform Params {
	uint atlas_slice;
	uint ray_count;
	uint ray_from;
	uint ray_to;

	// <ELIM> Direct-pass light-batch range (TDR guard); must match the C++
	// PushConstant in lightmapper_rd.h. MODE_DIRECT_LIGHT iterates the per-texel
	// light loop over [light_from, light_to) instead of all lights at once, so a
	// single dispatch's O(texels*lights*rays) cost cannot exceed the GPU driver
	// watchdog window. One batch (light_from==0, light_to==light_count) is
	// bit-identical to the original single pass.
	uint light_from;
	uint light_to;
	// </ELIM>

	ivec2 region_ofs;
	uint probe_count;
	uint denoiser_range;
}
params;

//check it, but also return distance and barycentric coords (for uv lookup)
bool ray_hits_triangle(vec3 from, vec3 dir, float max_dist, vec3 p0, vec3 p1, vec3 p2, out float r_distance, out vec3 r_barycentric) {
	const float EPSILON = 0.00001;
	const vec3 e0 = p1 - p0;
	const vec3 e1 = p0 - p2;
	vec3 triangle_normal = cross(e1, e0);

	float n_dot_dir = dot(triangle_normal, dir);

	if (abs(n_dot_dir) < EPSILON) {
		return false;
	}

	const vec3 e2 = (p0 - from) / n_dot_dir;
	const vec3 i = cross(dir, e2);

	r_barycentric.y = dot(i, e1);
	r_barycentric.z = dot(i, e0);
	r_barycentric.x = 1.0 - (r_barycentric.z + r_barycentric.y);
	r_distance = dot(triangle_normal, e2);

	return (r_distance > bake_params.bias) && (r_distance < max_dist) && all(greaterThanEqual(r_barycentric, vec3(0.0)));
}

const uint RAY_MISS = 0;
const uint RAY_FRONT = 1;
const uint RAY_BACK = 2;
const uint RAY_ANY = 3;

bool ray_box_test(vec3 p_from, vec3 p_inv_dir, vec3 p_box_min, vec3 p_box_max) {
	vec3 t0 = (p_box_min - p_from) * p_inv_dir;
	vec3 t1 = (p_box_max - p_from) * p_inv_dir;
	vec3 tmin = min(t0, t1), tmax = max(t0, t1);
	return max(tmin.x, max(tmin.y, tmin.z)) <= min(tmax.x, min(tmax.y, tmax.z));
}

#if CLUSTER_SIZE > 32
#define CLUSTER_TRIANGLE_ITERATION
#endif

// <ELIM> Added p_skip_sky: lets a directional-sun shadow ray treat a Q3 sky face as
// reached-light (the sun seen through the aperture) rather than an occluder. The name
// is historical (it used to skip/pass-through sky); see the closest-hit sky logic below.
// uint trace_ray(vec3 p_from, vec3 p_to, bool p_any_hit, out float r_distance, out vec3 r_normal, out uint r_triangle, out vec3 r_barycentric) {
uint trace_ray(vec3 p_from, vec3 p_to, bool p_any_hit, bool p_skip_sky, out float r_distance, out vec3 r_normal, out uint r_triangle, out vec3 r_barycentric) {
// </ELIM>
	// World coordinates.
	vec3 rel = p_to - p_from;
	float rel_len = length(rel);
	vec3 dir = normalize(rel);
	vec3 inv_dir = 1.0 / dir;

	// Cell coordinates.
	vec3 from_cell = (p_from - bake_params.to_cell_offset) * bake_params.to_cell_size;
	vec3 to_cell = (p_to - bake_params.to_cell_offset) * bake_params.to_cell_size;

	// Prepare DDA.
	vec3 rel_cell = to_cell - from_cell;
	ivec3 icell = ivec3(from_cell);
	ivec3 iendcell = ivec3(to_cell);
	vec3 dir_cell = normalize(rel_cell);
	vec3 delta = min(abs(1.0 / dir_cell), bake_params.grid_size); // Use bake_params.grid_size as max to prevent infinity values.
	ivec3 step = ivec3(sign(rel_cell));
	const vec3 init_next_cell = vec3(icell) + max(vec3(0), sign(step));
	vec3 t_max = mix(vec3(0), (init_next_cell - from_cell) / dir_cell, notEqual(step, vec3(0))); // Distance to next boundary.

	uint iters = 0;
	while (all(greaterThanEqual(icell, ivec3(0))) && all(lessThan(icell, ivec3(bake_params.grid_size))) && (iters < 1000)) {
		uvec2 cell_data = texelFetch(grid, icell, 0).xy;
		uint triangle_count = cell_data.x;
		if (triangle_count > 0) {
			uint hit = RAY_MISS;
			float best_distance = 1e20;
			// <ELIM> Whether the current closest hit is a Q3 sky face. Lets a
			// directional-sun shadow query (p_skip_sky) treat "closest thing toward
			// the sun is a sky aperture" as reached-light instead of an occluder.
			bool best_is_sky = false;
			// </ELIM>
			uint cluster_start = cluster_indices.data[cell_data.y * 2];
			uint cell_triangle_start = cluster_indices.data[cell_data.y * 2 + 1];
			uint cluster_count = (triangle_count + CLUSTER_SIZE - 1) / CLUSTER_SIZE;
			uint cluster_base_index = 0;
			while (cluster_base_index < cluster_count) {
				// To minimize divergence, all Ray-AABB tests on the clusters contained in the cell are performed
				// before checking against the triangles. We do this 32 clusters at a time and store the intersected
				// clusters on each bit of the 32-bit integer.
				uint cluster_test_count = min(32, cluster_count - cluster_base_index);
				uint cluster_hits = 0;
				for (uint i = 0; i < cluster_test_count; i++) {
					uint cluster_index = cluster_start + cluster_base_index + i;
					ClusterAABB cluster_aabb = cluster_aabbs.data[cluster_index];
					if (ray_box_test(p_from, inv_dir, cluster_aabb.min_bounds, cluster_aabb.max_bounds)) {
						cluster_hits |= (1 << i);
					}
				}

				// Check the triangles in any of the clusters that were intersected by toggling off the bits in the
				// 32-bit integer counter until no bits are left.
				while (cluster_hits > 0) {
					uint cluster_index = findLSB(cluster_hits);
					cluster_hits &= ~(1 << cluster_index);
					cluster_index += cluster_base_index;

					// Do the same divergence execution trick with triangles as well.
					uint triangle_base_index = 0;
#ifdef CLUSTER_TRIANGLE_ITERATION
					while (triangle_base_index < triangle_count)
#endif
					{
						uint triangle_start_index = cell_triangle_start + cluster_index * CLUSTER_SIZE + triangle_base_index;
						uint triangle_test_count = min(CLUSTER_SIZE, triangle_count - triangle_base_index);
						uint triangle_hits = 0;
						for (uint i = 0; i < triangle_test_count; i++) {
							uint triangle_index = triangle_indices.data[triangle_start_index + i];
							if (ray_box_test(p_from, inv_dir, triangles.data[triangle_index].min_bounds, triangles.data[triangle_index].max_bounds)) {
								triangle_hits |= (1 << i);
							}
						}

						while (triangle_hits > 0) {
							uint cluster_triangle_index = findLSB(triangle_hits);
							triangle_hits &= ~(1 << cluster_triangle_index);
							cluster_triangle_index += triangle_start_index;

							uint triangle_index = triangle_indices.data[cluster_triangle_index];
							Triangle triangle = triangles.data[triangle_index];

							// <ELIM> Q3-style sky faces are the sun's "window". For a
							// directional-sun shadow query (p_skip_sky; name is historical),
							// a sky face is NOT an occluder but REACHED-LIGHT: if the closest
							// thing along the ray to the sun is a sky face, the texel sees the
							// sun through the aperture and is LIT (a nearer SOLID face still
							// shadows). So do NOT skip it here — let it compete in the
							// closest-hit search, tag it (best_is_sky), and convert
							// "closest hit == sky" into a miss after the cell (this
							// p_any_hit == false path is closest-first). Only any-hit queries
							// (never combined with p_skip_sky today) still skip sky, so they
							// don't count it as an occluder.
							bool tri_is_sky = p_skip_sky && (triangle.surface_flags & SURFACE_FLAG_SKY) != 0u;
							if (tri_is_sky && p_any_hit) {
								continue;
							}
							// </ELIM>

							// Gather the triangle vertex positions.
							vec3 vtx0 = vertices.data[triangle.indices.x].position;
							vec3 vtx1 = vertices.data[triangle.indices.y].position;
							vec3 vtx2 = vertices.data[triangle.indices.z].position;
							vec3 normal = -normalize(cross((vtx0 - vtx1), (vtx0 - vtx2)));
							bool backface = dot(normal, dir) >= 0.0;
							float distance;
							vec3 barycentric;
							if (ray_hits_triangle(p_from, dir, rel_len, vtx0, vtx1, vtx2, distance, barycentric)) {
								if (p_any_hit) {
									// Return early if any hit was requested.
									return RAY_ANY;
								}
								vec3 position = p_from + dir * distance;
								// <ELIM> Clamp the hit cell into the grid. Faces lying exactly on
								// the scene AABB's max planes (every map's outer shell) compute a
								// hit_cell of grid_size — outside the grid, never equal to any
								// traversal cell — so upstream silently ignores ALL hits on them
								// and rays escape through the map's roof/outer walls.
								ivec3 hit_cell = clamp(ivec3((position - bake_params.to_cell_offset) * bake_params.to_cell_size), ivec3(0), ivec3(bake_params.grid_size) - ivec3(1));
								if (icell != hit_cell) {
									// </ELIM>
									// It's possible for the ray to hit a triangle in a position outside the bounds of the cell
									// if it's large enough to cover multiple ones. The hit must be ignored if this is the case.
									continue;
								}

								if (!backface) {
									// The case of meshes having both a front and back face in the same plane is more common than
									// expected, so if this is a front-face, bias it closer to the ray origin, so it always wins
									// over the back-face.
									distance = max(bake_params.bias, distance - bake_params.bias);
								}

								if (distance < best_distance) {
									switch (triangle.cull_mode) {
										case CULL_DISABLED:
											backface = false;
											break;
										case CULL_FRONT:
											backface = !backface;
											break;
										case CULL_BACK: // Default behavior.
											break;
									}

									hit = backface ? RAY_BACK : RAY_FRONT;
									best_distance = distance;
									// <ELIM> Is this closest-so-far hit a sky aperture?
									// (registers regardless of the sky face's facing —
									// ray_hits_triangle / the cull_mode switch never discard
									// the hit, so sky orientation does not matter.)
									best_is_sky = tri_is_sky;
									// </ELIM>
									r_distance = distance;
									r_normal = normal;
									r_triangle = triangle_index;
									r_barycentric = barycentric;
								}
							}
						}

#ifdef CLUSTER_TRIANGLE_ITERATION
						triangle_base_index += CLUSTER_SIZE;
#endif
					}
				}

				cluster_base_index += 32;
			}

			if (hit != RAY_MISS) {
				// <ELIM> Directional sun through a sky aperture: if the closest hit
				// toward the sun is a sky face, the texel sees the sun -> reached-light
				// (RAY_MISS = not occluded). best_is_sky is only ever set true under
				// p_skip_sky, so omni/spot/bounce/any-hit queries are unaffected.
				if (best_is_sky) {
					return RAY_MISS;
				}
				// </ELIM>
				return hit;
			}
		}

		if (icell == iendcell) {
			break;
		}

		// There should be only one axis updated at a time for DDA to work properly.
		if (t_max.x < t_max.y && t_max.x < t_max.z) {
			icell.x += step.x;
			t_max.x += delta.x;
		} else if (t_max.y < t_max.z) {
			icell.y += step.y;
			t_max.y += delta.y;
		} else {
			icell.z += step.z;
			t_max.z += delta.z;
		}
		iters++;
	}

	return RAY_MISS;
}

uint trace_ray_closest_hit_triangle(vec3 p_from, vec3 p_to, out uint r_triangle, out vec3 r_barycentric) {
	float distance;
	vec3 normal;
	// <ELIM> skip-sky arg (false: not a sun shadow ray).
	// return trace_ray(p_from, p_to, false, distance, normal, r_triangle, r_barycentric);
	return trace_ray(p_from, p_to, false, false, distance, normal, r_triangle, r_barycentric);
	// </ELIM>
}

// <ELIM> Added p_skip_sky (forwarded to trace_ray) for sky-aware sun shadows.
// uint trace_ray_closest_hit_triangle_albedo_alpha(vec3 p_from, vec3 p_to, out vec4 albedo_alpha, out vec3 hit_position) {
uint trace_ray_closest_hit_triangle_albedo_alpha(vec3 p_from, vec3 p_to, bool p_skip_sky, out vec4 albedo_alpha, out vec3 hit_position) {
// </ELIM>
	float distance;
	vec3 normal;
	uint tidx;
	vec3 barycentric;

	// <ELIM> forward p_skip_sky.
	// uint ret = trace_ray(p_from, p_to, false, distance, normal, tidx, barycentric);
	uint ret = trace_ray(p_from, p_to, false, p_skip_sky, distance, normal, tidx, barycentric);
	// </ELIM>
	if (ret != RAY_MISS) {
		Vertex vert0 = vertices.data[triangles.data[tidx].indices.x];
		Vertex vert1 = vertices.data[triangles.data[tidx].indices.y];
		Vertex vert2 = vertices.data[triangles.data[tidx].indices.z];

		// <ELIM> Trace-only faces (sky or not) own no atlas chart: their sentinel
		// slice == atlas_slices is OOB for the albedo atlas, so resolve them here
		// without a textureLod — opaque black (a=1 seals the bake; albedo 0 tints
		// no light, and a=1 fully occludes so the tint is zeroed regardless). Sun
		// shadows already skip sky via p_skip_sky; omni/spot treat sky as opaque.
		{
			uint trace_only_flags = triangles.data[tidx].surface_flags;
			if ((trace_only_flags & SURFACE_FLAG_TRACE_ONLY) != 0u) {
				albedo_alpha = vec4(0.0, 0.0, 0.0, 1.0);
				hit_position = barycentric.x * vert0.position + barycentric.y * vert1.position + barycentric.z * vert2.position;
				return ret;
			}
		}
		// </ELIM>

		vec3 uvw = vec3(barycentric.x * vert0.uv + barycentric.y * vert1.uv + barycentric.z * vert2.uv, float(triangles.data[tidx].slice));

		albedo_alpha = textureLod(sampler2DArray(albedo_tex, linear_sampler), uvw, 0);
		// <ELIM> TURNT bakes no transparent surfaces (CLEAR "windows" are faces
		// removed from the mesh), so every hit is opaque. Without this, hits
		// near a chart edge bilinearly mix the gutter's alpha=0 into the
		// sample, fabricating a translucent border around EVERY face — texels
		// are meters wide here, so the sun partially shines through a band at
		// every brush seam ("light bleeding everywhere").
		albedo_alpha.a = 1.0;
		// </ELIM>
		hit_position = barycentric.x * vert0.position + barycentric.y * vert1.position + barycentric.z * vert2.position;
	}

	return ret;
}

uint trace_ray_closest_hit_distance(vec3 p_from, vec3 p_to, out float r_distance, out vec3 r_normal) {
	uint triangle;
	vec3 barycentric;
	// <ELIM> skip-sky arg (false).
	// return trace_ray(p_from, p_to, false, r_distance, r_normal, triangle, barycentric);
	return trace_ray(p_from, p_to, false, false, r_distance, r_normal, triangle, barycentric);
	// </ELIM>
}

uint trace_ray_any_hit(vec3 p_from, vec3 p_to) {
	float distance;
	vec3 normal;
	uint triangle;
	vec3 barycentric;
	// <ELIM> skip-sky arg (false).
	// return trace_ray(p_from, p_to, true, distance, normal, triangle, barycentric);
	return trace_ray(p_from, p_to, true, false, distance, normal, triangle, barycentric);
	// </ELIM>
}

// https://www.reedbeta.com/blog/hash-functions-for-gpu-rendering/
uint hash(uint value) {
	uint state = value * 747796405u + 2891336453u;
	uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
	return (word >> 22u) ^ word;
}

uint random_seed(ivec3 seed) {
	return hash(seed.x ^ hash(seed.y ^ hash(seed.z)));
}

// generates a random value in range [0.0, 1.0)
float randomize(inout uint value) {
	value = hash(value);
	return float(value / 4294967296.0);
}

const float PI = 3.14159265f;

// http://www.realtimerendering.com/raytracinggems/unofficial_RayTracingGems_v1.4.pdf (chapter 15)
vec3 generate_hemisphere_cosine_weighted_direction(inout uint noise) {
	float noise1 = randomize(noise);
	float noise2 = randomize(noise) * 2.0 * PI;

	return vec3(sqrt(noise1) * cos(noise2), sqrt(noise1) * sin(noise2), sqrt(1.0 - noise1));
}

// Distribution generation adapted from "Generating uniformly distributed numbers on a sphere"
// <http://corysimon.github.io/articles/uniformdistn-on-sphere/>
vec3 generate_sphere_uniform_direction(inout uint noise) {
	float theta = 2.0 * PI * randomize(noise);
	float phi = acos(1.0 - 2.0 * randomize(noise));
	return vec3(sin(phi) * cos(theta), sin(phi) * sin(theta), cos(phi));
}

vec3 generate_ray_dir_from_normal(vec3 normal, inout uint noise) {
	vec3 v0 = abs(normal.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
	vec3 tangent = normalize(cross(v0, normal));
	vec3 bitangent = normalize(cross(tangent, normal));
	mat3 normal_mat = mat3(tangent, bitangent, normal);
	return normal_mat * generate_hemisphere_cosine_weighted_direction(noise);
}

#if defined(MODE_DIRECT_LIGHT) || defined(MODE_BOUNCE_LIGHT) || defined(MODE_LIGHT_PROBES)

float get_omni_attenuation(float distance, float inv_range, float decay) {
	float nd = distance * inv_range;
	nd *= nd;
	nd *= nd; // nd^4
	nd = max(1.0 - nd, 0.0);
	nd *= nd; // nd^2
	return nd * pow(max(distance, 0.0001), -decay);
}

const int AA_SAMPLES = 16;

const vec2 halton_map[AA_SAMPLES] = vec2[](
		vec2(0.5, 0.33333333),
		vec2(0.25, 0.66666667),
		vec2(0.75, 0.11111111),
		vec2(0.125, 0.44444444),
		vec2(0.625, 0.77777778),
		vec2(0.375, 0.22222222),
		vec2(0.875, 0.55555556),
		vec2(0.0625, 0.88888889),
		vec2(0.5625, 0.03703704),
		vec2(0.3125, 0.37037037),
		vec2(0.8125, 0.7037037),
		vec2(0.1875, 0.14814815),
		vec2(0.6875, 0.48148148),
		vec2(0.4375, 0.81481481),
		vec2(0.9375, 0.25925926),
		vec2(0.03125, 0.59259259));

vec2 get_vogel_disk(float p_i, float p_rotation, float p_sample_count_sqrt) {
	const float golden_angle = 2.4;

	float r = sqrt(p_i + 0.5) / p_sample_count_sqrt;
	float theta = p_i * golden_angle + p_rotation;

	return vec2(cos(theta), sin(theta)) * r;
}

void trace_direct_light(vec3 p_position, vec3 p_normal, uint p_light_index, bool p_soft_shadowing, out vec3 r_light, out vec3 r_light_dir, inout uint r_noise, float p_texel_size, out float r_shadow) {
	const float EPSILON = 0.00001;

	r_light = vec3(0.0f);
	r_shadow = 0.0f;

	vec3 light_pos;
	float dist;
	float attenuation;
	float soft_shadowing_disk_size;
	Light light_data = lights.data[p_light_index];
	// <ELIM> The directional sun treats a sky face as reached-light (sees the sun
	// through the aperture) so it lights sealed interiors through Q3-style sky
	// apertures with hard shadows from solid geometry (no effect when no face is sky).
	bool skip_sky = (light_data.type == LIGHT_TYPE_DIRECTIONAL);
	// </ELIM>
	if (light_data.type == LIGHT_TYPE_DIRECTIONAL) {
		vec3 light_vec = light_data.direction;
		light_pos = p_position - light_vec * length(bake_params.world_size);
		r_light_dir = normalize(light_pos - p_position);
		dist = length(bake_params.world_size);
		attenuation = 1.0;
		soft_shadowing_disk_size = light_data.size;
	// <ELIM> q3map2-style surface lights: one-sided Lambertian area patch with
	// form-factor falloff regularized by the patch area (the attenuation slot
	// is repurposed to carry AREA), so texels beside/near the emitter stay
	// finite — no saturated blob. size = patch radius, so the existing
	// Vogel-disk soft shadowing area-samples the patch footprint (correct
	// penumbrae for free).
	} else if (light_data.type == LIGHT_TYPE_AREA_PATCH) {
		light_pos = light_data.position;
		r_light_dir = normalize(light_pos - p_position);
		dist = distance(p_position, light_pos);
		if (dist > light_data.range) {
			// Energy-derived cutoff; early-out before any shadow rays.
			return;
		}
		float cos_e = dot(light_data.direction, -r_light_dir);
		if (cos_e < 0.0001) {
			// Emitter is one-sided; also culls coplanar self-lighting.
			return;
		}
		float patch_area = light_data.attenuation;
		attenuation = patch_area * cos_e / (dist * dist + patch_area);
		soft_shadowing_disk_size = light_data.size / dist;
	// </ELIM>
	} else {
		light_pos = light_data.position;
		r_light_dir = normalize(light_pos - p_position);
		dist = distance(p_position, light_pos);
		if (dist > light_data.range) {
			return;
		}

		soft_shadowing_disk_size = light_data.size / dist;

		attenuation = get_omni_attenuation(dist, 1.0 / light_data.range, light_data.attenuation);

		if (light_data.type == LIGHT_TYPE_SPOT) {
			vec3 rel = normalize(p_position - light_pos);
			float cos_spot_angle = light_data.cos_spot_angle;
			float cos_angle = dot(rel, light_data.direction);

			if (cos_angle < cos_spot_angle) {
				return;
			}

			float scos = max(cos_angle, cos_spot_angle);
			float spot_rim = max(0.0001, (1.0 - scos) / (1.0 - cos_spot_angle));
			attenuation *= 1.0 - pow(spot_rim, light_data.inv_spot_attenuation);
		}
	}

	attenuation *= max(0.0, dot(p_normal, r_light_dir));
	if (attenuation <= 0.0001) {
		return;
	}

	// <ELIM> The AA/penumbra sampling below jitters ray origins across the
	// texel footprint and advances them along the ray by bias + |disk_sample|.
	// That is sized for cm-scale texels; TURNT texels are meters, so an
	// un-capped jitter relocates origins through walls/ceilings and the sun
	// lights sealed interiors (one-texel bleed bands at every corner the sun
	// grazes). Cap the jitter in world units; below the cap behavior is
	// identical to upstream.
	const float MAX_AA_JITTER_WS = 0.05;
	float aa_texel_size = min(p_texel_size, MAX_AA_JITTER_WS);
	// </ELIM>

	float penumbra = 0.0;
	vec3 penumbra_color = vec3(0.0);
	if (p_soft_shadowing) {
		const bool use_soft_shadows = (light_data.size > 0.0);
		const uint ray_count = AA_SAMPLES;
		const uint total_ray_count = use_soft_shadows ? params.ray_count : ray_count;
		const uint shadowing_rays_check_penumbra_denom = 2;
		const uint shadowing_ray_count = max(1, params.ray_count / ray_count);
		const float shadowing_ray_count_sqrt = sqrt(float(total_ray_count));

		// Setup tangent pass to calculate AA samples over the current texel.
		vec3 aux = p_normal.y < 0.777 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
		vec3 tangent = normalize(cross(p_normal, aux));
		vec3 bitan = normalize(cross(p_normal, tangent));

		// Setup light tangent pass to calculate samples over disk aligned towards the light
		vec3 light_to_point = -r_light_dir;
		vec3 light_aux = light_to_point.y < 0.777 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
		vec3 light_to_point_tan = normalize(cross(light_to_point, light_aux));
		vec3 light_to_point_bitan = normalize(cross(light_to_point, light_to_point_tan));

		float aa_power = 0.0;
		for (uint i = 0; i < ray_count; i++) {
			// Create a random sample within the texel.
			// <ELIM> capped aa_texel_size (see above); was the unclamped p_texel_size.
			// vec2 disk_sample = (halton_map[i] - vec2(0.5)) * p_texel_size * light_data.shadow_blur;
			vec2 disk_sample = (halton_map[i] - vec2(0.5)) * aa_texel_size * light_data.shadow_blur;
			// </ELIM>
			// Align the sample to world space.
			vec3 disk_aligned = (disk_sample.x * tangent + disk_sample.y * bitan);
			vec3 origin = p_position - disk_aligned;
			vec3 light_dir = normalize(light_pos - origin);

			float power = 0.0;
			vec3 light_color = vec3(0.0);
			uint power_accm = 0;
			vec3 prev_pos = origin;
			if (use_soft_shadows) {
				uint soft_shadow_hits = 0;
				for (uint j = 0; j < shadowing_ray_count; j++) {
					origin = prev_pos;
					// Optimization:
					// Once already traced an important proportion of rays, if all are hits or misses,
					// assume we're not in the penumbra so we can infer the rest would have the same result.
					if (j == shadowing_ray_count / shadowing_rays_check_penumbra_denom) {
						if (soft_shadow_hits == j) {
							// Assume totally lit
							soft_shadow_hits = shadowing_ray_count;
							break;
						} else if (soft_shadow_hits == 0) {
							// Assume totally dark
							soft_shadow_hits = 0;
							break;
						}
					}

					float a = randomize(r_noise) * 2.0 * PI;
					float vogel_index = float(total_ray_count - 1 - (i * shadowing_ray_count + j)); // Start from (total_ray_count - 1) so we check the outer points first.
					vec2 light_disk_sample = get_vogel_disk(vogel_index, a, shadowing_ray_count_sqrt) * soft_shadowing_disk_size * light_data.shadow_blur;
					vec3 light_disk_to_point = normalize(light_to_point + light_disk_sample.x * light_to_point_tan + light_disk_sample.y * light_to_point_bitan);
					float sample_penumbra = 0.0;
					vec3 sample_penumbra_color = light_data.color.rgb;
					bool sample_did_hit = false;

					for (uint iter = 0; iter < bake_params.transparency_rays; iter++) {
						vec4 hit_albedo = vec4(1.0);
						vec3 hit_position;
						// Offset the ray origin for AA, offset the light position for soft shadows.
						// <ELIM> pass skip_sky.
						// uint ret = trace_ray_closest_hit_triangle_albedo_alpha(origin - light_disk_to_point * (bake_params.bias + length(disk_sample)), p_position - light_disk_to_point * dist, hit_albedo, hit_position);
						uint ret = trace_ray_closest_hit_triangle_albedo_alpha(origin - light_disk_to_point * (bake_params.bias + length(disk_sample)), p_position - light_disk_to_point * dist, skip_sky, hit_albedo, hit_position);
						// </ELIM>
						if (ret == RAY_MISS) {
							if (!sample_did_hit) {
								sample_penumbra = 1.0;
							}
							soft_shadow_hits += 1;
							break;
						} else if (ret == RAY_FRONT || ret == RAY_BACK) {
							bool contribute = ret == RAY_FRONT || !sample_did_hit;
							if (!sample_did_hit) {
								sample_penumbra = 1.0;
								sample_did_hit = true;
							}

							soft_shadow_hits += 1;

							if (contribute) {
								sample_penumbra_color = mix(sample_penumbra_color, sample_penumbra_color * hit_albedo.rgb, hit_albedo.a);
								sample_penumbra *= 1.0 - hit_albedo.a;
							}
							origin = hit_position + r_light_dir * bake_params.bias;

							if (sample_penumbra - EPSILON <= 0) {
								break;
							}
						}
					}

					power += sample_penumbra;
					light_color += sample_penumbra_color;
					power_accm++;
				}

			} else { // No soft shadows (size == 0).
				float sample_penumbra = 0.0;
				vec3 sample_penumbra_color = light_data.color.rgb;
				bool sample_did_hit = false;
				for (uint iter = 0; iter < bake_params.transparency_rays; iter++) {
					vec4 hit_albedo = vec4(1.0);
					vec3 hit_position;
					// Offset the ray origin for AA, offset the light position for soft shadows.
					// <ELIM> pass skip_sky.
					// uint ret = trace_ray_closest_hit_triangle_albedo_alpha(origin + light_dir * (bake_params.bias + length(disk_sample)), light_pos, hit_albedo, hit_position);
					uint ret = trace_ray_closest_hit_triangle_albedo_alpha(origin + light_dir * (bake_params.bias + length(disk_sample)), light_pos, skip_sky, hit_albedo, hit_position);
					// </ELIM>
					if (ret == RAY_MISS) {
						if (!sample_did_hit) {
							sample_penumbra = 1.0;
						}
						break;
					} else if (ret == RAY_FRONT || ret == RAY_BACK) {
						bool contribute = ret == RAY_FRONT || !sample_did_hit;
						if (!sample_did_hit) {
							sample_penumbra = 1.0;
							sample_did_hit = true;
						}

						if (contribute) {
							sample_penumbra_color = mix(sample_penumbra_color, sample_penumbra_color * hit_albedo.rgb, hit_albedo.a);
							sample_penumbra *= 1.0 - hit_albedo.a;
						}
						origin = hit_position + r_light_dir * bake_params.bias;

						if (sample_penumbra - EPSILON <= 0) {
							break;
						}
					}
				}
				power = sample_penumbra;
				light_color = sample_penumbra_color;
				power_accm = 1;
			}
			aa_power += power / float(power_accm);
			penumbra_color += light_color / float(power_accm);
		}
		penumbra = aa_power / ray_count;
		penumbra_color /= ray_count;
	} else { // No soft shadows and anti-aliasing (disabled via parameter).
		bool did_hit = false;
		penumbra = 0.0;
		penumbra_color = light_data.color.rgb;
		for (uint iter = 0; iter < bake_params.transparency_rays; iter++) {
			vec4 hit_albedo = vec4(1.0);
			vec3 hit_position;
			// <ELIM> pass skip_sky.
			// uint ret = trace_ray_closest_hit_triangle_albedo_alpha(p_position + r_light_dir * bake_params.bias, light_pos, hit_albedo, hit_position);
			uint ret = trace_ray_closest_hit_triangle_albedo_alpha(p_position + r_light_dir * bake_params.bias, light_pos, skip_sky, hit_albedo, hit_position);
			// </ELIM>
			if (ret == RAY_MISS) {
				if (!did_hit) {
					penumbra = 1.0;
				}
				break;
			} else if (ret == RAY_FRONT || ret == RAY_BACK) {
				bool contribute = (ret == RAY_FRONT || !did_hit);
				if (!did_hit) {
					penumbra = 1.0;
					did_hit = true;
				}

				if (contribute) {
					penumbra_color = mix(penumbra_color, penumbra_color * hit_albedo.rgb, hit_albedo.a);
					penumbra *= 1.0 - hit_albedo.a;
				}

				p_position = hit_position + r_light_dir * bake_params.bias;

				if (penumbra - EPSILON <= 0) {
					break;
				}
			}
		}

		penumbra = clamp(penumbra, 0.0, 1.0);
	}

	r_shadow = penumbra;
	r_light = light_data.energy * attenuation * penumbra * penumbra_color;
}

#endif

#if defined(MODE_BOUNCE_LIGHT) || defined(MODE_LIGHT_PROBES)

vec3 trace_environment_color(vec3 ray_dir) {
	vec3 sky_dir = normalize(mat3(bake_params.env_transform) * ray_dir);
	vec2 st = vec2(atan(sky_dir.x, sky_dir.z), acos(sky_dir.y));
	if (st.x < 0.0) {
		st.x += PI * 2.0;
	}

	return textureLod(sampler2D(environment, linear_sampler), st / vec2(PI * 2.0, PI), 0.0).rgb;
}

vec3 trace_indirect_light(vec3 p_position, vec3 p_ray_dir, inout uint r_noise, float p_texel_size) {
	// The lower limit considers the case where the lightmapper might have bounces disabled but light probes are requested.
	vec3 position = p_position;
	vec3 ray_dir = p_ray_dir;
	uint max_depth = max(bake_params.bounces, 1);
	uint transparency_rays_left = bake_params.transparency_rays;
	vec3 throughput = vec3(1.0);
	vec3 light = vec3(0.0);
	for (uint depth = 0; depth < max_depth; depth++) {
		uint tidx;
		vec3 barycentric;
		uint trace_result = trace_ray_closest_hit_triangle(position + ray_dir * bake_params.bias, position + ray_dir * length(bake_params.world_size), tidx, barycentric);
		if (trace_result == RAY_FRONT) {
			// <ELIM> Cache the per-surface flags once (read twice below: sky +
			// trace-only), mirroring trace_ray_closest_hit_triangle_albedo_alpha.
			uint surface_flags = triangles.data[tidx].surface_flags;
			// </ELIM>
			// <ELIM> A bounce/probe ray that reaches a Q3-style sky face sees the
			// environment (skylight) and stops, exactly like escaping to infinity.
			// Sealed maps thus get sky ambient through sky apertures without CLEAR.
			if ((surface_flags & SURFACE_FLAG_SKY) != 0u) {
				light += throughput * trace_environment_color(ray_dir);
				break;
			}
			// </ELIM>
			// <ELIM> Non-sky trace-only faces are opaque black occluders with no
			// atlas chart. A bounce/probe ray that reaches one stops and adds nothing
			// (albedo 0 = zero bounce), which also avoids the garbage atlas reads
			// below. The sky check above runs first, so sky faces never reach here.
			if ((surface_flags & SURFACE_FLAG_TRACE_ONLY) != 0u) {
				break;
			}
			// </ELIM>
			Vertex vert0 = vertices.data[triangles.data[tidx].indices.x];
			Vertex vert1 = vertices.data[triangles.data[tidx].indices.y];
			Vertex vert2 = vertices.data[triangles.data[tidx].indices.z];
			vec3 uvw = vec3(barycentric.x * vert0.uv + barycentric.y * vert1.uv + barycentric.z * vert2.uv, float(triangles.data[tidx].slice));
			position = barycentric.x * vert0.position + barycentric.y * vert1.position + barycentric.z * vert2.position;

			vec3 prev_normal = ray_dir;

			vec3 norm0 = vec3(vert0.normal_xy, vert0.normal_z);
			vec3 norm1 = vec3(vert1.normal_xy, vert1.normal_z);
			vec3 norm2 = vec3(vert2.normal_xy, vert2.normal_z);
			vec3 normal = barycentric.x * norm0 + barycentric.y * norm1 + barycentric.z * norm2;

			vec3 direct_light = vec3(0.0f);
#ifdef USE_LIGHT_TEXTURE_FOR_BOUNCES
			direct_light += textureLod(sampler2DArray(source_light, linear_sampler), uvw, 0.0).rgb;
#else
			// Trace the lights directly. Significantly more expensive but more accurate in scenarios
			// where the lightmap texture isn't reliable.
			for (uint i = 0; i < bake_params.light_count; i++) {
				vec3 light;
				vec3 light_dir;
				float shadow;
				trace_direct_light(position, normal, i, false, light, light_dir, r_noise, p_texel_size, shadow);
				direct_light += light * lights.data[i].indirect_energy;
			}

			direct_light *= bake_params.exposure_normalization;
#endif

			vec4 albedo_alpha = textureLod(sampler2DArray(albedo_tex, linear_sampler), uvw, 0).rgba;
			// <ELIM> Opaque bake: no translucent chart-edge borders (see trace_ray_closest_hit_triangle_albedo_alpha).
			albedo_alpha.a = 1.0;
			// </ELIM>
			vec3 emissive = textureLod(sampler2DArray(emission_tex, linear_sampler), uvw, 0).rgb;
			emissive *= bake_params.exposure_normalization;

			// <ELIM> Surface-light faces are integrated analytically as AREA_PATCH
			// lights in trace_direct_light (at texels AND at bounce vertices), so
			// adding their rasterized emission here would double-count. Albedo /
			// throughput below still apply, so light bounced OFF the emitter keeps
			// its tint, and direct_light above is still re-evaluated (that is where
			// the emitter's indirect now comes from).
			// light += throughput * emissive * albedo_alpha.a;
			if ((surface_flags & SURFACE_FLAG_SURFACE_LIGHT) == 0u) {
				light += throughput * emissive * albedo_alpha.a;
			}
			// </ELIM>
			throughput = mix(throughput, throughput * albedo_alpha.rgb, albedo_alpha.a);
			light += throughput * direct_light * bake_params.bounce_indirect_energy * albedo_alpha.a;

			if (albedo_alpha.a < 1.0) {
				transparency_rays_left -= 1;
				depth -= 1;
				if (transparency_rays_left <= 0) {
					break;
				}

				// Either bounce off the transparent surface or keep going forward.
				float pa = albedo_alpha.a * albedo_alpha.a;
				if (randomize(r_noise) > pa) {
					normal = prev_normal;
				}

				position += normal * bake_params.bias;
			}

			// Use Russian Roulette to determine a probability to terminate the bounce earlier as an optimization.
			// <https://computergraphics.stackexchange.com/questions/2316/is-russian-roulette-really-the-answer>
			float p = max(max(throughput.x, throughput.y), throughput.z);
			if (randomize(r_noise) > p) {
				break;
			}

			// Boost the throughput from the probability of the ray being terminated early.
			throughput *= 1.0 / p;

			// Generate a new ray direction for the next bounce from this surface's normal.
			ray_dir = generate_ray_dir_from_normal(normal, r_noise);
		} else if (trace_result == RAY_MISS) {
			// Look for the environment color and stop bouncing.
			light += throughput * trace_environment_color(ray_dir);
			break;
		} else if (trace_result == RAY_BACK) {
			// <ELIM> Opaque bake: a backface hit means the ray entered solid
			// geometry — terminate. Upstream tests the hit's albedo alpha to
			// support transparent surfaces, but that sample is gutter-
			// contaminated near chart edges (bilinear mixes the gutter's
			// alpha=0 in), letting rays continue straight through walls.
			// TURNT bakes no transparent surfaces, so the upstream
			// transparency continuation block is removed entirely.
			break;
			// </ELIM>
		}
	}

	return light;
}

#endif

void main() {
	// Check if invocation is out of bounds.
#ifdef MODE_LIGHT_PROBES
	int probe_index = int(gl_GlobalInvocationID.x);
	if (probe_index >= params.probe_count) {
		return;
	}

#else
	ivec2 atlas_pos = ivec2(gl_GlobalInvocationID.xy) + params.region_ofs;
	if (any(greaterThanEqual(atlas_pos, bake_params.atlas_size))) {
		return;
	}
#endif

#ifdef MODE_DIRECT_LIGHT
	vec3 normal = texelFetch(sampler2DArray(source_normal, linear_sampler), ivec3(atlas_pos, params.atlas_slice), 0).xyz;
	if (length(normal) < 0.5) {
		return; //empty texel, no process
	}
	// <ELIM> Texels killed by the unocclude pass (buried in overlapping brush
	// geometry) stay empty so the dilate pass fills them from visible neighbors.
	if (texelFetch(sampler2DArray(source_position, linear_sampler), ivec3(atlas_pos, params.atlas_slice), 0).w < 0.5) {
		return;
	}
	// </ELIM>
	vec3 position = texelFetch(sampler2DArray(source_position, linear_sampler), ivec3(atlas_pos, params.atlas_slice), 0).xyz;
	vec4 neighbor_position = texelFetch(sampler2DArray(source_position, linear_sampler), ivec3(atlas_pos + ivec2(1, 0), params.atlas_slice), 0).xyzw;

	if (neighbor_position.w < 0.001) {
		// Empty texel, try again.
		neighbor_position.xyz = texelFetch(sampler2DArray(source_position, linear_sampler), ivec3(atlas_pos + ivec2(-1, 0), params.atlas_slice), 0).xyz;
	}
	float texel_size_world_space = distance(position, neighbor_position.xyz) * bake_params.supersampling_factor;

	// <ELIM> Light-batch accumulation (TDR guard): the first batch initializes the
	// output images with imageStore (bit-identical to the original single pass);
	// later batches read-modify-write to add their partial sum. Batches are
	// serialized by submit()+sync() on the CPU side, so each batch sees the
	// previous batch's stores.
	bool first_light_batch = (params.light_from == 0u);
	// </ELIM>

	vec3 light_for_texture = vec3(0.0);
	vec3 light_for_bounces = vec3(0.0);

#ifdef USE_SHADOWMASK
	float shadowmask_value = 0.0f;
#endif

#ifdef USE_SH_LIGHTMAPS
	vec4 sh_accum[4] = vec4[](
			vec4(0.0, 0.0, 0.0, 1.0),
			vec4(0.0, 0.0, 0.0, 1.0),
			vec4(0.0, 0.0, 0.0, 1.0),
			vec4(0.0, 0.0, 0.0, 1.0));
#endif

	// Use atlas position and a prime number as the seed.
	uint noise = random_seed(ivec3(atlas_pos, 43573547));
	// <ELIM> Iterate this batch's light range [light_from, light_to) instead of
	// all lights (TDR guard). One batch spanning [0, light_count) is identical to
	// the original `for (i = 0; i < bake_params.light_count; i++)`.
	// for (uint i = 0; i < bake_params.light_count; i++) {
	for (uint i = params.light_from; i < params.light_to; i++) {
	// </ELIM>
		vec3 light;
		vec3 light_dir;
		float shadow;
		trace_direct_light(position, normal, i, true, light, light_dir, noise, texel_size_world_space, shadow);

		if (lights.data[i].static_bake) {
			light_for_texture += light;

#ifdef USE_SH_LIGHTMAPS
			// For L0, light needs to be attenuated by dot(normal, light_dir) else it is oversaturated when sampled later.
			// For L1, light can't be attenuated by dot(normal, light_dir) since when sampling later, the dot product is done.
			// The output of trace_direct_light() is already attenuated by dot(normal, light_dir).
			// So L0 and L1 has the following relationship: L1 = L0 / dot(normal, light_dir).

			// For L1 packing to work, there needs to be a defined ratio (4) between L0 and L1 values.
			// This ratio is achieved with two coefficients c_l0 and c_l1, and ensuring that
			// 4 = (c_l0 * LO) / (c_l1 * L1)

			// For direct lights to look right, its effective "energy" needs to be 1 since it is not being integrated
			// unlike indirect lighting.
			// This binds c_l0 and c_l1 to the following relationship: 1 = c_l0 + c_l1

			float attenuation = dot(normal, light_dir);

			if (attenuation <= 0.0001) {
				continue;
			}

			float c_l0 = 1 / (1 + 4 * attenuation);
			float c_l1 = 1 - c_l0;
			float c[4] = float[](
					c_l0, //l0
					c_l1 / attenuation * light_dir.y, //l1n1
					c_l1 / attenuation * light_dir.z, //l1n0
					c_l1 / attenuation * light_dir.x //l1p1
			);

			for (uint j = 0; j < 4; j++) {
				sh_accum[j].rgb += light * c[j] * bake_params.exposure_normalization;
			}
#endif
		}

		light_for_bounces += light * lights.data[i].indirect_energy;

#ifdef USE_SHADOWMASK
		if (lights.data[i].type == LIGHT_TYPE_DIRECTIONAL && i == bake_params.shadowmask_light_idx) {
			shadowmask_value = max(shadowmask_value, shadow);
		}
#endif
	}

	light_for_bounces *= bake_params.exposure_normalization;
	// <ELIM> Batch-accumulate into dest_light (the bounce source). The first light
	// batch stores; later batches add their partial sum. A single batch is the
	// original single imageStore, bit-identical.
	// imageStore(dest_light, ivec3(atlas_pos, params.atlas_slice), vec4(light_for_bounces, 1.0));
	if (first_light_batch) {
		imageStore(dest_light, ivec3(atlas_pos, params.atlas_slice), vec4(light_for_bounces, 1.0));
	} else {
		vec3 prev_bounces = imageLoad(dest_light, ivec3(atlas_pos, params.atlas_slice)).rgb;
		imageStore(dest_light, ivec3(atlas_pos, params.atlas_slice), vec4(prev_bounces + light_for_bounces, 1.0));
	}
	// </ELIM>

#ifdef USE_SH_LIGHTMAPS
	// <ELIM> Batch-accumulate the SH coefficients. sh_accum[k].a is always 1.0
	// (only .rgb accumulates), so the stored alpha matches the original. A single
	// batch reproduces the original four imageStores exactly.
	// Keep for adding at the end.
	// imageStore(accum_light, ivec3(atlas_pos, params.atlas_slice * 4 + 0), sh_accum[0]);
	// imageStore(accum_light, ivec3(atlas_pos, params.atlas_slice * 4 + 1), sh_accum[1]);
	// imageStore(accum_light, ivec3(atlas_pos, params.atlas_slice * 4 + 2), sh_accum[2]);
	// imageStore(accum_light, ivec3(atlas_pos, params.atlas_slice * 4 + 3), sh_accum[3]);
	for (int sh_i = 0; sh_i < 4; sh_i++) {
		if (first_light_batch) {
			imageStore(accum_light, ivec3(atlas_pos, params.atlas_slice * 4 + sh_i), sh_accum[sh_i]);
		} else {
			vec3 prev_sh = imageLoad(accum_light, ivec3(atlas_pos, params.atlas_slice * 4 + sh_i)).rgb;
			imageStore(accum_light, ivec3(atlas_pos, params.atlas_slice * 4 + sh_i), vec4(prev_sh + sh_accum[sh_i].rgb, 1.0));
		}
	}
	// </ELIM>
#else
	light_for_texture *= bake_params.exposure_normalization;
	// <ELIM> Batch-accumulate the final direct-light texture (see dest_light above).
	// imageStore(accum_light, ivec3(atlas_pos, params.atlas_slice), vec4(light_for_texture, 1.0));
	if (first_light_batch) {
		imageStore(accum_light, ivec3(atlas_pos, params.atlas_slice), vec4(light_for_texture, 1.0));
	} else {
		vec3 prev_tex = imageLoad(accum_light, ivec3(atlas_pos, params.atlas_slice)).rgb;
		imageStore(accum_light, ivec3(atlas_pos, params.atlas_slice), vec4(prev_tex + light_for_texture, 1.0));
	}
	// </ELIM>
#endif

#ifdef USE_SHADOWMASK
	// <ELIM> Batch-accumulate the directional shadowmask with max() — the
	// directional light lands in exactly one batch and other batches contribute
	// 0, so max() over batches yields its shadow. Single batch == original store.
	// imageStore(shadowmask, ivec3(atlas_pos, params.atlas_slice), vec4(shadowmask_value, shadowmask_value, shadowmask_value, 1.0));
	float shadowmask_out = shadowmask_value;
	if (!first_light_batch) {
		shadowmask_out = max(imageLoad(shadowmask, ivec3(atlas_pos, params.atlas_slice)).r, shadowmask_value);
	}
	imageStore(shadowmask, ivec3(atlas_pos, params.atlas_slice), vec4(shadowmask_out, shadowmask_out, shadowmask_out, 1.0));
	// </ELIM>
#endif

#endif

#ifdef MODE_BOUNCE_LIGHT

#ifdef USE_SH_LIGHTMAPS
	vec4 sh_accum[4] = vec4[](
			vec4(0.0, 0.0, 0.0, 1.0),
			vec4(0.0, 0.0, 0.0, 1.0),
			vec4(0.0, 0.0, 0.0, 1.0),
			vec4(0.0, 0.0, 0.0, 1.0));
#else
	vec3 light_accum = vec3(0.0);
#endif

	// <ELIM> Buried texels (killed by the unocclude pass) stay empty.
	if (texelFetch(sampler2DArray(source_position, linear_sampler), ivec3(atlas_pos, params.atlas_slice), 0).w < 0.5) {
		return;
	}
	// </ELIM>

	// Retrieve starting normal and position.
	vec3 normal = texelFetch(sampler2DArray(source_normal, linear_sampler), ivec3(atlas_pos, params.atlas_slice), 0).xyz;
	if (length(normal) < 0.5) {
		// The pixel is empty, skip processing it.
		return;
	}

	vec3 position = texelFetch(sampler2DArray(source_position, linear_sampler), ivec3(atlas_pos, params.atlas_slice), 0).xyz;
	int neighbor_offset = atlas_pos.x < bake_params.atlas_size.x - 1 ? 1 : -1;
	vec3 neighbor_position = texelFetch(sampler2DArray(source_position, linear_sampler), ivec3(atlas_pos + ivec2(neighbor_offset, 0), params.atlas_slice), 0).xyz;
	float texel_size_world_space = distance(position, neighbor_position);
	uint noise = random_seed(ivec3(params.ray_from, atlas_pos));
	for (uint i = params.ray_from; i < params.ray_to; i++) {
		vec3 ray_dir = generate_ray_dir_from_normal(normal, noise);
		vec3 light = trace_indirect_light(position, ray_dir, noise, texel_size_world_space);

#ifdef USE_SH_LIGHTMAPS
		// These coefficients include the factored out SH evaluation, diffuse convolution, and final application, as well as the BRDF 1/PI and the spherical monte carlo factor.
		// LO: 1/(2*sqrtPI) * 1/(2*sqrtPI) * PI * PI * 1/PI = 0.25
		// L1: sqrt(3/(4*pi)) * sqrt(3/(4*pi)) * (PI*2/3) * (2 * PI) * 1/PI = 1.0
		// Note: This only works because we aren't scaling, rotating, or combing harmonics, we are just directing applying them in the shader.

		float c[4] = float[](
				0.25, //l0
				ray_dir.y, //l1n1
				ray_dir.z, //l1n0
				ray_dir.x //l1p1
		);

		for (uint j = 0; j < 4; j++) {
			sh_accum[j].rgb += light * c[j];
		}
#else
		light_accum += light;
#endif
	}

	// Add the averaged result to the accumulated light texture.
#ifdef USE_SH_LIGHTMAPS
	for (int i = 0; i < 4; i++) {
		vec4 accum = imageLoad(accum_light, ivec3(atlas_pos, params.atlas_slice * 4 + i));
		accum.rgb += sh_accum[i].rgb / float(params.ray_count);
		imageStore(accum_light, ivec3(atlas_pos, params.atlas_slice * 4 + i), accum);
	}
#else
	vec4 accum = imageLoad(accum_light, ivec3(atlas_pos, params.atlas_slice));
	accum.rgb += light_accum / float(params.ray_count);
	imageStore(accum_light, ivec3(atlas_pos, params.atlas_slice), accum);
#endif

#endif

#ifdef MODE_UNOCCLUDE

	//texel_size = 0.5;
	//compute tangents

	vec4 position_alpha = imageLoad(position, ivec3(atlas_pos, params.atlas_slice));
	if (position_alpha.a < 0.5) {
		return;
	}

	vec3 vertex_pos = position_alpha.xyz;
	vec4 normal_tsize = imageLoad(unocclude, ivec3(atlas_pos, params.atlas_slice));

	vec3 face_normal = normal_tsize.xyz;
	float texel_size = normal_tsize.w;

	vec3 v0 = abs(face_normal.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
	vec3 tangent = normalize(cross(v0, face_normal));
	vec3 bitangent = normalize(cross(tangent, face_normal));
	vec3 base_pos = vertex_pos + face_normal * bake_params.bias; // Raise a bit.

	vec3 rays[4] = vec3[](tangent, bitangent, -tangent, -bitangent);
	float min_d = 1e20;
	float unocclude_mask = 0.0;

	for (int i = 0; i < 4; i++) {
		vec3 ray_to = base_pos + rays[i] * texel_size * params.denoiser_range;
		float d;
		vec3 norm;

		if (trace_ray_closest_hit_distance(base_pos, ray_to, d, norm) == RAY_BACK) {
			unocclude_mask = 1.0;
			if (d <= texel_size && d < min_d) {
				// <ELIM> Buried texel: its sample point sits inside overlapping
				// brush geometry (Quake-style maps never CSG-clip brushes, so
				// faces continue beneath their neighbors). Upstream relocates
				// the point out of the solid, which on thin outer shells often
				// lands on the SUNLIT side and bleeds light across every seam
				// via bilinear filtering. Kill the texel instead — the dilate
				// pass fills it from its visible same-chart neighbors.
				position_alpha.a = 0.0;
				min_d = d;
				// </ELIM>
			}
		}
	}

	position_alpha.xyz = vertex_pos;

	imageStore(position, ivec3(atlas_pos, params.atlas_slice), position_alpha);
	imageStore(unocclude, ivec3(atlas_pos, params.atlas_slice), vec4(unocclude_mask, 0, 0, 0));

#endif

#ifdef MODE_LIGHT_PROBES

	vec3 position = probe_positions.data[probe_index].xyz;

	vec4 probe_sh_accum[9] = vec4[](
			vec4(0.0),
			vec4(0.0),
			vec4(0.0),
			vec4(0.0),
			vec4(0.0),
			vec4(0.0),
			vec4(0.0),
			vec4(0.0),
			vec4(0.0));

	uint noise = random_seed(ivec3(params.ray_from, probe_index, 49502741 /* some prime */));
	for (uint i = params.ray_from; i < params.ray_to; i++) {
		vec3 ray_dir = generate_sphere_uniform_direction(noise);
		vec3 light = trace_indirect_light(position, ray_dir, noise, 0.0);

		float c[9] = float[](
				0.282095, //l0
				0.488603 * ray_dir.y, //l1n1
				0.488603 * ray_dir.z, //l1n0
				0.488603 * ray_dir.x, //l1p1
				1.092548 * ray_dir.x * ray_dir.y, //l2n2
				1.092548 * ray_dir.y * ray_dir.z, //l2n1
				0.315392 * (3.0 * ray_dir.z * ray_dir.z - 1.0), //l20
				1.092548 * ray_dir.x * ray_dir.z, //l2p1
				0.546274 * (ray_dir.x * ray_dir.x - ray_dir.y * ray_dir.y) //l2p2
		);

		for (uint j = 0; j < 9; j++) {
			probe_sh_accum[j].rgb += light * c[j];
		}
	}

	if (params.ray_from > 0) {
		for (uint j = 0; j < 9; j++) { //accum from existing
			probe_sh_accum[j] += light_probes.data[probe_index * 9 + j];
		}
	}

	if (params.ray_to == params.ray_count) {
		for (uint j = 0; j < 9; j++) { //accum from existing
			probe_sh_accum[j] *= 4.0 / float(params.ray_count);
		}
	}

	for (uint j = 0; j < 9; j++) { //accum from existing
		light_probes.data[probe_index * 9 + j] = probe_sh_accum[j];
	}

#endif

#ifdef MODE_DILATE

	const int max_radius = int(4.0 * bake_params.supersampling_factor);
	const ivec2 directions[8] = ivec2[8](ivec2(-1, 0), ivec2(0, 1), ivec2(1, 0), ivec2(0, -1), ivec2(-1, -1), ivec2(-1, 1), ivec2(1, -1), ivec2(1, 1));

	vec4 texel_color = texelFetch(sampler2DArray(source_light, linear_sampler), ivec3(atlas_pos, params.atlas_slice), 0);

	for (int radius = 1; radius <= max_radius; radius++) {
		for (uint i = 0; i < 8; i++) {
			const ivec2 sample_pos = atlas_pos + directions[i] * radius;
			// Texture bounds check for robustness.
			if (any(lessThan(sample_pos, ivec2(0))) ||
					any(greaterThanEqual(sample_pos, textureSize(source_light, 0).xy))) {
				continue;
			}

			vec4 neighbor_color = texelFetch(sampler2DArray(source_light, linear_sampler), ivec3(sample_pos, params.atlas_slice), 0);
			if (neighbor_color.a > 0.5) {
				texel_color = neighbor_color;
				break;
			}
		}

		if (texel_color.a > 0.5) {
			break;
		}
	}

	imageStore(dest_light, ivec3(atlas_pos, params.atlas_slice), texel_color);

#endif

#ifdef MODE_DENOISE
	// Joint Non-local means (JNLM) denoiser.
	//
	// Based on YoctoImageDenoiser's JNLM implementation with corrections from "Nonlinearly Weighted First-order Regression for Denoising Monte Carlo Renderings".
	//
	// <https://github.com/ManuelPrandini/YoctoImageDenoiser/blob/06e19489dd64e47792acffde536393802ba48607/libs/yocto_extension/yocto_extension.cpp#L207>
	// <https://benedikt-bitterli.me/nfor/nfor.pdf>
	//
	// MIT License
	//
	// Copyright (c) 2020 ManuelPrandini
	//
	// Permission is hereby granted, free of charge, to any person obtaining a copy
	// of this software and associated documentation files (the "Software"), to deal
	// in the Software without restriction, including without limitation the rights
	// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
	// copies of the Software, and to permit persons to whom the Software is
	// furnished to do so, subject to the following conditions:
	//
	// The above copyright notice and this permission notice shall be included in all
	// copies or substantial portions of the Software.
	//
	// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
	// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
	// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
	// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
	// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
	// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
	// SOFTWARE.
	//
	// Most of the constants below have been hand-picked to fit the common scenarios lightmaps
	// are generated with, but they can be altered freely to experiment and achieve better results.

	// Half the size of the patch window around each pixel that is weighted to compute the denoised pixel.
	// A value of 1 represents a 3x3 window, a value of 2 a 5x5 window, etc.
	const int HALF_PATCH_WINDOW = 3;

	// Half the size of the search window around each pixel that is denoised and weighted to compute the denoised pixel.
	const int HALF_SEARCH_WINDOW = denoise_params.half_search_window;

	// For all of the following sigma values, smaller values will give less weight to pixels that have a bigger distance
	// in the feature being evaluated. Therefore, smaller values are likely to cause more noise to appear, but will also
	// cause less features to be erased in the process.

	// Controls how much the spatial distance of the pixels influences the denoising weight.
	const float SIGMA_SPATIAL = denoise_params.spatial_bandwidth;

	// Controls how much the light color distance of the pixels influences the denoising weight.
	const float SIGMA_LIGHT = denoise_params.light_bandwidth;

	// Controls how much the albedo color distance of the pixels influences the denoising weight.
	const float SIGMA_ALBEDO = denoise_params.albedo_bandwidth;

	// Controls how much the normal vector distance of the pixels influences the denoising weight.
	const float SIGMA_NORMAL = denoise_params.normal_bandwidth;

	// Strength of the filter. The original paper recommends values around 10 to 15 times the Sigma parameter.
	const float FILTER_VALUE = denoise_params.filter_strength * SIGMA_LIGHT;

	// Formula constants.
	const int PATCH_WINDOW_DIMENSION = (HALF_PATCH_WINDOW * 2 + 1);
	const int PATCH_WINDOW_DIMENSION_SQUARE = (PATCH_WINDOW_DIMENSION * PATCH_WINDOW_DIMENSION);
	const float TWO_SIGMA_SPATIAL_SQUARE = 2.0f * SIGMA_SPATIAL * SIGMA_SPATIAL;
	const float TWO_SIGMA_LIGHT_SQUARE = 2.0f * SIGMA_LIGHT * SIGMA_LIGHT;
	const float TWO_SIGMA_ALBEDO_SQUARE = 2.0f * SIGMA_ALBEDO * SIGMA_ALBEDO;
	const float TWO_SIGMA_NORMAL_SQUARE = 2.0f * SIGMA_NORMAL * SIGMA_NORMAL;
	const float FILTER_SQUARE_TWO_SIGMA_LIGHT_SQUARE = FILTER_VALUE * FILTER_VALUE * TWO_SIGMA_LIGHT_SQUARE;
	const float EPSILON = 1e-6f;

	const uint slice_count = denoise_params.slice_count;
	const uint slice_base = params.atlas_slice * slice_count;
	const bool is_directional = (slice_count == 4);

	for (uint i = 0; i < slice_count; i++) {
		uint lightmap_slice = slice_base + i;
		vec3 denoised_rgb = vec3(0.0f);
		vec4 input_light = texelFetch(sampler2DArray(source_light, linear_sampler), ivec3(atlas_pos, lightmap_slice), 0);
		vec3 input_albedo = texelFetch(sampler2DArray(albedo_tex, linear_sampler), ivec3(atlas_pos, params.atlas_slice), 0).rgb;
		vec3 input_normal = texelFetch(sampler2DArray(source_normal, linear_sampler), ivec3(atlas_pos, params.atlas_slice), 0).xyz;
		if (length(input_normal) > EPSILON) {
			// Compute the denoised pixel if the normal is valid.
			float sum_weights = 0.0f;
			vec3 input_rgb = input_light.rgb;
			for (int search_y = -HALF_SEARCH_WINDOW; search_y <= HALF_SEARCH_WINDOW; search_y++) {
				for (int search_x = -HALF_SEARCH_WINDOW; search_x <= HALF_SEARCH_WINDOW; search_x++) {
					ivec2 search_pos = atlas_pos + ivec2(search_x, search_y);
					vec3 search_rgb = texelFetch(sampler2DArray(source_light, linear_sampler), ivec3(search_pos, lightmap_slice), 0).rgb;
					vec3 search_albedo = texelFetch(sampler2DArray(albedo_tex, linear_sampler), ivec3(search_pos, params.atlas_slice), 0).rgb;
					vec3 search_normal = texelFetch(sampler2DArray(source_normal, linear_sampler), ivec3(search_pos, params.atlas_slice), 0).xyz;
					float search_occlusion = texelFetch(sampler2DArray(unocclude_mask, linear_sampler), ivec3(search_pos, params.atlas_slice), 0).r;
					float patch_square_dist = 0.0f;
					for (int offset_y = -HALF_PATCH_WINDOW; offset_y <= HALF_PATCH_WINDOW; offset_y++) {
						for (int offset_x = -HALF_PATCH_WINDOW; offset_x <= HALF_PATCH_WINDOW; offset_x++) {
							ivec2 offset_input_pos = atlas_pos + ivec2(offset_x, offset_y);
							ivec2 offset_search_pos = search_pos + ivec2(offset_x, offset_y);
							vec3 offset_input_rgb = texelFetch(sampler2DArray(source_light, linear_sampler), ivec3(offset_input_pos, slice_base), 0).rgb;
							vec3 offset_search_rgb = texelFetch(sampler2DArray(source_light, linear_sampler), ivec3(offset_search_pos, slice_base), 0).rgb;
							vec3 offset_delta_rgb = offset_input_rgb - offset_search_rgb;

							if (is_directional) {
								// Since L0 data is 1/4 the value of a regular lightmap,
								// we have to multiply it by 4.
								offset_delta_rgb *= 4.0;
							}

							patch_square_dist += dot(offset_delta_rgb, offset_delta_rgb) - TWO_SIGMA_LIGHT_SQUARE;
						}
					}

					patch_square_dist = max(0.0f, patch_square_dist / (3.0f * PATCH_WINDOW_DIMENSION_SQUARE));

					float weight = 1.0f;

					// Ignore weight if search position is out of bounds.
					weight *= step(0, search_pos.x) * step(search_pos.x, bake_params.atlas_size.x - 1);
					weight *= step(0, search_pos.y) * step(search_pos.y, bake_params.atlas_size.y - 1);

					// Ignore weight if normal is zero length.
					weight *= step(EPSILON, length(search_normal));

					// Weight with pixel distance.
					vec2 pixel_delta = vec2(search_x, search_y);
					float pixel_square_dist = dot(pixel_delta, pixel_delta);
					weight *= exp(-pixel_square_dist / TWO_SIGMA_SPATIAL_SQUARE);

					// Weight with patch.
					weight *= exp(-patch_square_dist / FILTER_SQUARE_TWO_SIGMA_LIGHT_SQUARE);

					// Weight with albedo.
					vec3 albedo_delta = input_albedo - search_albedo;
					float albedo_square_dist = dot(albedo_delta, albedo_delta);
					weight *= exp(-albedo_square_dist / TWO_SIGMA_ALBEDO_SQUARE);

					// Weight with normal.
					vec3 normal_delta = input_normal - search_normal;
					float normal_square_dist = dot(normal_delta, normal_delta);
					weight *= exp(-normal_square_dist / TWO_SIGMA_NORMAL_SQUARE);

					// Weight with occlusion.
					weight *= 1.0 - search_occlusion;

					denoised_rgb += weight * search_rgb;
					sum_weights += weight;
				}
			}

			// Avoid division by zero if no weights were accumulated.
			denoised_rgb = sum_weights > EPSILON ? denoised_rgb / sum_weights : input_rgb;
		} else {
			// Ignore pixels where the normal is empty, just copy the light color.
			denoised_rgb = input_light.rgb;
		}

		imageStore(dest_light, ivec3(atlas_pos, lightmap_slice), vec4(denoised_rgb, input_light.a));
	}
#endif

#ifdef MODE_PACK_L1_COEFFS
	vec4 base_coeff = texelFetch(sampler2DArray(source_light, linear_sampler), ivec3(atlas_pos, params.atlas_slice * 4), 0);
	imageStore(dest_light, ivec3(atlas_pos, params.atlas_slice * 4), base_coeff);

	for (int i = 1; i < 4; i++) {
		vec4 c = texelFetch(sampler2DArray(source_light, linear_sampler), ivec3(atlas_pos, params.atlas_slice * 4 + i), 0);
		c.rgb /= (base_coeff.rgb * 8.0 + vec3(1e-6f));
		c.rgb = clamp(c.rgb + vec3(0.5), vec3(0.0), vec3(1.0));

		imageStore(dest_light, ivec3(atlas_pos, params.atlas_slice * 4 + i), c);
	}
#endif
}
