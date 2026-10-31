// Local patch P2 (private fork): virtual clock, lockstep stepping, time scale.
// Everything the main loop needs is here; main.cpp calls it at four seams.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <devctl.hpp>

namespace d2d::lockstep {

struct Clock {
    bool          lockstep = false;      // time moves only by `step`
    double        scale = 1.0;           // virtual ms per real ms outside lockstep
    bool          render_steps = false;  // draw every stepped frame (default: only the last)
    std::uint64_t frames = 0;            // frames executed since start
    std::uint64_t stepped = 0;           // frames executed by `step`
    std::uint64_t pending = 0;           // steps still to run
    std::uint32_t virt_ms = 0;           // the game clock handed to the world
    std::uint32_t last_real = 0;
    double        carry = 0;             // fractional virtual ms from scale
    bool          reply_due = false;     // a `step` reply is waiting for the last frame
    bool          stepping_frame = false;
    bool          draw_frame = true;     // this frame is drawn and presented
    std::uint32_t last_draw_real = 0;
    std::function<std::string()> shot_fn;   // lockstep screenshot waiting for a drawn frame (reply is deferred)
};

Clock& clock();

// Once per frame, where the loop reads the game time. `real_ms` is
// milliseconds since the loop started. Returns the virtual time.
std::uint32_t advance(std::uint32_t real_ms);

// This frame runs the world but skips drawing and presenting.
inline bool skip_draw() { return !clock().draw_frame; }

// False while stepping or time-scaled: the pacer must not sleep.
bool pace();

// Last thing in a frame: sends the deferred `step` reply when it is done.
void after_frame(d2d::devctl::Channel& channel);

// Verbs: clock, step, lockstep, timescale.
void register_verbs(d2d::devctl::Channel& channel);

}  // namespace d2d::lockstep
