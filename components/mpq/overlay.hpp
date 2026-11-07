// SPDX-License-Identifier: GPL-3.0-or-later
// Table overlay (d.idle mode): row fragments merged into excel tables.
//
// `--overlay-dir <dir>` names a directory with `excel/<Table>.txt` fragments
// (our own rows only: tab separated, the table's header names, any subset and
// order of columns). A fragment row whose key matches a base row REPLACES its
// non-empty cells (an empty cell keeps the base cell); any other row is
// APPENDED. Default off: without a directory nothing here runs.
//
// Header-only and free of StormLib so a unit test can feed it tiny fixtures.
// Hash, for the bot's mode stamp (the Go side, internal/overlay, must give the
// same value): FNV-1a 64 over, for each fragment sorted by lower-case file
// name, the name, a 0 byte, the file bytes, a 0 byte; the stamp is the first 8
// hex digits of the 16.
#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace d2d::mpq::overlay {

// Key column per table (lower-case table name). An empty key means the table
// is append-only: its rows have no unique name (CubeMain, affixes, whose ids
// are row numbers and may repeat a name). Unknown tables key on column 0.
// ponytail: a table not listed behaves as "first column"; add it here if that
// is wrong for it.
inline std::string_view key_column(std::string_view table_lower) {
    static const std::map<std::string, std::string, std::less<>> keys = {
        {"misc", "code"}, {"armor", "code"}, {"weapons", "code"}, {"gems", "code"},
        {"itemtypes", "Code"}, {"properties", "code"},
        {"uniqueitems", "index"}, {"setitems", "index"}, {"sets", "index"},
        {"treasureclassex", "Treasure Class"}, {"runes", "*Rune Name"},
        {"itemstatcost", "Stat"}, {"superuniques", "Superunique"}, {"monstats", "Id"},
        {"skills", "skill"},
        {"cubemain", ""}, {"magicprefix", ""}, {"magicsuffix", ""}, {"automagic", ""},
    };
    const auto at = keys.find(table_lower);
    return at == keys.end() ? std::string_view("\x01") : std::string_view(at->second);   // \x01: first column
}

using Cells = std::vector<std::string>;

struct Parsed {
    std::string       eol = "\r\n";
    std::vector<Cells> lines;       // line 0 is the header; blank lines are kept as empty Cells
};

inline Parsed parse(std::string_view text) {
    Parsed out;
    if (text.find("\r\n") == text.npos && text.find('\n') != text.npos) out.eol = "\n";
    while (!text.empty()) {
        const auto newline = text.find('\n');
        auto line = text.substr(0, newline);
        text = newline == text.npos ? std::string_view{} : text.substr(newline + 1);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        Cells cells;
        if (!line.empty())
            for (std::size_t at = 0;;) {
                const auto tab = line.find('\t', at);
                cells.emplace_back(line.substr(at, tab == line.npos ? line.npos : tab - at));
                if (tab == line.npos) break;
                at = tab + 1;
            }
        out.lines.push_back(std::move(cells));
    }
    return out;
}

