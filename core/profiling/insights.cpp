/**************************************************************************/
/*  insights.cpp                                                          */
/**************************************************************************/
/* TURNT ADDITION -- not upstream Godot.                                  */
/**************************************************************************/

#include "core/profiling/insights.h"

#ifdef GODOT_USE_TURNT_INSIGHTS

#include "core/config/engine.h"
#include "core/error/error_macros.h"
#include "core/io/file_access.h"
#include "core/os/mutex.h"
#include "core/os/os.h"
#include "core/os/semaphore.h"
#include "core/os/thread.h"
#include "core/os/time.h"
#include "core/templates/hash_map.h"
#include "core/templates/local_vector.h"

#include <string.h>

#ifdef WINDOWS_ENABLED
// Included last and with the noisiest macros suppressed, so nothing above is
// affected by windows.h.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <time.h>
#endif

namespace tnt_insights {

SafeNumeric<uint32_t> g_capture_flags(0);

// --- Clock ---------------------------------------------------------------

uint64_t now_qpc() {
#ifdef WINDOWS_ENABLED
	LARGE_INTEGER counter;
	QueryPerformanceCounter(&counter);
	return (uint64_t)counter.QuadPart;
#else
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
#endif
}

uint64_t qpc_frequency() {
#ifdef WINDOWS_ENABLED
	// Fixed at boot on Windows, so caching it is safe.
	static uint64_t s_frequency = 0;
	if (s_frequency == 0) {
		LARGE_INTEGER frequency;
		QueryPerformanceFrequency(&frequency);
		s_frequency = (uint64_t)frequency.QuadPart;
	}
	return s_frequency;
#else
	return 1000000000ULL;
#endif
}

// --- Buffers -------------------------------------------------------------

// 4096 records is 64 KiB of payload per chunk. MAX_CHUNKS caps in-flight event
// data at ~32 MiB; past that the writer is too far behind and events are
// dropped rather than growing without bound. The drop count lands in the file
// metadata so a capture never silently lies about being complete.
static constexpr uint32_t EVENTS_PER_CHUNK = 4096;
static constexpr uint32_t MAX_CHUNKS = 512;

struct EventChunk {
	EventChunk *next = nullptr;
	uint64_t thread_id = 0;
	uint32_t count = 0;
	EventRecord events[EVENTS_PER_CHUNK];
};

// Owned by the registry for the life of the process, never freed while a thread
// could still hold a pointer to it. Only the chunks move.
struct ThreadState {
	uint64_t thread_id = 0;
	uint32_t name_id = 0;
	EventChunk *current = nullptr;
	ThreadState *next = nullptr;
};

static thread_local ThreadState *tls_thread = nullptr;

static Mutex g_mutex;
static Semaphore g_writer_sem;
static Thread *g_writer_thread = nullptr;
static SafeFlag g_writer_exit;

static bool g_active = false;
static Ref<FileAccess> g_file;
static String g_last_capture_path;
static uint64_t g_dropped_events = 0;
static uint64_t g_written_bytes = 0;

static EventChunk *g_free_head = nullptr;
static EventChunk *g_full_head = nullptr;
static EventChunk *g_full_tail = nullptr;
static uint32_t g_chunks_allocated = 0;

static ThreadState *g_threads_head = nullptr;
static uint64_t g_synthetic_thread_id = 1000000;

static LocalVector<String> g_strings;
static HashMap<String, uint32_t> g_string_ids;
static uint32_t g_strings_written = 0;

static LocalVector<SourceLocRecord> g_sourcelocs;
static uint32_t g_sourcelocs_written = 0;

static LocalVector<CounterRecord> g_counters;
static LocalVector<FrameRecord> g_frames;
static LocalVector<GpuRecord> g_gpu;
static LocalVector<GpuCalibrationRecord> g_gpu_calibrations;
static LocalVector<String> g_meta;

// --- String and source-location tables -----------------------------------
//
// Both tables are process-global and monotonic: an id handed out during one
// capture is still valid in the next. That is what lets every call site cache
// its id in a function-local static. Each capture rewrites the whole table, so
// a file is always self-contained.

static uint32_t _intern_string_locked(const String &p_string) {
	HashMap<String, uint32_t>::Iterator existing = g_string_ids.find(p_string);
	if (existing) {
		return existing->value;
	}
	// Ids start at 1; 0 means "not yet interned" at the call sites.
	const uint32_t id = (uint32_t)g_strings.size() + 1;
	g_strings.push_back(p_string);
	g_string_ids.insert(p_string, id);
	return id;
}

uint32_t intern_literal(const char *p_literal) {
	MutexLock lock(g_mutex);
	return _intern_string_locked(String::utf8(p_literal));
}

uint32_t intern_string(const String &p_string) {
	MutexLock lock(g_mutex);
	return _intern_string_locked(p_string);
}

uint32_t intern_source_location(const StringName &p_file, const StringName &p_function, const StringName &p_name, uint32_t p_line) {
	MutexLock lock(g_mutex);
	SourceLocRecord record;
	record.name_id = _intern_string_locked(String(p_name));
	record.file_id = _intern_string_locked(String(p_file));
	record.function_id = _intern_string_locked(String(p_function));
	record.line = p_line;
	g_sourcelocs.push_back(record);
	return (uint32_t)g_sourcelocs.size(); // 1-based
}

// Fixed-capacity pointer-keyed cache for call sites with nowhere to store an
// id. Readers are lock free: the value is published before the key, so seeing a
// key guarantees the value is visible. Inserts take the table mutex.
static constexpr uint32_t LOC_CACHE_SLOTS = 8192; // power of two
static constexpr uint32_t LOC_CACHE_MASK = LOC_CACHE_SLOTS - 1;
static constexpr uint32_t LOC_CACHE_MAX_PROBE = 16;

struct LocCacheSlot {
	SafeNumeric<uint64_t> key;
	SafeNumeric<uint32_t> value;
};

static LocCacheSlot g_loc_cache[LOC_CACHE_SLOTS];

static _FORCE_INLINE_ uint32_t _loc_cache_hash(uint64_t p_key) {
	// Pointers are aligned, so the low bits carry little information.
	uint64_t h = p_key >> 4;
	h *= 0x9E3779B97F4A7C15ULL;
	return (uint32_t)(h >> 32) & LOC_CACHE_MASK;
}

uint32_t intern_source_location_cached(const void *p_key, const StringName &p_file, const StringName &p_function, const StringName &p_name, uint32_t p_line) {
	const uint64_t key = (uint64_t)(uintptr_t)p_key;
	if (key == 0) {
		return 0;
	}

	uint32_t index = _loc_cache_hash(key);
	for (uint32_t probe = 0; probe < LOC_CACHE_MAX_PROBE; probe++) {
		const uint64_t slot_key = g_loc_cache[index].key.get();
		if (slot_key == key) {
			return g_loc_cache[index].value.get();
		}
		if (slot_key == 0) {
			break;
		}
		index = (index + 1) & LOC_CACHE_MASK;
	}

	const uint32_t loc = intern_source_location(p_file, p_function, p_name, p_line);

	MutexLock lock(g_mutex);
	index = _loc_cache_hash(key);
	for (uint32_t probe = 0; probe < LOC_CACHE_MAX_PROBE; probe++) {
		const uint64_t slot_key = g_loc_cache[index].key.get();
		if (slot_key == key) {
			return g_loc_cache[index].value.get();
		}
		if (slot_key == 0) {
			g_loc_cache[index].value.set(loc);
			g_loc_cache[index].key.set(key); // published last
			return loc;
		}
		index = (index + 1) & LOC_CACHE_MASK;
	}
	// Cache is full in this neighbourhood. The location is still valid, it just
	// will not be found again next call.
	return loc;
}

// --- Chunk plumbing ------------------------------------------------------

static void _push_full_locked(EventChunk *p_chunk) {
	p_chunk->next = nullptr;
	if (g_full_tail) {
		g_full_tail->next = p_chunk;
	} else {
		g_full_head = p_chunk;
	}
	g_full_tail = p_chunk;
}

static ThreadState *_register_thread() {
	ThreadState *state = memnew(ThreadState);

	MutexLock lock(g_mutex);
	uint64_t id = Thread::get_caller_id();
	if (id == Thread::UNASSIGNED_ID) {
		// A thread Godot did not create. Give it a distinct label so its events
		// do not merge with another such thread's.
		id = g_synthetic_thread_id++;
	}
	state->thread_id = id;
	state->next = g_threads_head;
	g_threads_head = state;

	tls_thread = state;
	return state;
}

// Retires the thread's full chunk and hands back an empty one. Returns nullptr
// when no capture is running or the in-flight ceiling has been reached.
static EventChunk *_rotate_chunk(ThreadState *p_state) {
	MutexLock lock(g_mutex);

	if (p_state->current) {
		_push_full_locked(p_state->current);
		p_state->current = nullptr;
	}

	if (!g_active) {
		return nullptr;
	}

	EventChunk *chunk = g_free_head;
	if (chunk) {
		g_free_head = chunk->next;
	} else if (g_chunks_allocated < MAX_CHUNKS) {
		chunk = memnew(EventChunk);
		g_chunks_allocated++;
	} else {
		g_dropped_events++;
		g_writer_sem.post();
		return nullptr;
	}

	chunk->next = nullptr;
	chunk->count = 0;
	chunk->thread_id = p_state->thread_id;
	p_state->current = chunk;

	g_writer_sem.post();
	return chunk;
}

void emit(uint64_t p_qpc, uint32_t p_id, uint8_t p_type, uint8_t p_flags) {
	ThreadState *state = tls_thread;
	if (unlikely(state == nullptr)) {
		state = _register_thread();
	}

	EventChunk *chunk = state->current;
	if (unlikely(chunk == nullptr || chunk->count == EVENTS_PER_CHUNK)) {
		chunk = _rotate_chunk(state);
		if (unlikely(chunk == nullptr)) {
			return;
		}
	}

	EventRecord &record = chunk->events[chunk->count++];
	record.qpc = p_qpc;
	record.id = p_id;
	record.type = p_type;
	record.flags = p_flags;
	record.reserved = 0;
}

// --- Auxiliary streams ---------------------------------------------------

static SafeFlag g_frame_drawn_this_iteration;

static void _push_frame_record(uint64_t p_qpc, uint64_t p_frame_index) {
	MutexLock lock(g_mutex);
	if (!g_active) {
		return;
	}
	FrameRecord record;
	record.qpc = p_qpc;
	record.frame_index = p_frame_index;
	g_frames.push_back(record);
}

void note_frame(uint64_t p_frame_index) {
	g_frame_drawn_this_iteration.set();
	if (!is_armed()) {
		return;
	}
	_push_frame_record(now_qpc(), p_frame_index);
}

void note_iteration_end(uint64_t p_fallback_index) {
	if (g_frame_drawn_this_iteration.is_set()) {
		g_frame_drawn_this_iteration.clear();
		return;
	}
	if (!is_armed()) {
		return;
	}
	_push_frame_record(now_qpc(), p_fallback_index);
}

void note_counter(uint32_t p_name_id, double p_value) {
	if (!(capture_flags() & CAPTURE_COUNTERS)) {
		return;
	}
	const uint64_t qpc = now_qpc();
	MutexLock lock(g_mutex);
	if (!g_active) {
		return;
	}
	CounterRecord record;
	record.qpc = qpc;
	record.name_id = p_name_id;
	record.reserved = 0;
	record.value = p_value;
	g_counters.push_back(record);
}

void note_gpu_marker(uint32_t p_name_id, uint64_t p_gpu_ns, uint64_t p_frame_index) {
	MutexLock lock(g_mutex);
	if (!g_active) {
		return;
	}
	GpuRecord record;
	record.gpu_ns = p_gpu_ns;
	record.frame_index = p_frame_index;
	record.name_id = p_name_id;
	record.type = EVENT_INSTANT;
	record.flags = EVENT_FLAG_NONE;
	record.reserved = 0;
	g_gpu.push_back(record);
}

void note_gpu_calibration(uint64_t p_qpc, uint64_t p_gpu_ns, uint64_t p_frame_index) {
	MutexLock lock(g_mutex);
	if (!g_active) {
		return;
	}
	GpuCalibrationRecord record;
	record.qpc = p_qpc;
	record.gpu_ns = p_gpu_ns;
	record.frame_index = p_frame_index;
	record.reserved = 0;
	g_gpu_calibrations.push_back(record);
}

void set_meta(const String &p_key, const String &p_value) {
	MutexLock lock(g_mutex);
	g_meta.push_back(p_key + "=" + p_value);
}

void set_thread_name(const char *p_name) {
	const uint32_t name_id = intern_literal(p_name);
	ThreadState *state = tls_thread;
	if (state == nullptr) {
		state = _register_thread();
	}
	state->name_id = name_id;
}

// --- Writing -------------------------------------------------------------

static void _write_raw(const uint8_t *p_data, uint64_t p_length) {
	if (g_file.is_null()) {
		return;
	}
	g_file->store_buffer(p_data, p_length);
	g_written_bytes += p_length;
}

static void _write_chunk(uint32_t p_type, uint64_t p_param, const uint8_t *p_payload, uint64_t p_length) {
	ChunkHeader header;
	header.type = p_type;
	header.byte_length = (uint32_t)p_length;
	header.param = p_param;
	_write_raw((const uint8_t *)&header, sizeof(ChunkHeader));
	if (p_length > 0) {
		_write_raw(p_payload, p_length);
	}
}

// Serializes as: u32 first_id, then for each string a u32 byte length followed
// by that many utf8 bytes.
static void _write_strings_chunk(const LocalVector<String> &p_strings, uint32_t p_first_id) {
	if (p_strings.is_empty()) {
		return;
	}
	LocalVector<uint8_t> payload;
	payload.resize(sizeof(uint32_t));
	memcpy(payload.ptr(), &p_first_id, sizeof(uint32_t));

	for (const String &string : p_strings) {
		const CharString utf8 = string.utf8();
		const uint32_t length = (uint32_t)utf8.length();
		const uint32_t offset = (uint32_t)payload.size();
		payload.resize(offset + sizeof(uint32_t) + length);
		memcpy(payload.ptr() + offset, &length, sizeof(uint32_t));
		if (length > 0) {
			memcpy(payload.ptr() + offset + sizeof(uint32_t), utf8.get_data(), length);
		}
	}
	_write_chunk(CHUNK_STRINGS, 0, payload.ptr(), payload.size());
}

// Moves everything currently pending out to disk. Runs only on the writer
// thread. The mutex is held to take ownership of the pending data and released
// before any file I/O.
static void _drain_pending() {
	EventChunk *chunks = nullptr;
	LocalVector<String> new_strings;
	uint32_t new_strings_first_id = 0;
	LocalVector<SourceLocRecord> new_sourcelocs;
	uint32_t new_sourcelocs_first_id = 0;
	LocalVector<CounterRecord> counters;
	LocalVector<FrameRecord> frames;
	LocalVector<GpuRecord> gpu;
	LocalVector<GpuCalibrationRecord> gpu_calibrations;

	{
		MutexLock lock(g_mutex);

		chunks = g_full_head;
		g_full_head = nullptr;
		g_full_tail = nullptr;

		new_strings_first_id = g_strings_written + 1;
		for (uint32_t i = g_strings_written; i < g_strings.size(); i++) {
			new_strings.push_back(g_strings[i]);
		}
		g_strings_written = g_strings.size();

		new_sourcelocs_first_id = g_sourcelocs_written + 1;
		for (uint32_t i = g_sourcelocs_written; i < g_sourcelocs.size(); i++) {
			new_sourcelocs.push_back(g_sourcelocs[i]);
		}
		g_sourcelocs_written = g_sourcelocs.size();

		counters = g_counters;
		g_counters.clear();
		frames = g_frames;
		g_frames.clear();
		gpu = g_gpu;
		g_gpu.clear();
		gpu_calibrations = g_gpu_calibrations;
		g_gpu_calibrations.clear();
	}

	_write_strings_chunk(new_strings, new_strings_first_id);

	if (!new_sourcelocs.is_empty()) {
		LocalVector<uint8_t> payload;
		payload.resize(sizeof(uint32_t) + new_sourcelocs.size() * sizeof(SourceLocRecord));
		memcpy(payload.ptr(), &new_sourcelocs_first_id, sizeof(uint32_t));
		memcpy(payload.ptr() + sizeof(uint32_t), new_sourcelocs.ptr(), new_sourcelocs.size() * sizeof(SourceLocRecord));
		_write_chunk(CHUNK_SOURCELOC, 0, payload.ptr(), payload.size());
	}

	EventChunk *chunk = chunks;
	while (chunk) {
		EventChunk *next = chunk->next;
		if (chunk->count > 0) {
			_write_chunk(CHUNK_EVENTS, chunk->thread_id, (const uint8_t *)chunk->events, (uint64_t)chunk->count * sizeof(EventRecord));
		}
		chunk = next;
	}

	if (!counters.is_empty()) {
		_write_chunk(CHUNK_COUNTERS, 0, (const uint8_t *)counters.ptr(), counters.size() * sizeof(CounterRecord));
	}
	if (!frames.is_empty()) {
		_write_chunk(CHUNK_FRAMES, 0, (const uint8_t *)frames.ptr(), frames.size() * sizeof(FrameRecord));
	}
	if (!gpu.is_empty()) {
		_write_chunk(CHUNK_GPU, 0, (const uint8_t *)gpu.ptr(), gpu.size() * sizeof(GpuRecord));
	}
	if (!gpu_calibrations.is_empty()) {
		_write_chunk(CHUNK_GPU_CALIBRATION, 0, (const uint8_t *)gpu_calibrations.ptr(), gpu_calibrations.size() * sizeof(GpuCalibrationRecord));
	}

	if (chunks) {
		MutexLock lock(g_mutex);
		chunk = chunks;
		while (chunk) {
			EventChunk *next = chunk->next;
			chunk->count = 0;
			chunk->next = g_free_head;
			g_free_head = chunk;
			chunk = next;
		}
	}
}

static void _write_thread_table() {
	LocalVector<ThreadRecord> records;
	{
		MutexLock lock(g_mutex);
		for (ThreadState *state = g_threads_head; state; state = state->next) {
			ThreadRecord record;
			record.thread_id = state->thread_id;
			record.name_id = state->name_id;
			record.reserved = 0;
			records.push_back(record);
		}
	}
	if (!records.is_empty()) {
		_write_chunk(CHUNK_THREADS, 0, (const uint8_t *)records.ptr(), records.size() * sizeof(ThreadRecord));
	}
}

static void _write_meta_chunk() {
	LocalVector<String> meta;
	uint64_t dropped = 0;
	{
		MutexLock lock(g_mutex);
		meta = g_meta;
		dropped = g_dropped_events;
	}
	meta.push_back("dropped_events=" + itos(dropped));

	String joined;
	for (const String &line : meta) {
		joined += line;
		joined += "\n";
	}
	const CharString utf8 = joined.utf8();
	_write_chunk(CHUNK_META, 0, (const uint8_t *)utf8.get_data(), (uint64_t)utf8.length());
}

static void _writer_thread_func(void *) {
	while (!g_writer_exit.is_set()) {
		g_writer_sem.wait();
		_drain_pending();
	}

	// Anything that landed between the last post and the exit flag.
	_drain_pending();

	_write_thread_table();
	_write_meta_chunk();
	_write_chunk(CHUNK_END, 0, nullptr, 0);

	if (g_file.is_valid()) {
		g_file->flush();
		g_file.unref();
	}
}

// --- Capture control -----------------------------------------------------

Error start_capture(const String &p_path, uint32_t p_flags) {
	{
		MutexLock lock(g_mutex);
		ERR_FAIL_COND_V_MSG(g_active, ERR_ALREADY_IN_USE, "An Insights capture is already running.");
	}

	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE);
	ERR_FAIL_COND_V_MSG(file.is_null(), ERR_CANT_CREATE, "Cannot open '" + p_path + "' for an Insights capture.");

