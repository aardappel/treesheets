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

    bool operator==(const Line &o) const { return line == o.line && fileidx == o.fileidx; }
};

struct LoadedFile : Line {
    const char *p, *linestart, *tokenstart;
    shared_ptr<string> source;
    TType token;
    int errorline;  // line before, if current token crossed a line
    bool islf;
    bool cont;
    string_view sattr;
    size_t whitespacebefore;

    vector<pair<char, char>> bracketstack;
    vector<pair<int, bool>> indentstack;
    const char *prevline, *prevlinetok;

    struct Tok { TType t; string_view a; };

    vector<Tok> gentokens;

    LoadedFile(string_view fn, vector<string> &fns, const char *stringsource)
        : Line(1, (int)fns.size()), tokenstart(nullptr),
          source(new string()), token(T_NONE),
          errorline(1), islf(false), cont(false), whitespacebefore(0),
          prevline(nullptr), prevlinetok(nullptr) {
        if (stringsource) {
            *source.get() = stringsource;
        } else {
            if (LoadFile("include/" + fn, source.get()) < 0 &&
                LoadFile(fn, source.get()) < 0) {
                THROW_OR_ABORT("can't open file: " + fn);
            }
        }
        linestart = p = source.get()->c_str();

        indentstack.push_back({ 0, false });

        fns.push_back(string(fn));
    }
};

struct Lex : LoadedFile {
    vector<LoadedFile> parentfiles;
    set<string, less<>> allfiles;
    vector<shared_ptr<string>> allsources;

    vector<string> &filenames;

    Lex(string_view fn, vector<string> &fns, const char *_ss = nullptr)
        : LoadedFile(fn, fns, _ss), filenames(fns) {
        allsources.push_back(source);
        FirstToken();
    }

    void FirstToken() {
        Next();
        if (token == T_LINEFEED) Next();
    }

    void Include(string_view _fn) {
        if (allfiles.find(_fn) != allfiles.end()) {
            return;
        }
        allfiles.insert(string(_fn));
        parentfiles.push_back(*this);
        *((LoadedFile *)this) = LoadedFile(_fn, filenames, nullptr);
        allsources.push_back(source);
        FirstToken();
    }

    void PopIncludeContinue() {
        *((LoadedFile *)this) = parentfiles.back();
        parentfiles.pop_back();
    }

    void Push(TType t, string_view a = string_view()) {
        Tok tok;
        tok.t = t;
        if (a.data()) tok.a = a;
        gentokens.push_back(tok);
    }

    void PushCur() { Push(token, sattr); }

    void Undo(TType t, string_view a = string_view()) {
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

    void PopBracket(char c) {
        if (bracketstack.empty())
            Error(string("unmatched \'") + c + "\'");
        if (bracketstack.back().second != c)
            Error(string("mismatched \'") + c + "\', expected \'" + bracketstack.back().second +
                  "\'");
        bracketstack.pop_back();
    }

    TType NextToken() {
        errorline = line;
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
                    if (bracketstack.size())
                        Error(string("unmatched \'") + bracketstack.back().first +
                              "\' at end of file");
                    return parentfiles.empty() ? T_ENDOFFILE : T_ENDOFINCLUDE;
                }

            case '\n': line++; islf = bracketstack.empty(); linestart = p; break;
            case ' ': case '\t': case '\r': case '\f': whitespacebefore++; break;

            case '(': bracketstack.push_back({ c, ')' }); return T_LEFTPAREN;
            case '[': bracketstack.push_back({ c, ']' }); return T_LEFTBRACKET;
            case '{': bracketstack.push_back({ c, '}' }); return T_LEFTCURLY;
            case ')': PopBracket(c); return T_RIGHTPAREN;
            case ']': PopBracket(c); return T_RIGHTBRACKET;
            case '}': PopBracket(c); return T_RIGHTCURLY;

            case ';': return T_SEMICOLON;

            case ',': cont = true; return T_COMMA;

            #define secondb(s, t, b) if (*p == s) { p++; b; return t; }
            #define second(s, t) secondb(s, t, {})

            case '+': second('+', T_INCR); cont = true; second('=', T_PLUSEQ); return T_PLUS;
            case '-': second('-', T_DECR); cont = true; second('=', T_MINUSEQ);
                                                        second('>', T_CODOT); return T_MINUS;
            case '*':                      cont = true; second('=', T_MULTEQ); return T_MULT;
            case '%':                      cont = true; second('=', T_MODEQ); return T_MOD;

            case '<': cont = true; second('=', T_LTEQ); second('<', T_ASL);
                                                        second('-', T_DYNASSIGN); return T_LT;
            case '=': cont = true; second('=', T_EQ);   return T_ASSIGN;
            case '!': cont = true; second('=', T_NEQ);  cont = false; return T_NOT;
            case '>': cont = true; second('=', T_GTEQ); second('>', T_ASR); return T_GT;

            case '&': cont = true; second('&', T_AND); return T_BITAND;
            case '|': cont = true; second('|', T_OR);  return T_BITOR;
            case '^': cont = true; return T_XOR;
            case '~': cont = true; return T_NEG;

            case '?': cont = true; second('=', T_LOGASSIGN); second('.', T_DOTMAYBE); cont = false;
                      return T_QUESTIONMARK;

            case ':':
                cont = true;
                secondb('=', T_DEF, second('=', T_DEFCONST));
                if (*p == ':') {
                    p++;
                    second('=', T_DEFTYPEIN);
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
                        if (*p == '\n') line++;
                        if (*p == '*' && *(p + 1) == '/') { p += 2; break; }
                    }
                    linestart = p;  // not entirely correct, but best we can do
                    break;
                } else {
                    cont = true;
                    return T_DIV;
                }

