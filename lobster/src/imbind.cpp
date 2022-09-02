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

#include "imgui.h"
#include "imgui_impl_sdl.h"
#include "imgui_impl_opengl3.h"

#include "lobster/stdafx.h"

#include "lobster/natreg.h"

#define FLATBUFFERS_DEBUG_VERIFICATION_FAILURE
#include "lobster/bytecode_generated.h"

#include "lobster/sdlincludes.h"
#include "lobster/sdlinterface.h"
#include "lobster/glinterface.h"

using namespace lobster;

extern SDL_Window *_sdl_window;
extern SDL_GLContext _sdl_context;
extern SDL_Window *_sdl_debugger_window;
extern SDL_GLContext _sdl_debugger_context;

bool imgui_init = false;

enum Nesting {
    N_NONE,
    N_FRAME,
    N_WIN,
    N_GROUP,
    N_WIDTH,
    N_TREE,
};

vector<Nesting> nstack;

void IMGUIFrameCleanup() {
    nstack.clear();
}

void IMGUICleanup() {
    if (!imgui_init) return;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    imgui_init = false;
    IMGUIFrameCleanup();
}

void NPush(Nesting n) {
    nstack.push_back(n);
}

void NPop(VM &vm, Nesting n) {
    for (;;) {
        if (nstack.empty()) {
            // This should be a rare error given that they're all called from HOFs.
            vm.BuiltinError("imgui: nested end without a start");
        }
        // We pop things regardless if they're the thing we're wanting to pop.
        // This allows the Lobster code to return from a HOF doing start+end, and not
        // get asserts for missing ends.
        auto tn = nstack.back();
        nstack.pop_back();
        switch (tn) {
            case N_FRAME:
                ImGui::Render();
                ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
                break;
            case N_WIN:
                ImGui::End();
                break;
            case N_GROUP:
                ImGui::PopID();
                break;
            case N_WIDTH:
                ImGui::PopItemWidth();
                break;
            case N_TREE:
                ImGui::TreePop();
                break;
 
        }
        // If this was indeed the item we're looking for, we can stop popping.
        if (tn == n) break;
    }
}

void IsInit(VM &vm, Nesting require = N_WIN) {
    if (!imgui_init) vm.BuiltinError("imgui: not running: call im_init first");
    if (require != N_NONE) {
        for (auto n : nstack) if (n == require) return;
        vm.BuiltinError("imgui: invalid nesting (not inside im_window?)");
    }
}

pair<bool, bool> IMGUIEvent(SDL_Event *event) {
    if (!imgui_init) return { false, false };
    ImGui_ImplSDL2_ProcessEvent(event);
    return { ImGui::GetIO().WantCaptureMouse, ImGui::GetIO().WantCaptureKeyboard };
}

bool LoadFont(string_view name, float size) {
    string buf;
    auto l = LoadFile(name, &buf);
    if (l < 0) return false;
    auto mb = malloc(buf.size());  // FIXME.
    assert(mb);
    std::memcpy(mb, buf.data(), buf.size());
    ImFontConfig imfc;
    imfc.FontDataOwnedByAtlas = true;
    auto font =
        ImGui::GetIO().Fonts->AddFontFromMemoryTTF(mb, (int)buf.size(), size, &imfc);
    return font != nullptr;
}

LString *LStringInputText(VM &vm, const char *label, LString *str, ImGuiInputTextFlags flags = 0) {
    struct InputTextCallbackData {
        LString *str;
        VM &vm;
        static int InputTextCallback(ImGuiInputTextCallbackData *data) {
            if (data->EventFlag != ImGuiInputTextFlags_CallbackResize) return 0;
            auto cbd = (InputTextCallbackData *)data->UserData;
            IM_ASSERT(data->Buf == cbd->str->data());
            auto str = cbd->vm.NewString(string_view { data->Buf, (size_t)data->BufTextLen });
            cbd->str->Dec(cbd->vm);
            cbd->str = str;
            data->Buf = (char *)str->data();
            return 0;
        }
    };
    flags |= ImGuiInputTextFlags_CallbackResize;
    InputTextCallbackData cbd { str, vm };
    ImGui::InputText(label, (char *)str->data(), str->len + 1, flags,
                     InputTextCallbackData::InputTextCallback, &cbd);
    return cbd.str;
}

double InputFloat(const char *label, double value, double step = 0, double step_fast = 0, ImGuiInputTextFlags flags = 0) {
    ImGui::InputDouble(label, &value, step, step_fast, "%.3f", flags);
    return value;
}

