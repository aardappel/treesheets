// Bidirectional text: a simplified Unicode Bidirectional Algorithm (UAX #9), enough to lay out
// a cell's text with the right base direction and to place the cursor in it. Deliberately free
// of wx dependencies so it can be unit tested on its own.
//
// Simplifications: explicit embeddings, overrides and isolates in the text are ignored (their
// control characters count as boundary neutrals), and character classes are approximated by
// ranges, which is exact for Latin, Hebrew and Arabic.

#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace bidi {

enum Class : std::uint8_t { L, R, AL, EN, ES, ET, AN, CS, NSM, BN, B, S, WS, ON };

inline Class Classify(unsigned c) {
    if (c < 0x80) {
        if ((c | 0x20) >= 'a' && (c | 0x20) <= 'z') { return L; }
        if (c >= '0' && c <= '9') { return EN; }
        switch (c) {
            case '+':
            case '-': return ES;
            case '#':
            case '$':
            case '%': return ET;
            case ',':
            case '.':
            case '/':
            case ':': return CS;
            case '\t':
            case 0x0B:
            case 0x1F: return S;
            case '\n':
            case '\r':
            case 0x1C:
            case 0x1D:
            case 0x1E: return B;
            case ' ':
            case 0x0C: return WS;
        }
        return c < 0x20 || c == 0x7F ? BN : ON;
    }
    if (c < 0xC0) {
        if (c == 0x85) { return B; }
        if (c < 0xA0 || c == 0xAD) { return BN; }
        if (c == 0xA0) { return CS; }
        if ((c >= 0xA2 && c <= 0xA5) || c == 0xB0 || c == 0xB1) { return ET; }
        if (c == 0xB2 || c == 0xB3 || c == 0xB9) { return EN; }
        if (c == 0xAA || c == 0xB5 || c == 0xBA) { return L; }
        return ON;
    }
    if (c == 0xD7 || c == 0xF7) { return ON; }
    if ((c >= 0x0300 && c <= 0x036F) || (c >= 0x0483 && c <= 0x0489)) { return NSM; }
    if (c >= 0x0590 && c <= 0x05FF) {  // Hebrew
        if ((c >= 0x0591 && c <= 0x05BD) || c == 0x05BF || c == 0x05C1 || c == 0x05C2 ||
            c == 0x05C4 || c == 0x05C5 || c == 0x05C7) {
            return NSM;
        }
        return R;
    }
    if (c >= 0x0600 && c <= 0x06FF) {  // Arabic
        if (c <= 0x0605 || (c >= 0x0660 && c <= 0x0669) || c == 0x066B || c == 0x066C ||
            c == 0x06DD) {
            return AN;
        }
        if (c == 0x0609 || c == 0x060A || c == 0x066A) { return ET; }
        if (c == 0x060C) { return CS; }
        if (c == 0x060E || c == 0x060F || c == 0x06DE || c == 0x06E9) { return ON; }
        if ((c >= 0x0610 && c <= 0x061A) || (c >= 0x064B && c <= 0x065F) || c == 0x0670 ||
            (c >= 0x06D6 && c <= 0x06DC) || (c >= 0x06DF && c <= 0x06E4) || c == 0x06E7 ||
            c == 0x06E8 || (c >= 0x06EA && c <= 0x06ED)) {
            return NSM;
        }
        if (c >= 0x06F0 && c <= 0x06F9) { return EN; }
        return AL;
    }
    if (c >= 0x0700 && c <= 0x08FF) {  // Syriac, Arabic Supplement, Thaana, N'Ko, ...
        if (c == 0x0711 || (c >= 0x0730 && c <= 0x074A) || (c >= 0x07A6 && c <= 0x07B0) ||
            (c >= 0x07EB && c <= 0x07F3) || c == 0x07FD || (c >= 0x0816 && c <= 0x082D) ||
            (c >= 0x0859 && c <= 0x085B) || (c >= 0x0898 && c <= 0x089F) ||
            (c >= 0x08CA && c <= 0x08FF && c != 0x08E2)) {
            return NSM;
        }
        if (c == 0x08E2) { return AN; }
        if (c >= 0x07C0 && c <= 0x085F) { return R; }  // N'Ko, Samaritan, Mandaic
        return AL;
    }
    if ((c >= 0x1AB0 && c <= 0x1AFF) || (c >= 0x1DC0 && c <= 0x1DFF) ||
        (c >= 0x20D0 && c <= 0x20FF) || (c >= 0xFE00 && c <= 0xFE0F) ||
        (c >= 0xFE20 && c <= 0xFE2F) || (c >= 0xE0100 && c <= 0xE01EF)) {
        return NSM;
    }
    if (c >= 0x2000 && c <= 0x2BFF) {
        if (c <= 0x200A || c == 0x2028 || c == 0x205F) { return WS; }
        if (c == 0x200E) { return L; }   // left-to-right mark
        if (c == 0x200F) { return R; }   // right-to-left mark
        if (c == 0x2029) { return B; }
        if (c <= 0x200D || (c >= 0x202A && c <= 0x202E) || (c >= 0x2060 && c <= 0x206F)) {
            return BN;  // including the explicit formatting characters, see above
        }
        if ((c >= 0x2030 && c <= 0x2034) || (c >= 0x20A0 && c <= 0x20CF) || c == 0x2212 ||
            c == 0x2213) {
            return c == 0x2212 ? ES : ET;
        }
        if (c == 0x2070 || (c >= 0x2074 && c <= 0x2079) || (c >= 0x2080 && c <= 0x2089)) {
            return EN;
        }
        if (c == 0x207A || c == 0x207B || c == 0x208A || c == 0x208B) { return ES; }
        if (c == 0x2044) { return CS; }
        return c >= 0x2100 && c <= 0x214F ? L : ON;  // most letterlike symbols are letters
    }
    if (c == 0x3000) { return WS; }
    if (c >= 0x3001 && c <= 0x3004) { return ON; }
    if (c >= 0x3008 && c <= 0x3020) { return ON; }
    if (c >= 0xD800 && c <= 0xDFFF) { return L; }  // lone surrogate
    if (c >= 0xFB1D && c <= 0xFB4F) {  // Hebrew presentation forms
        if (c == 0xFB1E) { return NSM; }
        return c == 0xFB29 ? ES : R;
    }
    if (c >= 0xFB50 && c <= 0xFDFF) { return c == 0xFD3E || c == 0xFD3F ? ON : AL; }
    if (c >= 0xFE50 && c <= 0xFE6F) { return ON; }
    if (c >= 0xFE70 && c <= 0xFEFE) { return AL; }
    if (c == 0xFEFF) { return BN; }
    if (c >= 0xFF01 && c <= 0xFF20) {  // fullwidth ASCII punctuation and digits
        if (c >= 0xFF10 && c <= 0xFF19) { return EN; }
        if (c == 0xFF0B || c == 0xFF0D) { return ES; }
        if (c >= 0xFF03 && c <= 0xFF05) { return ET; }
        if (c == 0xFF0C || c == 0xFF0E || c == 0xFF0F || c == 0xFF1A) { return CS; }
        return ON;
    }
    if (c >= 0xFFF9 && c <= 0xFFFD) { return ON; }
    if (c >= 0x10800 && c <= 0x10FFF) {
        return c >= 0x10D00 && c <= 0x10D3F   ? AL
               : c >= 0x10E60 && c <= 0x10E7E ? AN
                                              : R;
    }
    if (c >= 0x1E800 && c <= 0x1EFFF) { return c >= 0x1EC70 && c <= 0x1EEFF ? AL : R; }
    if (c >= 0x1F000 && c <= 0x1FAFF) {  // emoji and other symbols
        return c >= 0x1F100 && c <= 0x1F10A ? EN : ON;
    }
    if (c >= 0xE0000 && c <= 0xE007F) { return BN; }
    return L;
}

