/**************************************************************************/
/*  insights_singleton.h                                                  */
/**************************************************************************/
/* TURNT ADDITION -- not upstream Godot.                                  */
/*                                                                        */
/* Script-facing control surface for TURNT Insights captures.             */
/*                                                                        */
/* This class is compiled into EVERY build, including export templates    */
/* that were built without `profiler=turnt`. The turnt/ GDScript project  */
/* is shared between the editor and the exported game, so                 */
/* `TntInsights.start_capture(...)` has to resolve and quietly do nothing */
/* rather than fail to parse. Ask is_available() to find out which you    */
/* are talking to.                                                        */
/*                                                                        */
/* Deliberately does not include core/profiling/profiling.h -- that would */
/* couple every consumer of this header to the profiler build option.     */
/**************************************************************************/

#pragma once

#include "core/object/object.h"
#include "core/variant/binder_common.h"

class TntInsights : public Object {
	GDCLASS(TntInsights, Object);

	static TntInsights *singleton;

	// Auto-stop bookkeeping for capture_for_frames()/capture_for_seconds().
	// Both are resolved in poll(), on the main thread, so a capture never ends
	// from inside the render path.
	uint64_t limit_start_frame = 0;
	uint64_t limit_frames = 0;
	uint64_t limit_start_usec = 0;
	uint64_t limit_usec = 0;

	void _clear_limits();

protected:
	static void _bind_methods();

public:
	// Mirrors tnt_insights::CaptureFlags so scripts can compose a capture
	// without the engine header.
	enum CaptureFlag {
		FLAG_NONE = 0,
		FLAG_SCRIPT_ZONES = 1 << 1,
		FLAG_SCRIPT_NATIVE_ZONES = 1 << 2,
		FLAG_GPU = 1 << 3,
		FLAG_GPU_COARSE = 1 << 4,
		FLAG_COUNTERS = 1 << 5,
		FLAG_NATIVE_ZONES = 1 << 6,
	};

	static TntInsights *get_singleton();

	// True when the engine was built with `profiler=turnt`. Everything below is
	// inert when this is false.
	bool is_available() const;

	Error start_capture(const String &p_path, int p_flags);
	Error capture_for_frames(const String &p_path, int p_flags, int p_frames);
	Error capture_for_seconds(const String &p_path, int p_flags, double p_seconds);
	void stop_capture();
	bool is_capturing() const;

	// Call once per frame. Applies the frame/second auto-stop and returns true
	// on the frame a capture actually finished.
	bool poll();

	String get_last_capture_path() const;
	int get_dropped_event_count() const;
	int get_written_bytes() const;

	void mark(const String &p_name);
	void begin_zone(const String &p_name);
	void end_zone();
	void set_counter(const String &p_name, double p_value);
	void set_capture_meta(const String &p_key, const String &p_value);

	// Called by the engine at the real frame boundary. Not exposed to scripts.
	static void frame_boundary(uint64_t p_frame_index);
	// Called at the end of every main-loop iteration. Supplies a frame boundary
	// for iterations that never drew, which is every iteration of a headless run.
	static void iteration_boundary(uint64_t p_fallback_index);

	// --- GPU track. Called from the renderer, not exposed to scripts. -----
	//
	// Recording a GPU timestamp makes the render graph treat it as an ordering
	// fence (RenderingDeviceGraph::_add_command_to_graph), so a frame full of
	// markers loses most of its command overlap. GPU capture is therefore an
	// explicit opt-in, and gpu_marker_allowed() thins the marker set down to the
	// structural passes unless the caller asked for everything.
	// True when the running capture is recording counters. Guards the sampling
	// itself, not just the recording -- reading the rendering-info counters costs
	// a server call per frame.
	static bool wants_counters();

	static bool is_gpu_capture_enabled();
	static bool gpu_marker_allowed(const String &p_name);
	static void gpu_marker(const String &p_name, uint64_t p_gpu_nsec, uint64_t p_frame_index);
	static void gpu_calibration(uint64_t p_cpu_ticks, uint64_t p_gpu_nsec, uint64_t p_frame_index);

	TntInsights();
	~TntInsights();
};

VARIANT_ENUM_CAST(TntInsights::CaptureFlag);
