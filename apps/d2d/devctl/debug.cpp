// SPDX-License-Identifier: GPL-3.0-or-later
// The `debug` verb's table: debug.hpp.
#include "debug.hpp"

#include "scene.hpp"
#include "town.hpp"
#include "ui.hpp"

#include <atomic>
#include <cassert>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace d2d::client {

DebugVerbs::DebugVerbs(CharCreateUI& character_ui, Town& in_game, std::optional<Scene>& loaded_scene)
    : character(character_ui), town(in_game), scene(loaded_scene) {
    add_fight();
    add_hero();
    add_lab();
    add_missiles();
    add_skills();
    add_level();
    add_net();
}

void DebugVerbs::add(const std::string& name, Verb verb) {
    [[maybe_unused]] const bool added = verbs_.emplace(name, std::move(verb)).second;
    assert(added && "a debug sub-verb added twice");
}

namespace {
std::atomic<bool> g_lab_verbs{false};
}

void DebugVerbs::set_lab_verbs(bool on) { g_lab_verbs = on; }
bool DebugVerbs::lab_verbs() { return g_lab_verbs; }

// Allow-list, not deny-list: a sub-verb added later is gated until someone lists it here.
bool DebugVerbs::read_only(const Args& args) {
    if (args.size() < 2) return true;   // bare `debug`: the usage line
    static constexpr std::string_view kRead[] = {
        "players", "level", "warps", "portals", "presets", "objects", "corpses", "layout", "nearby", "grid", "blocked",
        "attack", "passives", "itemat", "missiles", "help", "tomb", "unbuilt", "pets", "critters", "net", "roster",
        "laid", "light", "walls", "tiles",
    };
    const std::string& sub = args[1];
    for (const auto name : kRead) if (sub == name) return true;
    if (sub == "states") return args.size() == 2;                                  // `states set|clear` write
    if (sub == "quest") return (args.size() >= 3 && args[2] == "list") || (args.size() >= 4 && args[3] == "show");
    return false;
}

std::string DebugVerbs::run(const Args& args) const {
    if (!lab_verbs() && !read_only(args)) return "err lab verbs off: debug " + args[1] + " needs d2d --lab-verbs\n";
    if (args.size() >= 2) {
        if (const auto verb = verbs_.find(args[1]); verb != verbs_.end()) {
            if (auto reply = verb->second(args)) return *std::move(reply);
        }
    }
    return "err debug wp <index|all|level <id>>|collision|automap|portals|objects|net|statpts <n>|skillpts <n>|wear|unid|level|blocked <x> <y>|warp <x> <y>|act <1-5>|help|hit <dmg>|merc hire <type>|kill|revive|hurt <dmg>|pin on|off|cast <skill> <x> <y>|unit <id>|near|mana full|mon set <id> life|frac|maxlife|dmg|res ...|states set|clear <skill>|spawn <monster> [n] [champion|unique]|quest list|set|bit|join|durability <slot|all> <v>|belt clear|unequip <slot|belt>|give <code> pack|belt|stat <id> <v>|quest <q>|skill left|right <id>|points <id> <n>\n";
}

}  // namespace d2d::client
