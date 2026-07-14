/**************************************************************************/
/*  register_types.cpp                                                    */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "register_types.h"

#include "lightmapper_rd.h"

#include "core/config/project_settings.h"
#include "scene/3d/lightmapper.h"

#ifndef _3D_DISABLED
static Lightmapper *create_lightmapper_rd() {
	return memnew(LightmapperRD);
}
#endif

void initialize_lightmapper_rd_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/lightmapping/bake_quality/low_quality_ray_count", PROPERTY_HINT_RANGE, "1,4096,1,or_greater"), 32);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/lightmapping/bake_quality/medium_quality_ray_count", PROPERTY_HINT_RANGE, "1,4096,1,or_greater"), 128);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/lightmapping/bake_quality/high_quality_ray_count", PROPERTY_HINT_RANGE, "1,4096,1,or_greater"), 512);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/lightmapping/bake_quality/ultra_quality_ray_count", PROPERTY_HINT_RANGE, "1,4096,1,or_greater"), 2048);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/lightmapping/bake_performance/max_rays_per_pass", PROPERTY_HINT_RANGE, "1,256,1,or_greater"), 4);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/lightmapping/bake_performance/region_size", PROPERTY_HINT_RANGE, "1,4096,1,or_greater"), 512);
	// <ELIM> TDR guard for the direct-light pass. The direct pass loops every light
	// for every texel, so a single compute dispatch costs
	// O(region_texels * lights * shadow_rays); emissive surface-light maps push the
	// light count into the thousands and one dispatch runs long enough to trip the
	// GPU driver watchdog (DEVICE_REMOVED). LightmapperRD::bake() batches the light
	// loop so each dispatch's estimated worst-case trace count
	// (region_texels * lights_in_batch * soft_shadow_rays_per_texel) stays under
	// max_traces_per_dispatch. Sizing by traces (not a fixed light count) auto-shrinks
	// the batch at high quality — where each light fires ~16x more soft-shadow rays —
	// and keeps it large at low quality / quickbake. Keep the target well under the
	// ~2s watchdog: assuming a pessimistic ~1e9 incoherent traces/s, the default
	// 5e8 is ~0.5s worst case and ~4x below the empirically-safe point (a 256-light
	// LOW-quality dispatch that bakes fine is ~2.1e9 worst-case traces). Lower it if
	// a heavy map still trips the watchdog; raise it if bakes over-split and run slow.
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/lightmapping/bake_performance/max_traces_per_dispatch", PROPERTY_HINT_RANGE, "1000000,10000000000,1,or_greater"), 500000000);
	// Upper clamp on the cost-aware batch size above (and the light count below which
	// a bake still runs in a single, bit-identical batch). See LightmapperRD::bake().
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/lightmapping/bake_performance/max_lights_per_pass", PROPERTY_HINT_RANGE, "1,65536,1,or_greater"), 256);
	// Visibility rays per texel per analytic area-poly surface light (direct
	// pass). The analytic term is exact; these rays only estimate occlusion, so
	// noise appears solely in partial-shadow regions. Read into the push
	// constant in LightmapperRD::bake().
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/lightmapping/bake_quality/surface_light_visibility_rays", PROPERTY_HINT_RANGE, "1,256,1"), 16);
	// </ELIM>
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/lightmapping/bake_performance/max_transparency_rays", PROPERTY_HINT_RANGE, "1,256,1,or_greater"), 8);

	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/lightmapping/bake_quality/low_quality_probe_ray_count", PROPERTY_HINT_RANGE, "1,4096,1,or_greater"), 64);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/lightmapping/bake_quality/medium_quality_probe_ray_count", PROPERTY_HINT_RANGE, "1,4096,1,or_greater"), 256);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/lightmapping/bake_quality/high_quality_probe_ray_count", PROPERTY_HINT_RANGE, "1,4096,1,or_greater"), 512);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/lightmapping/bake_quality/ultra_quality_probe_ray_count", PROPERTY_HINT_RANGE, "1,4096,1,or_greater"), 2048);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/lightmapping/bake_performance/max_rays_per_probe_pass", PROPERTY_HINT_RANGE, "1,256,1,or_greater"), 64);

	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/lightmapping/denoising/denoiser", PROPERTY_HINT_ENUM, "JNLM,OIDN"), 0);
#ifndef _3D_DISABLED
	GDREGISTER_CLASS(LightmapperRD);
	Lightmapper::create_gpu = create_lightmapper_rd;
#endif
}

void uninitialize_lightmapper_rd_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
}