	FileHeader header;
	header.magic = FILE_MAGIC;
	header.version = FILE_VERSION;
	header.qpc_frequency = qpc_frequency();
	// Sampled together so the offline tooling can put the raw counter and
	// Godot's own microsecond clock on the same timeline.
	header.t0_qpc = now_qpc();
	header.t0_ticks_usec = OS::get_singleton()->get_ticks_usec();
	header.t0_unix_usec = (uint64_t)(Time::get_singleton()->get_unix_time_from_system() * 1000000.0);
	header.capture_flags = p_flags | CAPTURE_ARMED;
	header.reserved = 0;

	{
		MutexLock lock(g_mutex);

		g_file = file;
		g_last_capture_path = p_path;
		g_dropped_events = 0;
		g_written_bytes = 0;

		// Ids are handed out for the life of the process, so a capture always
		// rewrites the whole table and stays self-contained.
		g_strings_written = 0;
		g_sourcelocs_written = 0;

		g_counters.clear();
		g_frames.clear();
		g_gpu.clear();
		g_gpu_calibrations.clear();
		g_meta.clear();

		g_active = true;
	}

	_write_raw((const uint8_t *)&header, sizeof(FileHeader));

	g_writer_exit.clear();
	g_writer_thread = memnew(Thread);
	g_writer_thread->start(_writer_thread_func, nullptr);