            case '\"':
            case '\'':
                return SkipString(c);

            default: {
                if (isalpha(c) || c == '_' || c < 0) {
                    while (isalnum(*p) || *p == '_' || *p < 0) p++;
                    sattr = string_view(tokenstart, p - tokenstart);
                    if      (sattr == "nil")       return T_NIL;
                    else if (sattr == "true")      { sattr = "1"; return T_INT; }
                    else if (sattr == "false")     { sattr = "0"; return T_INT; }
                    else if (sattr == "return")    return T_RETURN;
                    else if (sattr == "struct")    return T_STRUCT;
                    else if (sattr == "value")     return T_VALUE;
                    else if (sattr == "include")   return T_INCLUDE;
                    else if (sattr == "int")       return T_INTTYPE;
                    else if (sattr == "float")     return T_FLOATTYPE;
                    else if (sattr == "string")    return T_STRTYPE;
                    else if (sattr == "any")       return T_ANYTYPE;
                    else if (sattr == "def")       return T_FUN;
                    else if (sattr == "is")        return T_IS;
                    else if (sattr == "from")      return T_FROM;
                    else if (sattr == "program")   return T_PROGRAM;
                    else if (sattr == "private")   return T_PRIVATE;
                    else if (sattr == "coroutine") return T_COROUTINE;
                    else if (sattr == "resource")  return T_RESOURCE;
                    else if (sattr == "enum")      return T_ENUM;
                    else if (sattr == "typeof")    return T_TYPEOF;
                    else if (sattr == "var")       return T_VAR;
                    else if (sattr == "let")       return T_CONST;
                    else if (sattr == "pakfile")   return T_PAKFILE;
                    else if (sattr == "switch")    return T_SWITCH;
                    else if (sattr == "case")      return T_CASE;
                    else if (sattr == "default")   return T_DEFAULT;
                    else if (sattr == "not")       return T_NOT;
                    else if (sattr == "and")       { cont = true; return T_AND; }
                    else if (sattr == "or")        { cont = true; return T_OR; }
                    else return T_IDENT;
                }
                bool isfloat = c == '.' && *p != '.';
                if (isdigit(c) || (isfloat && isdigit(*p))) {
                    if (c == '0' && *p == 'x') {
                        p++;
                        while (isxdigit(*p)) p++;
                        sattr = string_view(tokenstart, p - tokenstart);
                        return T_INT;
                    } else {
                        for (;;) {
                            auto isdot = *p == '.' && *(p + 1) != '.' && !isalpha(*(p + 1));
                            if (isdot) isfloat = true;
                            if (!isdigit(*p) && !isdot) break;
                            p++;
                        }
                        sattr = string_view(tokenstart, p - tokenstart);
                        return isfloat ? T_FLOAT : T_INT;
                    }
                }
                if (c == '.') {
                    if (*p == '.') { p++; return T_DOTDOT; }
                    return T_DOT;
                }
                auto tok = c <= ' ' ? cat("[ascii ", int(c), "]") : cat(int(c));
                Error("illegal token: " + tok);
                return T_NONE;
            }
        }
    }

    char HexDigit(char c) {
        if (isdigit(c)) return c - '0';
        if (isxdigit(c)) return c - (c < 'a' ? 'A' : 'a') + 10;
        return -1;
    }

    TType SkipString(char initial) {
        auto start = p - 1;
        char c = 0;
        // Check if its a multi-line constant.
        if (initial == '\"' && p[0] == '\"' && p[1] == '\"') {
            p += 2;
            for (;;) {
                switch (c = *p++) {
                    case '\0':
                        Error("end of file found in multi-line string constant");
                        break;
                    case '\n':
                        line++;
                        break;
                    case '\"':
                        if (p[0] == '\"' && p[1] == '\"') {
                            p += 2;
                            sattr = string_view(start, p - start);
                            return T_STR;
                        }
                }
            }
        }
        // Regular string or character constant.
        while ((c = *p++) != initial) switch (c) {
            case 0:
            case '\r':
            case '\n':
                p--;
                Error("end of line found in string constant");
            case '\'':
            case '\"':
                Error("\' and \" should be prefixed with a \\ in a string constant");
            case '\\':
                switch(*p) {
                    case '\\':
                    case '\"':
                    case '\'': p++;
                };
                break;
            default:
                if (c < ' ') Error("unprintable character in string constant");
        };
        sattr = string_view(start, p - start);
        return initial == '\"' ? T_STR : T_INT;
    }

    int64_t IntVal() {
        if (sattr[0] == '\'') {
            auto s = StringVal();
            if (s.size() > 4) Error("character constant too long");
            int64_t ival = 0;
            for (auto c : s) ival = (ival << 8) + c;
            return ival;
        } else if (sattr[0] == '0' && sattr.size() > 1 && sattr[1] == 'x') {
            // Test for hex explicitly since we don't want to allow octal parsing.
            return strtoll(sattr.data(), nullptr, 16);
        } else {
            return strtoll(sattr.data(), nullptr, 10);
        }
    }

    string StringVal() {
        auto s = sattr.data();
        auto initial = *s++;
        // Check if its a multi-line constant.
        if (initial == '\"' && s[0] == '\"' && s[1] == '\"') {
            return string(s + 2, sattr.data() + sattr.size() - 3);
        }
        // Regular string or character constant.
        string r;
        char c = 0;
        while ((c = *s++) != initial) switch (c) {
            case '\\':
                switch(c = *s++) {
                    case 'n': c = '\n'; break;
                    case 't': c = '\t'; break;
                    case 'r': c = '\r'; break;
                    case '\\':
                    case '\"':
                    case '\'': break;
                    case 'x':
                        if (!isxdigit(*s) || !isxdigit(s[1]))
                            Error("illegal hexadecimal escape code in string constant");
                        c = HexDigit(*s++) << 4;
                        c |= HexDigit(*s++);
                        break;
                    default:
                        s--;
                        Error("unknown control code in string constant");
                };
                r += c;
                break;
            default:
                r += c;
        };
        return r;
    };

    string_view TokStr(TType t = T_NONE) {
        if (t == T_NONE) t = token;
        switch (t) {
            case T_IDENT:
            case T_FLOAT:
            case T_INT:
            case T_STR:
                return sattr;
            default:
                return TName(t);
        }
    }

    string Location(const Line &ln) {
        return cat(filenames[ln.fileidx], "(", ln.line, ")");
    }

    void Error(string_view msg, const Line *ln = nullptr) {
        auto err = Location(ln ? *ln : Line(errorline, fileidx)) + ": error: " + msg;
        //Output(OUTPUT_DEBUG, err);
        THROW_OR_ABORT(err);
    }
};

}  // namespace lobster

#endif  // LOBSTER_LEX
