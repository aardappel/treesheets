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

void GVMAssert(const char *what) {
    g_vm->VMAssert(what);
}

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

LVector::LVector(intp _initial, intp _max, type_elem_t _tti)
    : RefObj(_tti), len(_initial), maxl(_max) {
    v = maxl ? AllocSubBuf<Value>(maxl, TYPE_ELEM_VALUEBUF) : nullptr;
}

void LVector::Resize(intp newmax) {
    // FIXME: check overflow
    auto mem = AllocSubBuf<Value>(newmax, TYPE_ELEM_VALUEBUF);
    if (len) memcpy(mem, v, sizeof(Value) * len);
    DeallocBuf();
    maxl = newmax;
    v = mem;
}

void LVector::Append(LVector *from, intp start, intp amount) {
    if (len + amount > maxl) Resize(len + amount);  // FIXME: check overflow
    memcpy(v + len, from->v + start, sizeof(Value) * amount);
    if (IsRefNil(from->ElemType())) {
        for (int i = 0; i < amount; i++) v[len + i].INCRTNIL();
    }
    len += amount;
}

void RefObj::DECDELETE(bool deref) {
    assert(refc == 0);
    switch (ti().t) {
        case V_BOXEDINT:   vmpool->dealloc(this, sizeof(BoxedInt)); break;
        case V_BOXEDFLOAT: vmpool->dealloc(this, sizeof(BoxedFloat)); break;
        case V_STRING:     ((LString *)this)->DeleteSelf(); break;
        case V_COROUTINE:  ((LCoRoutine *)this)->DeleteSelf(deref); break;
        case V_VECTOR:     ((LVector *)this)->DeleteSelf(deref); break;
        case V_STRUCT:     ((LStruct *)this)->DeleteSelf(deref); break;
        case V_RESOURCE:   ((LResource *)this)->DeleteSelf(); break;
        default:           assert(false);
    }
}

bool RefEqual(const RefObj *a, const RefObj *b, bool structural) {
    if (a == b) return true;
    if (!a || !b) return false;
    if (a->tti != b->tti) return false;
    switch (a->ti().t) {
        case V_BOXEDINT:    return ((BoxedInt *)a)->val == ((BoxedInt *)b)->val;
        case V_BOXEDFLOAT:  return ((BoxedFloat *)a)->val == ((BoxedFloat *)b)->val;
        case V_STRING:      return *((LString *)a) == *((LString *)b);
        case V_COROUTINE:   return false;
        case V_VECTOR:      return structural && ((LVector *)a)->Equal(*(LVector *)b);
        case V_STRUCT:      return structural && ((LStruct *)a)->Equal(*(LStruct *)b);
        default:            assert(0); return false;
    }
}

bool Value::Equal(ValueType vtype, const Value &o, ValueType otype, bool structural) const {
    if (vtype != otype) return false;
    switch (vtype) {
        case V_INT: return ival_ == o.ival_;
        case V_FLOAT: return fval_ == o.fval_;
        case V_FUNCTION: return ip_ == o.ip_;
        default: return RefEqual(refnil(), o.ref_, structural);
    }
}

void RefToString(ostringstream &ss, const RefObj *ro, PrintPrefs &pp) {
    if (!ro) { ss << "nil"; return; }
    auto &roti = ro->ti();
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
        case V_VECTOR:    ((LVector *)ro)->ToString(ss, pp);        break;
        case V_STRUCT:    ((LStruct *)ro)->ToString(ss, pp);        break;
        default:          ss << '(' << BaseTypeName(roti.t) << ')'; break;
    }
}

void Value::ToString(ostringstream &ss, ValueType vtype, PrintPrefs &pp) const {
    if (IsRefNil(vtype)) {
        if (ref_) RefToString(ss, ref_, pp); else ss << "nil";
    }
    else switch (vtype) {
        case V_INT:      ss << ival();                                    break;
        case V_FLOAT:    ss << to_string_float(fval(), (int)pp.decimals); break;
        case V_FUNCTION: ss << "<FUNCTION>";                              break;
        default:         ss << '(' << BaseTypeName(vtype) << ')';         break;
    }
}


void RefObj::Mark() {
    if (refc < 0) return;
    assert(refc);
    refc = -refc;
    switch (ti().t) {
        case V_STRUCT:     ((LStruct    *)this)->Mark(); break;
        case V_VECTOR:     ((LVector    *)this)->Mark(); break;
        case V_COROUTINE:  ((LCoRoutine *)this)->Mark(); break;
    }
}

