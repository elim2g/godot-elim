/**************************************************************************/
/*  raw_mouse_input_windows.h                                             */
/**************************************************************************/
/* TURNT ADDITION -- not upstream Godot.                                  */
/*                                                                        */
/* Reads raw mouse input on a dedicated thread, so a high polling rate    */
/* never reaches the main thread's message queue. While the mouse is      */
/* captured the thread spins: it drains every DRAIN_INTERVAL_USEC, and at */
/* once when the main thread flushes at frame start, so no frame waits on */
/* an OS wake-up. Otherwise it sleeps. Drained packets are published as   */
/* timestamped samples through a single-producer ring.                    */
/**************************************************************************/

#pragma once

#include "core/math/vector2i.h"
#include "core/os/semaphore.h"
#include "core/os/thread.h"
#include "core/templates/safe_refcount.h"

struct tagRAWMOUSE;

class RawMouseInputWindows {
public:
	struct Sample {
		uint64_t usec;
		int32_t dx;
		int32_t dy;
	};

	static constexpr uint64_t DRAIN_INTERVAL_USEC = 250;
	static constexpr uint64_t FLUSH_TIMEOUT_USEC = 500;
	static constexpr uint32_t RING_CAPACITY = 4096;
	static constexpr uint32_t RING_MASK = RING_CAPACITY - 1;

private:
	static constexpr uint32_t READ_BUFFER_BYTES = 16384;

	alignas(64) SafeNumeric<uint32_t> write_index;
	alignas(64) SafeNumeric<uint32_t> read_index;
	alignas(64) SafeNumeric<uint32_t> flush_requested;
	alignas(64) SafeNumeric<uint32_t> flush_completed;
	SafeNumeric<uint32_t> packet_count;
	SafeFlag spinning;
	Sample ring[RING_CAPACITY];

	Thread thread;
	Semaphore started;
	SafeFlag exit_requested;
	SafeFlag capture_requested;
	void *hwnd = nullptr;
	void *wake_event = nullptr;

	// Owned by the input thread.
	alignas(8) uint8_t read_buffer[READ_BUFFER_BYTES];
	Vector2i overflow;
	Vector2i last_absolute;
	bool has_last_absolute = false;

	// Owned by the main thread.
	uint32_t packets_reported = 0;

	static void _thread_func(void *p_self);
	void _run();
	void _register_mouse(bool p_capture);
	void _spin();
	void _drain();
	void _accumulate(const tagRAWMOUSE &p_mouse, Vector2i &r_motion);
	void _publish(uint64_t p_usec, const Vector2i &p_motion);

public:
	bool start();
	void stop();
	bool is_running() const { return hwnd != nullptr; }

	void set_capture(bool p_capture);

	// Main thread only.
	// Has the spinning thread drain everything that has arrived; false if it did not answer in time.
	bool flush();
	bool has_pending() const { return read_index.get() != write_index.get(); }
	// Hands each pending sample to Input with its timestamp and returns their sum.
	Vector2i consume_into_input();
	void discard();
	uint32_t take_packet_count();
};