bool BeginTable() {
    if (ImGui::BeginTable("", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersOuter)) {
        // FIXME: There seems to be no reliable way to make the first column fixed:
        // https://github.com/ocornut/imgui/issues/5478
        ImGui::TableSetupColumn(
            nullptr, ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn(
            nullptr, ImGuiTableColumnFlags_WidthStretch, 4.0f);
        return true;
    }
    return false;
}

void EndTable() {
    ImGui::EndTable();
}

void Nil() {
    auto label = string_view("nil");
    ImGui::TextUnformatted(label.data(), label.data() + label.size());
}

void ValToGUI(VM &vm, Value *v, const TypeInfo &ti, string_view label, bool expanded, bool in_table = true) {
    if (in_table) {
        // Early out for types that don't make sense to display.
        if (ti.t == V_FUNCTION) return;
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(label.data(), label.data() + label.size());
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-1);
        ImGui::PushID(v);  // Name may occur multiple times.
        label = "";
    }
    auto l = null_terminated(label);
    auto flags = expanded ? ImGuiTreeNodeFlags_DefaultOpen : 0;
    switch (ti.t) {
        case V_INT: {
            if (ti.enumidx == 0) {
                assert(vm.EnumName(ti.enumidx) == "bool");
                bool b = v->True();
                if (ImGui::Checkbox(l, &b)) *v = b;
            } else if (ti.enumidx >= 0) {
                int val = v->intval();
                int sel = 0;
                auto &vals = *vm.bcf->enums()->Get(ti.enumidx)->vals();
                vector<const char *> items(vals.size());
                int i = 0;
                for (auto vi : vals) {
                    items[i] = vi->name()->c_str();
                    if (val == vi->val()) sel = i;
                    i++;
                }
                ImGui::Combo(l, &sel, items.data(), (int)items.size());
                *v = vals[sel]->val();
            } else {
                iint i = v->ival();
                if (ImGui::InputScalar(l, ImGuiDataType_S64, (void *)&i, nullptr, nullptr, "%" PRId64, 0)) *v = i;
            }
            break;
        }
        case V_FLOAT: {
            double f = v->fval();
            if (ImGui::InputDouble(l, &f)) *v = f;
            break;
        }
        case V_VECTOR:
            if (v->False()) {
                Nil();
                break;
            }
            if (ImGui::TreeNodeEx(*l ? l : "[]", flags)) {
                if (BeginTable()) {
                    auto &sti = vm.GetTypeInfo(ti.subt);
                    auto vec = v->vval();
                    for (iint i = 0; i < vec->len; i++) {
                        ValToGUI(vm, vec->AtSt(i), sti, to_string(i), false);
                    }
                    EndTable();
                }
                ImGui::TreePop();
            }
            break;
        case V_CLASS:
            if (v->False()) {
                Nil();
                break;
            }
            v = v->oval()->Elems();  // To iterate it like a struct.
        case V_STRUCT_R:
        case V_STRUCT_S: {
            auto st = vm.bcf->udts()->Get(ti.structidx);
            // Special case for numeric structs & colors.
            if (ti.len >= 2 && ti.len <= 4) {
                for (int i = 1; i < ti.len; i++)
                    if (ti.elemtypes[i] != ti.elemtypes[0]) goto generic;
                if (ti.elemtypes[0] == TYPE_ELEM_INT) {
                    auto nums = ValueToI<4>(v, ti.len);
                    if (ImGui::InputScalarN(
                            l, ImGuiDataType_S64,
                            (void *)nums.data(), ti.len, NULL, NULL, "%d", flags)) {
                        ToValue(v, ti.len, nums);
                    }
                    break;
                } else if (ti.elemtypes[0] == TYPE_ELEM_FLOAT) {
                    if (st->name()->string_view() == "color") {
                        auto c = ValueToFLT<4>(v, ti.len);
                        if (ImGui::ColorEdit4(l, (float *)c.data())) {
                            ToValue(v, ti.len, c);
                        }
                    } else {
                        auto nums = ValueToF<4>(v, ti.len);
                        // FIXME: format configurable.
                        if (ImGui::InputScalarN(
                                l,
                                sizeof(double) == sizeof(float) ? ImGuiDataType_Float
                                                                : ImGuiDataType_Double,
                                (void *)nums.data(), ti.len, NULL, NULL, "%.3f", flags)) {
                            ToValue(v, ti.len, nums);
                        }
                    }
                    break;
                }
            }
            generic:
            if (ImGui::TreeNodeEx(*l ? l : st->name()->c_str(), flags)) {
                if (BeginTable()) {
                    auto fields = st->fields();
                    int fi = 0;
                    for (int i = 0; i < ti.len; i++) {
                        auto &sti = vm.GetTypeInfo(ti.GetElemOrParent(i));
                        ValToGUI(vm, v + i, sti, fields->Get(fi++)->name()->string_view(),
                                    false);
                        if (IsStruct(sti.t)) i += sti.len - 1;
                    }
                    EndTable();
                }
                ImGui::TreePop();
            }
            break;
        }
        case V_STRING: {
            if (v->False()) {
                Nil();
                break;
            }
            *v = LStringInputText(vm, l, v->sval());
            break;
        }
        case V_NIL:
            ValToGUI(vm, v, vm.GetTypeInfo(ti.subt), label, expanded, false);
            break;
        default:
            string sd;
            v->ToString(vm, sd, ti, vm.debugpp);
            ImGui::LabelText(l, "%s", sd.c_str());  // FIXME: no formatting?
            break;
    }
    if (in_table) {
        ImGui::PopID();
        //ImGui::PopItemWidth();
    }
}

