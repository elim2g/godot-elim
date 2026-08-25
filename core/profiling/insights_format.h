/**************************************************************************/
/*  insights_format.h                                                     */
/**************************************************************************/
/* TURNT ADDITION -- not upstream Godot.                                  */
/*                                                                        */
/* On-disk layout of a .tntinsight capture. Deliberately dependency-free  */
/* (no Godot headers) so the layout can be read by eye and kept in step   */
/* with automation/insights/tntinsight_format.py, which mirrors it.       */
/*                                                                        */
/* A file is a FileHeader followed by a stream of chunks. Chunks are      */
/* written as the capture runs, so a file truncated by a crash still      */
/* parses up to the last complete chunk.                                  */
/*                                                                        */
/* Timestamps are raw performance-counter ticks. Convert with             */
/* qpc_frequency from the header; correlate with Godot's own clock via    */
/* the (t0_qpc, t0_ticks_usec) pair sampled together at capture start.    */
/**************************************************************************/

#pragma once

#include <stdint.h>

namespace tnt_insights {

// 'TINS' little-endian.
inline constexpr uint32_t FILE_MAGIC = 0x534E4954u;
inline constexpr uint32_t FILE_VERSION = 1u;

enum ChunkType : uint32_t {
	CHUNK_META = 1, // utf8 key=value pairs, one per line
	CHUNK_STRINGS = 2, // u32 first_id, then byte_length-prefixed utf8 strings
	CHUNK_SOURCELOC = 3, // u32 first_id, then SourceLocRecord[]
	CHUNK_THREADS = 4, // ThreadRecord[]
	CHUNK_EVENTS = 5, // EventRecord[], param = thread id
	CHUNK_COUNTERS = 6, // CounterRecord[]
	CHUNK_GPU = 7, // GpuRecord[]
	CHUNK_FRAMES = 8, // FrameRecord[]
	CHUNK_GPU_CALIBRATION = 9, // GpuCalibrationRecord[]
	CHUNK_END = 0xFFFFFFFFu,
};

enum EventType : uint8_t {
	EVENT_BEGIN = 0,
	EVENT_END = 1,
	EVENT_INSTANT = 2,
};

enum EventFlags : uint8_t {
	EVENT_FLAG_NONE = 0,
	// `id` indexes the source-location table rather than the string table.
	EVENT_FLAG_SOURCE_LOC = 1 << 0,
	EVENT_FLAG_SCRIPT = 1 << 1,
	EVENT_FLAG_NATIVE = 1 << 2, // emitted by libturnt through the extension bridge
};

// What a capture is recording. Also the armed word: zero means not capturing.
enum CaptureFlags : uint32_t {
	CAPTURE_ARMED = 1 << 0, // always set while a capture is running
	CAPTURE_SCRIPT_ZONES = 1 << 1, // per GDScript function call
	CAPTURE_SCRIPT_NATIVE_ZONES = 1 << 2, // per engine call made from GDScript
	CAPTURE_GPU = 1 << 3,
	CAPTURE_GPU_COARSE = 1 << 4, // only top-level RENDER_TIMESTAMP markers
	CAPTURE_COUNTERS = 1 << 5,
	CAPTURE_NATIVE_ZONES = 1 << 6, // libturnt zones
};

#pragma pack(push, 1)

struct FileHeader {
	uint32_t magic;
	uint32_t version;
	uint64_t qpc_frequency; // performance-counter ticks per second
	uint64_t t0_qpc; // raw counter, sampled at capture start
	uint64_t t0_ticks_usec; // OS::get_ticks_usec() at the same instant
	uint64_t t0_unix_usec; // wall clock at the same instant
	uint32_t capture_flags;
	uint32_t reserved;
};
static_assert(sizeof(FileHeader) == 48, "FileHeader layout changed; bump FILE_VERSION and tntinsight_format.py");

struct ChunkHeader {
	uint32_t type;
	uint32_t byte_length; // payload bytes following this header
	uint64_t param; // CHUNK_EVENTS: thread id. Otherwise 0.
};
static_assert(sizeof(ChunkHeader) == 16, "ChunkHeader layout changed; bump FILE_VERSION");

struct EventRecord {
	uint64_t qpc;
	uint32_t id; // string id, or source-location id when EVENT_FLAG_SOURCE_LOC
	uint8_t type; // EventType
	uint8_t flags; // EventFlags
	uint16_t reserved;
};
static_assert(sizeof(EventRecord) == 16, "EventRecord layout changed; bump FILE_VERSION");

struct CounterRecord {
	uint64_t qpc;
	uint32_t name_id;
	uint32_t reserved;
	double value;
};
static_assert(sizeof(CounterRecord) == 24, "CounterRecord layout changed; bump FILE_VERSION");

struct FrameRecord {
	uint64_t qpc;
	uint64_t frame_index; // Engine::get_frames_drawn() at the boundary
};
static_assert(sizeof(FrameRecord) == 16, "FrameRecord layout changed; bump FILE_VERSION");

struct SourceLocRecord {
	uint32_t name_id;
	uint32_t file_id;
	uint32_t function_id;
	uint32_t line;
};
static_assert(sizeof(SourceLocRecord) == 16, "SourceLocRecord layout changed; bump FILE_VERSION");

struct ThreadRecord {
	uint64_t thread_id;
	uint32_t name_id;
	uint32_t reserved;
};
static_assert(sizeof(ThreadRecord) == 16, "ThreadRecord layout changed; bump FILE_VERSION");

// GPU timestamps arrive on the GPU's own clock, one or two frames late. Spans are
// reconstructed offline from the `>` / `<` name-prefix convention the renderer uses.
struct GpuRecord {
	uint64_t gpu_ns; // GPU clock, arbitrary epoch -- see CHUNK_GPU_CALIBRATION
	uint64_t frame_index; // the frame the marker was RECORDED in
	uint32_t name_id;
	uint8_t type; // EventType; EVENT_INSTANT until spans are paired offline
	uint8_t flags;
	uint16_t reserved;
};
static_assert(sizeof(GpuRecord) == 24, "GpuRecord layout changed; bump FILE_VERSION");

// A (cpu, gpu) pair sampled from the same instant, so the GPU clock can be mapped
// onto the CPU timeline. Emitted once per captured frame.
struct GpuCalibrationRecord {
	uint64_t qpc; // CPU performance counter
	uint64_t gpu_ns; // GPU clock at the same instant
	uint64_t frame_index;
	uint64_t reserved;
};
static_assert(sizeof(GpuCalibrationRecord) == 32, "GpuCalibrationRecord layout changed; bump FILE_VERSION");

#pragma pack(pop)

} // namespace tnt_insights
