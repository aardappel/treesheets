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

BoxedInt::BoxedInt(intp _v) : RefObj(TYPE_ELEM_BOXEDINT), val(_v) {}
BoxedFloat::BoxedFloat(floatp _v) : RefObj(TYPE_ELEM_BOXEDFLOAT), val(_v) {}
LString::LString(intp _l) : RefObj(TYPE_ELEM_STRING), len(_l) {}
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
    if (pp.cycles >= 0) {
        if (refc < 0) { CycleStr(ss); return; }
        CycleDone(pp.cycles);
    }
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
    v = maxl ? AllocSubBuf<Value>(vm, maxl, TYPE_ELEM_VALUEBUF) : nullptr;
}

void LVector::Resize(VM &vm, intp newmax) {
    // FIXME: check overflow
    auto mem = AllocSubBuf<Value>(vm, newmax, TYPE_ELEM_VALUEBUF);
    if (len) memcpy(mem, v, sizeof(Value) * len);
    DeallocBuf(vm);
    maxl = newmax;
    v = mem;
}

void LVector::Append(VM &vm, LVector *from, intp start, intp amount) {
    if (len + amount > maxl) Resize(vm, len + amount);  // FIXME: check overflow
    memcpy(v + len, from->v + start, sizeof(Value) * amount);
    if (IsRefNil(from->ElemType(vm))) {
        for (int i = 0; i < amount; i++) v[len + i].INCRTNIL();
    }
    len += amount;
}

void LVector::DeleteSelf(VM &vm, bool deref) {
    if (deref) {
        auto et = ElemType(vm);
        for (intp i = 0; i < len; i++) Dec(vm, i, et);
    };
    DeallocBuf(vm);
    vm.pool.dealloc_small(this);
}

void LStruct::DeleteSelf(VM &vm, bool deref) {
    auto len = Len(vm);
    if (deref) { for (intp i = 0; i < len; i++) DecS(vm, i); };
    vm.pool.dealloc(this, sizeof(LStruct) + sizeof(Value) * len);
}

void LResource::DeleteSelf(VM &vm) {
    type->deletefun(val);
    vm.pool.dealloc(this, sizeof(LResource));
}

void RefObj::DECDELETE(VM &vm, bool deref) {
    assert(refc == 0);
    switch (ti(vm).t) {
        case V_BOXEDINT:   vm.pool.dealloc(this, sizeof(BoxedInt)); break;
        case V_BOXEDFLOAT: vm.pool.dealloc(this, sizeof(BoxedFloat)); break;
        case V_STRING:     ((LString *)this)->DeleteSelf(vm); break;
        case V_COROUTINE:  ((LCoRoutine *)this)->DeleteSelf(vm, deref); break;
        case V_VECTOR:     ((LVector *)this)->DeleteSelf(vm, deref); break;
        case V_STRUCT:     ((LStruct *)this)->DeleteSelf(vm, deref); break;
        case V_RESOURCE:   ((LResource *)this)->DeleteSelf(vm); break;
        default:           assert(false);
    }
}

bool RefEqual(VM &vm, const RefObj *a, const RefObj *b, bool structural) {
    if (a == b) return true;
    if (!a || !b) return false;
    if (a->tti != b->tti) return false;
    switch (a->ti(vm).t) {
        case V_BOXEDINT:    return ((BoxedInt *)a)->val == ((BoxedInt *)b)->val;
        case V_BOXEDFLOAT:  return ((BoxedFloat *)a)->val == ((BoxedFloat *)b)->val;
        case V_STRING:      return *((LString *)a) == *((LString *)b);
        case V_COROUTINE:   return false;
        case V_VECTOR:      return structural && ((LVector *)a)->Equal(vm, *(LVector *)b);
        case V_STRUCT:      return structural && ((LStruct *)a)->Equal(vm, *(LStruct *)b);
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
        case V_BOXEDINT: {
            if (pp.anymark) ss << '#';
            ss << ((BoxedInt *)ro)->val;
            break;
        }
        case V_BOXEDFLOAT: {
            if (pp.anymark) ss << '#';
            ss << to_string_float(((BoxedFloat *)ro)->val, (int)pp.decimals);
            break;
        }
        case V_STRING:    ((LString *)ro)->ToString(ss, pp);        break;
        case V_COROUTINE: ss << "(coroutine)";                      break;
        case V_VECTOR:    ((LVector *)ro)->ToString(vm, ss, pp);        break;
        case V_STRUCT:    ((LStruct *)ro)->ToString(vm, ss, pp);    break;
        default:          ss << '(' << BaseTypeName(roti.t) << ')'; break;
    }
}

void Value::ToString(VM &vm, ostringstream &ss, ValueType vtype, PrintPrefs &pp) const {
    if (IsRefNil(vtype)) {
        if (ref_) RefToString(vm, ss, ref_, pp); else ss << "nil";
    }
    else switch (vtype) {
        case V_INT:      ss << ival();                                    break;
        case V_FLOAT:    ss << to_string_float(fval(), (int)pp.decimals); break;
        case V_FUNCTION: ss << "<FUNCTION>";                              break;
        default:         ss << '(' << BaseTypeName(vtype) << ')';         break;
    }
}