void VarsToGUI(VM &vm) {
    auto DumpVars = [&](bool constants) {
        if (BeginTable()) {
            for (uint32_t i = 0; i < vm.bcf->specidents()->size(); i++) {
                auto &val = vm.fvars[i];
                auto sid = vm.bcf->specidents()->Get(i);
                auto id = vm.bcf->idents()->Get(sid->ididx());
                if (!id->global() || id->readonly() != constants) continue;
                auto name = id->name()->string_view();
                auto &ti = vm.GetVarTypeInfo(i);
                #if RTT_ENABLED
                if (ti.t != val.type) continue;  // Likely uninitialized.
                #endif
                ValToGUI(vm, &val, ti, name, false);
                if (IsStruct(ti.t)) i += ti.len - 1;
            }
            EndTable();
        }
    };
    if (ImGui::TreeNodeEx("Globals", 0)) {
        DumpVars(false);
        ImGui::TreePop();
    }
    if (ImGui::TreeNodeEx("Constants", 0)) {
        DumpVars(true);
        ImGui::TreePop();
    }
}

void EngineStatsGUI() {
    auto &ft = SDLGetFrameTimeLog();
    ImGui::PlotLines("gl_deltatime", ft.data(), (int)ft.size());
}

// See also VM::DumpStackTrace
void DumpStackTrace(VM &vm) {
    if (vm.fun_id_stack.empty()) return;

    VM::DumperFun dumper = [](VM &vm, string_view name, const TypeInfo &ti, Value *x) {
        #if RTT_ENABLED
            auto debug_type = x->type;
        #else
            auto debug_type = ti.t;
        #endif
        if (debug_type == V_NIL && ti.t != V_NIL) {
            // Uninitialized.
            auto sd = string(name);
            append(sd, ":");
            ti.Print(vm, sd);
            append(sd, " (uninitialized)");
            ImGui::TextUnformatted(sd.data(), sd.data() + sd.size());
        } else if (ti.t != debug_type && !IsStruct(ti.t)) {
            // Some runtime type corruption, show the problem rather than crashing.
            auto sd = string(name);
            append(sd, ":");
            ti.Print(vm, sd);
            append(sd, " (ERROR != ", BaseTypeName(debug_type), ")");
            ImGui::TextUnformatted(sd.data(), sd.data() + sd.size());
        } else {
            ValToGUI(vm, x, ti, name, false);
        }
    };

    for (auto &funstackelem : reverse(vm.fun_id_stack)) {
        auto [name, fip] = vm.DumpStackFrameStart(funstackelem);
        if (ImGui::TreeNode(name.c_str())) {
            if (BeginTable()) {
                vm.DumpStackFrame(fip, funstackelem.locals, dumper);
                EndTable();
            }
            ImGui::TreePop();
        }
    }
}