// The mirror image of a character that is drawn mirrored in right-to-left text (rule L4), or the
// character itself. Only the common pairs.
inline unsigned Mirror(unsigned c) {
    static const unsigned pairs[][2] = {
        {'(', ')'},       {'<', '>'},       {'[', ']'},       {'{', '}'},       {0xAB, 0xBB},
        {0x2039, 0x203A}, {0x2045, 0x2046}, {0x207D, 0x207E}, {0x208D, 0x208E}, {0x2264, 0x2265},
        {0x2308, 0x2309}, {0x230A, 0x230B}, {0x2329, 0x232A}, {0x27E6, 0x27E7}, {0x27E8, 0x27E9},
        {0x3008, 0x3009}, {0x300A, 0x300B}, {0x300C, 0x300D}, {0x300E, 0x300F}, {0x3010, 0x3011},
        {0xFF08, 0xFF09}, {0xFF1C, 0xFF1E}, {0xFF3B, 0xFF3D}, {0xFF5B, 0xFF5D},
    };
    for (const auto &p : pairs) {
        if (c == p[0]) { return p[1]; }
        if (c == p[1]) { return p[0]; }
    }
    return c;
}

// Calls f(unit, length, codepoint) for each character of the code units [0, len), combining
// UTF-16 surrogate pairs where the units are UTF-16, until f returns false.
template<typename Get, typename F> void ForEachCodePoint(Get get, int len, F f) {
    for (auto i = 0; i < len;) {
        auto c = static_cast<unsigned>(get(i));
        auto n = 1;
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < len) {
            auto lo = static_cast<unsigned>(get(i + 1));
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
                n = 2;
            }
        }
        if (!f(i, n, c)) { return; }
        i += n;
    }
}