inline std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = char(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

struct Merged {
    std::string text;
    std::size_t replaced = 0, appended = 0;
};

// Merge one fragment into a base table. Throws std::runtime_error naming
// `file` and the 1-based line of the fault: a malformed fragment is a startup
// error, never a silent fallback.
inline Merged merge(std::string_view base_text, std::string_view fragment_text,
                    std::string_view table_lower, const std::string& file) {
    auto fail = [&](std::size_t line, const std::string& why) {
        return std::runtime_error("overlay " + file + ":" + std::to_string(line) + ": " + why);
    };
    Parsed base = parse(base_text);
    const Parsed frag = parse(fragment_text);
    if (base.lines.empty() || base.lines[0].empty()) throw fail(0, "base table " + std::string(table_lower) + " has no header");
    if (frag.lines.empty() || frag.lines[0].empty()) throw fail(1, "empty fragment (no header line)");
    const Cells& bh = base.lines[0];
    const Cells& fh = frag.lines[0];

    std::vector<std::size_t> to_base(fh.size());       // fragment column -> base column
    for (std::size_t c = 0; c < fh.size(); ++c) {
        const auto it = std::find_if(bh.begin(), bh.end(), [&](const std::string& h) { return lower(h) == lower(fh[c]); });
        if (it == bh.end()) throw fail(1, "column \"" + fh[c] + "\" is not in the base table header");
        to_base[c] = std::size_t(it - bh.begin());
    }

    const auto key_name = key_column(table_lower);
    const bool append_only = key_name.empty();
    std::size_t key_frag = 0, key_base = 0;
    if (!append_only) {
        if (key_name == "\x01") { key_base = 0; }
        else {
            const auto it = std::find_if(bh.begin(), bh.end(), [&](const std::string& h) { return lower(h) == lower(key_name); });
            if (it == bh.end()) throw fail(1, "key column \"" + std::string(key_name) + "\" is not in the base table header");
            key_base = std::size_t(it - bh.begin());
        }
        const auto it = std::find(to_base.begin(), to_base.end(), key_base);
        if (it == to_base.end()) throw fail(1, "the fragment needs the key column \"" + bh[key_base] + "\"");
        key_frag = std::size_t(it - to_base.begin());
    }

    std::map<std::string, std::size_t> row_of;          // key -> base line (first wins)
    if (!append_only)
        for (std::size_t i = 1; i < base.lines.size(); ++i)
            if (key_base < base.lines[i].size() && !base.lines[i][key_base].empty())
                row_of.emplace(base.lines[i][key_base], i);

    Merged out;
    std::map<std::string, std::size_t> seen;            // keys of this fragment: a repeat is a mistake
    for (std::size_t n = 1; n < frag.lines.size(); ++n) {
        const Cells& row = frag.lines[n];
        if (row.empty()) continue;
        if (row.size() != fh.size())
            throw fail(n + 1, std::to_string(row.size()) + " cells, header has " + std::to_string(fh.size()));
        if (!append_only) {
            const std::string& key = row[key_frag];
            if (key.empty()) throw fail(n + 1, "empty key in column \"" + bh[key_base] + "\"");
            if (!seen.emplace(key, n).second) throw fail(n + 1, "duplicate key \"" + key + "\" in this fragment");
            if (const auto hit = row_of.find(key); hit != row_of.end()) {
                Cells& target = base.lines[hit->second];
                if (target.size() < bh.size()) target.resize(bh.size());
                for (std::size_t c = 0; c < row.size(); ++c)
                    if (!row[c].empty()) target[to_base[c]] = row[c];
                ++out.replaced;
                continue;
            }
        }
        Cells added(bh.size());
        for (std::size_t c = 0; c < row.size(); ++c) added[to_base[c]] = row[c];
        // Keep a following append of the same key a replace of this one.
        if (!append_only) row_of.emplace(row[key_frag], base.lines.size());
        base.lines.push_back(std::move(added));
        ++out.appended;
    }

    for (const Cells& line : base.lines) {
        for (std::size_t c = 0; c < line.size(); ++c) { if (c) out.text += '\t'; out.text += line[c]; }
        out.text += base.eol;
    }
    return out;
}

inline std::vector<std::byte> to_bytes(const std::string& s) {
    const auto* p = reinterpret_cast<const std::byte*>(s.data());
    return {p, p + s.size()};
}

// One configured overlay: the fragments found under <dir>/excel/.
class Overlay {
public:
    struct Fragment { std::string file;  std::string table_lower;  std::string text; };

    Overlay() = default;
    explicit Overlay(const std::filesystem::path& dir) : dir_(dir) {
        namespace fs = std::filesystem;
        const fs::path excel = dir / "excel";
        if (!fs::is_directory(excel)) throw std::runtime_error("overlay: no excel/ directory in " + dir.string());
        for (const auto& entry : fs::directory_iterator(excel)) {
            if (!entry.is_regular_file() || lower(entry.path().extension().string()) != ".txt") continue;
            std::ifstream in(entry.path(), std::ios::binary);
            Fragment f;
            f.file = entry.path().filename().string();
            f.table_lower = lower(entry.path().stem().string());
            f.text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            fragments_.push_back(std::move(f));
        }
        std::sort(fragments_.begin(), fragments_.end(), [](const Fragment& a, const Fragment& b) { return lower(a.file) < lower(b.file); });
        hash_ = compute_hash();
    }

    [[nodiscard]] bool active() const noexcept { return !dir_.empty(); }
    [[nodiscard]] const std::filesystem::path& dir() const noexcept { return dir_; }
    [[nodiscard]] const std::vector<Fragment>& fragments() const noexcept { return fragments_; }
    [[nodiscard]] const std::string& hash8() const noexcept { return hash_; }
    [[nodiscard]] std::string tag() const { return active() ? "overlay=" + hash_ : "overlay=off"; }

    // The fragment for a requested archive path, if any.
    [[nodiscard]] const Fragment* find(std::string_view path) const {
        constexpr std::string_view prefix = "data\\global\\excel\\";
        const std::string p = lower(path);
        if (!p.starts_with(prefix) || !p.ends_with(".txt")) return nullptr;
        const std::string table = p.substr(prefix.size(), p.size() - prefix.size() - 4);
        for (const auto& f : fragments_) if (f.table_lower == table) return &f;
        return nullptr;
    }

private:
    [[nodiscard]] std::string compute_hash() const {
        std::uint64_t h = 1469598103934665603ull;
        auto feed = [&](std::string_view s) { for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; } };
        for (const auto& f : fragments_) {
            feed(lower(f.file)); feed(std::string_view("\0", 1)); feed(f.text); feed(std::string_view("\0", 1));
        }
        static constexpr char hex[] = "0123456789abcdef";
        std::string out;
        for (int shift = 60; shift >= 32; shift -= 4) out += hex[(h >> shift) & 0xf];
        return out;
    }

    std::filesystem::path dir_;
    std::vector<Fragment> fragments_;
    std::string           hash_;
};

// Process-wide option set once in main() (like the lab-verbs flag) and read
// by open_game_data. Empty: overlay off.
inline std::filesystem::path& configured_dir() {
    static std::filesystem::path dir;
    return dir;
}

}  // namespace d2d::mpq::overlay