	g_capture_flags.set(header.capture_flags);
	return OK;
}

void stop_capture() {
	{
		MutexLock lock(g_mutex);
		if (!g_active) {
			return;
		}
	}

	// Disarm first so no new zones start, then give threads already inside
	// emit() a moment to finish before their partial chunks are taken away.
	g_capture_flags.set(0);
	OS::get_singleton()->delay_usec(2000);

	{
		MutexLock lock(g_mutex);
		g_active = false;
		for (ThreadState *state = g_threads_head; state; state = state->next) {
			if (state->current) {
				_push_full_locked(state->current);
				state->current = nullptr;
			}
		}
	}

	g_writer_exit.set();
	g_writer_sem.post();

	if (g_writer_thread) {
		g_writer_thread->wait_to_finish();
		memdelete(g_writer_thread);
		g_writer_thread = nullptr;
	}
}

bool is_capturing() {
	MutexLock lock(g_mutex);
	return g_active;
}

String get_last_capture_path() {
	MutexLock lock(g_mutex);
	return g_last_capture_path;
}

uint64_t get_dropped_event_count() {
	MutexLock lock(g_mutex);
	return g_dropped_events;
}

uint64_t get_written_byte_count() {
	MutexLock lock(g_mutex);
	return g_written_bytes;
}

void init() {
	// Runs from platform main() before OS, ProjectSettings or FileAccess exist.
	// Nothing here may allocate or read settings.
	g_capture_flags.set(0);
}

void cleanup() {
	stop_capture();

	MutexLock lock(g_mutex);

	// ThreadState objects are deliberately left alive: a thread_local pointer on
	// a still-running thread would dangle, and the process is exiting anyway.
	EventChunk *chunk = g_free_head;
	while (chunk) {
		EventChunk *next = chunk->next;
		memdelete(chunk);
		chunk = next;
	}
	g_free_head = nullptr;
	g_chunks_allocated = 0;
}

} // namespace tnt_insights

#endif // GODOT_USE_TURNT_INSIGHTS