// The class of each code unit. Both units of a surrogate pair get the class of their character.
template<typename Get> std::vector<Class> Classes(Get get, int len) {
    std::vector<Class> classes(len);
    ForEachCodePoint(get, len, [&](int i, int n, unsigned c) {
        std::fill_n(classes.begin() + i, n, Classify(c));
        return true;
    });
    return classes;
}

// Whether the paragraph is right-to-left: whether its first letter is (rule P2, like HTML's
// dir="auto").
inline bool IsRightToLeft(const std::vector<Class> &classes) {
    for (auto c : classes) {
        if (c == L) { return false; }
        if (c == R || c == AL) { return true; }
    }
    return false;
}

// The embedding level of each code unit of a paragraph with the given classes and base level
// (0: left-to-right, 1: right-to-left), resolved by the weak (W1-W7), neutral (N1-N2) and
// implicit (I1-I2) rules.
inline std::vector<std::uint8_t> ResolveLevels(std::vector<Class> t, int base) {
    auto n = static_cast<int>(t.size());
    auto e = base & 1 ? R : L;  // embedding direction, also the start and end of sequence type
    // W1: marks (and the boundary neutrals, which X9 would remove) take the class before them.
    for (auto i = 0; i < n; i++) {
        if (t[i] == NSM || t[i] == BN) { t[i] = i == 0 ? e : t[i - 1]; }
    }
    // W2, W3: European digits after Arabic letters are Arabic digits; Arabic letters are R.
    auto strong = e;
    for (auto &c : t) {
        if (c == L || c == R || c == AL) { strong = c; }
        if (c == EN && strong == AL) { c = AN; }
    }
    for (auto &c : t) {
        if (c == AL) { c = R; }
    }
    // W4: a single separator between two numbers of the same kind joins them.
    for (auto i = 1; i + 1 < n; i++) {
        if (t[i - 1] == EN && t[i + 1] == EN && (t[i] == ES || t[i] == CS)) {
            t[i] = EN;
        } else if (t[i - 1] == AN && t[i + 1] == AN && t[i] == CS) {
            t[i] = AN;
        }
    }
    // W5: terminators next to European numbers ("$", "%") belong to them.
    for (auto i = 0; i < n;) {
        if (t[i] != ET) {
            i++;
            continue;
        }
        auto j = i;
        while (j < n && t[j] == ET) { j++; }
        if ((i > 0 && t[i - 1] == EN) || (j < n && t[j] == EN)) {
            std::fill(t.begin() + i, t.begin() + j, EN);
        }
        i = j;
    }
    // W6: the remaining separators and terminators are neutral.
    for (auto &c : t) {
        if (c == ES || c == ET || c == CS) { c = ON; }
    }
    // W7: European numbers in left-to-right text are left-to-right.
    strong = e;
    for (auto &c : t) {
        if (c == L || c == R) { strong = c; }
        if (c == EN && strong == L) { c = L; }
    }
    // N1, N2: neutrals between two runs of the same direction take it (numbers count as
    // right-to-left here), all other neutrals the embedding direction.
    auto direction = [&](int i) { return i < 0 || i >= n ? e : t[i] == L ? L : R; };
    for (auto i = 0; i < n;) {
        if (t[i] != B && t[i] != S && t[i] != WS && t[i] != ON) {
            i++;
            continue;
        }
        auto j = i;
        while (j < n && (t[j] == B || t[j] == S || t[j] == WS || t[j] == ON)) { j++; }
        auto before = direction(i - 1);
        std::fill(t.begin() + i, t.begin() + j, before == direction(j) ? before : e);
        i = j;
    }
    // I1, I2
    std::vector<std::uint8_t> levels(n);
    for (auto i = 0; i < n; i++) {
        auto c = t[i];
        auto level = base;
        if (base & 1) {
            if (c == L || c == EN || c == AN) { level++; }
        } else {
            if (c == R) {
                level++;
            } else if (c == AN || c == EN) {
                level += 2;
            }
        }
        levels[i] = static_cast<std::uint8_t>(level);
    }
    return levels;
}

