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

#include "lobster/stdafx.h"

#include "lobster/vmdata.h"

namespace lobster {

LString::LString(intp _l) : RefObj(TYPE_ELEM_STRING), len(_l) { ((char *)data())[_l] = 0; }

LResource::LResource(void *v, const ResourceType *t)
    : RefObj(TYPE_ELEM_RESOURCE), val(v), type(t) {}

char HexChar(char i) { return i + (i < 10 ? '0' : 'A' - 10); }

void EscapeAndQuote(string_view s, ostringstream &ss) {
    ss << '\"';
    for (auto c : s) switch(c) {
        case '\n': ss << "\\n"; break;
        case '\t': ss << "\\t"; break;
        case '\r': ss << "\\r"; break;
        case '\\': ss << "\\\\"; break;
        case '\"': ss << "\\\""; break;
        case '\'': ss << "\\\'"; break;
        default:
            if (c >= ' ' && c <= '~') ss << c;
            else ss << "\\x" << HexChar(((uchar)c) >> 4) << HexChar(c & 0xF);
            break;
    }
    ss << "\"";
}

void LString::DeleteSelf(VM &vm) { vm.pool.dealloc(this, sizeof(LString) + len + 1); }

void LString::ToString(ostringstream &ss, PrintPrefs &pp) {
    if (CycleCheck(ss, pp)) return;
    auto sv = strv();
    auto dd = string_view();
    if (len > pp.budget) {
        sv = sv.substr(0, pp.budget);
        dd = "..";
    }
    if (pp.quoted) {
        EscapeAndQuote(sv, ss);
    } else {
        ss << sv;
    }
    ss << dd;
}

LVector::LVector(VM &vm, intp _initial, intp _max, type_elem_t _tti)
    : RefObj(_tti), len(_initial), maxl(_max) {
    auto &sti = vm.GetTypeInfo(ti(vm).subt);
    width = IsStruct(sti.t) ? sti.len : 1;
    v = maxl ? AllocSubBuf<Value>(vm, maxl * width, TYPE_ELEM_VALUEBUF) : nullptr;
}

void LVector::Resize(VM &vm, intp newmax) {
    // FIXME: check overflow
    auto mem = AllocSubBuf<Value>(vm, newmax * width, TYPE_ELEM_VALUEBUF);
    if (len) t_memcpy(mem, v, len * width);
    DeallocBuf(vm);
    maxl = newmax;
    v = mem;
}

void LVector::Append(VM &vm, LVector *from, intp start, intp amount) {
    if (len + amount > maxl) Resize(vm, len + amount);  // FIXME: check overflow
    assert(width == from->width);
    t_memcpy(v + len * width, from->v + start * width, amount * width);
    auto et = from->ElemType(vm).t;
    if (IsRefNil(et)) {
        for (int i = 0; i < amount * width; i++) {
            v[len * width + i].LTINCRTNIL();
        }
    }
    len += amount;
}

void LVector::Remove(VM &vm, intp i, intp n, intp decfrom, bool stack_ret) {
    assert(n >= 0 && n <= len && i >= 0 && i <= len - n);
    if (stack_ret) {
        tsnz_memcpy(vm.TopPtr(), v + i * width, width);
        vm.PushN((int)width);
    }
    auto et = ElemType(vm).t;
    if (IsRefNil(et)) {
        for (intp j = decfrom * width; j < n * width; j++) DecSlot(vm, i * width + j, et);
    }
    t_memmove(v + i * width, v + (i + n) * width, (len - i - n) * width);
    len -= n;
}

void LVector::AtVW(VM &vm, intp i) const {
    auto src = AtSt(i);
    // TODO: split this up for the width==1 case?
    tsnz_memcpy(vm.TopPtr(), src, width);
    vm.PushN((int)width);
}

void LVector::AtVWSub(VM &vm, intp i, int w, int off) const {
    auto src = AtSt(i);
    tsnz_memcpy(vm.TopPtr(), src + off, w);
    vm.PushN(w);
}

void LVector::DeleteSelf(VM &vm) {
    auto et = ElemType(vm).t;
    if (IsRefNil(et)) {
        for (intp i = 0; i < len * width; i++) DecSlot(vm, i, et);
    }
    DeallocBuf(vm);
    vm.pool.dealloc_small(this);
}

void LObject::DeleteSelf(VM &vm) {
    auto len = Len(vm);
    for (intp i = 0; i < len; i++) {
        AtS(i).LTDECTYPE(vm, ElemTypeS(vm, i).t);
    }
    vm.pool.dealloc(this, sizeof(LObject) + sizeof(Value) * len);
}

void LResource::DeleteSelf(VM &vm) {
    type->deletefun(val);
    vm.pool.dealloc(this, sizeof(LResource));
}

void RefObj::DECDELETENOW(VM &vm) {
    switch (ti(vm).t) {
        case V_STRING:     ((LString *)this)->DeleteSelf(vm); break;
        case V_COROUTINE:  ((LCoRoutine *)this)->DeleteSelf(vm); break;
        case V_VECTOR:     ((LVector *)this)->DeleteSelf(vm); break;
        case V_CLASS:      ((LObject *)this)->DeleteSelf(vm); break;
        case V_RESOURCE:   ((LResource *)this)->DeleteSelf(vm); break;
        default:           assert(false);
    }
}

void RefObj::DECDELETE(VM &vm) {
    if (refc) {
        vm.DumpVal(this, "double delete");
        assert(false);
    }
    #if DELETE_DELAY
        vm.DumpVal(this, "delay delete");
        vm.delete_delay.push_back(this);
    #else
        DECDELETENOW(vm);
    #endif
}

void RefObj::DECSTAT(VM &vm) { vm.vm_count_decref++; }

bool RefEqual(VM &vm, const RefObj *a, const RefObj *b, bool structural) {
    if (a == b) return true;
    if (!a || !b) return false;
    if (a->tti != b->tti) return false;
    switch (a->ti(vm).t) {
        case V_STRING:      return *((LString *)a) == *((LString *)b);
        case V_COROUTINE:   return false;
        case V_VECTOR:      return structural && ((LVector *)a)->Equal(vm, *(LVector *)b);
        case V_CLASS:       return structural && ((LObject *)a)->Equal(vm, *(LObject *)b);
        default:            assert(0); return false;
    }
}

bool Value::Equal(VM &vm, ValueType vtype, const Value &o, ValueType otype, bool structural) const {
    if (vtype != otype) return false;
    switch (vtype) {
        case V_INT: return ival_ == o.ival_;
        case V_FLOAT: return fval_ == o.fval_;
        case V_FUNCTION: return ip_ == o.ip_;
        default: return RefEqual(vm, refnil(), o.ref_, structural);
    }
}

void RefToString(VM &vm, ostringstream &ss, const RefObj *ro, PrintPrefs &pp) {
    if (!ro) { ss << "nil"; return; }
    auto &roti = ro->ti(vm);
    switch (roti.t) {
        case V_STRING:    ((LString *)ro)->ToString(ss, pp);        break;
        case V_COROUTINE: ss << "(coroutine)";                      break;
        case V_VECTOR:    ((LVector *)ro)->ToString(vm, ss, pp);    break;
        case V_CLASS:     ((LObject *)ro)->ToString(vm, ss, pp);    break;
        case V_RESOURCE:  ((LResource *)ro)->ToString(ss);          break;
        default:          ss << '(' << BaseTypeName(roti.t) << ')'; break;
    }
}

void Value::ToString(VM &vm, ostringstream &ss, const TypeInfo &ti, PrintPrefs &pp) const {
    if (ti.t == V_INT && ti.enumidx >= 0) {
        auto name = vm.EnumName(ival(), ti.enumidx);
        if (!name.empty()) {
            ss << name;
            return;
        }
    }
    ToStringBase(vm, ss, ti.t, pp);
}

void Value::ToStringBase(VM &vm, ostringstream &ss, ValueType t, PrintPrefs &pp) const {
    if (IsRefNil(t)) {
        RefToString(vm, ss, ref_, pp);
    } else switch (t) {
        case V_INT:
            ss << ival();
            break;
        case V_FLOAT:
            ss << to_string_float(fval(), (int)pp.decimals);
            break;
        case V_FUNCTION:
            ss << "<FUNCTION>";
            break;
        default:
            ss << '(' << BaseTypeName(t) << ')';
            break;
    }
}


intp RefObj::Hash(VM &vm) {
    switch (ti(vm).t) {
        case V_STRING:      return ((LString *)this)->Hash();
        case V_VECTOR:      return ((LVector *)this)->Hash(vm);
        case V_CLASS:       return ((LObject *)this)->Hash(vm);
        default:            return (int)(size_t)this;
    }
}

intp LString::Hash() {
    return (int)FNV1A(strv());
}

intp Value::Hash(VM &vm, ValueType vtype) {
    switch (vtype) {
        case V_INT: return ival_;
        case V_FLOAT: return ReadMem<intp>(&fval_);
        case V_FUNCTION: return (intp)(size_t)ip_.f;
        default: return refnil() ? ref()->Hash(vm) : 0;
    }
}

Value Value::Copy(VM &vm) {
    if (!refnil()) return Value();
    auto &ti = ref()->ti(vm);
    switch (ti.t) {
    case V_VECTOR: {
        auto len = vval()->len;
        auto nv = vm.NewVec(len, len, vval()->tti);
        if (len) nv->Init(vm, vval()->Elems(), true);
        return Value(nv);
    }
    case V_CLASS: {
        auto len = oval()->Len(vm);
        auto nv = vm.NewObject(len, oval()->tti);
        if (len) nv->Init(vm, oval()->Elems(), len, true);
        return Value(nv);
    }
    case V_STRING: {
        auto s = vm.NewString(sval()->strv());
        return Value(s);
    }
    case V_COROUTINE:
        vm.Error("cannot copy coroutine");
        return Value();
    default:
        assert(false);
        return Value();
    }
}

string TypeInfo::Debug(VM &vm, bool rec) const {
    string s;
    s += BaseTypeName(t);
    if (t == V_VECTOR || t == V_NIL) {
        s += "[" + vm.GetTypeInfo(subt).Debug(vm, false) + "]";
    } else if (IsUDT(t)) {
        auto sname = vm.StructName(*this);
        s += ":" + sname;
        if (rec) {
            s += "{";
            for (int i = 0; i < len; i++)
                s += vm.GetTypeInfo(elemtypes[i]).Debug(vm, false) + ",";
            s += "}";
        }
    }
    return s;
}

#define ELEMTYPE(acc, ass) \
    auto &_ti = ti(vm); \
    ass; \
    auto &sti = vm.GetTypeInfo(_ti.acc); \
    return sti.t == V_NIL ? vm.GetTypeInfo(sti.subt) : sti;

const TypeInfo &LObject::ElemTypeS(VM &vm, intp i) const {
    ELEMTYPE(elemtypes[i], assert(i < _ti.len));
}

const TypeInfo &LObject::ElemTypeSP(VM &vm, intp i) const {
    ELEMTYPE(GetElemOrParent(i), assert(i < _ti.len));
}

const TypeInfo &LVector::ElemType(VM &vm) const {
    ELEMTYPE(subt, {})
}

void VectorOrObjectToString(VM &vm, ostringstream &ss, PrintPrefs &pp, char openb, char closeb,
                            intp len, intp width, const Value *elems, bool is_vector,
                            std::function<const TypeInfo &(intp)> getti) {
    ss << openb;
    if (pp.indent) ss << '\n';
    auto start_size = ss.tellp();
    pp.cur_indent += pp.indent;
    auto Indent = [&]() {
        for (int i = 0; i < pp.cur_indent; i++) ss << ' ';
    };
    for (intp i = 0; i < len; i++) {
        if (i) {
            ss << ',';
            ss << (pp.indent ? '\n' : ' ');
        }
        if (pp.indent) Indent();
        if ((int)ss.tellp() - start_size > pp.budget) {
            ss << "....";
            break;
        }
        auto &ti = getti(i);
        if (pp.depth || !IsRef(ti.t)) {
            PrintPrefs subpp(pp.depth - 1, pp.budget - (int)(ss.tellp() - start_size), true,
                             pp.decimals);
            subpp.indent = pp.indent;
            subpp.cur_indent = pp.cur_indent;
            if (IsStruct(ti.t)) {
                vm.StructToString(ss, subpp, ti, elems + i * width);
                if (!is_vector) i += ti.len - 1;
            } else {
                elems[i].ToString(vm, ss, ti, subpp);
            }
        } else {
            ss << "..";
        }
    }
    pp.cur_indent -= pp.indent;
    if (pp.indent) { ss << '\n'; Indent(); }
    ss << closeb;
}

void LObject::ToString(VM &vm, ostringstream &ss, PrintPrefs &pp) {
    if (CycleCheck(ss, pp)) return;
    ss << vm.ReverseLookupType(ti(vm).structidx);
    if (pp.indent) ss << ' ';
    VectorOrObjectToString(vm, ss, pp, '{', '}', Len(vm), 1, Elems(), false,
        [&](intp i) -> const TypeInfo & {
            return ElemTypeSP(vm, i);
        }
    );
}

void LVector::ToString(VM &vm, ostringstream &ss, PrintPrefs &pp) {
    if (CycleCheck(ss, pp)) return;
    VectorOrObjectToString(vm, ss, pp, '[', ']', len, width, v, true,
        [&](intp) ->const TypeInfo & {
            return ElemType(vm);
        }
    );
}

void VM::StructToString(ostringstream &ss, PrintPrefs &pp, const TypeInfo &ti, const Value *elems) {
    ss << ReverseLookupType(ti.structidx);
    if (pp.indent) ss << ' ';
    VectorOrObjectToString(*this, ss, pp, '{', '}', ti.len, 1, elems, false,
        [&](intp i) -> const TypeInfo & {
            return GetTypeInfo(ti.GetElemOrParent(i));
        }
    );
}

}  // namespace lobster