string BreakPoint(VM &vm, string_view reason) {
    if (!imgui_init) return "Debugger requires im_init()";

    auto cursor_was_on = SDLCursor(true);

    auto err = SDLDebuggerWindow();
    if (!err.empty()) return "Couldn\'t create debugger: " + err;

    auto existing_context = ImGui::GetCurrentContext();
    // FIXME: this is supposed to be able to share the font atlas with the other context,
    // but that seems to destroy it when the debugger context gets destroyed.
    ImGuiContext *debugger_imgui_context = ImGui::CreateContext();
    ImGui::SetCurrentContext(debugger_imgui_context);

    ImGui_ImplSDL2_InitForOpenGL(_sdl_debugger_window, _sdl_debugger_context);
    ImGui_ImplOpenGL3_Init("#version 150");

    // Set our own font.. would be better to inherit the one from the game.
    LoadFont("data/fonts/Droid_Sans/DroidSans.ttf", 16.0);

    bool quit = false;
    int cont = 0;
    for (;;) {
        quit = SDLDebuggerFrame();
        if (quit) break;

        ClearFrameBuffer(float3(0.5f));

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame(_sdl_debugger_window);

        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::GetStyle().WindowRounding = 0.0f;
        ImGui::Begin("Lobster Debugger", nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove);

        if (cont) {
            ImGui::Text("Program Running, debugger inactive");
            // Ensure we've rendered a full frame with the above text before aborting,
            // since this window will get no rendering updates.
            if (++cont == 3) break;
        } else {
            ImGui::TextUnformatted(reason.data(), reason.data() + reason.size());
            if (ImGui::Button("Continue")) {
                cont = 1;
            }
            ImGui::SameLine();
            if (ImGui::Button("Quit")) {
                quit = true;
                break;
            }

            DumpStackTrace(vm);
            VarsToGUI(vm);

            if (ImGui::TreeNode("Memory Usage")) {
                // FIXME: imgui-ify? Table?
                auto mu = vm.MemoryUsage(25);
                ImGui::TextUnformatted(mu.data(), mu.data() + mu.size());
                ImGui::TreePop();
            }
        }

        ImGui::End();

        ImGui::Render();
        SetTexture(0, Texture{}, 0);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();

    ImGui::SetCurrentContext(existing_context);
    ImGui::DestroyContext(debugger_imgui_context);

    SDLDebuggerOff();

    if (quit) vm.NormalExit("Program terminated by debugger");

    SDLCursor(cursor_was_on);

    return "";
}

void AddIMGUI(NativeRegistry &nfr) {

nfr("im_init", "dark_style,flags", "B?I?", "",
    "",
    [](StackPtr &, VM &, Value &darkstyle, Value &flags) {
        if (imgui_init) return NilVal();
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::GetIO().ConfigFlags |= (ImGuiConfigFlags)flags.ival();
        if (darkstyle.True()) ImGui::StyleColorsDark(); else ImGui::StyleColorsClassic();
        ImGui_ImplSDL2_InitForOpenGL(_sdl_window, _sdl_context);
        ImGui_ImplOpenGL3_Init(
            #ifdef PLATFORM_ES3
                "#version 300 es"
            #else
                "#version 150"
            #endif
        );
        imgui_init = true;
        return NilVal();
    });

nfr("im_add_font", "font_path,size", "SF", "B",
    "",
    [](StackPtr &, VM &vm, Value &fontname, Value &size) {
        IsInit(vm, N_NONE);
        return Value(LoadFont(fontname.sval()->strv(), size.fltval()));
    });

nfr("im_frame_start", "", "", "",
    "(use im_frame instead)",
    [](StackPtr &, VM &vm) {
        IsInit(vm, N_NONE);
        IMGUIFrameCleanup();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame(_sdl_window);
        ImGui::NewFrame();
        NPush(N_FRAME);
    });

nfr("im_frame_end", "", "", "",
    "",
    [](StackPtr &, VM &vm) {
        IsInit(vm, N_NONE);
        NPop(vm, N_FRAME);
    });

nfr("im_window_demo", "", "", "B",
    "",
    [](StackPtr &, VM &vm) {
        IsInit(vm, N_FRAME);
        bool show = true;
        ImGui::ShowDemoWindow(&show);
        return Value(show);
    });

nfr("im_window_start", "title,flags", "SI", "",
    "(use im_window instead)",
    [](StackPtr &sp, VM &vm) {
        IsInit(vm, N_FRAME);
        auto flags = Pop(sp);
        auto title = Pop(sp);
        ImGui::Begin(title.sval()->data(), nullptr, (ImGuiWindowFlags)flags.ival());
        NPush(N_WIN);
    });

nfr("im_window_end", "", "", "",
    "",
    [](StackPtr &, VM &vm) {
        IsInit(vm, N_FRAME);
        NPop(vm, N_WIN);
    });

nfr("im_button", "label", "S", "B",
    "",
    [](StackPtr &sp, VM &vm) {
        IsInit(vm);
        auto title = Pop(sp);
        auto press = ImGui::Button(title.sval()->data());
        Push(sp, press);
    });

nfr("im_same_line", "", "", "", "",
    [](StackPtr &, VM &vm) {
        IsInit(vm);
        ImGui::SameLine();
        return NilVal();
    });

nfr("im_separator", "", "", "",
    "",
    [](StackPtr &, VM &vm) {
        IsInit(vm);
        ImGui::Separator();
        return NilVal();
    });

nfr("im_text", "label", "S", "",
    "",
    [](StackPtr &, VM &vm, Value &text) {
        IsInit(vm);
        auto &s = *text.sval();
        ImGui::TextUnformatted(s.data(), s.data() + s.len);
        return NilVal();
    });

nfr("im_tooltip", "label", "S", "",
    "",
    [](StackPtr &, VM &vm, Value &text) {
        IsInit(vm);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", text.sval()->data());
        return NilVal();
    });

nfr("im_checkbox", "label,bool", "SI", "I2",
    "",
    [](StackPtr &, VM &vm, Value &text, Value &boolval) {
        IsInit(vm);
        bool b = boolval.True();
        ImGui::Checkbox(text.sval()->data(), &b);
        return Value(b);
    });

nfr("im_input_text", "label,str", "SSk", "S",
    "",
    [](StackPtr &, VM &vm, Value &text, Value &str) {
        IsInit(vm);
        return Value(LStringInputText(vm, text.sval()->data(), str.sval()));
    });

nfr("im_input_float", "label,val", "SF", "F",
    "",
    [](StackPtr &, VM &vm, Value &text, Value &val) {
        IsInit(vm);
        return Value(InputFloat(text.sval()->data(), val.fval()));
    });

nfr("im_radio", "labels,active,horiz", "S]II", "I",
    "",
    [](StackPtr &, VM &vm, Value &strs, Value &active, Value &horiz) {
        IsInit(vm);
        int sel = active.intval();
        for (iint i = 0; i < strs.vval()->len; i++) {
            if (i && horiz.True()) ImGui::SameLine();
            ImGui::RadioButton(strs.vval()->At(i).sval()->data(), &sel, (int)i);
        }
        return Value(sel);
    });

nfr("im_combo", "label,labels,active", "SS]I", "I",
    "",
    [](StackPtr &, VM &vm, Value &text, Value &strs, Value &active) {
        IsInit(vm);
        int sel = active.intval();
        vector<const char *> items(strs.vval()->len);
        for (iint i = 0; i < strs.vval()->len; i++) {
            items[i] = strs.vval()->At(i).sval()->data();
        }
        ImGui::Combo(text.sval()->data(), &sel, items.data(), (int)items.size());
        return Value(sel);
    });

nfr("im_listbox", "label,labels,active,height", "SS]II", "I",
    "",
    [](StackPtr &, VM &vm, Value &text, Value &strs, Value &active, Value &height) {
        IsInit(vm);
        int sel = active.intval();
        vector<const char *> items(strs.vval()->len);
        for (iint i = 0; i < strs.vval()->len; i++) {
            items[i] = strs.vval()->At(i).sval()->data();
        }
        ImGui::ListBox(text.sval()->data(), &sel, items.data(), (int)items.size(), height.intval());
        return Value(sel);
    });

nfr("im_sliderint", "label,i,min,max", "SIII", "I",
    "",
    [](StackPtr &, VM &vm, Value &text, Value &integer, Value &min, Value &max) {
        IsInit(vm);
        int i = integer.intval();
        ImGui::SliderInt(text.sval()->data(), &i, min.intval(), max.intval());
        return Value(i);
    });

nfr("im_sliderfloat", "label,f,min,max", "SFFF", "F",
    "",
    [](StackPtr &, VM &vm, Value &text, Value &flt, Value &min, Value &max) {
        IsInit(vm);
        float f = flt.fltval();
        ImGui::SliderFloat(text.sval()->data(), &f, min.fltval(), max.fltval());
        return Value(f);
    });

nfr("im_coloredit", "label,color", "SF}", "A2",
    "",
    [](StackPtr &sp, VM &vm) {
        IsInit(vm);
        auto c = PopVec<float4>(sp);
        ImGui::ColorEdit4(Pop(sp).sval()->data(), (float *)c.data());
        PushVec(sp, c);
    });

nfr("im_treenode_start", "label", "S", "B",
    "(use im_treenode instead)",
    [](StackPtr &sp, VM &vm) {
        IsInit(vm);
        auto title = Pop(sp);
        bool open = ImGui::TreeNode(title.sval()->data());
        Push(sp, open);
        if (open) NPush(N_TREE);
    });

nfr("im_treenode_end", "", "", "",
    "",
    [](StackPtr &, VM &vm) {
        IsInit(vm);
        NPop(vm, N_TREE);
    });

nfr("im_group_start", "label", "Ss", "",
    "an invisble group around some widgets, useful to ensure these widgets are unique"
    " (if they have the same label as widgets in another group that has a different group"
    " label). Use im_group instead",
    [](StackPtr &sp, VM &vm) {
        IsInit(vm);
        auto title = Pop(sp);
        ImGui::PushID(title.sval()->data());
        NPush(N_GROUP);
    });

nfr("im_group_end", "", "", "",
    "",
    [](StackPtr &, VM &vm) {
        IsInit(vm);
        NPop(vm, N_GROUP);
    });

nfr("im_disabled_start", "disabled", "B", "",
    "(use im_disabled instead)",
    [](StackPtr &sp, VM &vm) {
        IsInit(vm);
        const auto disabled = Pop(sp).True();
        ImGui::BeginDisabled(disabled);
    });

nfr("im_disabled_end", "", "", "",
    "",
    [](StackPtr &, VM &vm) {
        IsInit(vm);
        ImGui::EndDisabled();
    });

nfr("im_width_start", "width", "F", "",
    "Sets the width of an item: 0 = default, -1 = use full width without label,"
    " any other value is custom width. Use im_width instead",
    [](StackPtr &sp, VM &vm) {
        IsInit(vm);
        auto width = Pop(sp).fltval();
        ImGui::PushItemWidth(width);
        NPush(N_WIDTH);
    });

nfr("im_width_end", "", "", "", "", [](StackPtr &, VM &vm) {
    IsInit(vm);
    NPop(vm, N_WIDTH);
});

nfr("im_edit_anything", "value,label", "AkS?", "A1",
    "creates a UI for any lobster reference value, and returns the edited version",
    [](StackPtr &, VM &vm, Value &v, Value &label) {
        IsInit(vm);
        // FIXME: would be good to support structs, but that requires typeinfo, not just len.
        auto &ti = vm.GetTypeInfo(v.True() ? v.ref()->tti : TYPE_ELEM_ANY);
        ValToGUI(vm, &v, ti, label.True() ? label.sval()->strv() : "", true, false);
        return v;
    });

nfr("im_graph", "label,values,ishistogram", "SF]I", "",
    "",
    [](StackPtr &, VM &vm, Value &label, Value &vals, Value &histogram) {
        IsInit(vm);
        auto getter = [](void *data, int i) -> float {
            return ((Value *)data)[i].fltval();
        };
        if (histogram.True()) {
            ImGui::PlotHistogram(label.sval()->data(), getter, vals.vval()->Elems(),
                (int)vals.vval()->len);
        } else {
            ImGui::PlotLines(label.sval()->data(), getter, vals.vval()->Elems(),
                (int)vals.vval()->len);
        }
        return NilVal();
    });

nfr("im_show_vars", "", "", "",
    "shows an automatic editing UI for each global variable in your program",
    [](StackPtr &, VM &vm) {
        IsInit(vm);
        VarsToGUI(vm);
        return NilVal();
    });

nfr("im_show_engine_stats", "", "", "",
    "",
    [](StackPtr &, VM &vm) {
        IsInit(vm);
        EngineStatsGUI();
        return NilVal();
    });

nfr("breakpoint", "condition", "I", "",
    "stops the program in the debugger if passed true."
    " debugger needs --runtime-verbose on, and im_init() to have run.",
    [](StackPtr &, VM &vm, Value &c) {
        if (c.True()) {
            auto err = BreakPoint(vm, "Conditional breakpoint hit!");
            if (!err.empty()) vm.Error(err);
        }
        return NilVal();
    });

nfr("breakpoint", "", "", "",
    "stops the program in the debugger always."
    " debugger needs --runtime-verbose on, and im_init() to have run.",
    [](StackPtr &, VM &vm) {
        auto err = BreakPoint(vm, "Breakpoint hit!");
        if (!err.empty()) vm.Error(err);
        return NilVal();
    });

}  // AddIMGUI
