/**************************************************************************/
/*  insights_native.h                                                     */
/**************************************************************************/
/* TURNT ADDITION -- not upstream Godot.                                  */
/*                                                                        */
/* The bridge libturnt uses to emit into an Insights capture.             */
/*                                                                        */
/* The engine registers tnt_insights_get_api_v1 as a GDExtension          */
/* interface function (see GDExtension::initialize_gdextensions), and     */
/* libturnt resolves it once at startup via get_proc_address. That route  */
/* costs no Variant marshalling, no ClassDB lookup, and works at any      */
/* initialization level.                                                  */
/*                                                                        */
/* Registration is UNCONDITIONAL -- a build without the backend still     */
/* registers this and returns a table with `available == 0`. Registering  */
/* conditionally would make every launch of a template build print an     */
/* error, because GDExtension::get_interface_function ERR_FAILs on an     */
/* unknown name.                                                          */
/*                                                                        */
/* libturnt keeps a byte-for-byte copy of this struct at                  */
/* libturnt/src/insights/tnt_insights_native_api.h. If you change the     */
/* layout, bump STRUCT_VERSION in both -- the client refuses to bind to a */
/* version it does not recognise, which turns an ABI mismatch into a      */
/* silent no-op instead of a crash.                                       */
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
