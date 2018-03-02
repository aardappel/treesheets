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

int ParseOpAndGetArity(int opc, const int *&ip, const int *code) {
    auto arity = ILArity()[opc];
    switch(opc) {
        default: {
            assert(arity >= 0);
            ip += arity;
            break;
        }
        case IL_CORO: {
            ip += 2;
            int n = *ip++;
            ip += n;
            arity = n + 3;
            break;
        }
        case IL_CALLMULTI: {
            ip++;
            auto nargs = code[*ip++ + 2];
            ip++;
            ip += nargs;
            arity = nargs + 4;
            break;
        }
        case IL_FUNSTART: {
            int n = *ip++;
            ip += n;
            int m = *ip++;
            ip += m;
            arity = n + m + 3;
            break;
        }
        case IL_FUNMULTI: {
            auto n = *ip++;
            auto nargs = *ip++;
            auto tablesize = (nargs + 1) * n;
            ip += tablesize;
            arity = tablesize + 2;
            break;
        }
    }
    return arity;
}

void ToCPP(string &s, const string &bytecode_buffer) {
    int dispatch = VM_DISPATCH_METHOD;
    auto bcf = bytecode::GetBytecodeFile(bytecode_buffer.c_str());
    assert(FLATBUFFERS_LITTLEENDIAN);
    auto code = (const int *)bcf->bytecode()->Data();  // Assumes we're on a little-endian machine.
    //auto typetable = (const type_elem_t *)bcf->typetable()->Data();  // Same.
    map<int, const bytecode::Function *> function_lookup;
    for (flatbuffers::uoffset_t i = 0; i < bcf->functions()->size(); i++) {
        auto f = bcf->functions()->Get(i);
        function_lookup[f->bytecodestart()] = f;
    }
    s += "#include \"stdafx.h\"\n"
            "#include \"vmdata.h\"\n"
            "#include \"sdlinterface.h\"\n"
            "\n"
            "#ifndef VM_COMPILED_CODE_MODE\n"
            "  #error VM_COMPILED_CODE_MODE must be set for the entire code base.\n"
            "#endif\n"
            "\n"
            "using lobster::g_vm;\n"
            "\n"
            "#pragma warning (disable: 4102)  // Unused label.\n"
            "\n";
    auto len = bcf->bytecode()->Length();
    auto ilnames = ILNames();
    vector<int> block_ids(bcf->bytecode_attr()->size());
    auto BlockRef = [&](ptrdiff_t ip) {
        if (dispatch == VM_DISPATCH_TRAMPOLINE) s += "block";
        s += to_string(dispatch == VM_DISPATCH_TRAMPOLINE ? ip : block_ids[ip]);
    };
    auto JumpIns = [&](ptrdiff_t ip = 0) {
        if (dispatch == VM_DISPATCH_TRAMPOLINE) {
            s += "return ";
        } else if (dispatch == VM_DISPATCH_SWITCH_GOTO) {
            if (ip) s += "goto block_label";
            else s += "{ ip = ";
        }
        if (ip) BlockRef(ip); else s += "g_vm->next_call_target";
        s += ";";
        if (dispatch == VM_DISPATCH_SWITCH_GOTO) {
            if (!ip) s += " continue; }";
        }
    };
    const int *ip = code;
    // Skip past 1st jump.
    assert(*ip == IL_JUMP);
    ip++;
    auto starting_point = *ip++;
    int block_id = 1;
    while (ip < code + len) {
        if (bcf->bytecode_attr()->Get((flatbuffers::uoffset_t)(ip - code)) & bytecode::Attr_SPLIT) {
            if (dispatch == VM_DISPATCH_TRAMPOLINE) {
                s += "static void *block";
                s += to_string(ip - code);
                s += "();\n";
            } else if (dispatch == VM_DISPATCH_SWITCH_GOTO) {
                block_ids[ip - code] = block_id++;
            }
        }
        int opc = *ip++;
        if (opc < 0 || opc >= IL_MAX_OPS) {
            s += "// Corrupt bytecode starts here: ";
            s += to_string(opc);
            s += "\n";
            return;
        }
        ParseOpAndGetArity(opc, ip, code);
    }
    s += "\n";
    if (dispatch == VM_DISPATCH_SWITCH_GOTO) {
        s += "static void *one_gigantic_function() {\n  int ip = ";
        BlockRef(starting_point);
        s += ";\n  for(;;) switch(ip) {\n    default: assert(false); continue;\n";
    }
    ip = code + 2;
    bool already_returned = false;
    while (ip < code + len) {
        int opc = *ip++;
        if (opc == IL_FUNSTART) {
            s += "\n";
            auto it = function_lookup.find((int)(ip - 1 - code));
            if (it != function_lookup.end()) s += "// " + it->second->name()->str() + "\n";
        }
        auto ilname = ilnames[opc];
        auto args = ip;
        if (bcf->bytecode_attr()->Get((flatbuffers::uoffset_t)(ip - 1 - code)) & bytecode::Attr_SPLIT) {
            if (dispatch == VM_DISPATCH_TRAMPOLINE) {
                s += "static void *block";
                s += to_string(args - 1 - code);
                s += "() {\n";
            } else if (dispatch == VM_DISPATCH_SWITCH_GOTO) {
                s += "  case ";
                BlockRef(args - 1 - code);
                s += ": block_label";
                BlockRef(args - 1 - code);
                s += ":\n";
            }
            already_returned = false;
        }
        auto arity = ParseOpAndGetArity(opc, ip, code);
        s += "    ";
        if (opc == IL_JUMP) {
            already_returned = true;
            JumpIns(args[0]);
            s += "\n";
        } else if ((opc >= IL_JUMPFAIL && opc <= IL_JUMPNOFAILRREF) ||
                   (opc >= IL_IFOR && opc <= IL_NFOR)) {
            s += "if (g_vm->F_";
            s += ilname;
            s += "()) ";
            JumpIns(args[0]);
            s += "\n";
        } else {
            s += "{ ";
            if (arity) {
                s += "static int args[] = {";
                for (int i = 0; i < arity; i++) {
                    if (i) s += ", ";
                    s += to_string(args[i]);
                }
                s += "}; ";
            }
            if (opc == IL_FUNMULTI) {
                s += "static lobster::block_t mmtable[] = {";
                auto nargs = args[1];
                for (int i = 0; i < args[0]; i++) {
                    BlockRef(args[2 + (nargs + 1) * i + nargs]);
                    s += ", ";
                }
                s += "}; g_vm->next_mm_table = mmtable; ";
            // FIXME: make resume a vm op.
            } else if (opc >= IL_BCALLRET2 && opc <= IL_BCALLUNB2 &&
                       natreg.nfuns[args[0]]->name == "resume") {
                s += "g_vm->next_call_target = ";
                BlockRef(ip - code);
                s += "; ";
            }
            s += "g_vm->F_";
            s += ilname;
            s += "(";
            s += arity ? "args" : "nullptr";
            if (opc == IL_CALL || opc == IL_CALLMULTI || opc == IL_CALLV || opc == IL_CALLVCOND ||
                opc == IL_YIELD) {
                s += ", ";
                BlockRef(ip - code);
            } else if (opc == IL_PUSHFUN || opc == IL_CORO) {
                s += ", ";
                BlockRef(args[0]);
            }
            s += ");";
            if (opc >= IL_BCALLRET0 && opc <= IL_BCALLUNB6) {
                s += " /* ";
                s += natreg.nfuns[args[0]]->name;
                s += " */";
            } else if (opc == IL_PUSHVAR || opc == IL_PUSHVARREF) {
                s += " /* ";
                s += IdName(bcf, args[0]);
                s += " */";
            } else if (opc == IL_LVALVAR) {
                s += " /* ";
                s += LvalOpNames()[args[0]];
                s += " ";
                s += IdName(bcf, args[1]);
                s += " */";
            } else if (opc == IL_PUSHSTR) {
                s += " /* ";
                EscapeAndQuote(bcf->stringtable()->Get(args[0])->c_str(), s);
                s += " */";
            } else if (opc == IL_CALL || opc == IL_CALLMULTI) {
                s += " /* ";
                s += bcf->functions()->Get(args[0])->name()->c_str();
                s += " */";
            }
            if (opc == IL_CALL || opc == IL_CALLMULTI) {
                s += " ";
                JumpIns(args[1]);
                already_returned = true;
            } else if (opc == IL_CALLV || opc == IL_FUNEND || opc == IL_FUNMULTI ||
                       opc == IL_YIELD || opc == IL_COEND || opc == IL_RETURN ||
                       // FIXME: make resume a vm op.
                       (opc >= IL_BCALLRET2 && opc <= IL_BCALLUNB2 &&
                        natreg.nfuns[args[0]]->name == "resume")) {
                s += " ";
                JumpIns();
                already_returned = true;
            } else if (opc == IL_CALLVCOND) {
                s += " if (g_vm->next_call_target) ";
                JumpIns();
            }
            s += " }\n";
        }
        if (bcf->bytecode_attr()->Get((flatbuffers::uoffset_t)(ip - code)) & bytecode::Attr_SPLIT) {
            if (dispatch == VM_DISPATCH_TRAMPOLINE) {
                if (!already_returned) {
                    s += "  ";
                    JumpIns(opc == IL_EXIT ? 0 : ip - code);
                    s += "\n";
                }
                s += "}\n";
            }
        }
    }
    if (dispatch == VM_DISPATCH_SWITCH_GOTO) {
        s += "}\n}\n";  // End of gigantic function.
    }
    // FIXME: this obviously does NOT need to include the actual bytecode, just the metadata.
    // in fact, it be nice if those were in readable format in the generated code.
    s += "\nstatic const int bytecodefb[] =\n{";
    auto bytecode_ints = (const int *)bytecode_buffer.c_str();
    for (size_t i = 0; i < bytecode_buffer.length() / sizeof(int); i++) {
        if ((i & 0xF) == 0) s += "\n  ";
        s += to_string(bytecode_ints[i]);
        s += ", ";
    }
    s += "\n};\n\n";
    s += "int main(int argc, char *argv[])\n{\n  return EngineRunCompiledCodeMain(argc, argv, ";
    if (dispatch == VM_DISPATCH_SWITCH_GOTO) {
        s += "one_gigantic_function";
    } else if (dispatch == VM_DISPATCH_TRAMPOLINE) {
        s += "block";
        s += to_string(starting_point);
    }
    s += ", bytecodefb);\n}\n";
}

}
