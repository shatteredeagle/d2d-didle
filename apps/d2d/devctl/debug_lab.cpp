// SPDX-License-Identifier: GPL-3.0-or-later
// devctl `debug` sub-verbs of the lab patch set (the d.idle fork's only):
// level layout reads for the navigation tests, and the test cheats of the drills
// (act, belt, durability, help, hit, merc hire|kill|revive|hurt, pin, quest list|set|bit|join, spawn,
// unequip, give <code> pack|belt [q<n>] [unid]). In name order.
#include "debug.hpp"

#include "ai.hpp"
#include "inventory.hpp"
#include "panels.hpp"
#include "town.hpp"

#include <d2s_items.hpp>
#include <rules.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <format>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace d2d::client {

namespace {

// `pin on`: the collision cells the ring changed, to put back.
struct Pin {
    const Level* level = nullptr;
    std::vector<std::pair<std::size_t, std::uint16_t>> cells;   // Level::walk index, its value before
};

}  // namespace

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
    add("help", [this](const Args& /*args*/) -> Reply {   // every `debug` sub-verb with its usage (the lab's own verbs in full, the rest by their usage line)
        static constexpr std::pair<std::string_view, std::string_view> kUsage[] = {
            { "act", "act <1-5>: waypoint tabs for acts 1..n (quest flags the panel reads)" },
            { "automap", "automap: reveal the whole level" },
            { "belt", "belt clear: empty the belt boxes" },
            { "blocked", "blocked <x> <y>: 1 if a unit can't stand there" },
            { "cast", "cast <skill> <x> <y>|unit <monster id>|near: the player casts the skill through the normal path (needs a level; offline --no-save only)" },
            { "durability", "durability <slot|all> <value>: worn items' durability, capped at the maximum" },
            { "equip", "equip <code> <slot>: a normal item worn in a body slot" },
            { "give", "give <code> [quality] [unid]: on the ground (0 by level, 4 magic, 6 rare); give <code> pack|belt [box] [q<quality>] [l<ilvl>] [unid]: into the pack or a belt box" },
            { "help", "help: this list" },
            { "hit", "hit <damage>: one monster blow on the player (life, wear, death, get-hit)" },
            { "kill", "kill [<keep>] [spare <MonStats id>] [within <cells>]: kill the level's monsters" },
            { "mana", "mana full: mana := max mana (offline --no-save only)" },
            { "merc", "merc hire <type>|kill|revive|hurt <damage>: the merc (no gold)" },
            { "mon", "mon set <monster id> [life <n>] [frac <0..255>] [maxlife <n>] [dmg <lo> <hi>] [res <phys|magic|fire|light|cold|poison> <-100..100>]: one live monster (offline --no-save only)" },
            { "missiles", "missiles: one row per live missile: unit, id, x, y, dx, dy (unit vector), speed (cells/s), owner type, owner unit, ttl ticks, friendly, name" },
            { "pin", "pin on [radius 3..8 subtiles]|off|status: wall the player into a pocket (collision ring), or take it down" },
            { "quest", "quest <q> [show|reset]: done (bit 0) | quest list: nonzero records | quest set <q> <flags 0..0xffff> | quest bit <q> <0..15> on|off | quest join: re-run the act's join" },
            { "spawn", "spawn <MonStats id|row> [n 1..20] [near] [champion|unique [mod ids]]: monsters round the player (field level)" },
            { "states", "states: the player buffs and cursed monsters | states set <skill> [level] | states clear <skill>|all: the player self states (offline --no-save only)" },
            { "stat", "stat <0..15> <value>: a character stat (14 purse, 15 stash)" },
            { "unequip", "unequip <slot|belt>: take what is worn there off" },
            { "unid", "unid: unidentify every pack item (magic or better ones too)" },
            { "warp", "warp <x> <y>: put the player at a cell" },
            { "wp", "wp <index|all|level <id>>: activate waypoints" },
        };
        std::string out;
        for (const auto& [name, verb] : verbs_) {
            const auto found = std::ranges::find(kUsage, name, &std::pair<std::string_view, std::string_view>::first);
            out += found != std::end(kUsage) ? std::format("{}\n", found->second) : std::format("{}: see docs/control_channel.md\n", name);
        }
        return out + "ok\n";
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
        if (args[2] == "hurt") {   // `merc hurt <damage>`: one blow of that many points on the merc through its own hurt path (life, death at 0)
            if (args.size() < 4) return std::nullopt;
            const int damage = std::atoi(args[3].c_str());
            if (damage <= 0) return std::string("err damage > 0\n");
            world.act_as(world.lead());
            auto& merc = world.fight.actor->merc;
            if (!merc || merc->mode == "DT" || !merc->alive()) return std::string("err no live merc\n");
            world.fight.debug_blow(damage, true, world.now);
            return std::format("ok merc life={} mode={}\n", merc ? merc->hit_points : -1, merc ? std::string(merc->mode) : std::string("gone"));
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
    add("pin", [this, pinned = std::make_shared<Pin>()](const Args& args) -> Reply {   // `pin on [radius]|off|status`: a ring of blocked collision subtiles round the player (a pocket no walk leaves), then the ring taken down
        if (args.size() < 3 || !town.level) return std::nullopt;
        if (args[2] == "status") return std::format("ok pinned={} cells={}\n", pinned->level ? 1 : 0, pinned->cells.size());
        if (args[2] == "off") {
            if (!pinned->level) return std::string("err not pinned\n");
            for (const auto& [index, old] : pinned->cells) pinned->level->walk[index] = old;
            const auto count = pinned->cells.size();
            *pinned = {};
            return std::format("ok freed cells={}\n", count);
        }
        if (args[2] != "on") return std::nullopt;
        if (pinned->level) return std::string("err already pinned (pin off first)\n");
        const int radius = args.size() >= 4 ? std::atoi(args[3].c_str()) : 4;
        if (radius < 3 || radius > 8) return std::string("err radius 3..8 subtiles\n");
        const auto& level = *town.level;
        const int width = level.ds1.width() * 5, height = level.ds1.height() * 5;
        const int centre_x = int(std::floor(town.hero.player.x * 5)), centre_y = int(std::floor(town.hero.player.y * 5));
        if (level.walk.empty() || centre_x - radius < 0 || centre_y - radius < 0 || centre_x + radius >= width || centre_y + radius >= height) return std::string("err too near the level edge\n");
        for (int y = centre_y - radius; y <= centre_y + radius; ++y)
            for (int x = centre_x - radius; x <= centre_x + radius; ++x) {
                if (std::max(std::abs(x - centre_x), std::abs(y - centre_y)) != radius) continue;
                const auto index = std::size_t(y) * std::size_t(width) + std::size_t(x);
                pinned->cells.emplace_back(index, level.walk[index]);
                level.walk[index] |= 0x01;   // 0x01 blocks walking (gamedata.hpp Level::walk)
            }
        pinned->level = &level;
        return std::format("ok pinned cells={} centre={},{} radius={}\n", pinned->cells.size(), centre_x, centre_y, radius);
    });
    add("spawn", [this](const Args& args) -> Reply {   // `spawn <MonStats id|row> [n] [near] [champion|unique [mod ids]]`: monsters made as the level's population makes them, round the player
        if (args.size() < 3 || !scene) return std::nullopt;
        auto& world = town.world;
        const auto& monster_types = scene->monsters.types;
        int row = -1;
        const bool numeric = !args[2].empty() && std::ranges::all_of(args[2], [](unsigned char c) { return std::isdigit(c); });
        if (numeric) row = std::atoi(args[2].c_str());
        else {
            const auto lower = [](std::string text) { for (auto& c : text) c = char(std::tolower(static_cast<unsigned char>(c))); return text; };
            const auto wanted = lower(args[2]);
            for (std::size_t i = 0; i < monster_types.size() && i < scene->mon_npc.size(); ++i)
                if (lower(std::string(monster_types[i].id)) == wanted || lower(std::string(scene->mon_npc[i].name)) == wanted) { row = int(i); break; }
        }
        if (row < 0 || std::size_t(row) >= monster_types.size() || std::size_t(row) >= scene->mon_npc.size() || monster_types[std::size_t(row)].id.empty()) return std::string("err no such monster (a MonStats Id like zombie1, or its row)\n");
        int count = 1;
        std::size_t next = 3;
        if (args.size() > next && !args[next].empty() && std::ranges::all_of(args[next], [](unsigned char c) { return std::isdigit(c); })) count = std::atoi(args[next++].c_str());
        if (count < 1 || count > 20) return std::string("err n 1..20\n");
        auto kind = d2d::rules::Boss::none;
        std::vector<int> mods;
        for (; next < args.size(); ++next) {
            if (args[next] == "near") continue;
            if (args[next] == "champion") kind = d2d::rules::Boss::champion;
            else if (args[next] == "unique") kind = d2d::rules::Boss::unique;
            else if (kind != d2d::rules::Boss::none && std::ranges::all_of(args[next], [](unsigned char c) { return std::isdigit(c); })) mods.push_back(std::atoi(args[next].c_str()));
            else return std::string("err unknown token ") + args[next] + "\n";
        }
        auto& fight = world.fight;
        if (!town.level || fight.mon_level != town.level) return std::string("err no monster level here (the field only, not town)\n");
        if (kind == d2d::rules::Boss::champion && mods.empty()) mods = { d2d::rules::umod::champion };
        const int difficulty = town.hero.character.header.active_difficulty();
        std::string ids;
        for (int made = 0; made < count; ++made) {
            const double angle = 2.0 * std::numbers::pi * double(made) / double(count);
            const auto [spot_x, spot_y] = town.level->nearest_free(town.hero.player.x + 3.f * float(std::cos(angle)), town.hero.player.y + 3.f * float(std::sin(angle)));
            auto monster = make_monster(*scene, row, spot_x, spot_y, fight.rng, difficulty, true, int(world.players.size()));
            if (kind != d2d::rules::Boss::none) make_boss(*scene, monster, kind, mods, -1, int(fight.rng.next() & 0xffff), difficulty, fight.rng, int(world.players.size()));
            fight.add_monster(std::move(monster));
            ids += std::format("{}#{}", ids.empty() ? "" : ",", fight.monsters.back().id);
        }
        const auto& first = fight.monsters.back();
        return std::format("ok spawned={} ids={} {} hp={} lvl={}\n", count, ids, first.npc.name, first.hit_points, first.stats.level);
    });
    add("unequip", [this](const Args& args) -> Reply {   // `unequip <body slot|belt>`: take what is worn there off the character (it is gone; belt boxes stay: `belt clear`)
        if (args.size() < 3) return std::nullopt;
        const int slot = args[2] == "belt" ? d2d::d2s::body_location::kBelt : std::atoi(args[2].c_str());
        const auto removed = std::erase_if(town.hero.character.items, [&](const auto& item) { return item.location == d2d::d2s::item_location::kEquipped && item.slot == slot; });
        return removed ? std::string("ok\n") : std::string("err nothing worn there\n");
    });
}

// `quest list|set|bit|join` (the `quest <q>` verb of debug_fight.cpp hands these over): the quest records of the active
// difficulty, 16 flag bits each (48 records, header.quests: record q is bits q*16..q*16+15), written directly. Then the
// act's join runs again (World::actN_join, what a new game does with the flags), so the quest machines
// (Den, Blood Raven, Andariel, ...) take the new state; the quest NPCs' hidden flags are refreshed.
DebugVerbs::Reply DebugVerbs::quest_record(const Args& args) {
    auto& world = town.world;
    auto& bits = world.quests();
    const auto act_of = [](int quest) { return quest < 7 ? 1 : quest < 15 ? 2 : quest < 23 ? 3 : quest < 26 ? 4 : 5; };   // the record words: act 1 0..6, act 2 7..14, act 3 15..22, act 4 23..25, act 5 26..
    const auto rejoin = [&](int act) {
        world.act_as(world.lead());
        switch (act) {
        case 1: world.act1_join(); break;
        case 2: world.act2_join(); break;
        case 3: world.act3_join(); break;
        case 4: world.act4_join(); break;
        default: world.act5_join(); break;
        }
        const auto& header = town.hero.character.header;
        for (std::size_t i = 0; i < town.level->npcs.size() && i < town.npc_states.size(); ++i)
            if (const int gated_quest = town.level->npcs[i].quest) town.npc_states[i].hidden = !header.quest_flag(header.active_difficulty(), gated_quest, 0);
    };
    const auto word = [&](int quest) { return unsigned(bits[std::size_t(quest) * 2]) | unsigned(bits[std::size_t(quest) * 2 + 1]) << 8; };
    const auto number = [](const std::string& text, long& value) { char* end = nullptr; value = std::strtol(text.c_str(), &end, 0); return !text.empty() && *end == 0; };
    if (args[2] == "list") {
        std::string out;
        for (int quest = 0; quest < 48; ++quest) {
            if (!word(quest)) continue;
            std::string set_bits;
            for (int bit = 0; bit < 16; ++bit) if (word(quest) >> bit & 1) set_bits += std::format("{}{}", set_bits.empty() ? "" : ",", bit);
            out += std::format("{}\t{:#06x}\tact{}\tbits={}\n", quest, word(quest), act_of(quest), set_bits);
        }
        return out + "ok\n";
    }
    if (!town.level) return std::nullopt;
    if (args[2] == "join") {
        if (args.size() < 4) return std::nullopt;
        long act = 0;
        if (!number(args[3], act) || act < 1 || act > 5) return std::string("err act 1..5\n");
        rejoin(int(act));
        return std::string("ok\n");
    }
    long quest = 0, value = 0;
    if (args[2] == "set") {
        if (args.size() < 5 || !number(args[3], quest) || !number(args[4], value)) return std::nullopt;
        if (quest < 0 || quest >= 48) return std::string("err quest 0..47\n");
        if (value < 0 || value > 0xffff) return std::string("err flags 0..0xffff\n");
        bits[std::size_t(quest) * 2] = std::uint8_t(value & 0xff);
        bits[std::size_t(quest) * 2 + 1] = std::uint8_t(value >> 8);
    } else {   // bit <q> <b> on|off
        long bit = 0;
        if (args.size() < 6 || !number(args[3], quest) || !number(args[4], bit) || (args[5] != "on" && args[5] != "off")) return std::nullopt;
        if (quest < 0 || quest >= 48) return std::string("err quest 0..47\n");
        if (bit < 0 || bit > 15) return std::string("err bit 0..15\n");
        d2d::rules::quest::set(bits, int(quest), int(bit), args[5] == "on");
    }
    rejoin(act_of(int(quest)));
    return std::format("ok quest {} = {:#06x}\n", quest, word(int(quest)));
}

// `give <code> pack|belt [box]`: a new normal item straight into the carried grid, as a pickup would place it
// (the pack's first free spot by the player's search; the belt's rules::belt_box box, or the box asked for).
DebugVerbs::Reply DebugVerbs::give_carried(const Args& args) {
    if (!scene || !town.world.game_data) return std::nullopt;
    auto& items = town.hero.character.items;
    int quality = 2, ilvl = -1, box = -1;
    bool unid = false;
    for (std::size_t i = 4; i < args.size(); ++i) {   // after the place: unid, q<quality 1..8>, l<item level>, or the belt box
        if (args[i] == "unid") unid = true;
        else if (args[i].size() > 1 && args[i][0] == 'q') quality = std::atoi(args[i].c_str() + 1);
        else if (args[i].size() > 1 && args[i][0] == 'l') ilvl = std::atoi(args[i].c_str() + 1);
        else if (args[3] == "belt" && !args[i].empty() && std::isdigit(static_cast<unsigned char>(args[i][0]))) box = std::atoi(args[i].c_str());
        else return std::string("err unknown token ") + args[i] + "\n";
    }
    if (quality < 1 || quality > 8) return std::string("err quality 1..8\n");
    auto seed = town.world.rng;
    auto item = d2d::rules::generate_item(scene->rules, args[2], ilvl > 0 ? ilvl : quality > 2 ? 30 : 1, quality, town.world.rng, &seed);
    item.identified = !unid;
    const auto& game_data = *town.world.game_data;
    if (args[3] == "belt") {
        const int boxes = game_data.belts[std::size_t(d2d::game::belt_index(game_data, items))].boxes;
        const int slot = box >= 0 ? box : d2d::rules::belt_box(scene->rules, items, item, boxes);
        const bool taken = std::ranges::any_of(items, [&](const auto& carried) { return carried.location == d2d::d2s::item_location::kBelt && carried.column == slot; });
        if (slot < 0 || slot >= boxes || taken) return std::string("err no belt box\n");
        item.location = d2d::d2s::item_location::kBelt; item.column = slot; item.row = 0; item.panel = 0; item.slot = 0;
        items.push_back(std::move(item));
        return std::format("ok belt box {} quality={} identified={}\n", slot, quality, item.identified ? 1 : 0);
    }
    const auto& layout = game_data.inv_layout[std::size_t(std::max(town.hero.character.character_class, 0))];
    const int cols = layout.cols ? layout.cols : 10, rows = layout.rows ? layout.rows : 4;
    std::vector<const d2d::d2s::Item*> pack;
    for (const auto& carried : items) if (carried.location == d2d::d2s::item_location::kStored && carried.panel == d2d::d2s::item_panel::kInventory) pack.push_back(&carried);
    const auto [width, height] = d2d::rules::item_size(scene->rules, item.code);
    const auto [column, row] = d2d::rules::free_spot(scene->rules, pack, cols, rows, width, height, true);
    std::optional<d2d::d2s::Item> held = std::move(item);
    if (column < 0 || !d2d::rules::put_in_grid(scene->rules, items, held, d2d::d2s::item_panel::kInventory, cols, rows, column, row)) return std::string("err no room\n");
    return std::format("ok pack {} {} quality={} identified={}\n", column, row, quality, unid ? 0 : 1);
}

}  // namespace d2d::client
