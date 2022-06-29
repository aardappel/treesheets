// Copyright 2014 Wouter van Oortmerssen. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef LOBSTER_LEX
#define LOBSTER_LEX

#include "lobster/ttypes.h"

namespace lobster {

struct Line {
    int line;
    int fileidx;

    Line(int _line, int _fileidx) : line(_line), fileidx(_fileidx) {}

    bool operator==(const Line &o) const {
        return line == o.line && fileidx == o.fileidx;
    }
    bool operator<(const Line &o) const {
        return fileidx < o.fileidx || (fileidx == o.fileidx && line < o.line);
    }
};

struct LoadedFile : Line {
    const char *p = nullptr;
    const char *linestart = nullptr;
    const char *tokenstart = nullptr;
    const char *prevtokenstart = nullptr;
    const char *prevtokenend = nullptr;
    shared_ptr<string> source { new string() };
    TType token = T_NONE;
    int tokline = 1;  // line before, if current token crossed a line
    bool islf = false;
    bool cont = false;
    string_view sattr;
    size_t whitespacebefore = 0;

    string sval;
    int64_t ival;
    double fval;

    vector<pair<TType, TType>> bracketstack;
    vector<pair<int, bool>> indentstack;
    const char *prevline = nullptr, *prevlinetok = nullptr;

    struct Tok { TType t; string_view a; };

    vector<Tok> gentokens;

    string filename;

    LoadedFile(string_view fn, vector<string> &fns, string_view stringsource)
        : Line(1, (int)fns.size()) {
        if (!stringsource.empty()) {
            *source.get() = stringsource;
        } else {
            if (LoadFile("modules/" + fn, source.get()) < 0 &&
                LoadFile(fn, source.get()) < 0) {
                THROW_OR_ABORT("can't open file: " + fn);
            }
        }
        prevtokenstart = prevtokenend = tokenstart = linestart = p = source.get()->c_str();

        indentstack.push_back({ 0, false });

        fns.push_back(string(fn));
        filename = fn;
    }
};

struct Lex : LoadedFile {
    vector<LoadedFile> parentfiles;
    set<string, less<>> allfiles;
    vector<shared_ptr<string>> allsources;

    vector<string> &filenames;

    bool do_string_interpolation = true;

    Lex(string_view fn, vector<string> &fns, string_view _ss = {})
        : LoadedFile(fn, fns, _ss), filenames(fns) {
        allsources.push_back(source);
        if (!fn.empty()) allfiles.insert(string(fn));
        FirstToken();
    }

    void FirstToken() {
        Next();
        if (token == T_LINEFEED) Next();
    }

    void Include(string_view _fn, bool do_cycle_check = true) {
        auto cycle_check = [&](const LoadedFile &pf) {
            if (pf.filename == _fn) {
                string err = "cyclic import: ";
                for (auto &ef : parentfiles) append(err, ef.filename, " -> ");
                append(err, filename, " -> ", _fn);
                Error(err);
            }
        };
        if (do_cycle_check) {
            cycle_check(*this);
            for (auto &pf : parentfiles)
                cycle_check(pf);
        }
        if (allfiles.find(_fn) != allfiles.end()) {
            return;
        }
        allfiles.insert(string(_fn));
        parentfiles.push_back(*this);
        *((LoadedFile *)this) = LoadedFile(_fn, filenames, {});
        allsources.push_back(source);
        FirstToken();
    }

    void PopIncludeContinue() {
        *((LoadedFile *)this) = parentfiles.back();
        parentfiles.pop_back();
    }

    void Push(TType t, string_view a = {}) {
        Tok tok;
        tok.t = t;
        if (a.data()) tok.a = a;
        gentokens.push_back(tok);
    }

    void PushCur() { Push(token, sattr); }

    void Undo(TType t, string_view a = {}) {
        PushCur();
        Push(t, a);
        Next();
    }

