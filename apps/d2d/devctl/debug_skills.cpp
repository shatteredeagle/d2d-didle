// SPDX-License-Identifier: GPL-3.0-or-later
// devctl `debug` sub-verbs of the lab patch set (the private fork only, never an upstream PR) for the live skill
// drills: `mana full`, `states set|clear`, `mon set`, `cast`. Test cheats: the harness ledgers every call before
// it is sent. Each one refuses unless this is an offline --no-save game (no character store, no joined host), and
// every reply says what changed.
#include "debug.hpp"

#include "ai.hpp"
#include "protocol.hpp"
#include "town.hpp"

#include <d2s_items.hpp>
#include <skills.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <optional>
#include <string>

namespace d2d::client {

namespace {

constexpr const char* kRefused = "err refused: not an offline --no-save game\n";
constexpr std::array<const char*, 6> kResNames = { "phys", "magic", "fire", "light", "cold", "poison" };   // Target::res order

bool whole(const std::string& text, long& out, long low, long high) {
    char* end = nullptr;
    out = std::strtol(text.c_str(), &end, 10);
    return !text.empty() && *end == '\0' && out >= low && out <= high;
}

}  // namespace

void DebugVerbs::add_skills() {
    // `cast <skill id> <x> <y> | unit <monster id> | near`: the player casts the skill through the normal skill path
    // (cmd::UseSkill, the one `cmd skill` sends): start check, mana at the action frame, missiles as a real cast
    // makes them. Queued; the World applies it at its next tick. The skill needs a level (`debug points`).
    add("cast", [this](const Args& args) -> Reply {
        if (args.size() < 4) return std::nullopt;
        if (town.world.characters || town.net_game) return std::string(kRefused);
        auto& world = town.world;
        world.act_as(world.lead());
        long id = 0;
        if (!whole(args[2], id, 0, 1000)) return std::string("err skill id 0..1000\n");
        const auto* skill = world.game_data ? world.game_data->skills.get(int(id)) : nullptr;
        if (!skill) return std::string("err no such skill\n");
        const int level = world.fight.skill_level ? world.fight.skill_level(int(id)) : 1;
        if (level <= 0) return std::string("err skill has no level (debug points <id> <n>)\n");
        float x = 0, y = 0;
        int unit = -1;
        if (args[3] == "unit" || args[3] == "near") {
            const Monster* target = nullptr;
            long want = 0;
            if (args[3] == "unit" && (args.size() < 5 || !whole(args[4], want, 0, 1 << 30))) return std::nullopt;
            for (const auto& monster : world.fight.monsters) {
                if (!monster.targetable()) continue;
                if (args[3] == "unit" ? monster.id == want
                                      : !target || std::hypot(monster.unit.x - town.player.x, monster.unit.y - town.player.y) < std::hypot(target->unit.x - town.player.x, target->unit.y - town.player.y))
                    target = &monster;
                if (args[3] == "unit" && target) break;
            }
            if (!target) return std::string("err no such monster\n");
            x = target->unit.x; y = target->unit.y; unit = target->id;
        } else {
            if (args.size() < 5) return std::nullopt;
            char* end_x = nullptr; char* end_y = nullptr;
            x = std::strtof(args[3].c_str(), &end_x);
            y = std::strtof(args[4].c_str(), &end_y);
            if (*end_x != '\0' || *end_y != '\0' || !std::isfinite(x) || !std::isfinite(y) || std::abs(x) > 4096 || std::abs(y) > 4096) return std::string("err x y: cells, |v| <= 4096\n");
        }
        const long mana = world.fight.actor->character.stats.values[d2d::d2s::kMana] >> 8;
        town.send(cmd::UseSkill{ int(id), x, y, unit, false, -1 });
        return std::format("ok queued skill={} {} lvl={} at={:.2f},{:.2f} unit={} mana={}\n", id, skill->name, level, x, y, unit, mana);
    });

    // `mana full`: mana := max mana (the lead player's stat 8 from stat 9).
    add("mana", [this](const Args& args) -> Reply {
        if (args.size() != 3 || args[2] != "full") return std::nullopt;
        if (town.world.characters || town.net_game) return std::string(kRefused);
        auto& stats = town.hero.character.stats;
        const auto before = stats.values[d2d::d2s::kMana];
        stats.values[d2d::d2s::kMana] = stats.values[d2d::d2s::kMaxMana];
        return std::format("ok mana {} -> {} (max {})\n", before >> 8, stats.values[d2d::d2s::kMana] >> 8, stats.values[d2d::d2s::kMaxMana] >> 8);
    });

    // `mon set <monster id> [life <n>] [frac <0..255>] [maxlife <n>] [dmg <lo> <hi>] [res <phys|magic|fire|light|cold|poison> <-100..100>]...`
    // for one live monster (the `#id` of `monsters`). life/maxlife are points; frac is 256ths already taken off
    // life; dmg is its A1, A2 and S1 range; res is the absolute resistance (the mod bonus is set to make it).
    add("mon", [this](const Args& args) -> Reply {
        if (args.size() < 5 || args[2] != "set") return std::nullopt;
        if (town.world.characters || town.net_game) return std::string(kRefused);
        long id = 0;
        if (!whole(args[3], id, 0, 1 << 30)) return std::string("err monster id\n");
        auto& monsters = town.world.fight.monsters;
        const auto found = std::ranges::find_if(monsters, [&](const Monster& monster) { return monster.id == id && monster.alive(); });
        if (found == monsters.end()) return std::string("err no live monster with that id\n");
        Monster& monster = *found;
        long life = -1, frac = -1, maxlife = -1, lo = -1, hi = -1;
        std::array<std::optional<long>, 6> res{};
        for (std::size_t i = 4; i < args.size();) {
            const auto& key = args[i];
            if (key == "life" && i + 1 < args.size()) { if (!whole(args[i + 1], life, 1, 1000000)) return std::string("err life 1..1000000\n"); i += 2; }
            else if (key == "frac" && i + 1 < args.size()) { if (!whole(args[i + 1], frac, 0, 255)) return std::string("err frac 0..255\n"); i += 2; }
            else if (key == "maxlife" && i + 1 < args.size()) { if (!whole(args[i + 1], maxlife, 1, 1000000)) return std::string("err maxlife 1..1000000\n"); i += 2; }
            else if (key == "dmg" && i + 2 < args.size()) {
                if (!whole(args[i + 1], lo, 0, 100000) || !whole(args[i + 2], hi, 0, 100000) || lo > hi) return std::string("err dmg <lo> <hi> 0..100000, lo <= hi\n");
                i += 3;
            } else if (key == "res" && i + 2 < args.size()) {
                const auto kind = std::ranges::find_if(kResNames, [&](const char* name) { return args[i + 1] == name; });
                long value = 0;
                if (kind == kResNames.end()) return std::string("err res phys|magic|fire|light|cold|poison\n");
                if (!whole(args[i + 2], value, -100, 100)) return std::string("err res -100..100\n");
                res[std::size_t(kind - kResNames.begin())] = value;
                i += 3;
            } else return std::string("err unknown token ") + key + "\n";
        }
        if (life < 0 && frac < 0 && maxlife < 0 && lo < 0 && std::ranges::none_of(res, [](const auto& value) { return value.has_value(); })) return std::nullopt;
        std::string out = std::format("ok #{} {}", monster.id, monster.npc.name);
        if (maxlife >= 0) { out += std::format(" maxlife {} -> {}", monster.stats.hit_points, maxlife); monster.stats.hit_points = int(maxlife); }
        if (life >= 0) {
            if (life > monster.stats.hit_points) monster.stats.hit_points = int(life);   // keep life <= max
            out += std::format(" life {} -> {}", monster.hit_points, life);
            monster.hit_points = int(life);
            monster.last_hp = monster.hit_points;   // no "life dropped" effect from the edit
            if (frac < 0) monster.life_frac = 0;
        }
        if (frac >= 0) { monster.life_frac = int(frac); out += std::format(" frac {}", frac); }
        if (lo >= 0) {
            monster.stats.a1_min = monster.stats.a2_min = monster.stats.s1_min = int(lo);
            monster.stats.a1_max = monster.stats.a2_max = monster.stats.s1_max = int(hi);
            out += std::format(" dmg {}-{}", lo, hi);
        }
        const auto& base = town.world.game_data->monsters.types[std::size_t(monster.type)].diff[std::size_t(std::clamp(monster.difficulty, 0, 2))].res;
        for (std::size_t kind = 0; kind < 6; ++kind) {
            if (!res[kind]) continue;
            monster.boss_res[kind] = int(*res[kind]) - base[kind];
            out += std::format(" res.{} {}", kResNames[kind], *res[kind]);
        }
        return out + "\n";
    });

    // `states set <skill id> [level]` / `states clear <skill id>|all`: the player's self states (Fight::self_states, what
    // `states` lists): the skill's state as a cast starts it (duration from its auralen; Bone Armor's absorb pool),
    // or removed. Frozen Armor is skill 40 (States.txt row 10). `states` alone still only lists.
    add("states", [this](const Args& args) -> Reply {
        auto& world = town.world;
        if (args.size() >= 3 && (args[2] == "set" || args[2] == "clear")) {
            if (world.characters || town.net_game) return std::string(kRefused);
            world.act_as(world.lead());
            auto& fight = world.fight;
            if (args[2] == "clear" && args.size() == 4 && args[3] == "all") {
                const auto count = fight.actor->self_states.size();
                fight.actor->self_states.clear();
                fight.actor->absorb_pool = 0;
                return std::format("ok cleared {} states\n", count);
            }
            long id = 0;
            if (args.size() < 4 || !whole(args[3], id, 0, 1000)) return std::nullopt;
            if (args[2] == "clear") {
                const auto removed = std::erase_if(fight.actor->self_states, [&](const auto& state) { return state.skill == id; });
                if (fight.actor->absorb_skill == id) fight.actor->absorb_pool = 0;
                return removed ? std::format("ok cleared state of skill {}\n", id) : std::string("err skill not in states\n");
            }
            const auto* skill = world.game_data ? world.game_data->skills.get(int(id)) : nullptr;
            if (!skill || skill->aurastate.empty()) return std::string("err no such skill, or it has no state\n");
            long level = fight.skill_level ? std::max(fight.skill_level(int(id)), 1) : 1;
            if (args.size() >= 5 && !whole(args[4], level, 1, 99)) return std::string("err level 1..99\n");
            fight.start_state(*skill, int(level), world.now);
            return std::format("ok state {} of skill {} {} lvl={}\n", skill->aurastate, id, skill->name, level);
        }
        if (args.size() > 2) return std::nullopt;
        std::string out = std::format("absorb={} maxlife={} maxmana={} def={} buffs=", town.hero.absorb_pool,
                                      character.stats.fixed(d2d::d2s::kMaxLife), character.stats.fixed(d2d::d2s::kMaxMana), town.hero.player_combat.defense);
        for (const auto& state : town.hero.self_states) out += std::format("{}:{},", state.skill, state.level);
        out += "\n";
        for (const auto& monster : town.fight.monsters)
            if (monster.curse.skill >= 0 || monster.cry.skill >= 0 || monster.stun_until > 0)
                out += std::format("{} hp={} curse={} cry={} dmg%={} speed%={} reflect%={} align={} stun={} ai={}{} afraid={}\n", monster.npc.name, monster.hit_points,
                                   monster.curse.skill, monster.cry.skill, monster.dmg_pct, monster.speed_pct, monster.reflect_pct, monster.align,
                                   monster.stun_until, monster.special, monster.other ? "/other" : "", int(monster.afraid));
        return out + "ok\n";
    });
}

}  // namespace d2d::client
