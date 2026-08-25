/**************************************************************************/
/*  insights_singleton.cpp                                                */
/**************************************************************************/
/* TURNT ADDITION -- not upstream Godot.                                  */
/**************************************************************************/

#include "core/profiling/insights_singleton.h"

#include "core/profiling/profiling.gen.h"

#include "core/config/engine.h"
#include "core/error/error_macros.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "core/string/ustring.h"
#include "core/version.h"

#ifdef GODOT_USE_TURNT_INSIGHTS
#include "core/profiling/insights.h"
#endif

TntInsights *TntInsights::singleton = nullptr;

TntInsights *TntInsights::get_singleton() {
	return singleton;
}

TntInsights::TntInsights() {
	singleton = this;
}

TntInsights::~TntInsights() {
#ifdef GODOT_USE_TURNT_INSIGHTS
	tnt_insights::stop_capture();
#endif
	if (singleton == this) {
		singleton = nullptr;
	}
}

bool TntInsights::is_available() const {
#ifdef GODOT_USE_TURNT_INSIGHTS
	return true;
#else
	return false;
#endif
}

void TntInsights::_clear_limits() {
	limit_frames = 0;
	limit_usec = 0;
	limit_start_frame = 0;
	limit_start_usec = 0;
}

Error TntInsights::start_capture(const String &p_path, int p_flags) {
#ifdef GODOT_USE_TURNT_INSIGHTS
	_clear_limits();

	const Error err = tnt_insights::start_capture(p_path, (uint32_t)p_flags);
	if (err != OK) {
		return err;
	}

	tnt_insights::set_thread_name("Main");
	tnt_insights::set_meta("engine_version", String(VERSION_FULL_CONFIG));
	tnt_insights::set_meta("os", OS::get_singleton()->get_name());
	tnt_insights::set_meta("cpu", OS::get_singleton()->get_processor_name());
	tnt_insights::set_meta("cpu_threads", itos(OS::get_singleton()->get_processor_count()));
	tnt_insights::set_meta("start_frame", itos(Engine::get_singleton()->get_frames_drawn()));
	return OK;
#else
	ERR_FAIL_V_MSG(ERR_UNAVAILABLE, "This build has no Insights backend. Rebuild the editor with `profiler=turnt`.");
#endif
}

Error TntInsights::capture_for_frames(const String &p_path, int p_flags, int p_frames) {
	ERR_FAIL_COND_V_MSG(p_frames <= 0, ERR_INVALID_PARAMETER, "Frame count must be positive.");
	const Error err = start_capture(p_path, p_flags);
	if (err != OK) {
		return err;
	}
	limit_start_frame = Engine::get_singleton()->get_frames_drawn();
	limit_frames = (uint64_t)p_frames;
	return OK;
}

Error TntInsights::capture_for_seconds(const String &p_path, int p_flags, double p_seconds) {
	ERR_FAIL_COND_V_MSG(p_seconds <= 0.0, ERR_INVALID_PARAMETER, "Duration must be positive.");
	const Error err = start_capture(p_path, p_flags);
	if (err != OK) {
		return err;
	}
	limit_start_usec = OS::get_singleton()->get_ticks_usec();
	limit_usec = (uint64_t)(p_seconds * 1000000.0);
	return OK;
}

void TntInsights::stop_capture() {
	_clear_limits();
#ifdef GODOT_USE_TURNT_INSIGHTS
	tnt_insights::stop_capture();
#endif
}

bool TntInsights::is_capturing() const {
#ifdef GODOT_USE_TURNT_INSIGHTS
	return tnt_insights::is_capturing();
#else
	return false;
#endif
}

bool TntInsights::poll() {
	if (!is_capturing()) {
		return false;
	}

	bool reached = false;
	if (limit_frames > 0) {
		reached = (Engine::get_singleton()->get_frames_drawn() - limit_start_frame) >= limit_frames;
	} else if (limit_usec > 0) {
		reached = (OS::get_singleton()->get_ticks_usec() - limit_start_usec) >= limit_usec;
	}

	if (reached) {
		stop_capture();
		return true;
	}
	return false;
}

String TntInsights::get_last_capture_path() const {
#ifdef GODOT_USE_TURNT_INSIGHTS
	return tnt_insights::get_last_capture_path();
#else
	return String();
#endif
}

int TntInsights::get_dropped_event_count() const {
#ifdef GODOT_USE_TURNT_INSIGHTS
	return (int)tnt_insights::get_dropped_event_count();
#else
	return 0;
#endif
}

int TntInsights::get_written_bytes() const {
#ifdef GODOT_USE_TURNT_INSIGHTS
	return (int)tnt_insights::get_written_byte_count();
#else
	return 0;
#endif
}

void TntInsights::mark(const String &p_name) {
#ifdef GODOT_USE_TURNT_INSIGHTS
	if (!tnt_insights::is_armed()) {
		return;
	}
	tnt_insights::emit_instant(tnt_insights::intern_string(p_name), tnt_insights::EVENT_FLAG_NONE);
#endif
}

void TntInsights::begin_zone(const String &p_name) {
#ifdef GODOT_USE_TURNT_INSIGHTS
	if (!tnt_insights::is_armed()) {
		return;
	}
	const uint32_t id = tnt_insights::intern_string(p_name);
	tnt_insights::emit(tnt_insights::now_qpc(), id, tnt_insights::EVENT_BEGIN, tnt_insights::EVENT_FLAG_SCRIPT);
#endif
}

void TntInsights::end_zone() {
#ifdef GODOT_USE_TURNT_INSIGHTS
	if (!tnt_insights::is_armed()) {
		return;
	}
	tnt_insights::emit(tnt_insights::now_qpc(), 0, tnt_insights::EVENT_END, tnt_insights::EVENT_FLAG_NONE);
#endif
}

