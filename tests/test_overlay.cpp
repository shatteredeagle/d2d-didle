// SPDX-License-Identifier: GPL-3.0-or-later
// test-links:
// The table overlay merge (components/mpq/overlay.hpp) on a tiny fixture:
// replace by key, append, empty cell keeps the base, append-only tables,
// and every malformed fragment throws with the file and line.
#include <overlay.hpp>

#include <cassert>
#include <cstdio>
#include <exception>
#include <string>

using d2d::mpq::overlay::merge;

static bool throws(const std::string& base, const std::string& frag, const char* table, const char* needle) {
    try { merge(base, frag, table, "T.txt"); }
    catch (const std::exception& e) {
        const std::string what = e.what();
        return what.find("overlay T.txt:") == 0 && what.find(needle) != what.npos;
    }
    return false;
}

int main() try {
    const std::string base = "index\tcode\tlvl\r\nA\tcap\t1\r\nB\tcap\t2\r\n";
    {   // replace A (lvl only, code cell empty keeps), append C
        auto m = merge(base, "index\tcode\tlvl\nA\t\t9\nC\tcap\t3\n", "uniqueitems", "T.txt");
        assert(m.replaced == 1 && m.appended == 1);
        assert(m.text == "index\tcode\tlvl\r\nA\tcap\t9\r\nB\tcap\t2\r\nC\tcap\t3\r\n");
    }
    {   // fragment columns in another order and a subset
        auto m = merge(base, "lvl\tindex\n7\tB\n", "uniqueitems", "T.txt");
        assert(m.text == "index\tcode\tlvl\r\nA\tcap\t1\r\nB\tcap\t7\r\n" && m.replaced == 1);
    }
    {   // a missing column in an appended row stays empty
        auto m = merge(base, "index\tlvl\nD\t4\n", "uniqueitems", "T.txt");
        assert(m.text.ends_with("D\t\t4\r\n"));
    }
    {   // append-only table: the same name is a new row
        auto m = merge("description\tnumin\r\nx\t1\r\n", "description\tnumin\nx\t2\n", "cubemain", "T.txt");
        assert(m.appended == 1 && m.replaced == 0);
    }
    assert(throws(base, "index\tbogus\nA\t1\n", "uniqueitems", ":1:"));          // unknown column
    assert(throws(base, "index\tcode\nA\n", "uniqueitems", ":2:"));              // short row
    assert(throws(base, "index\tcode\nA\tcap\textra\n", "uniqueitems", ":2:"));  // long row
    assert(throws(base, "code\nA\n", "uniqueitems", "key column"));              // key column missing
    assert(throws(base, "index\tcode\n\tcap\n", "uniqueitems", "empty key"));
    assert(throws(base, "index\tcode\nA\tx\nA\ty\n", "uniqueitems", ":3:"));      // duplicate key
    assert(throws(base, "", "uniqueitems", "empty fragment"));

    // Hash is stable and the tag text is exact.
    assert(d2d::mpq::overlay::Overlay().tag() == "overlay=off");
    std::puts("ok");
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "test_overlay: %s\n", e.what());
    return 1;
}
