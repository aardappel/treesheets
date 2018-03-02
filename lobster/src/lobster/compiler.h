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

extern void Compile(const char *fn, const char *stringsource, string &bytecode,
                    string *parsedump = nullptr, string *pakfile = nullptr,
                    bool dump_builtins = false, bool dump_names = false);
extern bool LoadPakDir(const char *lpak);
extern bool LoadByteCode(string &bytecode);
extern void RegisterBuiltin(const char *name, void (* regfun)());
extern void RegisterCoreLanguageBuiltins();

extern void ToCPP(string &s, const string &bytecode_buffer);

}
