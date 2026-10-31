/**************************************************************************/
/*  lightmap_surface_lights.h                                             */
/**************************************************************************/
/* TURNT ADDITION -- not upstream Godot.                                  */
/*                                                                        */
/* Emissive surfaces as analytic area lights for LightmapGI: surfaces     */
/* whose material carries the "tnt_surface_light" meta are merged into    */
/* coplanar islands, and each island becomes one AREA_POLY light.         */
/**************************************************************************/

#pragma once

#include "core/math/transform_3d.h"
#include "core/templates/local_vector.h"
#include "scene/3d/lightmapper.h"
#include "scene/resources/material.h"
#include "scene/resources/mesh.h"

// One analytic polygonal light produced per merged coplanar island.
struct SurfaceLightIslandLight {
	Vector3 pos; // Island centroid, nudged off the face by the bake bias.
	Vector3 normal;
	float area = 0.0f; // True island (member-triangle) area.
	Color color;
	float energy = 0.0f;
	// Island index. The bake ignores it; the debug hook returns it so a
	// visualizer can colour footprints by island.
	int32_t component = -1;
	// Emitter fixture: islands that touch and emit alike (the faces of one
	// emissive brush, or the pieces chunking cut it into) share it.
	int32_t group = -1;
	// True when this island sits in face-to-face contact with solid geometry
	// (emits into solid). The bake skips it at emission; the debug hook returns
	// it with the flag set so the cull is visible in the visualizer.
	bool occluded = false;
	// The analytic polygon: flat v0,v1,v2 triangle triples, winding-normalized
	// so cross(v1-v0, v2-v0) points along `normal`, each vertex nudged off the
	// face by the bake bias. Hull fan for convex islands (exact), raw member
	// triangles for non-convex under the cap, hull fan + radiance_scale past it.
	PackedVector3Array poly_verts;
	// Furthest poly vertex from the centroid; the range cull adds it so the
	// centroid distance test is conservative for texels near a big island's edge.
	float bounding_radius = 0.0f;
	// island_area / hull_area (< 1) when a non-convex island fell back to its
	// hull fan past the tri cap - conserves emitted power over the larger hull.
	float radiance_scale = 1.0f;
	// Debug-only REAL footprint geometry (pre-bias): the world-space triangles
	// (3*N verts) this light integrates. Filled only when the collector runs
	// with geometry capture (the debug hook); the bake leaves it empty.
	PackedVector3Array dbg_verts;
};

// One mesh handed to the surface-light collector. xform is mesh -> target frame:
// lightmapper-relative for the bake, world for the debug hook. overrides are the
// per-surface override materials.
struct SurfaceLightSurfaceArrays {
	Vector<Vector3> vertices;
	Vector<Vector3> normals;
	Vector<int> index;
};

struct SurfaceLightMeshInput {
	Ref<Mesh> mesh;
	Transform3D xform;
	Vector<Ref<Material>> overrides;
	// Arrays the caller already fetched, by surface index. Missing or empty
	// entries are fetched by the collector (each fetch is a GPU readback).
	LocalVector<SurfaceLightSurfaceArrays> surfaces;
};

class LightmapSurfaceLights {
public:
	// Bake flags of a surface's effective (override-preferred) material.
	static uint32_t surface_bake_flags(const Ref<Material> &p_material);
	// Island lights for every emitter surface of p_inputs, in their xform frame.
	static void collect(const Vector<SurfaceLightMeshInput> &p_inputs, float p_pos_bias, Vector<SurfaceLightIslandLight> &r_islands, bool p_capture_geometry = false);
	// Adds the islands that are not occluded; returns how many.
	static int add_to_lightmapper(const Ref<Lightmapper> &p_lightmapper, const Vector<SurfaceLightIslandLight> &p_islands);
};