    void Next() {
        if (gentokens.size()) {
            token = gentokens.back().t;
            sattr = gentokens.back().a;
            gentokens.pop_back();
            return;
        }
        bool lastcont = cont;
        cont = false;
        prevtokenstart = tokenstart;
        prevtokenend = p;
        token = NextToken();
        if (islf && token != T_ENDOFFILE && token != T_ENDOFINCLUDE) {
            int indent = (int)(tokenstart - linestart);
            if (indent > 0) {
                if (prevline)
                    for (const char *indentp = linestart;
                         indentp < tokenstart && prevline < prevlinetok; indentp++, prevline++)
                        if (*indentp != *prevline)
                            Error("adjacent lines do not start with the same sequence of spaces"
                                  " and/or tabs");
                prevline = linestart;
                prevlinetok = tokenstart;
            } else {
                //prevlineindenttype = 0;
                prevlinetok = prevline = nullptr;
            }
            if (lastcont) {
                if (indent < indentstack.back().first)
                    Error("line continuation can't indent less than the previous line");
                if (indent > indentstack.back().first)
                    indentstack.push_back({ indent, true });
                return;
            }
            PushCur();
            tryagain:
            if (indent != indentstack.back().first) {
                if (indent > indentstack.back().first) {
                    indentstack.push_back({ indent, false });
                    Push(T_INDENT);
                } else {
                    bool iscont = false;
                    while (indentstack.back().first > indent) {
                        iscont = indentstack.back().second;
                        indentstack.pop_back();
                        if (!iscont) {
                            Push(T_LINEFEED);
                            Push(T_DEDENT);
                        }
                    }
                    if (iscont) goto tryagain;
                    if (indent != indentstack.back().first) Error("inconsistent dedent");
                }
            } else {
                Push(T_LINEFEED);
            }
            Next();
        }
    }

    void OverrideCont(bool c) { cont = c; }

    void PopBracket(TType c) {
        if (bracketstack.empty())
            Error("unmatched \'" + TokStr(c) + "\'");
        if (bracketstack.back().second != c)
            Error("mismatched \'" + TokStr(c) + "\', expected \'" +
                  TokStr(bracketstack.back().second) + "\'");
        bracketstack.pop_back();
    }

