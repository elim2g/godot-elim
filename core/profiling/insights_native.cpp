/**************************************************************************/
/*  insights_native.cpp                                                   */
/**************************************************************************/
/* TURNT ADDITION -- not upstream Godot.                                  */
/**************************************************************************/

#include "core/profiling/insights_native.h"

#include "core/profiling/profiling.gen.h"

#ifdef GODOT_USE_TURNT_INSIGHTS

#include "core/profiling/insights.h"

static uint32_t _native_capture_flags() {
	return tnt_insights::capture_flags();
}

// The armed word is a SafeNumeric<uint32_t>, i.e. a std::atomic<uint32_t>, which
// is layout-compatible with a plain uint32_t on every platform this ships on.
// Handing out its address lets libturnt test "is a capture running" with a
// single relaxed load instead of a call.
static const volatile uint32_t *_native_capture_flags_address() {
	return reinterpret_cast<const volatile uint32_t *>(&tnt_insights::g_capture_flags);
}

static uint32_t _native_intern_literal(const char *p_literal) {
	return tnt_insights::intern_literal(p_literal);
}

static void _native_zone_begin(uint32_t p_name_id) {
	tnt_insights::emit(tnt_insights::now_qpc(), p_name_id, tnt_insights::EVENT_BEGIN, tnt_insights::EVENT_FLAG_NATIVE);
}

static void _native_zone_end() {
	tnt_insights::emit(tnt_insights::now_qpc(), 0, tnt_insights::EVENT_END, tnt_insights::EVENT_FLAG_NONE);
}

static void _native_instant(uint32_t p_name_id) {
	tnt_insights::emit(tnt_insights::now_qpc(), p_name_id, tnt_insights::EVENT_INSTANT, tnt_insights::EVENT_FLAG_NATIVE);
}

static void _native_counter(uint32_t p_name_id, double p_value) {
	tnt_insights::note_counter(p_name_id, p_value);
}

static void _native_set_thread_name(const char *p_name) {
	tnt_insights::set_thread_name(p_name);
}

static const TntInsightsNativeApi g_native_api = {
	TNT_INSIGHTS_NATIVE_API_VERSION,
	1,
	&_native_capture_flags,
	&_native_capture_flags_address,
	&_native_intern_literal,
	&_native_zone_begin,
	&_native_zone_end,
	&_native_instant,
	&_native_counter,
	&_native_set_thread_name,
};

#else // GODOT_USE_TURNT_INSIGHTS

// No backend in this build. The table still exists so the interface function
// can be registered unconditionally; `available == 0` tells libturnt to compile
// its zones down to nothing at bind time.
static uint32_t _native_capture_flags() {
	return 0;
}

static const uint32_t _native_never_armed = 0;

static const volatile uint32_t *_native_capture_flags_address() {
	return &_native_never_armed;
}

static uint32_t _native_intern_literal(const char *) {
	return 0;
}

static void _native_zone_begin(uint32_t) {}
static void _native_zone_end() {}
static void _native_instant(uint32_t) {}
static void _native_counter(uint32_t, double) {}
static void _native_set_thread_name(const char *) {}

static const TntInsightsNativeApi g_native_api = {
	TNT_INSIGHTS_NATIVE_API_VERSION,
	0,
	&_native_capture_flags,
	&_native_capture_flags_address,
	&_native_intern_literal,
	&_native_zone_begin,
	&_native_zone_end,
	&_native_instant,
	&_native_counter,
	&_native_set_thread_name,
};

#endif // GODOT_USE_TURNT_INSIGHTS

const TntInsightsNativeApi *tnt_insights_get_api_v1() {
	return &g_native_api;
}