intp RefObj::Hash() {
    switch (ti().t) {
        case V_BOXEDINT:    return ((BoxedInt *)this)->val;
        case V_BOXEDFLOAT:  return ReadMem<intp>(&((BoxedFloat *)this)->val);
        case V_STRING:      return ((LString *)this)->Hash();
        case V_VECTOR:      return ((LVector *)this)->Hash();
        case V_STRUCT:      return ((LStruct *)this)->Hash();
        default:            return (int)(size_t)this;
    }
}

intp LString::Hash() {
    return (int)FNV1A(str());
}

intp Value::Hash(ValueType vtype) {
    switch (vtype) {
        case V_INT: return ival_;
        case V_FLOAT: return ReadMem<intp>(&fval_);
        case V_FUNCTION: return (intp)(size_t)ip_.f;
        default: return refnil() ? ref()->Hash() : 0;
    }
}

void Value::Mark(ValueType vtype) {
    if (IsRefNil(vtype) && ref_) ref_->Mark();
}

void Value::MarkRef() {
    if (ref_) ref_->Mark();
}

Value Value::Copy() {
    if (!refnil()) return Value();
    auto &ti = ref()->ti();
    switch (ti.t) {
    case V_VECTOR: {
        auto len = vval()->len;
        auto nv = g_vm->NewVec(len, len, vval()->tti);
        if (len) nv->Init(&vval()->At(0), true);
        DECRT();
        return Value(nv);
    }
    case V_STRUCT: {
        auto len = stval()->Len();
        auto nv = g_vm->NewStruct(len, stval()->tti);
        if (len) nv->Init(&stval()->At(0), len, true);
        DECRT();
        return Value(nv);
    }
    case V_STRING: {
        auto s = g_vm->NewString(sval()->strv());
        DECRT();
        return Value(s);
    }
    case V_BOXEDINT: {
        auto bi = g_vm->NewInt(bival()->val);
        DECRT();
        return Value(bi);
    }
    case V_BOXEDFLOAT: {
        auto bf = g_vm->NewFloat(bfval()->val);
        DECRT();
        return Value(bf);
    }
    case V_COROUTINE:
        g_vm->Error("cannot copy coroutine");
        return Value();
    default:
        assert(false);
        return Value();
    }
}

string TypeInfo::Debug(bool rec) const {
    string s;
    s += BaseTypeName(t);
    if (t == V_VECTOR || t == V_NIL) {
        s += "[" + g_vm->GetTypeInfo(subt).Debug(false) + "]";
    } else if (t == V_STRUCT) {
        auto sname = g_vm->StructName(*this);
        s += ":" + sname;
        if (rec) {
            s += "{";
            for (int i = 0; i < len; i++)
                s += g_vm->GetTypeInfo(elems[i]).Debug(false) + ",";
            s += "}";
        }
    }
    return s;
}

#define ELEMTYPE(acc) \
    auto &_ti = ti(); \
    auto &sti = g_vm->GetTypeInfo(_ti.acc); \
    auto vt = sti.t; \
    if (vt == V_NIL) vt = g_vm->GetTypeInfo(sti.subt).t; \
    return vt;

ValueType LStruct::ElemType(intp i) const {
    ELEMTYPE(elems[i])
}

ValueType LVector::ElemType() const {
    ELEMTYPE(subt)
}

template<typename T, bool is_vect> void VectorOrStructToString(
        ostringstream &ss, PrintPrefs &pp, T &o, char openb, char closeb) {
    if (pp.cycles >= 0) {
        if (o.refc < 0) {
            o.CycleStr(ss);
            return;
        }
        o.CycleDone(pp.cycles);
    }
    auto &_ti = o.ti();
    (void)_ti;
    if constexpr(!is_vect) ss << g_vm->ReverseLookupType(_ti.structidx);
    ss << openb;
    auto start_size = ss.tellp();
    intp len;
    if constexpr(is_vect) len = o.len; else len = o.Len();
    for (intp i = 0; i < len; i++) {
        if (i) ss << ", ";
        if ((int)ss.tellp() - start_size > pp.budget) {
            ss << "....";
            break;
        }
        PrintPrefs subpp(pp.depth - 1, pp.budget - (int)(ss.tellp() - start_size), true,
                         pp.decimals, pp.anymark);
        ValueType elemtype;
        if constexpr(is_vect) elemtype = o.ElemType(); else elemtype = o.ElemType(i);
        if (pp.depth || !IsRef(elemtype))
            o.At(i).ToString(ss, elemtype, subpp);
        else
            ss << "..";
    }
    ss << closeb;
}

void LStruct::ToString(ostringstream &ss, PrintPrefs &pp) {
    VectorOrStructToString<LStruct, false>(ss, pp, *this, '{', '}');
}

void LVector::ToString(ostringstream &ss, PrintPrefs &pp) {
    VectorOrStructToString<LVector, true>(ss, pp, *this, '[', ']');
}

}  // namespace lobster
