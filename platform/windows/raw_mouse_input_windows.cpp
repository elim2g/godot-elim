/**************************************************************************/
/*  raw_mouse_input_windows.cpp                                           */
/**************************************************************************/
/* TURNT ADDITION -- not upstream Godot.                                  */
/**************************************************************************/

#include "raw_mouse_input_windows.h"

#include "core/input/input.h"
#include "core/os/os.h"
#include "core/profiling/profiling.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

bool RawMouseInputWindows::start() {
	wake_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	if (wake_event == nullptr) {
		return false;
	}

	thread.start(_thread_func, this);
	started.wait();
	if (hwnd == nullptr) {
		stop();
		return false;
	}

	return true;
}

void RawMouseInputWindows::stop() {
	if (thread.is_started()) {
		exit_requested.set();
		SetEvent(static_cast<HANDLE>(wake_event));
		thread.wait_to_finish();
	}

	if (wake_event != nullptr) {
		CloseHandle(static_cast<HANDLE>(wake_event));
		wake_event = nullptr;
	}
	hwnd = nullptr;
}

void RawMouseInputWindows::set_capture(bool p_capture) {
	if (!is_running() || capture_requested.is_set() == p_capture) {
		return;
	}

	capture_requested.set_to(p_capture);
	SetEvent(static_cast<HANDLE>(wake_event));
}

bool RawMouseInputWindows::flush() {
	if (!spinning.is_set()) {
		return true;
	}

	GodotProfileZone("raw mouse: flush");
	const uint32_t request = flush_requested.increment();
	const uint64_t deadline_usec = OS::get_singleton()->get_ticks_usec() + FLUSH_TIMEOUT_USEC;
	while (flush_completed.get() != request) {
		if (OS::get_singleton()->get_ticks_usec() > deadline_usec) {
			return false;
		}
		YieldProcessor();
	}

	return true;
}

Vector2i RawMouseInputWindows::consume_into_input() {
	const uint32_t read = read_index.get();
	const uint32_t write = write_index.get();

	Input *input = Input::get_singleton();
	Vector2i motion;
	for (uint32_t i = read; i != write; i++) {
		const Sample &sample = ring[i & RING_MASK];
		const Vector2i relative(sample.dx, sample.dy);
		input->push_mouse_motion_sample(sample.usec, relative);
		motion += relative;
	}
	read_index.set(write);

	return motion;
}

void RawMouseInputWindows::discard() {
	read_index.set(write_index.get());
}

uint32_t RawMouseInputWindows::take_packet_count() {
	const uint32_t total = packet_count.get();
	const uint32_t taken = total - packets_reported;
	packets_reported = total;

	return taken;
}

void RawMouseInputWindows::_thread_func(void *p_self) {
	static_cast<RawMouseInputWindows *>(p_self)->_run();
}