    // The ones from ctype.h assert on negative values, even though char is signed on most
    // platforms?? And they contain a bunch of locale crap we don't care about.
    static bool IsAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');  }
    static bool IsDigit(char c) { return c >= '0' && c <= '9'; }
    static bool IsXDigit(char c) { return IsDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
    static bool IsAlNum(char c) { return IsAlpha(c) || IsDigit(c); }

    TType NextToken() {
        line = tokline;
        islf = false;
        whitespacebefore = 0;
        char c;
        for (;;) switch (tokenstart = p, c = *p++) {
            case '\0':
                p--;
                if (indentstack.size() > 1) {
                    bool iscont = indentstack.back().second;
                    indentstack.pop_back();
                    if (iscont) return NextToken();
                    islf = false; // avoid indents being generated because of this dedent
                    return T_DEDENT;
                } else {
                    if (!bracketstack.empty())
                        Error("unmatched \'" + TokStr(bracketstack.back().first) +
                              "\' at end of file");
                    return parentfiles.empty() ? T_ENDOFFILE : T_ENDOFINCLUDE;
                }

            case '\n': tokline++; islf = bracketstack.empty(); linestart = p; break;
            case ' ': case '\t': case '\r': case '\f': whitespacebefore++; break;

            case '(': bracketstack.push_back({ T_LEFTPAREN, T_RIGHTPAREN }); return T_LEFTPAREN;
            case '[': bracketstack.push_back({ T_LEFTBRACKET, T_RIGHTBRACKET }); return T_LEFTBRACKET;
            case '{': bracketstack.push_back({ T_LEFTCURLY, T_RIGHTCURLY }); return T_LEFTCURLY;
            case ')': PopBracket(T_RIGHTPAREN); return T_RIGHTPAREN;
            case ']': PopBracket(T_RIGHTBRACKET); return T_RIGHTBRACKET;
            case '}':
                if (!bracketstack.empty() && bracketstack.back().first == T_STR_INT_START &&
                    do_string_interpolation) {
                    PopBracket(T_STR_INT_END);
                    return StringConstant(false, true);
                }
                PopBracket(T_RIGHTCURLY); return T_RIGHTCURLY;

            case ';': return T_SEMICOLON;

            case ',': cont = true; return T_COMMA;

            #define secondb(s, t, b) if (*p == s) { p++; b; return t; }
            #define second(s, t) secondb(s, t, {})

            case '+':
                second('+', T_INCR);
                cont = true;
                second('=', T_PLUSEQ);
                return T_PLUS;
            case '-':
                second('-', T_DECR);
                cont = true;
                second('=', T_MINUSEQ);
                second('>', T_RETURNTYPE);
                return T_MINUS;
            case '*':
                cont = true;
                second('=', T_MULTEQ);
                return T_MULT;
            case '%':
                cont = true;
                second('=', T_MODEQ);
                return T_MOD;

            case '<':
                cont = true;
                second('=', T_LTEQ);
                secondb('<', T_ASL, second('=', T_ASLEQ));
                return T_LT;
            case '=':
                cont = true;
                second('=', T_EQ);
                return T_ASSIGN;
            case '!':
                cont = true;
                second('=', T_NEQ);
                Error("use \"not\" instead of !");
            case '>':
                cont = true;
                second('=', T_GTEQ);
                secondb('>', T_ASR, second('=', T_ASREQ));
                return T_GT;

            case '&':
                cont = true;
                second('=', T_ANDEQ);
                secondb('&', T_AND, Error("use \"and\" instead of &&"));
                return T_BITAND;
            case '|':
                cont = true;
                second('=', T_OREQ);
                secondb('|', T_OR, Error("use \"or\" instead of ||"));
                return T_BITOR;
            case '^':
                cont = true;
                second('=', T_XOREQ);
                return T_XOR;
            case '~':
                cont = true;
                return T_NEG;

            case '?': return T_QUESTIONMARK;

            case ':':
                cont = true;
                if (*p == ':') {
                    p++;
                    return T_TYPEIN;
                };
                cont = false;
                return T_COLON;

            case '/':
                cont = true;
                second('=', T_DIVEQ);
                cont = false;
                if (*p == '/') {
                    while (*p != '\n' && *p != '\0') p++;
                    break;
                } else if (*p == '*') {
                    for (;;) {
                        p++;
                        if (*p == '\0') Error("end of file in multi-line comment");
                        if (*p == '\n') tokline++;
                        if (*p == '*' && *(p + 1) == '/') { p += 2; break; }
                    }
                    linestart = p;  // not entirely correct, but best we can do
                    break;
                } else {
                    cont = true;
                    return T_DIV;
                }

            #undef second
            #undef secondb

            case '\"':
            case '\'':
                return StringConstant(c == '\'', false);

            default: {
                if (IsAlpha(c) || c == '_' || c < 0) {
                    while (IsAlNum(*p) || *p == '_' || *p < 0) p++;
                    sattr = string_view(tokenstart, p - tokenstart);
                    switch (sattr[0]) {
                        case 'a':
                            if (sattr == TName(T_AND)) { cont = true; return T_AND; }
                            if (sattr == TName(T_ANYTYPE)) return T_ANYTYPE;
                            break;
                        case 'b':
                            if (sattr == TName(T_BREAK)) return T_BREAK;
                            break;
                        case 'c':
                            if (sattr == TName(T_CLASS)) return T_CLASS;
                            if (sattr == TName(T_CASE)) return T_CASE;
                            break;
                        case 'd':
                            if (sattr == TName(T_FUN)) return T_FUN;
                            if (sattr == TName(T_DEFAULT)) return T_DEFAULT;
                            break;
                        case 'e':
                            if (sattr == TName(T_ELSE)) return T_ELSE;
                            if (sattr == TName(T_ELIF)) return T_ELIF;
                            if (sattr == TName(T_ENUM)) return T_ENUM;
                            if (sattr == TName(T_ENUM_FLAGS)) return T_ENUM_FLAGS;
                            break;
                        case 'f':
                            if (sattr == TName(T_FLOATTYPE)) return T_FLOATTYPE;
                            if (sattr == TName(T_FOR)) return T_FOR;
                            if (sattr == TName(T_LAMBDA)) return T_LAMBDA;
                            if (sattr == TName(T_FROM)) return T_FROM;
                            break;
                        case 'i':
                            if (sattr == TName(T_INTTYPE)) return T_INTTYPE;
                            if (sattr == TName(T_IF)) return T_IF;
                            if (sattr == TName(T_INCLUDE)) return T_INCLUDE;
                            if (sattr == TName(T_IS)) return T_IS;
                            break;
                        case 'l':
                            if (sattr == TName(T_CONST)) return T_CONST;
                            break;
                        case 'n':
                            if (sattr == TName(T_NIL)) return T_NIL;
                            if (sattr == TName(T_NOT)) return T_NOT;
                            if (sattr == TName(T_NAMESPACE)) return T_NAMESPACE;
                            break;
                        case 'o':
                            if (sattr == TName(T_OR)) { cont = true; return T_OR; }
                            if (sattr == TName(T_OPERATOR)) { cont = true; return T_OPERATOR; }
                            break;
                        case 'p':
                            if (sattr == TName(T_PROGRAM)) return T_PROGRAM;
                            if (sattr == TName(T_PRIVATE)) return T_PRIVATE;
                            if (sattr == TName(T_PAKFILE)) return T_PAKFILE;
                            break;
                        case 'r':
                            if (sattr == TName(T_RETURN)) return T_RETURN;
                            if (sattr == TName(T_RESOURCE)) return T_RESOURCE;
                            break;
                        case 's':
                            if (sattr == TName(T_STRUCT)) return T_STRUCT;
                            if (sattr == TName(T_STRTYPE)) return T_STRTYPE;
                            if (sattr == TName(T_SWITCH)) return T_SWITCH;
                            if (sattr == TName(T_SUPER)) return T_SUPER;
                            break;
                        case 't':
                            if (sattr == TName(T_TYPEOF)) return T_TYPEOF;
                            break;
                        case 'v':
                            if (sattr == TName(T_VOIDTYPE)) return T_VOIDTYPE;
                            if (sattr == TName(T_VAR)) return T_VAR;
                            break;
                        case 'w':
                            if (sattr == TName(T_WHILE)) return T_WHILE;
                            break;
                    }
                    return T_IDENT;
                }
                bool isfloat = c == '.' && *p != '.';
                if (IsDigit(c) || (isfloat && IsDigit(*p))) {
                    if (c == '0' && *p == 'x') {
                        p++;
                        while (IsXDigit(*p)) p++;
                        sattr = string_view(tokenstart, p - tokenstart);
                        ival = parse_int<int64_t>(sattr, 16);
                        return T_INT;
                    } else {
                        while (IsDigit(*p)) p++;
                        if (!isfloat && *p == '.' && *(p + 1) != '.' && !IsAlpha(*(p + 1))) {
                            p++;
                            isfloat = true;
                            while (IsDigit(*p)) p++;
                        }
                        if (isfloat && (*p == 'e' || *p == 'E')) {
                            p++;
                            if (*p == '+' || *p == '-') p++;
                            while (IsDigit(*p)) p++;
                        }
                        sattr = string_view(tokenstart, p - tokenstart);
                        if (isfloat) {
                            fval = strtod(sattr.data(), nullptr);
                            return T_FLOAT;
                        } else {
                            ival = parse_int<int64_t>(sattr);
                            return T_INT;
                        }
                    }
                }
                if (c == '.') {
                    if (*p == '.') { p++; return T_DOTDOT; }
                    islf = false;  // Support "builder" APIs.
                    return T_DOT;
                }
                auto tok = c < ' ' || c >= 127
                    ? cat("[ascii ", int(c), "]")
                    : cat("\'", string(1, char(c)), "\'");
                Error("illegal token: " + tok);
                return T_NONE;
            }
        }
    }

    TType StringConstant(bool character_constant, bool interp) {
        sval.clear();
        auto start = p - 1;
        char c = 0;
        // Check if its a multi-line constant.
        if (!interp && !character_constant && p[0] == '\"' && p[1] == '\"') {
            p += 2;
            if (*p == '\r') p++;
            if (*p == '\n') p++;
            for (;;) {
                switch (c = *p++) {
                    case '\0':
                        Error("end of file found in multi-line string constant");
                        break;
                    case '\r':
                        break;
                    case '\"':
                        if (p[0] == '\"' && p[1] == '\"') {
                            p += 2;
                            sattr = string_view(start, p - start);
                            return T_STR;
                        }
                        sval += c;
                        break;
                    case '\n':
                        tokline++;
                        sval += c;
                        break;
                    default:
                        sval += c;
                        break;
                }
            }
        }
        // Regular string or character constant.
        for(;;) switch (c = *p++) {
            case 0:
            case '\r':
            case '\n':
                p--;
                Error("end of line found in string constant");
                break;
            case '\'':
                if (!character_constant)
                    Error("\' should be prefixed with a \\ in a string constant");
                sattr = string_view(start, p - start);
                if (sval.size() > 4) Error("character constant too long");
                ival = 0;
                for (auto c : sval) ival = (ival << 8) + c;
                return T_INT;
            case '\"':
                if (character_constant)
                    Error("\" should be prefixed with a \\ in a character constant");
                sattr = string_view(start, p - start);
                return interp ? T_STR_INT_END : T_STR;
            case '\\': {
                auto HexDigit = [](char c) -> char {
                    if (IsDigit(c)) return c - '0';
                    assert(IsXDigit(c));
                    return c - (c < 'a' ? 'A' : 'a') + 10;
                };
                switch (c = *p++) {
                    case 'n': c = '\n'; break;
                    case 't': c = '\t'; break;
                    case 'r': c = '\r'; break;
                    case '\\':
                    case '\"':
                    case '\'':
                    case '{':
                    case '}':
                        break;
                    case 'x':
                        if (!IsXDigit(*p) || !IsXDigit(p[1]))
                            Error("illegal hexadecimal escape code in string constant");
                        c = HexDigit(*p++) << 4;
                        c |= HexDigit(*p++);
                        break;
                    case 'u': {
                        if (!IsXDigit(*p) || !IsXDigit(p[1]) || !IsXDigit(p[2]) || !IsXDigit(p[3]))
                            Error("illegal unicode escape code in string constant");
                        int i = HexDigit(*p++) << 12;
                        i |= HexDigit(*p++) << 8;
                        i |= HexDigit(*p++) << 4;
                        i |= HexDigit(*p++);
                        char buf[7];
                        ToUTF8(i, buf);
                        sval += buf;
                        continue;
                    }
                    default:
                        p--;
                        Error("unknown control code in string constant");
                };
                sval += c;
                break;
            }
            case '{':
                if (!do_string_interpolation || character_constant) {
                    sval += c;
                } else if (*p == '{') {  // Escaped.
                    sval += c;
                    p++;
                } else if (*p == '\"') {
                    // Special purpose error for the common case of "{".
                    Error("{ in string constant must be escaped as {{");
                } else {
                    sattr = string_view(start, p - start);
                    bracketstack.push_back({ T_STR_INT_START, T_STR_INT_END });
                    return interp ? T_STR_INT_MIDDLE : T_STR_INT_START;
                }
                break;
            case '}':
                if (!do_string_interpolation || character_constant) {
                    sval += c;
                } else if (*p == '}') {  // Escaped.
                    sval += c;
                    p++;
                } else {
                    Error("} in string constant must be escaped as }}");
                }
                break;
            default:
                // Allow UTF-8 chars.
                if ((c >= 0 && c < ' ') || c == 127)
                    Error("unprintable character in string constant");
                sval += c;
        };
    };

    string_view TokStr(TType t) {
        return TName(t);
    }

    string_view TokStr() {
        switch (token) {
            case T_IDENT:
            case T_FLOAT:
            case T_INT:
            case T_STR:
                return sattr;
            default:
                return TName(token);
        }
    }

    string Location(const Line &ln) {
        return cat(filenames[ln.fileidx], "(", ln.line, ")");
    }

    void Error(string_view msg, const Line *ln = nullptr) {
        auto err = Location(ln ? *ln : *this) + ": error: " + msg;
        if (!ln) {
            auto begin = prevtokenstart;
            auto end = prevtokenend;
            while (begin > source.get()->c_str() && *(begin - 1) != '\n') begin--;
            while (*end && *end != '\n' && *end != '\r') end++;
            if (end - begin > 0) {
                append(err, "\nin: ", string_view(begin, end - begin));
                if (prevtokenend - prevtokenstart > 0) {
                    append(err, "\nat: ");
                    for (; begin < prevtokenstart; begin++) err.push_back(' ');
                    for (; begin < prevtokenend; begin++) err.push_back('^');
                }
            }
        }
        //LOG_DEBUG(err);
         THROW_OR_ABORT(err);
    }

    void Warn(string_view msg, const Line *ln = nullptr) {
        LOG_WARN(Location(ln ? *ln : *this) + ": warning: " + msg);
    }
};

}  // namespace lobster

#endif  // LOBSTER_LEX
