// SPDX-License-Identifier: GPL-3.0-or-later
// devctl `debug missiles` of the lab patch set (the private fork only): the live missiles, read only.
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
    // frame join it after its update), tab separated: unit (its place in the list: not stable), Missiles.txt id, x, y
    // (cells, as `nearby` / `dump`), dx, dy (cells per SECOND, as stored in Missile::velocity_*; per tick = x 0.04),
    // speed (hypot of those), owner type (0 player, 1 monster or merc, -1 none: a shrine's), owner unit id, ttl in
    // ticks of 40 ms until it ends (-1 unlimited), friendly (1 the player's or merc's: hits monsters, not the player).
    add("missiles", [this](const Args& args) -> Reply {
        if (args.size() > 2) return std::string("err usage: debug missiles\n");
        const auto now = town.world.now;
        std::string out;
        for (std::size_t i = 0; i < town.fight.missiles.size(); ++i) {
            const auto& missile = town.fight.missiles[i];
            const int owner_type = missile.friendly && !missile.by_merc ? 0 : missile.owner >= 0 || missile.striker >= 0 || missile.friendly ? 1 : -1;
            const int owner_unit = missile.friendly ? (missile.by_merc ? missile.striker : missile.caster) : missile.owner;
            const long ttl = missile.dies >= 0x80000000u ? -1 : std::max(0L, (long(missile.dies) - long(now) + 39) / 40);
            out += std::format("{}\t{}\t{:.2f}\t{:.2f}\t{:.3f}\t{:.3f}\t{:.3f}\t{}\t{}\t{}\t{}\n", i, missile.info ? missile.info->name : "?", missile.x, missile.y,
                               missile.velocity_x, missile.velocity_y, std::hypot(missile.velocity_x, missile.velocity_y), owner_type, owner_unit, ttl, missile.friendly ? 1 : 0);
        }
        return out + "ok\n";
    });
}

}  // namespace d2d::client
