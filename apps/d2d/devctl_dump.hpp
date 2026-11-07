// SPDX-License-Identifier: GPL-3.0-or-later
// Local patch P1 (d.idle fork): `dump [sections]` returns one line of JSON.
// Read-only. Sections: player, stats, skills, items, quests, ui, merc, time.
// Header-only on purpose: it is included once, right after devctl/verbs.hpp,
// and uses the same locals the other verbs capture.
#pragma once

#include "devctl/verbs.hpp"
#include "lockstep.hpp"

#include <format>
#include <set>
#include <sstream>

namespace d2d::client {

namespace dump_detail {

inline std::string jstr(std::string_view text) {
    std::string out = "\"";
    for (const unsigned char c : text) {
        if (c == '"' || c == '\\') { out += '\\'; out += char(c); }
        else if (c < 0x20) out += std::format("\\u{:04x}", int(c));
        else out += char(c);
    }
    return out + "\"";
}

inline const char* class_name(int cls) {
    static constexpr const char* kNames[] = { "amazon", "sorceress", "necromancer", "paladin", "barbarian", "druid", "assassin" };
    return cls >= 0 && cls < 7 ? kNames[cls] : "unknown";
}

inline const char* item_loc(const d2d::d2s::Item& item) {
    using namespace d2d::d2s;
    switch (item.location) {
        case item_location::kEquipped: return "body";
        case item_location::kBelt: return "belt";
        case item_location::kCursor: return "cursor";
        case item_location::kGround: return "ground";
        case item_location::kSocketed: return "socketed";
        default: break;
    }
    return item.panel == item_panel::kStash ? "stash" : item.panel == item_panel::kCube ? "cube" : "inventory";
}

}  // namespace dump_detail

inline void register_dump_verb(d2d::devctl::Channel& channel, CharCreateUI& character, Town& town, std::optional<Scene>& scene, Screen& screen) {
    channel.on("dump", [&](const std::vector<std::string>& args) -> std::string {
        using namespace dump_detail;
        if (!scene) return "err no scene\n";
        std::set<std::string> want;
        if (args.size() > 1) {
            std::stringstream list(args[1]);
            for (std::string part; std::getline(list, part, ',');) want.insert(part);
        } else want = { "player", "stats", "skills", "items", "quests", "ui", "merc", "time" };
        for (const auto& name : want)
            if (!std::set<std::string>{ "player", "stats", "skills", "items", "quests", "ui", "merc", "time", "vendor" }.contains(name)) return "err unknown section " + name + "\n";
        const auto& stats = character.stats;
        const auto& header = character.header;
        std::string out = "{\"v\":1";
        if (want.contains("time")) {
            const auto& c = d2d::lockstep::clock();
            out += std::format(",\"time\":{{\"frame\":{},\"game_ms\":{},\"tick\":{},\"lockstep\":{},\"scale\":{}}}", c.frames, c.virt_ms, c.virt_ms / 40, c.lockstep, c.scale);
        }
        if (want.contains("player")) {
            const auto* level = town.level;
            out += std::format(",\"player\":{{\"name\":{},\"class\":{},\"level\":{},\"exp\":{},\"gold\":{},\"stash_gold\":{},\"difficulty\":{},\"hardcore\":{},\"expansion\":{},"
                               "\"area\":{},\"area_name\":{},\"x\":{:.2f},\"y\":{:.2f},\"walking\":{},\"mode\":{},\"life\":[{},{}],\"mana\":[{},{}],\"stamina\":[{},{}],"
                               "\"stat_pts\":{},\"skill_pts\":{},\"lskill\":{},\"rskill\":{},\"screen\":{}}}",
                               jstr(character.name), jstr(class_name(character.character_class)), stats.get(d2d::d2s::kLevel), stats.get(d2d::d2s::kExp),
                               stats.get(d2d::d2s::kGold), stats.get(d2d::d2s::kGoldBank), header.active_difficulty(), character.hardcore, character.expansion,
                               level ? level->id : 0, jstr(level ? level->name : ""), town.player.x, town.player.y, town.player.walking,
                               jstr(town.hero.pmode >= 0 ? kModeCode[town.hero.pmode] : "-"),
                               stats.fixed(d2d::d2s::kLife), stats.fixed(d2d::d2s::kMaxLife), stats.fixed(d2d::d2s::kMana), stats.fixed(d2d::d2s::kMaxMana),
                               stats.fixed(d2d::d2s::kStamina), stats.fixed(d2d::d2s::kMaxStamina), stats.get(d2d::d2s::kStatPts), stats.get(d2d::d2s::kSkillPts),
                               town.skillbar.left, town.skillbar.right, jstr(screen_name(screen)));
        }
        if (want.contains("stats")) {
            const auto& panel = character.panel;
            out += std::format(",\"stats\":{{\"base\":{{\"str\":{},\"dex\":{},\"vit\":{},\"ene\":{}}},\"bonus\":[{}],\"defense\":{},\"res\":{{\"fire\":{},\"cold\":{},\"light\":{},\"poison\":{}}},\"res_cap\":[{},{},{},{}],\"raw\":[",
                               stats.get(d2d::d2s::kStr), stats.get(d2d::d2s::kDex), stats.get(d2d::d2s::kVit), stats.get(d2d::d2s::kEne),
                               [&] { std::string list; for (const auto v : panel.bonus) list += (list.empty() ? "" : ",") + std::to_string(v); return list; }(),
                               panel.defense, panel.res[0], panel.res[1], panel.res[2], panel.res[3],
                               panel.res_cap[0], panel.res_cap[1], panel.res_cap[2], panel.res_cap[3]);
            for (std::size_t i = 0; i < stats.values.size(); ++i) out += (i ? "," : "") + std::to_string(stats.values[i]);
            out += "]}";
        }
        if (want.contains("skills")) {
            out += ",\"skills\":{\"base_levels\":[";
            for (std::size_t i = 0; i < stats.skills.size(); ++i) out += (i ? "," : "") + std::to_string(int(stats.skills[i]));
            out += "]}";
        }
        if (want.contains("items")) {
            out += ",\"items\":[";
            const auto wearer = d2d::game::wearer(*scene, character.character_class, character.items, character.stats);
            bool first = true;
            for (const auto& item : character.items) {
                const auto size = d2d::rules::item_size(scene->rules, item.code);
                const auto lines = item_lines(*scene, item, wearer.lvl, &wearer);
                out += std::format("{}{{\"id\":{},\"code\":{},\"loc\":{},\"slot\":{},\"x\":{},\"y\":{},\"w\":{},\"h\":{},\"quality\":{},\"ilvl\":{},\"sockets\":{},\"qty\":{},"
                                   "\"dur\":[{},{}],\"unique\":{},\"set\":{},\"flags\":{{\"identified\":{},\"ethereal\":{},\"socketed\":{},\"runeword\":{}}},\"name\":{}}}",
                                   first ? "" : ",", item.id, jstr(item.code), jstr(item_loc(item)), item.slot, item.column, item.row, size.first, size.second,
                                   item.quality, item.ilvl, item.sockets, item.quantity, item.durability, item.max_durability, item.unique_id, item.set_id,
                                   item.identified, item.ethereal, item.socketed, item.runeword, jstr(lines.empty() ? "" : lines.front().text));
                first = false;
            }
            out += "]";
        }
        if (want.contains("vendor")) {
            const auto& st = town.store;
            const bool open = st.npc >= 0;
            out += std::format(",\"vendor\":{{\"open\":{},\"vendor\":{},\"npc\":{},\"tab\":{},\"gamble\":{},\"tabs\":[", open, st.vendor, st.npc, st.tab, st.gamble);
            for (int t = 0; t < 4; ++t) {
                out += std::string(t ? ",[" : "[");
                bool first = true;
                if (open) for (const auto& item : st.tabs[std::size_t(t)]) {
                    const auto size = d2d::rules::item_size(scene->rules, item.code);
                    const int cost = d2d::rules::item_price(scene->rules, item, st.npc_id, false, st.header, st.reduced);
                    out += std::format("{}{{\"id\":{},\"code\":{},\"x\":{},\"y\":{},\"w\":{},\"h\":{},\"quality\":{},\"ilvl\":{},\"sockets\":{},\"qty\":{},\"price\":{}}}",
                                       first ? "" : ",", item.id, jstr(item.code), item.column, item.row, size.first, size.second, item.quality, item.ilvl, item.sockets, item.quantity, cost);
                    first = false;
                }
                out += "]";
            }
            out += "]}";
        }
        if (want.contains("quests")) {
            out += ",\"quests\":{\"diff\":[";
            for (int d = 0; d < 3; ++d) {
                out += std::string(d ? ",[" : "[");
                // 96 bytes per difficulty as 48 little-endian u16 words, the save's layout
                for (int w = 0; w < 48; ++w) out += (w ? "," : "") + std::to_string(header.quests[std::size_t(d)][std::size_t(2 * w)] | header.quests[std::size_t(d)][std::size_t(2 * w + 1)] << 8);
                out += "]";
            }
            out += "],\"waypoints\":[";
            for (int d = 0; d < 3; ++d) {
                std::uint64_t mask = 0;
                for (int b = 0; b < 40; ++b) if (header.waypoint(d, b)) mask |= std::uint64_t(1) << b;
                out += (d ? "," : "") + std::to_string(mask);
            }
            out += "]}";
        }
        if (want.contains("ui")) {
            out += std::format(",\"ui\":{{\"stash\":{},\"cube\":{},\"npc_menu\":{},\"vendor\":{},\"skill_tree\":{},\"waypoint\":{},\"game_menu\":{},\"mini_panel\":{},\"automap\":{},\"held\":{}",
                               town.stash_open, town.cube_open, town.npc_menu.npc >= 0, town.store.npc >= 0, town.tree_open, town.waypoint.open,
                               town.game_menu.open, town.mini.open, town.automap.open, jstr(town.held ? town.held->code : ""));
            // Patch P4: screen rectangles [x, y, w, h] (game pixels, 800x600) of what is open, from the same numbers the hit tests use.
            // Panels: inv_grid, stash_grid, cube_grid; coins: inv_coin, stash_coin; store: store_tabs, store_stock, store_buy, store_sell,
            // store_repair (button 3), store_repair_all (button 4 at repair vendors, else store_close); belt (boxes, inclusive edges
            // as belts.txt); waypoint: wp_tabs, wp_rows (activated only, with level id), wp_cancel; gold_box_* when the gold box is up.
            {
                using Rect = std::array<int, 4>;
                auto rect_json = [](const Rect& r) { return std::format("[{},{},{},{}]", r[0], r[1], r[2], r[3]); };
                auto field = [&](std::string& dst, const char* name, const Rect& r) { dst += std::format("{}\"{}\":{}", dst.empty() ? "" : ",", name, rect_json(r)); };
                auto grid = [&](const Scene::InvLayout& l) { return Rect{ l.grid_x, l.grid_y, l.cols * l.box_w, l.rows * l.box_h }; };
                std::string rects;
                const int cls = std::max(character.character_class, 0);
                if (town.inv_open) {
                    field(rects, "inv_grid", grid(scene->inv_layout[std::size_t(cls)]));
                    field(rects, "inv_coin", { 483, 471 - 17, 20, 18 });      // inventory_coin_at: x 483..502, y 454..471
                }
                if (town.cube_open) field(rects, "cube_grid", grid(scene->cube_layout));
                else if (town.stash_open) {
                    field(rects, "stash_grid", grid(scene->stash_layout[character.expansion ? 1 : 0]));
                    field(rects, "stash_coin", { 153, 102 - 17, 20, 18 });    // stash_coin_at: x 153..172, y 85..102
                }
                if (town.store.npc >= 0) {
                    static constexpr int kBtnX[4] = { 116, 169, 221, 273 };
                    const bool repairer = store_button_frames(town.store)[2] == 6;
                    const char* names[4] = { "store_buy", "store_sell", repairer ? "store_repair" : "store_spare", repairer ? "store_repair_all" : "store_close" };
                    for (int i = 0; i < 4; ++i) field(rects, names[i], { kCharPanelX - 1 + kBtnX[i], 476 - 32 + 1, 32, 32 });
                    field(rects, "store_stock", { 96, 123, 10 * 29, 10 * 29 });
                    std::string tabs;
                    for (int i = 0; i < 4; ++i) tabs += (i ? "," : "") + rect_json({ kCharPanelX + 80 * i, 60, 80, 31 });
                    rects += std::string(rects.empty() ? "" : ",") + "\"store_tabs\":[" + tabs + "]";
                }
                {
                    const auto& belt = scene->belts[std::size_t(belt_index(*scene, character.items))];
                    std::string boxes;
                    for (int box = 0; box < belt.boxes && box < int(belt.box.size()); ++box) {
                        const auto& b = belt.box[std::size_t(box)];
                        if (b[1] <= b[0]) continue;
                        boxes += (boxes.empty() ? "" : ",") + rect_json({ b[0], b[2], b[1] - b[0] + 1, b[3] - b[2] + 1 });
                    }
                    rects += std::string(rects.empty() ? "" : ",") + "\"belt\":[" + boxes + "]";
                }
                if (town.waypoint.open) {
                    const bool expansion = character.expansion;
                    std::string tabs, rows;
                    for (int tab = 0; tab < (expansion ? 5 : 4); ++tab)
                        if (waypoint_act_open(header, tab)) tabs += (tabs.empty() ? "" : ",") + rect_json({ kCharPanelX + tab * (expansion ? 64 : 80), 60, expansion ? 64 : 80, 31 });
                    const auto& levels = scene->waypoint_levels[std::size_t(town.waypoint.tab)];
                    for (std::size_t i = 0; i < levels.size() && i < 9; ++i)
                        if (header.waypoint(header.active_difficulty(), levels[i].waypoint))
                            rows += std::format("{}{{\"level\":{},\"rect\":{}}}", rows.empty() ? "" : ",", levels[i].level, rect_json({ kCharPanelX + 18, 60 + kWpHitTop[i] + 1, 279, 29 }));
                    rects += std::string(rects.empty() ? "" : ",") + "\"wp_tabs\":[" + tabs + "],\"wp_rows\":[" + rows + "],\"wp_tab\":" + std::to_string(town.waypoint.tab);
                    field(rects, "wp_cancel", { 353, 447, 36, 34 });
                }
                if (town.gold_box) {
                    field(rects, "gold_box_up", { 224, 219 - 11, 15, 12 });
                    field(rects, "gold_box_down", { 224, 232 - 11, 15, 12 });
                    field(rects, "gold_box_ok", { 250, 287 - 31, 32, 32 });
                    field(rects, "gold_box_cancel", { 355, 287 - 31, 32, 32 });
                }
                out += ",\"rects\":{" + rects + "}";
            }
            out += "}";
        }
        if (want.contains("merc")) {
            if (town.merc && town.hero.merc_npc)
                out += std::format(",\"merc\":{{\"present\":true,\"code\":{},\"x\":{:.2f},\"y\":{:.2f},\"life\":[{},{}],\"dead\":{}}}", jstr(town.hero.merc_npc->code),
                                   town.merc->unit.x, town.merc->unit.y, town.merc->hit_points, town.merc->stats.hit_points, header.merc_dead);
            else out += std::format(",\"merc\":{{\"present\":false,\"dead\":{}}}", header.merc_dead);
        }
        return out + "}\nok\n";
    });
}

}  // namespace d2d::client
