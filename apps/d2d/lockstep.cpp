// SPDX-License-Identifier: GPL-3.0-or-later
#include "lockstep.hpp"
#include <gamedata.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

namespace d2d::lockstep {

namespace {
constexpr std::uint32_t kTickMs = 40;       // the world's fixed step (25 Hz)
constexpr std::uint32_t kDrawEveryMs = 100; // real-time draw cadence while time-scaled (10 fps)
}

Clock& clock() {
    static Clock instance;
    return instance;
}

std::uint32_t advance(std::uint32_t real_ms) {
    Clock& c = clock();
    ++c.frames;
    c.stepping_frame = false;
    c.draw_frame = true;
    const std::uint32_t real_dt = real_ms - c.last_real;
    c.last_real = real_ms;
    if (c.lockstep) {
        if (c.pending > 0) {
            c.virt_ms += kTickMs;
            --c.pending;
            ++c.stepped;
            c.stepping_frame = true;
            // Lockstep never draws on its own: the frame only advances the simulation. `step n render` draws every
            // step frame; a screenshot request (shot_fn) draws the next frame and answers from it.
            c.draw_frame = c.render_steps || bool(c.shot_fn);
        } else {
            c.draw_frame = bool(c.shot_fn);
        }
    } else {
        c.carry += double(real_dt) * c.scale;
        const auto whole = std::uint32_t(c.carry);
        c.carry -= whole;
        c.virt_ms += whole;
        if (c.scale > 1.0) {
            c.draw_frame = real_ms - c.last_draw_real >= kDrawEveryMs;
            if (c.draw_frame) c.last_draw_real = real_ms;
        }
    }
    return c.virt_ms;
}

bool pace() {
    const Clock& c = clock();
    if (c.lockstep) return !c.stepping_frame;
    return c.scale <= 1.0;
}

void after_frame(d2d::devctl::Channel& channel) {
    Clock& c = clock();
    if (!c.reply_due || c.pending > 0) return;
    c.reply_due = false;
    channel.reply("frame=" + std::to_string(c.frames) + " game_ms=" + std::to_string(c.virt_ms) +
                  " tick=" + std::to_string(c.virt_ms / kTickMs) + "\nok\n");
}

static std::string state_line(const Clock& c) {
    char scale[32];
    std::snprintf(scale, sizeof scale, "%g", c.scale);
    return std::string("lockstep=") + (c.lockstep ? "1" : "0") + " scale=" + scale + " frame=" + std::to_string(c.frames) +
           " stepped=" + std::to_string(c.stepped) + " game_ms=" + std::to_string(c.virt_ms) + " tick=" + std::to_string(c.virt_ms / kTickMs) +
           " pending=" + std::to_string(c.pending) + " render_steps=" + (c.render_steps ? "1" : "0") + "\nok\n";
}

void register_verbs(d2d::devctl::Channel& channel) {
    channel.on("clock", [](const std::vector<std::string>&) { return state_line(clock()); });
    channel.on("lockstep", [](const std::vector<std::string>& args) -> std::string {
        Clock& c = clock();
        if (args.size() < 2 || (args[1] != "on" && args[1] != "off")) throw std::runtime_error("usage: lockstep on|off");
        c.lockstep = args[1] == "on";
        d2d::game::wait_for_level_builds = c.lockstep;
        c.pending = 0;
        return state_line(c);
    });
    channel.on("timescale", [](const std::vector<std::string>& args) -> std::string {
        Clock& c = clock();
        const double scale = args.size() > 1 ? std::atof(args[1].c_str()) : 0;
        if (!(scale > 0 && scale <= 1000)) throw std::runtime_error("usage: timescale <x>, 0 < x <= 1000");
        c.scale = scale;
        return state_line(c);
    });
    channel.on("step", [&channel](const std::vector<std::string>& args) -> std::string {
        Clock& c = clock();
        if (!c.lockstep) throw std::runtime_error("not in lockstep mode (lockstep on)");
        const long n = args.size() > 1 ? std::atol(args[1].c_str()) : 1;
        if (n < 1 || n > 100000000) throw std::runtime_error("usage: step <n> [render]");
        c.render_steps = args.size() > 2 && args[2] == "render";
        c.pending = std::uint64_t(n);
        c.reply_due = true;
        channel.defer();
        return {};
    });
}

}  // namespace d2d::lockstep
