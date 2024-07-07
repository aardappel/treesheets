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

struct Deserializer {
    VM &vm;
    vector<Value> stack;
    vector<bool> is_ref;

    ~Deserializer() {
        assert(stack.size() == is_ref.size());
        for (size_t i = 0; i < stack.size(); i++) {
            if (is_ref[i]) stack[i].ref()->Dec(vm);
        }
    }

    void PushV(Value v, bool ir = false) {
        stack.emplace_back(v);
        is_ref.push_back(ir);
    }

    Value PopV() {
        auto v = stack.back();
        stack.pop_back();
        is_ref.pop_back();
        return v;
    }

    void PopVN(size_t len) {
        stack.resize(stack.size() - len);
        is_ref.resize(is_ref.size() - len);
    }

    Deserializer(VM &vm) : vm(vm) {
        stack.reserve(16);
        is_ref.reserve(16);
    }

    bool PushDefault(type_elem_t typeoff, int defval) {
        auto &ti = vm.GetTypeInfo(typeoff);
        switch (ti.t) {
            case V_INT:
                PushV(defval);
                break;
            case V_FLOAT:
                PushV(int2float(defval).f);
                break;
            case V_NIL:
                PushV(NilVal());
                break;
            case V_STRING:
                PushV(vm.NewString(0), true);
                break;
            case V_VECTOR:
                PushV(vm.NewVec(0, 0, typeoff), true);
                break;
            case V_STRUCT_S:
            case V_STRUCT_R:
            case V_CLASS: {
                for (int i = 0; i < ti.len; i++) {
                    if (!PushDefault(ti.elemtypes[i].type, ti.elemtypes[i].defval))
                        return false;
                }
                if (ti.t == V_CLASS) {
                    auto vec = vm.NewObject(ti.len, typeoff);
                    if (ti.len) vec->CopyElemsShallow(&stack[stack.size() - (size_t)ti.len], ti.len);
                    PopVN(ti.len);
                    PushV(vec, true);
                }
                break;
            }
            default:
                return false;
        }
        return true;
    }

    pair<const TypeInfo *, type_elem_t> LookupSubClass(string_view sname,
            const TypeInfo *ti, type_elem_t typeoff) {
        // Attempt to find this a subsclass.
        vm.EnsureUDTLookupPopulated();
        auto &udts = vm.UDTLookup[sname];
        for (auto udt : udts) {
            for (auto ludt = udt;;) {
                auto super_idx = ludt->super_idx();
                if (super_idx < 0) break;
                if (super_idx == ti->structidx) {
                    // Note: this field only not -1 for UDTs actually constructed/used.
                    typeoff = (type_elem_t)udt->typeidx();
                    if (typeoff >= 0) {
                        return { &vm.GetTypeInfo(typeoff), typeoff };
                    }
                }
                ludt = vm.bcf->udts()->Get(super_idx);
            }
        }
        return { nullptr, (type_elem_t)-1 };
    }
};

struct LobsterBinaryParser : Deserializer {

    LobsterBinaryParser(VM &vm) : Deserializer(vm) {}

    Value Parse(type_elem_t typeoff, const uint8_t *data, const uint8_t *end) {
        ParseElem(data, end, typeoff);
        assert(stack.size() == 1);
        return PopV();
    }

    void Error(const string &s) {
        // FIXME: not great on non-exception platforms, this should not abort.
        THROW_OR_ABORT(cat("lobster_binary_to_value: ", s));
    }

    void Truncated() {
        Error("data truncated");
    }

    void ParseElem(const uint8_t *&data, const uint8_t *end, type_elem_t typeoff) {
        auto base_ti = &vm.GetTypeInfo(typeoff);
        auto ti = base_ti;
        if (ti->t == V_NIL) {
            ti = &vm.GetTypeInfo(typeoff = ti->subt);
        }
        if (end == data) Truncated();
        switch (ti->t) {
            case V_INT: {
                PushV(DecodeVarintS(data, end));
                break;
            }
            case V_FLOAT: {
                float f;
                if (end - data < (ptrdiff_t)sizeof(float)) Truncated();
                memcpy(&f, data, sizeof(float));
                data += sizeof(float);
                PushV(f);
                break;
            }
            case V_STRING: {
                auto len = DecodeVarintU(data, end);
                if (!len && base_ti->t == V_NIL) {
                    PushV(NilVal());
                } else {
                    auto str = vm.NewString(string_view((const char *)data, (size_t)len));
                    data += len;
                    PushV(str, true);
                }
                break;
            }
            case V_VECTOR: {
                auto len = DecodeVarintU(data, end);
                if (!len && base_ti->t == V_NIL) {
                    PushV(NilVal());
                } else {
                    auto stack_start = stack.size();
                    for (size_t i = 0; i < len; i++) {
                        ParseElem(data, end, ti->subt);
                    }
                    auto &sti = vm.GetTypeInfo(ti->subt);
                    auto width = IsStruct(sti.t) ? sti.len : 1;
                    auto len = iint(stack.size() - stack_start);
                    auto n = len / width;
                    auto vec = vm.NewVec(n, n, typeoff);
                    if (len) vec->CopyElemsShallow(stack.size() - len + stack.data());
                    PopVN(len);
                    PushV(vec, true);
                }
                break;
            }
            case V_CLASS: {
                auto elen = (int)DecodeVarintU(data, end);
                if (!elen && base_ti->t == V_NIL) {
                    PushV(NilVal());
                } else {
                    auto ser_id = DecodeVarintU(data, end);
                    typeoff = vm.GetSubClassFromSerID(typeoff, (uint32_t)ser_id);
                    if (typeoff < 0)
                        Error(cat("serialization id ", ser_id, " is not a sub-class of ",
                                  vm.StructName(*ti)));
                    ti = &vm.GetTypeInfo(typeoff);
                    auto stack_start = stack.size();
                    auto NumElems = [&]() { return iint(stack.size() - stack_start); };
                    for (int i = 0; NumElems() != ti->len; i++) {
                        auto eti = ti->GetElemOrParent(NumElems());
                        if (NumElems() >= elen) {
                            if (!PushDefault(eti, ti->elemtypes[NumElems()].defval))
                                Error("no default value exists for missing field " +
                                      vm.LookupField(ti->structidx, i));
                        } else {
                            ParseElem(data, end, eti);
                        }
                    }
                    if (elen > NumElems()) {
                        // We have fields from a future version of this class, sadly we don't
                        // know how to read past these fields since we have no type data.
                        Error("extra fields presents in " + vm.StructName(*ti));
                    }
                    auto len = NumElems();
                    auto vec = vm.NewObject(len, typeoff);
                    if (len) vec->CopyElemsShallow(stack.size() - len + stack.data(), len);
                    PopVN(len);
                    PushV(vec, true);
                }
                break;
            }
            case V_STRUCT_S:
            case V_STRUCT_R: {
                auto stack_start = stack.size();
                auto NumElems = [&]() { return iint(stack.size() - stack_start); };
                // NOTE: this provides no protection against structs changing in size,
                // unlike classes. It will simply parse wrong.
                while (NumElems() != ti->len) {
                    auto eti = ti->GetElemOrParent(NumElems());
                    ParseElem(data, end, eti);
                }
                break;
            }
            default:
                Error("can\'t convert to value: " + ti->Debug(vm, false));
                PushV(NilVal());
                break;
        }
    }
};

}