struct Run {
    int start {0};
    int len {0};
    int level {0};

    bool RightToLeft() const { return (level & 1) != 0; }
};

// The line [start, end) of a paragraph with the given original classes and resolved levels, as
// runs of one level each, in visual order from left to right (rules L1 and L2). The characters
// within a right-to-left run go from right to left.
inline std::vector<Run> VisualRuns(const std::vector<Class> &classes,
                                   const std::vector<std::uint8_t> &levels, int base, int start,
                                   int end) {
    std::vector<std::uint8_t> lv(levels.begin() + start, levels.begin() + end);
    // L1: separators, and whitespace before them or at the end of the line, get the base level.
    auto trailing = true;
    for (auto i = end - start - 1; i >= 0; i--) {
        auto c = classes[start + i];
        if (c == S || c == B) {
            lv[i] = static_cast<std::uint8_t>(base);
            trailing = true;
        } else if (trailing && (c == WS || c == BN)) {
            lv[i] = static_cast<std::uint8_t>(base);
        } else {
            trailing = false;
        }
    }
    std::vector<Run> runs;
    for (auto i = 0; i < static_cast<int>(lv.size()); i++) {
        if (runs.empty() || runs.back().level != lv[i]) {
            runs.push_back({start + i, 0, lv[i]});
        }
        runs.back().len++;
    }
    // L2: from the highest level down to the lowest odd one, reverse every sequence of runs at
    // that level or higher.
    auto highest = 0;
    auto lowestodd = 255;
    for (auto &r : runs) {
        highest = std::max(highest, r.level);
        if (r.level & 1) { lowestodd = std::min(lowestodd, r.level); }
    }
    for (auto level = highest; level >= lowestodd; level--) {
        for (auto i = 0; i < static_cast<int>(runs.size());) {
            if (runs[i].level < level) {
                i++;
                continue;
            }
            auto j = i;
            while (j < static_cast<int>(runs.size()) && runs[j].level >= level) { j++; }
            std::reverse(runs.begin() + i, runs.begin() + j);
            i = j;
        }
    }
    return runs;
}

}  // namespace bidi
