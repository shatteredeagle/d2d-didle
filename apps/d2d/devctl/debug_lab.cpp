// SPDX-License-Identifier: GPL-3.0-or-later
// devctl `debug` sub-verbs of the lab patch set (the private fork's only):
// level layout reads for the navigation tests. In name order.
#include "debug.hpp"

#include "town.hpp"

#include <format>
#include <optional>
#include <string>

namespace d2d::client {

void DebugVerbs::add_lab() {
    add("layout", [this](const Args& /*args*/) -> Reply {   // where the act's levels sit (act tiles): level, x, y, width, height
        if (!town.world.game_data || !town.level) return std::nullopt;
        std::string out;
        for (const auto& placed : town.world.game_data->layout_of(town.level->id))
            out += std::format("{}\t{}\t{}\t{}\t{}\n", placed.level, placed.x, placed.y, placed.width, placed.height);
        return out + "ok\n";
    });
    add("nearby", [this](const Args& /*args*/) -> Reply {   // the levels linked past this one's edge so far: level, dx, dy (their origin minus ours, cells)
        if (!town.level) return std::nullopt;
        std::string out;
        for (const auto& neighbour : town.level->nearby)
            out += std::format("{}\t{}\t{}\n", neighbour.level->id, neighbour.dx, neighbour.dy);
        return out + "ok\n";
    });
}

}  // namespace d2d::client
