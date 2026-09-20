// Style runs for rich text: sub-ranges of a Text's flat string that deviate from the cell's
// base style (Text::stylebits / Cell::textcolor). Deliberately free of wx dependencies so the
// bookkeeping can be unit tested on its own.
//
// Invariants (see Valid()): runs are sorted, non-overlapping, non-empty and lie within the text.
// Text not covered by any run uses the base style.

#pragma once

#include <algorithm>
#include <optional>
#include <vector>

struct TextRun {
    int start {0};
    int len {0};
    int stylebits {0};
    unsigned color {0};    // only meaningful if hascolor
    bool hascolor {false};  // false: use the cell's text color

    int end() const { return start + len; }
    bool SameStyle(const TextRun &o) const {
        return stylebits == o.stylebits && hascolor == o.hascolor && (!hascolor || color == o.color);
    }
    bool operator==(const TextRun &o) const {
        return start == o.start && len == o.len && SameStyle(o);
    }
};

struct TextRuns {
    std::vector<TextRun> v;

    bool empty() const { return v.empty(); }
    void clear() { v.clear(); }

    bool Valid(int textlen) const {
        auto pos = 0;
        for (auto &r : v) {
            if (r.start < pos || r.len <= 0 || r.end() > textlen) { return false; }
            pos = r.end();
        }
        return true;
    }

    // Merges neighbours with equal style, drops runs that don't differ from the base style.
    void Normalize(int basestylebits) {
        std::vector<TextRun> out;
        out.reserve(v.size());
        for (auto &r : v) {
            if (r.len <= 0) { continue; }
            if (r.stylebits == basestylebits && !r.hascolor) { continue; }
            if (!out.empty() && out.back().end() == r.start && out.back().SameStyle(r)) {
                out.back().len += r.len;
            } else {
                out.push_back(r);
            }
        }
        v = std::move(out);
    }

    // Style of the character at pos, or nullptr if it has the base style.
    const TextRun *At(int pos) const {
        for (auto &r : v) {
            if (pos < r.start) { break; }
            if (pos < r.end()) { return &r; }
        }
        return nullptr;
    }

    // Text of length n was inserted at pos. It takes the style of the character before it (or,
    // at the very start, of the one after it), like in most word processors.
    void Insert(int pos, int n) {
        if (n <= 0) { return; }
        for (auto &r : v) {
            if ((r.start < pos && pos <= r.end()) || (pos == 0 && r.start == 0)) {
                r.len += n;
            } else if (r.start >= pos) {
                r.start += n;
            }
        }
    }

    // Text [pos, pos + n) was removed.
    void Remove(int pos, int n, int basestylebits) {
        if (n <= 0) { return; }
        auto map = [&](int x) { return x <= pos ? x : (x >= pos + n ? x - n : pos); };
        for (auto &r : v) {
            auto s = map(r.start);
            auto e = map(r.end());
            r.start = s;
            r.len = e - s;
        }
        Normalize(basestylebits);
    }

    // Text [pos, pos + n) was replaced by text of length m. The new text takes the style of the
    // first replaced character.
    void Replace(int pos, int n, int m, int basestylebits) {
        if (n <= 0) {
            Insert(pos, m);
        } else if (m <= 0) {
            Remove(pos, n, basestylebits);
        } else {
            Insert(pos + 1, m);
            Remove(pos, 1, basestylebits);
            Remove(pos + m, n - 1, basestylebits);
        }
        Normalize(basestylebits);
    }

    // Applies f(TextRun &) to every character in [from, to), splitting runs at the boundaries
    // and materializing base-styled runs for the parts that had none.
    template<typename F>
    void Modify(int from, int to, int textlen, int basestylebits, F f) {
        from = std::max(from, 0);
        to = std::min(to, textlen);
        if (from >= to) { return; }
        std::vector<TextRun> out;
        auto pos = from;
        auto gap = [&](int a, int b) {
            if (a >= b) { return; }
            TextRun g;
            g.start = a;
            g.len = b - a;
            g.stylebits = basestylebits;
            f(g);
            out.push_back(g);
        };
        for (auto r : v) {
            if (r.end() <= from) {
                out.push_back(r);
            } else if (r.start >= to) {
                gap(pos, to);
                pos = to;
                out.push_back(r);
            } else {
                // Split off the parts of r outside [from, to); they keep their style.
                if (r.start < from) {
                    auto head = r;
                    head.len = from - r.start;
                    out.push_back(head);
                    r.len -= head.len;
                    r.start = from;
                }
                std::optional<TextRun> tail;
                if (r.end() > to) {
                    tail = r;
                    tail->start = to;
                    tail->len = r.end() - to;
                    r.len = to - r.start;
                }
                gap(pos, r.start);
                f(r);
                out.push_back(r);
                pos = r.end();
                if (tail) { out.push_back(*tail); }
            }
        }
        gap(pos, to);
        v = std::move(out);
        Normalize(basestylebits);
    }
};
