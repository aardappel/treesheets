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

namespace lobster {

struct Line {
    int line;
    int fileidx;

    Line(int _line, int _fileidx) : line(_line), fileidx(_fileidx) {}

    bool operator==(const Line &o) const { return line == o.line && fileidx == o.fileidx; }
};

struct LoadedFile : Line {
    const char *p, *linestart, *tokenstart, *stringsource;
    shared_ptr<string> source;
    TType token;
    int errorline;  // line before, if current token crossed a line
    bool islf;
    bool cont;
    string sattr;
    size_t whitespacebefore;

    vector<pair<char, char>> bracketstack;
    vector<pair<int, bool>> indentstack;
    const char *prevline, *prevlinetok;

    struct Tok { TType t; string a; };

    vector<Tok> gentokens;

    LoadedFile(const char *fn, vector<string> &fns, const char *_ss)
        : Line(1, (int)fns.size()), tokenstart(nullptr), stringsource(_ss),
          source(new string()), token(T_NONE),
          errorline(1), islf(false), cont(false), whitespacebefore(0),
          prevline(nullptr), prevlinetok(nullptr) {
        if (stringsource) {
            linestart = p = stringsource;
        } else {
            if (LoadFile((string("include/") + fn).c_str(), source.get()) < 0 &&
                LoadFile(fn, source.get()) < 0) {
                throw string("can't open file: ") + fn;
            }
            linestart = p = source.get()->c_str();
        }

        indentstack.push_back(make_pair(0, false));

        fns.push_back(fn);
    }
};

struct Lex : LoadedFile {
    vector<LoadedFile> parentfiles;
    set<string, less<string>> allfiles;

    vector<string> &filenames;

    Lex(const char *fn, vector<string> &fns, const char *_ss = nullptr)
        : LoadedFile(fn, fns, _ss), filenames(fns) {
        FirstToken();
    }

    void FirstToken() {
        Next();
        if (token == T_LINEFEED) Next();
    }

    void Include(const char *_fn) {
        if (allfiles.find(_fn) != allfiles.end()) {
            return;
        }
        allfiles.insert(_fn);
        parentfiles.push_back(*this);
        *((LoadedFile *)this) = LoadedFile(_fn, filenames, nullptr);
        FirstToken();
    }

    void PopIncludeContinue() {
        *((LoadedFile *)this) = parentfiles.back();
        parentfiles.pop_back();
    }

    void Push(TType t, const string &a = string()) {
        Tok tok;
        tok.t = t;
        tok.a = a;
        gentokens.push_back(tok);
    }

    void PushCur() { Push(token, sattr); }

    void Undo(TType t, const string &a = string()) {
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
                    indentstack.push_back(make_pair(indent, true));
                return;
            }
            PushCur();
            tryagain:
            if (indent != indentstack.back().first) {
                if (indent > indentstack.back().first) {
                    indentstack.push_back(make_pair(indent, false));
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

            case '(': bracketstack.push_back(make_pair(c, ')')); return T_LEFTPAREN;
            case '[': bracketstack.push_back(make_pair(c, ']')); return T_LEFTBRACKET;
            case '{': bracketstack.push_back(make_pair(c, '}')); return T_LEFTCURLY;
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
                return LexString(c);

            default: {
                if (isalpha(c) || c == '_' || c < 0) {
                    while (isalnum(*p) || *p == '_' || *p < 0) p++;
                    sattr = string(tokenstart, p);
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
                    else if (sattr == "const")     return T_CONST;
                    else if (sattr == "pakfile")   return T_PAKFILE;
                    else if (sattr == "not")       return T_NOT;
                    else if (sattr == "and")       { cont = true; return T_AND; }
                    else if (sattr == "or")        { cont = true; return T_OR; }
                    else return T_IDENT;
                }
                if (isdigit(c) || (c == '.' && isdigit(*p))) {
                    if (c == '0' && *p == 'x') {
                        p++;
                        int val = 0;
                        while (isxdigit(*p)) val = (val << 4) | HexDigit(*p++);
                        sattr = to_string(val);
                        return T_INT;
                    }
                    while (isdigit(*p) || (*p=='.' && !isalpha(*(p + 1)))) p++;
                    sattr = string(tokenstart, p);
                    return strchr(sattr.c_str(), '.') ? T_FLOAT : T_INT;
                }
                if (c == '.') return T_DOT;
                auto tok = c <= ' ' ? "[ascii " + to_string(c) + "]" : string("") + c;
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

    TType LexString(int initial) {
        char c = 0;
        sattr = "";
        // Check if its a multi-line constant.
        if (initial == '\"' && p[0] == '\"' && p[1] == '\"') {
            p += 2;
            for (;;) {
                switch (c = *p++) {
                    case '\0':
                        Error("end of file found in multi-line string constant");
                        break;
                    case '\r':  // Don't want these in our string constants.
                        break;
                    case '\n':
                        line++;
                        sattr += c;
                        break;
                    case '\"':
                        if (p[0] == '\"' && p[1] == '\"') {
                            p += 2;
                            return T_STR;
                        }
                        // FALL-THRU:
                    default:
                        sattr += c;
                        break;
                }
            }
        }
        // Regular string or character constant.
        while ((c = *p++) != initial) switch (c) {
            case 0:
            case '\n':
                p--;
                Error("end of line found in string constant");
            case '\'':
            case '\"':
                Error("\' and \" should be prefixed with a \\ in a string constant");
            case '\\':
                switch(c = *p++) {
                    case 'n': c = '\n'; break;
                    case 't': c = '\t'; break;
                    case 'r': c = '\r'; break;
                    case '\\':
                    case '\"':
                    case '\'': break;
                    case 'x':
                        if (!isxdigit(*p) || !isxdigit(p[1]))
                            Error("illegal hexadecimal escape code in string constant");
                        c = HexDigit(*p++) << 4;
                        c |= HexDigit(*p++);
                        break;
                    default:
                        p--;
                        Error("unknown control code in string constant");
                };
                sattr += c;
                break;
            default:
                if (c<' ') Error("unprintable character in string constant");
                sattr += c;
        };
        if (initial == '\"') {
            return T_STR;
        } else {
            if (sattr.size() > 4) Error("character constant too long");
            int ival = 0;
            for (auto c : sattr) ival = (ival << 8) + c;
            sattr = to_string(ival);
            return T_INT;
        };
    };

    string TokStr(TType t = T_NONE) {
        if (t == T_NONE) {
            t = token;
            switch (t) {
                case T_IDENT:
                case T_FLOAT:
                case T_INT: return sattr;
                case T_STR:  // FIXME: will not deal with other escape codes, use ToString code
                             return "\"" + sattr + "\"";
            }
        }
        return TName(t);
    }

    string Location(const Line &ln) {
        return filenames[ln.fileidx] + "(" + to_string(ln.line) + ")";
    }

    void Error(string err, const Line *ln = nullptr) {
        err = Location(ln ? *ln : Line(errorline, fileidx)) + ": error: " + err;
        //Output(OUTPUT_DEBUG, "%s", err.c_str());
        throw err;
    }
};

}  // namespace lobster
