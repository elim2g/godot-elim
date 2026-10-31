/**************************************************************************/
/*  insights_native.h                                                     */
/**************************************************************************/
/* TURNT ADDITION -- not upstream Godot.                                  */
/*                                                                        */
/* The bridge libturnt uses to emit into an Insights capture: a           */
/* GDExtension interface function (tnt_insights_get_api_v1) libturnt      */
/* resolves once via get_proc_address, so no Variant marshalling or       */
/* ClassDB lookup. Registered in every build (a build without the backend */
/* returns `available == 0`): an unknown name would ERR_FAIL on launch.   */
/*                                                                        */
/* libturnt/src/insights/tnt_insights_native_api.h is a byte-for-byte     */
/* copy. Change the layout and bump STRUCT_VERSION in both: the client    */
/* refuses an unknown version, so a mismatch is a no-op, not a crash.     */
/**************************************************************************/

#pragma once

#include <stdint.h>

extern "C" {

struct TntInsightsNativeApi {
	// Bump on ANY layout change. Mirrored in libturnt's copy.
	uint32_t struct_version;
	// 0 when the engine was built without `profiler=turnt`.
	uint32_t available;

	// Mirrors tnt_insights::CaptureFlags. Zero means no capture is running.
	uint32_t (*capture_flags)(void);

	// Address of the armed word, so a client can test "is a capture running" with
	// a plain load. libturnt's zones sit on the collision sweep, which runs
	// thousands of times per 125 Hz tick; paying an indirect call there just to
	// find out that nothing is capturing would be a permanent tax on the
	// simulation. Never null.
	const volatile uint32_t *(*capture_flags_address)(void);

	// Interns a string literal and returns its id. Call once per site and cache
	// the result; ids stay valid for the life of the process.
	uint32_t (*intern_literal)(const char *p_literal);

	// Timestamped internally, so a zone costs one indirect call per edge.
	void (*zone_begin)(uint32_t p_name_id);
	void (*zone_end)(void);
	void (*instant)(uint32_t p_name_id);
	void (*counter)(uint32_t p_name_id, double p_value);
	void (*set_thread_name)(const char *p_name);
};

inline constexpr uint32_t TNT_INSIGHTS_NATIVE_API_VERSION = 1;

const TntInsightsNativeApi *tnt_insights_get_api_v1();
}
