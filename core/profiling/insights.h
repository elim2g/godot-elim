/**************************************************************************/
/*  insights.h                                                            */
/**************************************************************************/
/* TURNT ADDITION -- not upstream Godot.                                  */
/*                                                                        */
/* Runtime for the `profiler=turnt` backend of core/profiling. Everything */
/* here is on the hot path: the RAII types below are what the             */
/* GodotProfile* macros expand to, and one of them sits on every GDScript */
/* function call. Keep the inline paths to a load, a branch and a store.  */
/*                                                                        */
/* Do not include this from another header -- see the note at the top of  */
/* profiling.h. Nothing in the tree includes profiling.h from a header,   */
/* and this file inherits that rule.                                      */
/**************************************************************************/

#pragma once

// Carries the GODOT_USE_* backend selection. This header is profiler-coupled by
// definition, so unlike profiling.h it may pull the generated header in.
#include "core/profiling/profiling.gen.h"

#include "core/profiling/insights_format.h"

#include "core/error/error_list.h"
#include "core/string/string_name.h"
#include "core/string/ustring.h"
#include "core/templates/safe_refcount.h"
#include "core/typedefs.h"

namespace tnt_insights {

// The armed word. Zero means no capture is running. Written only under the
// capture mutex in start_capture()/stop_capture(); read on every single zone.
extern SafeNumeric<uint32_t> g_capture_flags;

_FORCE_INLINE_ uint32_t capture_flags() {
	return g_capture_flags.get();
}

_FORCE_INLINE_ bool is_armed() {
	return g_capture_flags.get() != 0;
}

// Raw performance counter, no division. The frequency is stored in the file
// header and the conversion happens offline. OS::get_ticks_usec() is unusable
// here: it costs two 64-bit divides and a modulo per call.
uint64_t now_qpc();
uint64_t qpc_frequency();

// String and source-location tables are process-global and monotonic: ids stay
// valid for the life of the process, across any number of captures. That is
// what lets each call site cache its id in a function-local static.
// Id 0 is reserved as "not yet interned".
uint32_t intern_literal(const char *p_literal);
uint32_t intern_string(const String &p_string);
uint32_t intern_source_location(const StringName &p_file, const StringName &p_function, const StringName &p_name, uint32_t p_line);
// Pointer-keyed variant for call sites with nowhere to cache an id. Returns 0
// if the fixed-capacity cache is full.
uint32_t intern_source_location_cached(const void *p_key, const StringName &p_file, const StringName &p_function, const StringName &p_name, uint32_t p_line);

// Appends one record to the calling thread's buffer. Never blocks; drops the
// event (and counts the drop) if the writer has fallen too far behind.
void emit(uint64_t p_qpc, uint32_t p_id, uint8_t p_type, uint8_t p_flags);

_FORCE_INLINE_ void emit_instant(uint32_t p_id, uint8_t p_flags) {
	emit(now_qpc(), p_id, EVENT_INSTANT, p_flags);
}

void note_counter(uint32_t p_name_id, double p_value);

// Backing for the GodotProfileCounter / GodotProfileInstant macros. Each call
// site owns an id slot, so the literal is interned once for the process.
_FORCE_INLINE_ void counter_slot(uint32_t &r_id, const char *p_literal, double p_value) {
	if (capture_flags() & CAPTURE_COUNTERS) {
		if (r_id == 0) {
			r_id = intern_literal(p_literal);
		}
		note_counter(r_id, p_value);
	}
}

_FORCE_INLINE_ void instant_slot(uint32_t &r_id, const char *p_literal) {
	if (is_armed()) {
		if (r_id == 0) {
			r_id = intern_literal(p_literal);
		}
		emit(now_qpc(), r_id, EVENT_INSTANT, EVENT_FLAG_NONE);
	}
}

// --- RAII zone types. These are what the macros expand to. ---------------

// Scope timer. One atomic load in the constructor, none in the destructor --
// the arm decision is cached in m_open so a disarmed zone costs a byte test.
class ScopedZone {
	uint8_t m_open;

public:
	_FORCE_INLINE_ ScopedZone(uint32_t &r_id, const char *p_literal) {
		if (is_armed()) {
			if (r_id == 0) {
				r_id = intern_literal(p_literal);
			}
			emit(now_qpc(), r_id, EVENT_BEGIN, EVENT_FLAG_NONE);
			m_open = 1;
		} else {
			m_open = 0;
		}
	}

	_FORCE_INLINE_ ~ScopedZone() {
		if (m_open) {
			emit(now_qpc(), 0, EVENT_END, EVENT_FLAG_NONE);
		}
	}