void RawMouseInputWindows::_run() {
	Thread::set_name("Raw Mouse Input");
#ifdef GODOT_USE_TURNT_INSIGHTS
	tnt_insights::set_thread_name("Raw Mouse Input");
#endif

	// Raw input is delivered to a window's thread queue, so the thread needs a window of its own.
	hwnd = CreateWindowExW(0, L"Message", nullptr, 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
	started.post();
	if (hwnd == nullptr) {
		return;
	}

	bool registered = false;
	while (!exit_requested.is_set()) {
		const bool capture = capture_requested.is_set();
		if (capture != registered) {
			_register_mouse(capture);
			registered = capture;
			if (!capture) {
				// Publish what was queued before the release; the main thread discards it.
				_drain();
			}
		}

		if (capture) {
			_spin();
		} else {
			WaitForSingleObject(static_cast<HANDLE>(wake_event), INFINITE);
		}
	}

	if (registered) {
		_register_mouse(false);
	}
	DestroyWindow(static_cast<HWND>(hwnd));
}

void RawMouseInputWindows::_register_mouse(bool p_capture) {
	RAWINPUTDEVICE rid = {};
	rid.usUsagePage = 0x01; // HID_USAGE_PAGE_GENERIC
	rid.usUsage = 0x02; // HID_USAGE_GENERIC_MOUSE
	rid.dwFlags = p_capture ? RIDEV_INPUTSINK : RIDEV_REMOVE;
	rid.hwndTarget = p_capture ? static_cast<HWND>(hwnd) : nullptr;

	has_last_absolute = false;

	if (!RegisterRawInputDevices(&rid, 1, sizeof(rid)) && p_capture) {
		ERR_PRINT("Raw mouse input registration failed; captured mouse motion is unavailable.");
	}
}

void RawMouseInputWindows::_spin() {
	spinning.set();

	uint64_t next_drain_usec = 0;
	while (capture_requested.is_set() && !exit_requested.is_set()) {
		const uint32_t request = flush_requested.get();
		const uint64_t now_usec = OS::get_singleton()->get_ticks_usec();
		if (request == flush_completed.get() && now_usec < next_drain_usec) {
			YieldProcessor();
			continue;
		}

		_drain();
		flush_completed.set(request);
		next_drain_usec = now_usec + DRAIN_INTERVAL_USEC;
	}

	spinning.clear();
}

// NEXTRAWINPUTBLOCK, without the QWORD that windows.h leaves undeclared.
static RAWINPUT *_next_raw_input(RAWINPUT *p_raw) {
	constexpr uintptr_t ALIGN = sizeof(void *);
	return reinterpret_cast<RAWINPUT *>((reinterpret_cast<uintptr_t>(p_raw) + p_raw->header.dwSize + ALIGN - 1) & ~(ALIGN - 1));
}

void RawMouseInputWindows::_drain() {
	GodotProfileZone("raw mouse: drain");

	Vector2i motion;
	uint32_t packets = 0;
	for (;;) {
		UINT size = READ_BUFFER_BYTES;
		const UINT count = GetRawInputBuffer(reinterpret_cast<RAWINPUT *>(read_buffer), &size, sizeof(RAWINPUTHEADER));
		if (count == 0 || count == UINT(-1)) {
			break;
		}

		RAWINPUT *raw = reinterpret_cast<RAWINPUT *>(read_buffer);
		for (UINT i = 0; i < count; i++) {
			if (raw->header.dwType == RIM_TYPEMOUSE) {
				_accumulate(raw->data.mouse, motion);
			}
			raw = _next_raw_input(raw);
		}
		packets += count;
	}

	if (packets == 0) {
		return;
	}

	packet_count.add(packets);
	_publish(OS::get_singleton()->get_ticks_usec(), motion);
}

void RawMouseInputWindows::_accumulate(const tagRAWMOUSE &p_mouse, Vector2i &r_motion) {
	if (!(p_mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) {
		r_motion.x += p_mouse.lLastX;
		r_motion.y += p_mouse.lLastY;
		return;
	}

	// Remote desktop, VMs and tablets report a normalized absolute position instead.
	const bool virtual_desktop = p_mouse.usFlags & MOUSE_VIRTUAL_DESKTOP;
	const Vector2i position(
			MulDiv(p_mouse.lLastX, GetSystemMetrics(virtual_desktop ? SM_CXVIRTUALSCREEN : SM_CXSCREEN), 65535),
			MulDiv(p_mouse.lLastY, GetSystemMetrics(virtual_desktop ? SM_CYVIRTUALSCREEN : SM_CYSCREEN), 65535));
	if (has_last_absolute) {
		r_motion += position - last_absolute;
	}
	last_absolute = position;
	has_last_absolute = true;
}

void RawMouseInputWindows::_publish(uint64_t p_usec, const Vector2i &p_motion) {
	if (p_motion == Vector2i()) {
		return;
	}

	// A full ring means the main thread has stalled for seconds; carry the motion rather than drop it.
	const uint32_t write = write_index.get();
	if (write - read_index.get() == RING_CAPACITY) {
		overflow += p_motion;
		return;
	}

	const Vector2i motion = p_motion + overflow;
	ring[write & RING_MASK] = { p_usec, motion.x, motion.y };
	overflow = Vector2i();
	write_index.set(write + 1);
}