void RefObj::Mark(VM &vm) {
    if (refc < 0) return;
    assert(refc);
    refc = -refc;
    switch (ti(vm).t) {
        case V_STRUCT:     ((LStruct    *)this)->Mark(vm); break;
        case V_VECTOR:     ((LVector    *)this)->Mark(vm); break;
        case V_COROUTINE:  ((LCoRoutine *)this)->Mark(vm); break;
        default:                                         break;
    }
}

intp RefObj::Hash(VM &vm) {
    switch (ti(vm).t) {
        case V_BOXEDINT:    return ((BoxedInt *)this)->val;
        case V_BOXEDFLOAT:  return ReadMem<intp>(&((BoxedFloat *)this)->val);
        case V_STRING:      return ((LString *)this)->Hash();
        case V_VECTOR:      return ((LVector *)this)->Hash(vm);
        case V_STRUCT:      return ((LStruct *)this)->Hash(vm);
        default:            return (int)(size_t)this;
    }
}

intp LString::Hash() {
    return (int)FNV1A(str());
}

intp Value::Hash(VM &vm, ValueType vtype) {
    switch (vtype) {
        case V_INT: return ival_;
        case V_FLOAT: return ReadMem<intp>(&fval_);
        case V_FUNCTION: return (intp)(size_t)ip_.f;
        default: return refnil() ? ref()->Hash(vm) : 0;
    }
}

void Value::Mark(VM &vm, ValueType vtype) {
    if (IsRefNil(vtype) && ref_) ref_->Mark(vm);
}

void Value::MarkRef(VM &vm) {
    if (ref_) ref_->Mark(vm);
}

Value Value::Copy(VM &vm) {
    if (!refnil()) return Value();
    auto &ti = ref()->ti(vm);
    switch (ti.t) {
    case V_VECTOR: {
        auto len = vval()->len;
        auto nv = vm.NewVec(len, len, vval()->tti);
        if (len) nv->Init(vm, vval()->Elems(), true);
        DECRT(vm);
        return Value(nv);
    }
    case V_STRUCT: {
        auto len = stval()->Len(vm);
        auto nv = vm.NewStruct(len, stval()->tti);
        if (len) nv->Init(vm, stval()->Elems(), len, true);
        DECRT(vm);
        return Value(nv);
    }
    case V_STRING: {
        auto s = vm.NewString(sval()->strv());
        DECRT(vm);
        return Value(s);
    }
    case V_BOXEDINT: {
        auto bi = vm.NewInt(bival()->val);
        DECRT(vm);
        return Value(bi);
    }
    case V_BOXEDFLOAT: {
        auto bf = vm.NewFloat(bfval()->val);
        DECRT(vm);
        return Value(bf);
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
    } else if (t == V_STRUCT) {
        auto sname = vm.StructName(*this);
        s += ":" + sname;
        if (rec) {
            s += "{";
            for (int i = 0; i < len; i++)
                s += vm.GetTypeInfo(elems[i]).Debug(vm, false) + ",";
            s += "}";
        }
    }
    return s;
}

#define ELEMTYPE(acc, ass) \
    auto &_ti = ti(vm); \
    ass; \
    auto &sti = vm.GetTypeInfo(_ti.acc); \
    auto vt = sti.t; \
    if (vt == V_NIL) vt = vm.GetTypeInfo(sti.subt).t; \
    return vt;

ValueType LStruct::ElemTypeS(VM &vm, intp i) const {
    ELEMTYPE(elems[i], assert(i < _ti.len));
}

ValueType LVector::ElemType(VM &vm) const {
    ELEMTYPE(subt, {})
}

template<typename T, bool is_vect> void VectorOrStructToString(
        VM &vm, ostringstream &ss, PrintPrefs &pp, T &o, char openb, char closeb) {
    if (pp.cycles >= 0) {
        if (o.refc < 0) {
            o.CycleStr(ss);
            return;
        }
        o.CycleDone(pp.cycles);
    }
    auto &_ti = o.ti(vm);
    (void)_ti;
    if constexpr(!is_vect) ss << vm.ReverseLookupType(_ti.structidx);
    ss << openb;
    auto start_size = ss.tellp();
    intp len;
    if constexpr(is_vect) len = o.len; else len = o.Len(vm);
    for (intp i = 0; i < len; i++) {
        if (i) ss << ", ";
        if ((int)ss.tellp() - start_size > pp.budget) {
            ss << "....";
            break;
        }
        PrintPrefs subpp(pp.depth - 1, pp.budget - (int)(ss.tellp() - start_size), true,
                         pp.decimals, pp.anymark);
        ValueType elemtype;
        if constexpr(is_vect) elemtype = o.ElemType(vm); else elemtype = o.ElemTypeS(vm, i);
        if (pp.depth || !IsRef(elemtype)) {
            Value v;
            if constexpr(is_vect) v = o.At(i);
            else v = o.AtS(i);
            v.ToString(vm, ss, elemtype, subpp);
        } else {
            ss << "..";
        }
    }
    ss << closeb;
}

void LStruct::ToString(VM &vm, ostringstream &ss, PrintPrefs &pp) {
    VectorOrStructToString<LStruct, false>(vm, ss, pp, *this, '{', '}');
}

void LVector::ToString(VM &vm, ostringstream &ss, PrintPrefs &pp) {
    VectorOrStructToString<LVector, true>(vm, ss, pp, *this, '[', ']');
}

}  // namespace lobster
