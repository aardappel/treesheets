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

#define TTYPES_LIST \
    TOK(T_NONE, "invalid_token") \
    TOK(T_PLUS, "+") \
    TOK(T_MINUS, "-") \
    TOK(T_MULT, "*") \
    TOK(T_DIV, "/") \
    TOK(T_MOD, "%") \
    TOK(T_PLUSEQ, "+=") \
    TOK(T_MINUSEQ, "-=") \
    TOK(T_MULTEQ, "*=") \
    TOK(T_DIVEQ, "/=") \
    TOK(T_MODEQ, "%=") \
    TOK(T_AND, "and") \
    TOK(T_OR, "or") \
    TOK(T_NOT, "~") \
    TOK(T_INCR, "++") \
    TOK(T_DECR, "--") \
    TOK(T_EQ, "==") \
    TOK(T_NEQ, "!=") \
    TOK(T_LT, "<") \
    TOK(T_GT, ">") \
    TOK(T_LTEQ, "<=") \
    TOK(T_GTEQ, ">=") \
    TOK(T_BITAND, "&") \
    TOK(T_BITOR, "|") \
    TOK(T_XOR, "^") \
    TOK(T_NEG, "~") \
    TOK(T_ASL, "<<") \
    TOK(T_ASR, ">>") \
    TOK(T_ASSIGN, "=") \
    TOK(T_DYNASSIGN, "<-") \
    TOK(T_LOGASSIGN, "?=") \
    TOK(T_DEF, ":=") \
    TOK(T_DOT, ".") \
    TOK(T_DOTDOT, "..") \
    TOK(T_DOTMAYBE, "?.") \
    TOK(T_CODOT, "->") \
    TOK(T_INT, "integer literal") \
    TOK(T_FLOAT, "floating point literal") \
    TOK(T_STR, "string literal") \
    TOK(T_NIL, "nil") \
    TOK(T_DEFAULTVAL, "default value") \
    TOK(T_IDENT, "identifier") \
    TOK(T_STRUCT, "struct") \
    TOK(T_FUN, "def") \
    TOK(T_RETURN, "return") \
    TOK(T_IS, "is") \
    TOK(T_TYPEOF, "typeof") \
    TOK(T_COROUTINE, "coroutine") \
    TOK(T_LINEFEED, "linefeed") \
    TOK(T_ENDOFINCLUDE, "end of include") \
    TOK(T_ENDOFFILE, "end of file") \
    TOK(T_INDENT, "indentation") \
    TOK(T_DEDENT, "de-indentation") \
    TOK(T_LEFTPAREN, "(") \
    TOK(T_RIGHTPAREN, ")") \
    TOK(T_LEFTBRACKET, "[") \
    TOK(T_RIGHTBRACKET, "]") \
    TOK(T_LEFTCURLY, "{") \
    TOK(T_RIGHTCURLY, "}") \
    TOK(T_SEMICOLON, ";") \
    TOK(T_AT, "@") \
    TOK(T_QUESTIONMARK, "?") \
    TOK(T_COMMA, ",") \
    TOK(T_COLON, ":") \
    TOK(T_DEFCONST, ":==") \
    TOK(T_DEFTYPEIN, "::=") \
    TOK(T_TYPEIN, "::") \
    TOK(T_VALUE, "value") \
    TOK(T_INCLUDE, "include") \
    TOK(T_INTTYPE, "int") \
    TOK(T_FLOATTYPE, "float") \
    TOK(T_STRTYPE, "string") \
    TOK(T_ANYTYPE, "any") \
    TOK(T_FROM, "from") \
    TOK(T_PROGRAM, "program") \
    TOK(T_PRIVATE, "private") \
    TOK(T_RESOURCE, "resource") \
    TOK(T_ENUM, "enum") \
    TOK(T_VAR, "var") \
    TOK(T_CONST, "let") \
    TOK(T_PAKFILE, "pakfile") \
    TOK(T_SWITCH, "switch") \
    TOK(T_CASE, "case") \
    TOK(T_DEFAULT, "default")

enum TType {
    #define TOK(ENUM, STR) ENUM,
        TTYPES_LIST
    #undef TOK
};

inline const char *TName(TType t) {
    static const char *names[] = {
        #define TOK(ENUM, STR) STR,
            TTYPES_LIST
        #undef TOK
    };
    return names[t];
}

}  // namespace lobster