void TntInsights::set_counter(const String &p_name, double p_value) {
#ifdef GODOT_USE_TURNT_INSIGHTS
	if (!(tnt_insights::capture_flags() & tnt_insights::CAPTURE_COUNTERS)) {
		return;
	}
	tnt_insights::note_counter(tnt_insights::intern_string(p_name), p_value);
#endif
}

void TntInsights::set_capture_meta(const String &p_key, const String &p_value) {
#ifdef GODOT_USE_TURNT_INSIGHTS
	tnt_insights::set_meta(p_key, p_value);
#endif
}

void TntInsights::frame_boundary(uint64_t p_frame_index) {
#ifdef GODOT_USE_TURNT_INSIGHTS
	tnt_insights::note_frame(p_frame_index);
#endif
}

void TntInsights::iteration_boundary(uint64_t p_fallback_index) {
#ifdef GODOT_USE_TURNT_INSIGHTS
	tnt_insights::note_iteration_end(p_fallback_index);
#endif
}

bool TntInsights::wants_counters() {
#ifdef GODOT_USE_TURNT_INSIGHTS
	return (tnt_insights::capture_flags() & tnt_insights::CAPTURE_COUNTERS) != 0;
#else
	return false;
#endif
}

bool TntInsights::is_gpu_capture_enabled() {
#ifdef GODOT_USE_TURNT_INSIGHTS
	return (tnt_insights::capture_flags() & tnt_insights::CAPTURE_GPU) != 0;
#else
	return false;
#endif
}

bool TntInsights::gpu_marker_allowed(const String &p_name) {
#ifdef GODOT_USE_TURNT_INSIGHTS
	if (!(tnt_insights::capture_flags() & tnt_insights::CAPTURE_GPU_COARSE)) {
		return true;
	}
	// `>` and `<` open and close the renderer's nesting, so dropping one would
	// unbalance the tree. `vp_` markers feed viewport_get_measured_render_time_*,
	// which is unrelated to Insights and must keep working.
	if (p_name.begins_with(">") || p_name.begins_with("<") || p_name.begins_with("vp_")) {
		return true;
	}
	// The passes worth a row when the goal is "where did the frame go", rather
	// than every dispatch in the renderer.
	static const char *kept[] = {
		"Frame Begin",
		"Prepare Render Frame",
		"Cull 3D Scene",
		"Render Shadows",
		"Render Depth Pre-Pass",
		"Render Opaque Pass",
		"Render Sky",
		"Render 3D Transparent Pass",
		"Render CanvasItems",
		"Process SSAO",
		"Tonemap",
		"Resolve MSAA",
		nullptr,
	};
	for (uint32_t i = 0; kept[i] != nullptr; i++) {
		if (p_name == kept[i]) {
			return true;
		}
	}
	return false;
#else
	return true;
#endif
}

void TntInsights::gpu_marker(const String &p_name, uint64_t p_gpu_nsec, uint64_t p_frame_index) {
#ifdef GODOT_USE_TURNT_INSIGHTS
	tnt_insights::note_gpu_marker(tnt_insights::intern_string(p_name), p_gpu_nsec, p_frame_index);
#endif
}

void TntInsights::gpu_calibration(uint64_t p_cpu_ticks, uint64_t p_gpu_nsec, uint64_t p_frame_index) {
#ifdef GODOT_USE_TURNT_INSIGHTS
	tnt_insights::note_gpu_calibration(p_cpu_ticks, p_gpu_nsec, p_frame_index);
#endif
}

void TntInsights::_bind_methods() {
	ClassDB::bind_method(D_METHOD("is_available"), &TntInsights::is_available);
	ClassDB::bind_method(D_METHOD("start_capture", "path", "flags"), &TntInsights::start_capture, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("capture_for_frames", "path", "flags", "frames"), &TntInsights::capture_for_frames);
	ClassDB::bind_method(D_METHOD("capture_for_seconds", "path", "flags", "seconds"), &TntInsights::capture_for_seconds);
	ClassDB::bind_method(D_METHOD("stop_capture"), &TntInsights::stop_capture);
	ClassDB::bind_method(D_METHOD("is_capturing"), &TntInsights::is_capturing);
	ClassDB::bind_method(D_METHOD("poll"), &TntInsights::poll);
	ClassDB::bind_method(D_METHOD("get_last_capture_path"), &TntInsights::get_last_capture_path);
	ClassDB::bind_method(D_METHOD("get_dropped_event_count"), &TntInsights::get_dropped_event_count);
	ClassDB::bind_method(D_METHOD("get_written_bytes"), &TntInsights::get_written_bytes);
	ClassDB::bind_method(D_METHOD("mark", "name"), &TntInsights::mark);
	ClassDB::bind_method(D_METHOD("begin_zone", "name"), &TntInsights::begin_zone);
	ClassDB::bind_method(D_METHOD("end_zone"), &TntInsights::end_zone);
	ClassDB::bind_method(D_METHOD("set_counter", "name", "value"), &TntInsights::set_counter);
	ClassDB::bind_method(D_METHOD("set_capture_meta", "key", "value"), &TntInsights::set_capture_meta);

	BIND_ENUM_CONSTANT(FLAG_NONE);
	BIND_ENUM_CONSTANT(FLAG_SCRIPT_ZONES);
	BIND_ENUM_CONSTANT(FLAG_SCRIPT_NATIVE_ZONES);
	BIND_ENUM_CONSTANT(FLAG_GPU);
	BIND_ENUM_CONSTANT(FLAG_GPU_COARSE);
	BIND_ENUM_CONSTANT(FLAG_COUNTERS);
	BIND_ENUM_CONSTANT(FLAG_NATIVE_ZONES);
}
