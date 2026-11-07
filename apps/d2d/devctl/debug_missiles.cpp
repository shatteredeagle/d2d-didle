// SPDX-License-Identifier: GPL-3.0-or-later
// devctl `debug missiles` of the lab patch set (the d.idle fork only): the live missiles, read only.
#include "debug.hpp"

#include "ai.hpp"
#include "town.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>

namespace d2d::client {

void DebugVerbs::add_missiles() {
    // `missiles`: one row per missile in flight (Fight::missiles, the list `state missiles=` counts; the ones made this
    // frame join it after its update), tab separated: unit (its place in the list: not stable), Missiles.txt Id (-1 if
    // unknown), x, y (cells, as `monsters`), dx, dy (the unit vector of its flight, 0 0 when it does not move), speed
    // (cells per second), owner type (0 player, 1 monster or merc, -1 none: a shrine's, a hit effect's), owner unit id,
    // ttl in ticks of 40 ms until it ends (-1 unlimited); then two lab extras: friendly (1 the player's or merc's: hits
    // monsters, not the player) and the Missiles.txt name.
    add("missiles", [this](const Args& args) -> Reply {
        if (args.size() > 2) return std::string("err usage: debug missiles\n");
        const auto now = town.world.now;
        std::string out;
        for (std::size_t i = 0; i < town.fight.missiles.size(); ++i) {
            const auto& missile = town.fight.missiles[i];
            const int owner_type = missile.friendly && !missile.by_merc ? 0 : missile.owner >= 0 || missile.striker >= 0 || missile.friendly ? 1 : -1;
            const int owner_unit = missile.friendly ? (missile.by_merc ? missile.striker : missile.caster) : missile.owner;
            const long ttl = missile.dies >= 0x80000000u ? -1 : std::max(0L, (long(missile.dies) - long(now) + 39) / 40);
            const float speed = std::hypot(missile.velocity_x, missile.velocity_y);
            const float dx = speed > 0 ? missile.velocity_x / speed : 0, dy = speed > 0 ? missile.velocity_y / speed : 0;
            out += std::format("{}\t{}\t{:.2f}\t{:.2f}\t{:.3f}\t{:.3f}\t{:.3f}\t{}\t{}\t{}\t{}\t{}\n", i, missile.info ? missile.info->id : -1, missile.x, missile.y, dx, dy, speed,
                               owner_type, owner_unit, ttl, missile.friendly ? 1 : 0, missile.info ? missile.info->name : "?");
        }
        return out + "ok\n";
    });
}

}  // namespace d2d::client