	ScopedZone(const ScopedZone &) = delete;
	ScopedZone &operator=(const ScopedZone &) = delete;
};

// A phase cursor: each restart() closes the current span and opens the next.
// end() is idempotent, which is what makes this safe against upstream's
// GodotProfileZoneGroupedEndEarly pattern (an explicit close followed by the
// ordinary scope-exit close).
class GroupedZone {
	uint8_t m_open = 0;

public:
	_FORCE_INLINE_ void begin(uint32_t &r_id, const char *p_literal) {
		if (is_armed()) {
			if (r_id == 0) {
				r_id = intern_literal(p_literal);
			}
			emit(now_qpc(), r_id, EVENT_BEGIN, EVENT_FLAG_NONE);
			m_open = 1;
		}
	}

	_FORCE_INLINE_ void end() {
		if (m_open) {
			emit(now_qpc(), 0, EVENT_END, EVENT_FLAG_NONE);
			m_open = 0;
		}
	}

	_FORCE_INLINE_ void restart(uint32_t &r_id, const char *p_literal) {
		end();
		begin(r_id, p_literal);
	}

	_FORCE_INLINE_ GroupedZone(uint32_t &r_id, const char *p_literal) {
		begin(r_id, p_literal);
	}

	_FORCE_INLINE_ ~GroupedZone() {
		end();
	}

	GroupedZone(const GroupedZone &) = delete;
	GroupedZone &operator=(const GroupedZone &) = delete;
};

// One GDScript function call. The caller owns the id cache -- GDScriptFunction
// carries a _profile_loc_id field -- so interning happens once per function for
// the life of the process rather than once per call.
class ScriptZone {
	uint8_t m_open;

public:
	_FORCE_INLINE_ ScriptZone(uint32_t &r_cached_loc, const StringName &p_file, const StringName &p_function, const StringName &p_name, uint32_t p_line) {
		if (capture_flags() & CAPTURE_SCRIPT_ZONES) {
			if (r_cached_loc == 0) {
				r_cached_loc = intern_source_location(p_file, p_function, p_name, p_line);
			}
			emit(now_qpc(), r_cached_loc, EVENT_BEGIN, EVENT_FLAG_SOURCE_LOC | EVENT_FLAG_SCRIPT);
			m_open = 1;
		} else {
			m_open = 0;
		}
	}

	_FORCE_INLINE_ ~ScriptZone() {
		if (m_open) {
			emit(now_qpc(), 0, EVENT_END, EVENT_FLAG_NONE);
		}
	}

	ScriptZone(const ScriptZone &) = delete;
	ScriptZone &operator=(const ScriptZone &) = delete;
};

// An engine call made from GDScript. There is no per-call-site cache slot here,
// so this pays a pointer-keyed lookup and is gated behind its own flag, off by
// default.
class SystemCallZone {
	uint8_t m_open;

public:
	_FORCE_INLINE_ SystemCallZone(const void *p_key, const StringName &p_file, const StringName &p_function, const StringName &p_name, uint32_t p_line) {
		m_open = 0;
		if (capture_flags() & CAPTURE_SCRIPT_NATIVE_ZONES) {
			const uint32_t loc = intern_source_location_cached(p_key, p_file, p_function, p_name, p_line);
			if (loc != 0) {
				emit(now_qpc(), loc, EVENT_BEGIN, EVENT_FLAG_SOURCE_LOC | EVENT_FLAG_SCRIPT);
				m_open = 1;
			}
		}
	}

	_FORCE_INLINE_ ~SystemCallZone() {
		if (m_open) {
			emit(now_qpc(), 0, EVENT_END, EVENT_FLAG_NONE);
		}
	}

	SystemCallZone(const SystemCallZone &) = delete;
	SystemCallZone &operator=(const SystemCallZone &) = delete;
};

// --- Capture control. Called from the TntInsights singleton. -------------

Error start_capture(const String &p_path, uint32_t p_flags);
void stop_capture();
bool is_capturing();
String get_last_capture_path();
uint64_t get_dropped_event_count();
uint64_t get_written_byte_count();

void set_meta(const String &p_key, const String &p_value);
void set_thread_name(const char *p_name);

void note_frame(uint64_t p_frame_index);
// Fallback boundary for iterations that never reached the renderer -- a
// headless run draws nothing, so without this a CPU-only capture would have no
// frame track at all. Does nothing when note_frame() already fired.
void note_iteration_end(uint64_t p_fallback_index);
void note_gpu_marker(uint32_t p_name_id, uint64_t p_gpu_ns, uint64_t p_frame_index);
void note_gpu_calibration(uint64_t p_qpc, uint64_t p_gpu_ns, uint64_t p_frame_index);

// Zeroes POD state only. Runs from platform main() before the engine exists --
// no OS, no ProjectSettings, no FileAccess -- so it must not allocate or read
// settings. All real work happens in start_capture().
void init();
void cleanup();

} // namespace tnt_insights
