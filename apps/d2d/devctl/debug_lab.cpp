// SPDX-License-Identifier: GPL-3.0-or-later
// devctl `debug` sub-verbs of the lab patch set (the private fork's only):
// level layout reads for the navigation tests, and the test cheats of the drills
// (act, belt, durability, hit, merc, unequip, give <code> pack|belt). In name order.
#include "debug.hpp"

#include "inventory.hpp"
#include "panels.hpp"
#include "town.hpp"

#include <d2s_items.hpp>
#include <rules.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <optional>
#include <string>
#include <vector>

namespace d2d::client {

void DebugVerbs::add_lab() {
    add("act", [this](const Args& args) -> Reply {   // the acts the waypoint panel shows tabs for: 1..n (the quest flags it reads: panels.cpp waypoint_act_open), nothing else
        if (args.size() < 3) return std::nullopt;
        const int acts = std::atoi(args[2].c_str());
        if (acts < 1 || acts > 5) return std::string("err act 1..5\n");
        static constexpr int kQuest[5] = { -1, 7, 15, 23, 26 };   // as waypoint_act_open's
        auto& header = town.hero.character.header;
        auto& quest_bits = header.quests[std::size_t(header.active_difficulty())];
        for (int act = 1; act < 5; ++act) {
            const int bit = kQuest[act] * 16;
            const auto mask = std::uint8_t(1 << (bit & 7));
            auto& byte = quest_bits[std::size_t(bit >> 3)];
            byte = std::uint8_t(act < acts ? byte | mask : byte & ~mask);
        }
        std::string tabs;
        for (int act = 0; act < 5; ++act) if (waypoint_act_open(header, act)) tabs += std::to_string(act + 1);
        return std::format("ok tabs={}\n", tabs);
    });
    add("belt", [this](const Args& args) -> Reply {   // `belt clear`: empty the belt boxes (the belt itself stays worn)
        if (args.size() < 3 || args[2] != "clear") return std::nullopt;
        auto& items = town.hero.character.items;
        const auto removed = std::erase_if(items, [](const auto& item) { return item.location == d2d::d2s::item_location::kBelt; });
        return std::format("ok removed={}\n", removed);
    });
    add("durability", [this](const Args& args) -> Reply {   // `durability <body slot|all> <value>`: worn items' durability, capped at the item's maximum
        if (args.size() < 4) return std::nullopt;
        const bool every = args[2] == "all";
        const int slot = every ? -1 : std::atoi(args[2].c_str()), value = std::atoi(args[3].c_str());
        if (value < 0) return std::string("err value >= 0\n");
        int changed = 0;
        for (auto& item : town.hero.character.items) {
            if (item.location != d2d::d2s::item_location::kEquipped || (!every && item.slot != slot)) continue;
            const int top = d2d::rules::max_durability(item);
            if (top <= 0) continue;   // none wears (an indestructible or a durability-less item)
            item.durability = std::min(value, top);
            ++changed;
        }
        return changed ? std::format("ok {}\n", changed) : std::string("err no worn item with durability\n");
    });
    add("hit", [this](const Args& args) -> Reply {   // `hit <damage>`: one monster blow of that many points on the player (life, armour wear, death, get-hit mode); it always flinches unless it kills
        if (args.size() < 3) return std::nullopt;
        const int damage = std::atoi(args[2].c_str());
        if (damage <= 0) return std::string("err damage > 0\n");
        auto& world = town.world;
        world.act_as(world.lead());
        world.fight.debug_blow(damage, false, world.now);
        return std::format("ok life={} mode={}\n", town.hero.character.stats.get(d2d::d2s::kLife) >> 8, world.fight.actor->pmode);
    });
    add("layout", [this](const Args& /*args*/) -> Reply {   // where the act's levels sit (act tiles): level, x, y, width, height
        if (!town.world.game_data || !town.level) return std::nullopt;
        std::string out;
        for (const auto& placed : town.world.game_data->layout_of(town.level->id))
            out += std::format("{}\t{}\t{}\t{}\t{}\n", placed.level, placed.x, placed.y, placed.width, placed.height);
        return out + "ok\n";
    });
    add("merc", [this](const Args& args) -> Reply {   // `merc hire <type>` (a hireling.txt Id; no gold), `merc kill` (a blow that kills it, its death plays), `merc revive` (no gold)
        if (args.size() < 3) return std::nullopt;
        auto& world = town.world;
        auto& header = town.hero.character.header;
        if (args[2] == "hire") {
            if (args.size() < 4) return std::nullopt;
            const int type = std::atoi(args[3].c_str());
            if (!world.game_data->mercs.contains(type)) {
                std::string ids;
                for (const auto& [id, merc] : world.game_data->mercs) ids += std::format(" {}", id);
                return "err no such type; known:" + ids + "\n";
            }
            header.merc_type = std::uint16_t(type);
            header.merc_name = 0;
            header.merc_exp = 0;
            header.merc_seed = std::max<std::uint32_t>(std::uint32_t(world.rng(0x7fffffff)), 1);
            header.merc_dead = false;
            world.act_as(world.lead());
            world.spawn_merc();
            return std::format("ok present={}\n", world.fight.actor->merc ? 1 : 0);
        }
        if (args[2] == "kill") {
            world.act_as(world.lead());
            auto& merc = world.fight.actor->merc;
            if (!merc || merc->mode == "DT") return std::string("err no live merc\n");
            world.fight.debug_blow(merc->hit_points + 1, true, world.now);
            return std::string("ok dying\n");
        }
        if (args[2] == "revive") {
            if (!header.merc_seed || !header.merc_dead) return std::string("err no dead merc\n");
            header.merc_dead = false;
            world.act_as(world.lead());
            world.spawn_merc();
            return std::format("ok present={}\n", world.fight.actor->merc ? 1 : 0);
        }
        return std::nullopt;
    });
    add("nearby", [this](const Args& /*args*/) -> Reply {   // the levels linked past this one's edge so far: level, dx, dy (their origin minus ours, cells)
        if (!town.level) return std::nullopt;
        std::string out;
        for (const auto& neighbour : town.level->nearby)
            out += std::format("{}\t{}\t{}\n", neighbour.level->id, neighbour.dx, neighbour.dy);
        return out + "ok\n";
    });
    add("unequip", [this](const Args& args) -> Reply {   // `unequip <body slot|belt>`: take what is worn there off the character (it is gone; belt boxes stay: `belt clear`)
        if (args.size() < 3) return std::nullopt;
        const int slot = args[2] == "belt" ? d2d::d2s::body_location::kBelt : std::atoi(args[2].c_str());
        const auto removed = std::erase_if(town.hero.character.items, [&](const auto& item) { return item.location == d2d::d2s::item_location::kEquipped && item.slot == slot; });
        return removed ? std::string("ok\n") : std::string("err nothing worn there\n");
    });
}

// `give <code> pack|belt [box]`: a new normal item straight into the carried grid, as a pickup would place it
// (the pack's first free spot by the player's search; the belt's rules::belt_slot box, or the box asked for).
DebugVerbs::Reply DebugVerbs::give_carried(const Args& args) {
    if (!scene || !town.world.game_data) return std::nullopt;
    auto& items = town.hero.character.items;
    auto seed = town.world.rng;
    auto item = d2d::rules::generate_item(scene->rules, args[2], 1, 2, town.world.rng, &seed);
    const auto& game_data = *town.world.game_data;
    if (args[3] == "belt") {
        const int boxes = game_data.belts[std::size_t(d2d::game::belt_index(game_data, items))].boxes;
        const int slot = args.size() >= 5 ? std::atoi(args[4].c_str()) : d2d::rules::belt_slot(scene->rules, items, item, boxes);
        const bool taken = std::ranges::any_of(items, [&](const auto& carried) { return carried.location == d2d::d2s::item_location::kBelt && carried.column == slot; });
        if (slot < 0 || slot >= boxes || taken) return std::string("err no belt box\n");
        item.location = d2d::d2s::item_location::kBelt; item.column = slot; item.row = 0; item.panel = 0; item.slot = 0;
        items.push_back(std::move(item));
        return std::format("ok belt box {}\n", slot);
    }
    const auto& layout = game_data.inv_layout[std::size_t(std::max(town.hero.character.character_class, 0))];
    const int cols = layout.cols ? layout.cols : 10, rows = layout.rows ? layout.rows : 4;
    std::vector<const d2d::d2s::Item*> pack;
    for (const auto& carried : items) if (carried.location == d2d::d2s::item_location::kStored && carried.panel == d2d::d2s::item_panel::kInventory) pack.push_back(&carried);
    const auto [width, height] = d2d::rules::item_size(scene->rules, item.code);
    const auto [column, row] = d2d::rules::free_spot(scene->rules, pack, cols, rows, width, height, true);
    std::optional<d2d::d2s::Item> held = std::move(item);
    if (column < 0 || !d2d::rules::put_in_grid(scene->rules, items, held, d2d::d2s::item_panel::kInventory, cols, rows, column, row)) return std::string("err no room\n");
    return std::format("ok pack {} {}\n", column, row);
}

}  // namespace d2d::client
