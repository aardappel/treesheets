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

// Converts between positions in a string, counted in its code units, and positions counted in
// Unicode code points. They differ where the string is stored as UTF-16 (as wxString is on
// Windows) and contains characters outside the Basic Multilingual Plane, which take two units
// (a surrogate pair). Where units are code points, both are the same.
// Positions have to be asked for in ascending order.
template<typename Get> struct CodePointCursor {
    Get get;  // get(unit) returns the code unit at a position
    int len;  // number of code units
    int unit {0};
    int codepoint {0};

    int Step() const {
        auto c = get(unit);
        if (c < 0xD800 || c > 0xDBFF || unit + 1 >= len) { return 1; }
        auto next = get(unit + 1);
        return next >= 0xDC00 && next <= 0xDFFF ? 2 : 1;
    }
    // The code point position of the code unit position `at`.
    int CodePointAt(int at) {
        while (unit < at && unit < len) {
            unit += Step();
            codepoint++;
        }
        return codepoint;
    }
    // The code unit position of the code point position `at`.
    int UnitAt(int at) {
        while (codepoint < at && unit < len) {
            unit += Step();
            codepoint++;
        }
        return unit;
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

// Hyperlinks: ranges of a Text's flat string that link to a target, independently of the style
// runs they may span. An external link goes to a URL or a file, an internal one to the nearest
// cell whose text is the target. S is the string type of the targets.
//
// Invariants (see Valid()): links are sorted, non-overlapping, non-empty and lie within the text.
template<typename S> struct TextLink {
    int start {0};
    int len {0};
    bool internal {false};
    S target;

    int end() const { return start + len; }
    bool SameTarget(const TextLink &o) const {
        return internal == o.internal && target == o.target;
    }
    bool operator==(const TextLink &o) const {
        return start == o.start && len == o.len && SameTarget(o);
    }
};

template<typename S> struct TextLinks {
    std::vector<TextLink<S>> v;

    bool empty() const { return v.empty(); }
    void clear() { v.clear(); }

    bool Valid(int textlen) const {
        auto pos = 0;
        for (auto &l : v) {
            if (l.start < pos || l.len <= 0 || l.end() > textlen) { return false; }
            pos = l.end();
        }
        return true;
    }

    // The link of the character at pos, or nullptr.
    const TextLink<S> *At(int pos) const {
        for (auto &l : v) {
            if (pos < l.start) { break; }
            if (pos < l.end()) { return &l; }
        }
        return nullptr;
    }

    // Text of length n was inserted at pos. Inside a link it becomes part of it, but not at its
    // ends, so typing after a link doesn't extend it, unless `atend`.
    void Insert(int pos, int n, bool atend = false) {
        if (n <= 0) { return; }
        for (auto &l : v) {
            if (l.start < pos && (pos < l.end() || (atend && pos == l.end()))) {
                l.len += n;
            } else if (l.start >= pos) {
                l.start += n;
            }
        }
    }

    // Text [pos, pos + n) was removed.
    void Remove(int pos, int n) {
        if (n <= 0) { return; }
        auto map = [&](int x) { return x <= pos ? x : (x >= pos + n ? x - n : pos); };
        for (auto &l : v) {
            auto s = map(l.start);
            l.len = map(l.end()) - s;
            l.start = s;
        }
        std::erase_if(v, [](const TextLink<S> &l) { return l.len <= 0; });
    }

    // Text [pos, pos + n) was replaced by text of length m, which takes the link of the first
    // replaced character.
    void Replace(int pos, int n, int m) {
        if (n <= 0) {
            Insert(pos, m);
        } else if (m <= 0) {
            Remove(pos, n);
        } else {
            Insert(pos + 1, m, true);
            Remove(pos, 1);
            Remove(pos + m, n - 1);
        }
    }

    // Links [from, to) to `target`, instead of what any of it linked to. An empty target unlinks
    // it.
    void Set(int from, int to, int textlen, bool internal, const S &target) {
        from = std::max(from, 0);
        to = std::min(to, textlen);
        if (from >= to) { return; }
        std::vector<TextLink<S>> out;
        auto add = [&](const TextLink<S> &l) {
            if (l.len <= 0) { return; }
            if (!out.empty() && out.back().end() == l.start && out.back().SameTarget(l)) {
                out.back().len += l.len;
            } else {
                out.push_back(l);
            }
        };
        auto added = target.empty();
        for (auto l : v) {
            if (l.start < from) {
                auto head = l;
                head.len = std::min(l.end(), from) - l.start;
                add(head);
            }
            if (!added && l.end() > from) {
                add({from, to - from, internal, target});
                added = true;
            }
            if (l.end() > to) {
                auto tail = l;
                tail.start = std::max(l.start, to);
                tail.len = l.end() - tail.start;
                add(tail);
            }
        }
        if (!added) { add({from, to - from, internal, target}); }
        v = std::move(out);
    }
};
