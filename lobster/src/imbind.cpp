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
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"

#include "lobster/stdafx.h"

#include "lobster/natreg.h"

#define FLATBUFFERS_DEBUG_VERIFICATION_FAILURE
#include "lobster/bytecode_generated.h"

#include "lobster/sdlincludes.h"
#include "lobster/sdlinterface.h"
#include "lobster/glinterface.h"

#include "lobster/graphics.h"

using namespace lobster;

extern SDL_Window *_sdl_window;
extern SDL_GLContext _sdl_context;
extern SDL_Window *_sdl_debugger_window;
extern SDL_GLContext _sdl_debugger_context;

bool imgui_init = false;

enum Nesting {
    N_NONE,
    N_FRAME,
    N_MAIN_MENU_BAR,
    N_WIN,
    N_MENU_BAR,
    N_MENU,
    N_TAB_BAR,
    N_TAB,
    N_ID,
    N_WIDTH,
    N_TREE,
    N_POPUP,
    N_CHILD,
    N_VGROUP,
    N_TOOLTIP,
    N_FONT,
    N_COLOR,
    N_DRAG_DROP_SOURCE,
    N_DRAG_DROP_TARGET,
    N_TABLE,
};

vector<Nesting> nstack;
ImGuiID last_dock_id = 0;
struct ListSelect { int sel = -1; int64_t last_use = -1; };
map<ImGuiID, ListSelect> list_state;
int64_t imgui_frame = 0;

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

bool IMGUIInit(iint flags, bool dark, float rounding, float border) {
    if (imgui_init) return true;
    if (!_sdl_window || !_sdl_context) return false;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= (ImGuiConfigFlags)flags;
    if (dark)
        ImGui::StyleColorsDark();
    else
        ImGui::StyleColorsClassic();
    auto r = rounding;
    // For some reason active tabs are by default almost the same color as inactive ones, fix that.
    // TODO: instead, expose color/style API thru bindings.
    auto &style = ImGui::GetStyle();
    style.WindowBorderSize = style.ChildBorderSize = style.PopupBorderSize = border;
    style.Colors[ImGuiCol_Tab].w = 0.37f;  // More transparent.
    style.FrameRounding = r;
    style.WindowRounding = r;
    style.GrabRounding = r;
    style.ChildRounding = r;
    style.PopupRounding = r;
    style.ScrollbarRounding = r;
    style.TabRounding = r;
    if (IsSRGBMode()) {
        // Colors are specified as SRGB, so pre-convert them if we're using linear.
        auto cols = ImGui::GetStyle().Colors;
        for (int i = 0; i < ImGuiCol_COUNT; i++) {
            cols[i].x = powf(cols[i].x, 2.2f);
            cols[i].y = powf(cols[i].y, 2.2f);
            cols[i].z = powf(cols[i].z, 2.2f);
        }
    }
    ImGui_ImplSDL2_InitForOpenGL(_sdl_window, _sdl_context);
    ImGui_ImplOpenGL3_Init(
        #ifdef PLATFORM_ES3
            "#version 300 es"
        #else
            "#version 150"
        #endif
    );
    imgui_init = true;
    return true;
}

void NPush(Nesting n) {
    nstack.push_back(n);
}

Nesting NTop() {
    return nstack.empty() ? N_NONE : nstack.back();
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
                // FIXME: this seems buggy.
                /*
                // Update and Render additional Platform Windows
                if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
                    ImGui::UpdatePlatformWindows();
                    ImGui::RenderPlatformWindowsDefault();
                }
                */
                break;
            case N_WIN:
                ImGui::End();
                break;
            case N_TAB_BAR:
                ImGui::EndTabBar();
                break;
            case N_TAB:
                ImGui::EndTabItem();
                break;            
            case N_ID:
                ImGui::PopID();
                break;
            case N_WIDTH:
                ImGui::PopItemWidth();
                break;
            case N_TREE:
                ImGui::TreePop();
                break;
            case N_POPUP:
                ImGui::EndPopup();
                break;
            case N_CHILD:
                ImGui::EndChild();
                break;
            case N_VGROUP:
                ImGui::EndGroup();
                break;
            case N_MENU_BAR:
                ImGui::EndMenuBar();
                break;
            case N_MAIN_MENU_BAR:
                ImGui::EndMainMenuBar();
                break;
            case N_MENU:
                ImGui::EndMenu();
                break;
            case N_TOOLTIP:
                ImGui::EndTooltip();
                break;
            case N_FONT:
                ImGui::PopFont();
                break;
            case N_COLOR:
                ImGui::PopStyleColor();
                break;
            case N_DRAG_DROP_SOURCE:
                ImGui::EndDragDropSource();
                break;
            case N_DRAG_DROP_TARGET:
                ImGui::EndDragDropTarget();
                break;
            case N_TABLE:
                ImGui::EndTable();
                break;
        }
        // If this was indeed the item we're looking for, we can stop popping.
        if (tn == n) break;
    }
}

void IsInit(VM &vm, pair<Nesting, Nesting> require = { N_WIN, N_MENU }) {
    if (!imgui_init) vm.BuiltinError("imgui: not running: call im.init first");
    for (auto n : nstack) if (n == require.first || n == require.second) return;
    if (require.first != N_NONE || require.second != N_NONE) {
        vm.BuiltinError("imgui: invalid nesting (not inside im.window?)");
    }
}

void RequireMenuNesting(VM &vm) {
    for (auto n : nstack) if (n == N_MENU_BAR || n == N_MAIN_MENU_BAR || n == N_POPUP) return;
    vm.BuiltinError("imgui: invalid menu nesting");
}

pair<bool, bool> IMGUIEvent(SDL_Event *event) {
    if (!imgui_init) return { false, false };
    switch (event->type) {
        case SDL_KEYDOWN:
        case SDL_KEYUP: {
            // ImGui likes to use ESC for cancelling edit actions, but pretty much
            // every game uses ESC to switch menus, which would cause users wanting to
            // switch menus to lose their edits unintentionally.
            // TODO: make this behavior configurable from Lobster code?
            if (event->key.keysym.sym == SDLK_ESCAPE) return { false, false };
            break;
        }
    }
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

LString *LStringInputText(VM &vm, const char *label, LString *str, int num_lines = 1) {
    struct InputTextCallbackData {
        LString *str;
        VM &vm;
        static int InputTextCallback(ImGuiInputTextCallbackData *data) {
            auto cbd = (InputTextCallbackData *)data->UserData;
            if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
                IM_ASSERT(data->Buf == cbd->str->data());
                auto str = cbd->vm.NewString(string_view{ data->Buf, (size_t)data->BufTextLen });
                cbd->str->Dec(cbd->vm);
                cbd->str = str;
                data->Buf = (char *)str->data();
            } else if (data->EventFlag == ImGuiInputTextFlags_CallbackEdit) {
                // If we get here the widget changed the buffer, so important we
                // do nothing and do not overwrite.
            } else if (data->EventFlag == ImGuiInputTextFlags_CallbackAlways) {
                if (strcmp(cbd->str->data(), data->Buf)) {
                    // If we get here, the program is trying to overwrite the widget text.
                    // https://github.com/ocornut/imgui/issues/5377
                    auto cursor = data->CursorPos;
                    data->DeleteChars(0, data->BufTextLen);
                    data->InsertChars(0, cbd->str->data());
                    if (cursor <= data->BufTextLen) {
                        // For algorithmic changes to the input data, nice to not lose cursor pos,
                        // though for switching unrelated data would be better not?
                        data->CursorPos = cursor;
                    }
                }
            }
            return 0;
        }
    };
    ImGuiInputTextFlags flags = ImGuiInputTextFlags_CallbackResize |
                                ImGuiInputTextFlags_CallbackEdit |
                                ImGuiInputTextFlags_CallbackAlways;
    InputTextCallbackData cbd { str, vm };
    if (num_lines > 1) {
        ImGui::InputTextMultiline(label, (char *)str->data(), str->len + 1,
            ImVec2(ImGui::CalcItemWidth(), ImGui::GetTextLineHeight() * num_lines), flags,
            InputTextCallbackData::InputTextCallback, &cbd);
    } else {
        ImGui::InputText(label, (char *)str->data(), str->len + 1, flags,
            InputTextCallbackData::InputTextCallback, &cbd);
    }
    return cbd.str;
}

double InputFloat(const char *label, double value, double step = 0, double step_fast = 0, ImGuiInputTextFlags flags = 0) {
    ImGui::InputDouble(label, &value, step, step_fast, "%.3f", flags);
    return value;
}

bool BeginTable(const char *id) {
    if (ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersOuter)) {
        ImGui::TableSetupColumn("1", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("2", ImGuiTableColumnFlags_WidthStretch);
        return true;
    }
    return false;
}

void EndTable() {
    ImGui::EndTable();
}

void Text(string_view s) {
    ImGui::TextUnformatted(s.data(), s.data() + s.size());
}

void Nil() {
    Text("nil");
}

void ValToGUI(VM &vm, Value *v, const TypeInfo *ti, string_view_nt label, bool expanded, bool in_table = true) {
    if (in_table) {
        // Early out for types that don't make sense to display.
        if (ti->t == V_FUNCTION) return;
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        Text(label.sv);
        ImGui::TableSetColumnIndex(1);
        // This seems to be the only way to make the second column stretch to the available space without it
        // collapsing to nothing in some cases: https://github.com/ocornut/imgui/issues/5478
        ImGui::SetNextItemWidth(std::max(200.0f, ImGui::GetContentRegionAvail().x));
        ImGui::PushID(v);  // Name may occur multiple times.
        label = string_view_nt("");
    }
    auto l = label.c_str();
    auto flags = expanded ? ImGuiTreeNodeFlags_DefaultOpen : 0;
    switch (ti->t) {
        case V_INT: {
            if (ti->enumidx == 0) {
                assert(vm.EnumName(ti->enumidx) == "bool");
                bool b = v->True();
                if (ImGui::Checkbox(l, &b)) *v = b;
            } else if (ti->enumidx >= 0) {
                int val = v->intval();
                int sel = 0;
                auto &vals = *vm.bcf->enums()->Get(ti->enumidx)->vals();
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
            if (!v->vval()->len) {
                Text("[]");
                break;
            }
            if (ImGui::TreeNodeEx(*l ? l : "[..]", flags)) {
                if (BeginTable("[]")) {
                    auto &sti = vm.GetTypeInfo(ti->subt);
                    auto vec = v->vval();
                    for (iint i = 0; i < vec->len; i++) {
                        ValToGUI(vm, vec->AtSt(i), &sti, to_string(i), false);
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
            // Upgrade to dynamic type if maybe subclass.
            ti = &v->oval()->ti(vm);
            v = v->oval()->Elems();  // To iterate it like a struct.
        case V_STRUCT_R:
        case V_STRUCT_S: {
            auto st = vm.bcf->udts()->Get(ti->structidx);
            // Special case for numeric structs & colors.
            if (ti->len >= 2 && ti->len <= 4) {
                for (int i = 1; i < ti->len; i++)
                    if (ti->elemtypes[i].type != ti->elemtypes[0].type) goto generic;
                if (ti->elemtypes[0].type == TYPE_ELEM_INT) {
                    auto nums = ValueToI<4>(v, ti->len);
                    if (ImGui::InputScalarN(
                            l, ImGuiDataType_S64,
                            (void *)nums.data(), ti->len, NULL, NULL, "%d", flags)) {
                        ToValue(v, ti->len, nums);
                    }
                    break;
                } else if (ti->elemtypes[0].type == TYPE_ELEM_FLOAT) {
                    if (st->name()->string_view() == "color") {
                        auto c = ValueToFLT<4>(v, ti->len);
                        if (ImGui::ColorEdit4(l, (float *)c.data())) {
                            ToValue(v, ti->len, c);
                        }
                    } else {
                        auto nums = ValueToF<4>(v, ti->len);
                        // FIXME: format configurable.
                        if (ImGui::InputScalarN(
                                l,
                                sizeof(double) == sizeof(float) ? ImGuiDataType_Float
                                                                : ImGuiDataType_Double,
                                (void *)nums.data(), ti->len, NULL, NULL, "%.3f", flags)) {
                            ToValue(v, ti->len, nums);
                        }
                    }
                    break;
                }
            }
            generic:
            if (ImGui::TreeNodeEx(*l ? l : st->name()->c_str(), flags)) {
                if (BeginTable(st->name()->c_str())) {
                    auto fields = st->fields();
                    int fi = 0;
                    for (int i = 0; i < ti->len; i++) {
                        auto &sti = vm.GetTypeInfo(ti->GetElemOrParent(i));
                        ValToGUI(vm, v + i, &sti, string_view_nt(fields->Get(fi++)->name()->string_view()),
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
            ValToGUI(vm, v, &vm.GetTypeInfo(ti->subt), label, expanded, false);
            break;
        case V_RESOURCE: {
            if (v->False()) {
                Nil();
                break;
            }
            auto r = v->xval();
            // This would be awesome, but can't work as-is because the texture is in a different
            // context, to fix this, we'd need a vendor specific call like
            // https://registry.khronos.org/OpenGL/extensions/NV/NV_copy_image.txt
            // or a simple thing like switching context, glReadPixels etc, which will be super
            // slow.
            /*
            if (r->type == &texture_type) {
                auto t = (OwnedTexture *)r->res;
                auto tex = (ImTextureID)(size_t)t->t.id;
                auto sz = float2(t->t.size.xy()) / 4.0f;
                ImGui::Image(tex, ImVec2(sz.x, sz.y));
                break;
            }
            */
            string sd;
            auto mu = r->res->MemoryUsage();
            append(sd, "resource<", r->type->name, "> (", mu.x + mu.y, " bytes) ");
            r->res->Dump(sd);
            Text(sd);
            break;
        }
        default:
            string sd;
            v->ToString(vm, sd, *ti, vm.debugpp);
            Text(sd);
            break;
    }
    if (in_table) {
        ImGui::PopID();
        //ImGui::PopItemWidth();
    }
}

void VarsToGUI(VM &vm) {
    auto DumpVars = [&](bool constants) {
        if (BeginTable("vars")) {
            for (uint32_t i = 0; i < vm.bcf->specidents()->size(); i++) {
                auto &val = vm.fvars[i];
                auto sid = vm.bcf->specidents()->Get(i);
                auto id = vm.bcf->idents()->Get(sid->ididx());
                if (!id->global() || id->readonly() != constants) continue;
                auto name = string_view_nt(id->name()->string_view());
                auto &ti = vm.GetVarTypeInfo(i);
                #if RTT_ENABLED
                if (ti.t != val.type) continue;  // Likely uninitialized.
                #endif
                ValToGUI(vm, &val, &ti, name, false);
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
    ImGui::PlotLines("gl.deltatime", ft.data(), (int)ft.size());
}

void FlexBufferGUI(flexbuffers::Reference r, const char *label, bool in_table) {
    if (in_table) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        Text(label);
        ImGui::TableSetColumnIndex(1);
        // This seems to be the only way to make the second column stretch to the available space
        // without it collapsing to nothing in some cases:
        // https://github.com/ocornut/imgui/issues/5478
        ImGui::SetNextItemWidth(std::max(200.0f, ImGui::GetContentRegionAvail().x));
        ImGui::PushID(label);  // Name may occur multiple times.
    }
    switch (r.GetType()) {
        case flexbuffers::FBT_MAP: {
            if (ImGui::TreeNodeEx("{..}", 0)) {
                if (BeginTable(label)) {
                    auto m = r.AsMap();
                    auto keys = m.Keys();
                    auto vals = m.Values();
                    for (size_t i = 0; i < keys.size(); i++) {
                        string key;
                        keys[i].ToString(true, false, key);
                        FlexBufferGUI(vals[i], key.c_str(), true);
                    }
                    EndTable();
                }
                ImGui::TreePop();
            }
            break;
        }
        case flexbuffers::FBT_VECTOR: {
            if (ImGui::TreeNodeEx("[..]", 0)) {
                if (BeginTable(label)) {
                    auto vals = r.AsVector();
                    for (size_t i = 0; i < vals.size(); i++) {
                        auto labeli = cat(i);
                        FlexBufferGUI(vals[i], labeli.c_str(), true);
                    }
                    EndTable();
                }
                ImGui::TreePop();
            }
            break;
        }
        default: {
            auto s = r.ToString();
            ImGui::LabelText(label, "%s", s.c_str());
            break;
        }
    }
    if (in_table) {
        ImGui::PopID();
    }
}

// See also VM::DumpStackTrace and VM::DumpStackTraceMemory()
void DumpStackTrace(VM &vm) {
    if (vm.fun_id_stack.empty()) return;

    VM::DumperFun dumper = [](VM &vm, string_view_nt name, const TypeInfo &ti, Value *x) {
        #if RTT_ENABLED
            auto debug_type = x->type;
        #else
            auto debug_type = ti.t;
        #endif
        if (debug_type == V_NIL && ti.t != V_NIL) {
            // Uninitialized.
            auto sd = string(name.sv);
            append(sd, ":");
            ti.Print(vm, sd, nullptr);
            append(sd, " (uninitialized)");
            Text(sd);
        } else if (ti.t != debug_type && !IsStruct(ti.t)) {
            // Some runtime type corruption, show the problem rather than crashing.
            auto sd = string(name.sv);
            append(sd, ":");
            ti.Print(vm, sd, nullptr);
            append(sd, " (ERROR != ", BaseTypeName(debug_type), ")");
            Text(sd);
        } else {
            ValToGUI(vm, x, &ti, name, false);
        }
    };

    auto cur_fileidx = vm.last_fileidx;
    auto cur_line = vm.last_line;
    for (auto &funstackelem : reverse(vm.fun_id_stack)) {
        auto [name, fip] = vm.DumpStackFrameStart(funstackelem.funstartinfo, cur_fileidx, cur_line);
        if (ImGui::TreeNode(name.c_str())) {
            if (BeginTable(name.c_str())) {
                vm.DumpStackFrame(fip, funstackelem.locals, dumper);
                EndTable();
            }
            ImGui::TreePop();
        }
        cur_fileidx = funstackelem.fileidx;
        cur_line = funstackelem.line;
    }
}

string BreakPoint(VM &vm, string_view reason) {
    // Init just in case it wasn't already.
    // NOTE: this inits on main window first.
    if (!IMGUIInit(0, false, 3.0f, 1.0f)) {
        // FIXME: we could make this work without a main window, but I guess if we're running
        // in console its good to stay there?
        return "breakpoint: no main window for imgui";
    }

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
    bool first_stack_trace = true;
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
            first_stack_trace = true;
            // Ensure we've rendered a full frame with the above text before aborting,
            // since this window will get no rendering updates.
            if (++cont == 3) break;
        } else {
            Text(reason);
            if (ImGui::Button("Continue")) {
                cont = 1;
            }
            ImGui::SameLine();
            if (ImGui::Button("Quit")) {
                quit = true;
                break;
            }

            if (first_stack_trace) {
                ImGui::SetNextItemOpen(true);
                first_stack_trace = false;
            }
            DumpStackTrace(vm);
            VarsToGUI(vm);

            if (ImGui::TreeNode("Memory Usage")) {
                // FIXME: imgui-ify? Table?
                auto mu = vm.MemoryUsage(25);
                Text(mu);
                ImGui::TreePop();
            }
        }

        ImGui::End();

        ImGui::Render();
        SetTexture(0, DummyTexture());
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

int &ListState(int &cur, const char* label, iint max, int def) {
    auto pcur = &cur;
    if (cur < -1) {
        auto id = ImGui::GetID(label);
        auto &ls = list_state[id];
        if (ls.sel < 0) ls.sel = def;
        ls.last_use = imgui_frame;
        pcur = &ls.sel;
    }
    // Convenient: user code doesn't need to keep track of changes in size.
    *pcur = min(*pcur, (int)max - 1);
    return *pcur;
}

const char *Label(VM &vm, Value val) {
    auto l = val.sval()->data();
    if (!*l)
        vm.BuiltinError("imgui: widget label must not be empty (use ##something for empty label)");
    return l;
}

void AddIMGUI(NativeRegistry &nfr) {

nfr("init", "dark_style,flags,rounding,border", "B?I?F?F?", "",
    "",
    [](StackPtr &, VM &vm, Value &darkstyle, Value &flags, Value &rounding, Value &border) {
        if (!IMGUIInit(flags.ival(), darkstyle.True(), rounding.fltval(), border.fltval()))
            vm.BuiltinError("im.init: no window");
        return NilVal();
    });

nfr("add_font", "font_path,size", "SF", "B",
    "",
    [](StackPtr &, VM &vm, Value &fontname, Value &size) {
        IsInit(vm, { N_NONE, N_NONE });
        return Value(LoadFont(fontname.sval()->strv(), size.fltval()));
    });

nfr("set_style_color", "i,color", "IF}:4", "",
    "",
    [](StackPtr &sp, VM &vm) {
        IsInit(vm,  { N_NONE, N_NONE });
        auto c = PopVec<float4>(sp);
        auto i = Pop(sp).intval();
        if (i < 0 || i >= ImGuiCol_COUNT) return;
        ImGui::GetStyle().Colors[i] = ImVec4(c.x, c.y, c.z, c.w);
    });

nfr("set_style_spacing", "spacing", "F}:2", "",
    "",
    [](StackPtr &sp, VM &vm) {
        IsInit(vm,  { N_NONE, N_NONE });
        auto s = PopVec<float2>(sp);
        ImGui::GetStyle().ItemSpacing = ImVec2(s.x, s.y);
    });

nfr("set_style_inner_spacing", "spacing", "F}:2", "",
    "",
    [](StackPtr &sp, VM &vm) {
        IsInit(vm,  { N_NONE, N_NONE });
        auto s = PopVec<float2>(sp);
        ImGui::GetStyle().ItemInnerSpacing = ImVec2(s.x, s.y);
    });

nfr("set_style_window_padding", "spacing", "F}:2", "",
    "",
    [](StackPtr &sp, VM &vm) {
        IsInit(vm,  { N_NONE, N_NONE });
        auto s = PopVec<float2>(sp);
        ImGui::GetStyle().WindowPadding = ImVec2(s.x, s.y);
    });

nfr("set_style_frame_padding", "spacing", "F}:2", "",
    "",
    [](StackPtr &sp, VM &vm) {
        IsInit(vm,  { N_NONE, N_NONE });
        auto s = PopVec<float2>(sp);
        ImGui::GetStyle().FramePadding = ImVec2(s.x, s.y);
    });

nfr("frame_start", "", "", "",
    "(use im.frame instead)",
    [](StackPtr &, VM &vm) {
        IsInit(vm, { N_NONE, N_NONE });
        IMGUIFrameCleanup();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame(_sdl_window);
        ImGui::NewFrame();
        NPush(N_FRAME);
        imgui_frame++;
    });

nfr("frame_end", "", "", "",
    "",
    [](StackPtr &, VM &vm) {
        IsInit(vm, { N_NONE, N_NONE });
        NPop(vm, N_FRAME);
        for (auto it = list_state.cbegin(); it != list_state.cend(); ) {
            if (it->second.last_use != imgui_frame) {
                it = list_state.erase(it);
            } else {
                ++it;
            }
        }
    });

nfr("dockspace_over_viewport", "", "", "",
    "",
    [](StackPtr &, VM &vm) {
        IsInit(vm, { N_FRAME, N_NONE });
    ImGui::DockSpaceOverViewport(
        nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
    });

nfr("window_demo", "", "", "B",
    "",
    [](StackPtr &, VM &vm) {
        IsInit(vm, { N_FRAME, N_NONE });
        bool show = true;
        ImGui::ShowDemoWindow(&show);
        return Value(show);
    });

nfr("window_start", "title,flags,dock", "SII", "",
    "(use im.window instead)",
    [](StackPtr &sp, VM &vm) {
        IsInit(vm, { N_FRAME, N_NONE });
        auto dock = Pop(sp);
        auto flags = Pop(sp);
        auto title = Pop(sp);
        if (dock.True() && ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable) {
            ImGui::SetNextWindowDockID(last_dock_id, ImGuiCond_FirstUseEver);
        }
        ImGui::Begin(Label(vm, title), nullptr, (ImGuiWindowFlags)flags.ival());
        last_dock_id = ImGui::GetWindowDockID();
        /*
        if (dock.False() && ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable) {
            ImGui::DockSpace(last_dock_id);
        }
        */
        NPush(N_WIN);
    });

nfr("window_end", "", "", "",
    "",
    [](StackPtr &, VM &vm) {
        IsInit(vm, { N_FRAME, N_NONE });
        NPop(vm, N_WIN);
    });

nfr("next_window_size", "size", "F}:2", "",
    "size in pixels",
    [](StackPtr &sp, VM &vm) {
        IsInit(vm, { N_FRAME, N_NONE });
        auto size = PopVec<float2>(sp);
        ImGui::SetNextWindowSize(ImVec2(size.x, size.y), ImGuiCond_Appearing);
    });

nfr("next_window_size", "size,cond", "F}:2I", "",
    "size in pixels",
    [](StackPtr &sp, VM &vm) {
        IsInit(vm, { N_FRAME, N_NONE });
        auto cond = Pop(sp).ival();
        auto size = PopVec<float2>(sp);
        ImGui::SetNextWindowSize(ImVec2(size.x, size.y), (ImGuiCond)cond);
    });


nfr("next_window_pos", "pos,pivot", "F}:2F}:2", "",
    "pos in pixels, pivot values 0..1 relative to pos",
    [](StackPtr &sp, VM &vm) {
        IsInit(vm, { N_FRAME, N_NONE });
        auto pivot = PopVec<float2>(sp);
        auto pos = PopVec<float2>(sp);
        ImGui::SetNextWindowPos(ImVec2(pos.x, pos.y), ImGuiCond_Appearing, ImVec2(pivot.x, pivot.y));
    });

nfr("next_window_pos", "pos,pivot,cond", "F}:2F}:2I", "",
    "pos in pixels, pivot values 0..1 relative to pos",
    [](StackPtr &sp, VM &vm) {
        IsInit(vm, { N_FRAME, N_NONE });
        auto cond = Pop(sp).ival();
        auto pivot = PopVec<float2>(sp);
        auto pos = PopVec<float2>(sp);
        ImGui::SetNextWindowPos(ImVec2(pos.x, pos.y), (ImGuiCond)cond, ImVec2(pivot.x, pivot.y));
    });

nfr("button", "label,size", "SF}:2?", "B",
    "",
    [](StackPtr &sp, VM &vm) {
        IsInit(vm);
        auto size = PopVec<float2>(sp);
        auto title = Pop(sp);
        auto press = ImGui::Button(Label(vm, title), ImVec2(size.x, size.y));
        Push(sp, press);
    });

nfr("selectable", "label,selected", "SB?", "B",
    "",
    [](StackPtr &sp, VM &vm) {
        IsInit(vm);
        auto selected = Pop(sp).True();
        auto title = Pop(sp);
        auto press = ImGui::Selectable(Label(vm, title), selected);
        Push(sp, press);
    });

nfr("same_line", "", "", "", "",
    [](StackPtr &, VM &vm) {
        IsInit(vm);
        ImGui::SameLine();
        return NilVal();
    });

nfr("new_line", "", "", "", "",
    [](StackPtr &, VM &vm) {
        IsInit(vm);
        ImGui::NewLine();
    });

nfr("separator", "", "", "",
    "",
    [](StackPtr &, VM &vm) {
        IsInit(vm);
        ImGui::Separator();
        return NilVal();
    });

nfr("is_item_deactivated_after_edit", "", "", "B",
    "returns true if the last item was made inactive and made a value change when it was active",
    [](StackPtr &sp, VM &vm) {
        IsInit(vm);
        auto result = ImGui::IsItemDeactivatedAfterEdit();
        Push(sp, result);
    });

nfr("get_layout_pos", "", "", "F}:2", "",
    [](StackPtr &sp, VM &vm) {
        IsInit(vm);
        auto pos = ImGui::GetCursorPos();
        PushVec(sp, float2(pos.x, pos.y));
    });

nfr("set_layout_pos", "pos", "F}:2", "", "",
    [](StackPtr &sp, VM &vm) {
        IsInit(vm);
        auto pos = PopVec<float2>(sp);
        ImGui::SetCursorPos(ImVec2(pos.x, pos.y));
    });

nfr("get_content_region_avail", "", "", "F}:2",
    "returns the amount of space left in the current region from the cursor pos",
    [](StackPtr &sp, VM &vm) {
        IsInit(vm);
        auto avail = ImGui::GetContentRegionAvail();
        PushVec(sp, float2(avail.x, avail.y));
    });

nfr("calc_text_size", "text", "S", "F}:2",
    "returns the amount of space used by the given text in the current font",
    [](StackPtr &sp, VM &vm) {
        IsInit(vm);
        auto &text = *Pop(sp).sval();
        auto size = ImGui::CalcTextSize(text.strvnt().c_str());
        PushVec(sp, float2(size.x, size.y));
    });

nfr("mouse_clicked", "button", "I", "B",
    "returns whether the given mouse button was clicked anywhere",
    [](StackPtr &sp, VM &vm) {
        IsInit(vm);
        auto button = Pop(sp).ival();
        Push(sp, Value(ImGui::IsMouseClicked((ImGuiMouseButton)button)));
    });

nfr("text", "label", "S", "",
    "",
    [](StackPtr &, VM &vm, Value &text) {
        IsInit(vm);
        auto &s = *text.sval();
        Text(s.strv());
        return NilVal();
    });

nfr("text_wrapped", "label", "S", "",
    "",
    [](StackPtr &, VM &vm, Value &text) {
        IsInit(vm);
        auto &s = *text.sval();
        ImGui::TextWrapped("%s", s.strvnt().c_str());
        return NilVal();
    });

nfr("font_start", "font_idx", "I", "",
    "(use im.font instead)",
    [](StackPtr &sp, VM &vm) {
        IsInit(vm);
        int i = std::max(0, std::min(ImGui::GetIO().Fonts->Fonts.size() - 1, Pop(sp).intval()));
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[i]);
        NPush(N_FONT);
    });

nfr("font_end", "", "", "",
    "",
    [](StackPtr &, VM &vm) {
        IsInit(vm);
        NPop(vm, N_FONT);
    });

nfr("color_start", "color", "F}:4", "",
    "(use im.tooltip_multi instead)",
    [](StackPtr &sp, VM &vm) {
        IsInit(vm);
        auto c = PopVec<float4>(sp);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(c.x, c.y, c.z, c.w));
        NPush(N_COLOR);
    });

nfr("color_end", "", "", "",
    "",
    [](StackPtr &, VM &vm) {
        IsInit(vm);
        NPop(vm, N_COLOR);
    });

nfr("tooltip", "label", "S", "",
    "",
    [](StackPtr &, VM &vm, Value &text) {
        IsInit(vm);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", Label(vm, text));
        return NilVal();
    });

nfr("tooltip_multi_start", "", "", "B",
    "(use im.tooltip_multi instead)",
    [](StackPtr &sp, VM &vm) {
        IsInit(vm);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::BeginTooltip();
            Push(sp, true);
            NPush(N_TOOLTIP);
        } else {
            Push(sp, false);
        }
    });

nfr("tooltip_multi_end", "", "", "",
    "",
    [](StackPtr &, VM &vm) {
        IsInit(vm);
        NPop(vm, N_TOOLTIP);
    });

nfr("checkbox", "label,bool", "SI", "I2",
    "",
    [](StackPtr &, VM &vm, Value &text, Value &boolval) {
        IsInit(vm);
        bool b = boolval.True();
        ImGui::Checkbox(Label(vm, text), &b);
        return Value(b);
    });

nfr("input_text", "label,str", "SSk", "S",
    "",
    [](StackPtr &, VM &vm, Value &text, Value &str) {
        IsInit(vm);
        return Value(LStringInputText(vm, Label(vm, text), str.sval()));
    });

nfr("input_text_multi_line", "label,str,num_lines", "SSkI", "S",
    "",
    [](StackPtr &, VM &vm, Value &text, Value &str, Value &num_lines) {
        IsInit(vm);
        return Value(LStringInputText(vm, Label(vm, text), str.sval(), num_lines.intval()));
    });

nfr("input_int", "label,val,min,max", "SIII", "I",
    "",
    [](StackPtr &, VM &vm, Value &text, Value &val, Value &min, Value &max) {
        IsInit(vm);
        ImGui::InputScalar(Label(vm, text), ImGuiDataType_S64, (void *)&val,
                           nullptr, nullptr, "%d", 0);
        if (val.ival() < min.ival()) val = min;
        if (val.ival() > max.ival()) val = max;
        return val;
    });

nfr("input_int", "label,val", "SI", "I",
    "",
    [](StackPtr &, VM &vm, Value &text, Value &val) {
        IsInit(vm);
        ImGui::InputScalar(Label(vm, text), ImGuiDataType_S64, (void *)&val,
                           nullptr, nullptr, "%d", 0);
        return val;
    });

nfr("input_float", "label,val", "SF", "F",
    "",
    [](StackPtr &, VM &vm, Value &text, Value &val) {
        IsInit(vm);
        return Value(InputFloat(Label(vm, text), val.fval()));
    });

nfr("radio", "labels,active,horiz", "S]II", "I",
    "active to select which one is activated, -2 for last frame\'s "
    "selection or 0",
    [](StackPtr &, VM &vm, Value &strs, Value &active, Value &horiz) {
        IsInit(vm);
        auto v = strs.vval();
        auto act = active.intval();
        int &sel = ListState(act, v->len ? Label(vm, v->At(0)) : "empty?", v->len, 0);
        for (iint i = 0; i < v->len; i++) {
            if (i && horiz.True()) ImGui::SameLine();
            ImGui::RadioButton(Label(vm, v->At(i)), &sel, (int)i);
        }
        return Value(sel);
    });

nfr("progress_bar", "fraction,size,overlay", "FF}:2S", "",
    "display progress bar filled up to the given fraction. size.x < 0 to use all available space, "
    " size.x > 0 for a specific pixel width",
    [](StackPtr &sp, VM &vm) {
        IsInit(vm);
        auto &overlay = *Pop(sp).sval();
        auto size = PopVec<float2>(sp);
        auto fraction = Pop(sp).fltval();
        ImGui::ProgressBar(fraction, ImVec2(size.x, size.y), overlay.strvnt().c_str());
    });

nfr("combo", "label,labels,active", "SS]I", "I",
    "active to select which one is activated, -2 for last frame\'s "
    "selection or 0",
    [](StackPtr &, VM &vm, Value &text, Value &strs, Value &active) {
        IsInit(vm);
        auto v = strs.vval();
        auto act = active.intval();
        int &sel = ListState(act, Label(vm, text), v->len, 0);
        vector<const char *> items(v->len);
        for (iint i = 0; i < v->len; i++) {
            items[i] = Label(vm, v->At(i));
        }
        ImGui::Combo(Label(vm, text), &sel, items.data(), (int)items.size());
        return Value(sel);
    });

nfr("listbox", "label,labels,active,height", "SS]II", "I",
    "active to select which one is activated, -1 for no initial selection, -2 for last frame\'s "
    "selection or none",
    [](StackPtr &, VM &vm, Value &text, Value &strs, Value &active, Value &height) {
        IsInit(vm);
        auto v = strs.vval();
        auto act = active.intval();
        int &sel = ListState(act, Label(vm, text), v->len, -1);
        vector<const char *> items(v->len);
        for (iint i = 0; i < v->len; i++) {
            items[i] = v->At(i).sval()->data();
        }
        ImGui::ListBox(Label(vm, text), &sel, items.data(), (int)items.size(), height.intval());
        return Value(sel);
    });

nfr("sliderint", "label,i,min,max", "SIII", "I",
    "",
    [](StackPtr &, VM &vm, Value &text, Value &integer, Value &min, Value &max) {
        IsInit(vm);
        int i = integer.intval();
        ImGui::SliderInt(Label(vm, text), &i, min.intval(), max.intval());
        return Value(i);
    });

nfr("sliderfloat", "label,f,min,max", "SFFF", "F",
    "",
    [](StackPtr &, VM &vm, Value &text, Value &flt, Value &min, Value &max) {
        IsInit(vm);
        float f = flt.fltval();
        ImGui::SliderFloat(Label(vm, text), &f, min.fltval(), max.fltval());
        return Value(f);
    });

    #define VECSLIDER(Type, type, typeval, T, N)                                               \
    nfr("slider" #type #N, "label,"#T #N ",min,max", "S" #T "}:" #N #T #T, #T "}:" #N, "",  \
        [](StackPtr &sp, VM &vm) {                                                             \
            IsInit(vm);                                                                        \
            auto max = Pop(sp).typeval();                                                      \
            auto min = Pop(sp).typeval();                                                      \
            auto v = PopVec<type ## N>(sp);                                                    \
            auto label = Label(vm, Pop(sp));                                                   \
            ImGui::Slider ## Type ## N(label, &v.c[0], min, max);                              \
            PushVec(sp, v);                                                                    \
        }) // no semicolon.
    VECSLIDER(Int, int, intval, I, 2);
    VECSLIDER(Int, int, intval, I, 3);
    VECSLIDER(Int, int, intval, I, 4);
    VECSLIDER(Float, float, fltval, F, 2);
    VECSLIDER(Float, float, fltval, F, 3);
    VECSLIDER(Float, float, fltval, F, 4);
    #undef VECSLIDER

nfr("coloredit", "label,color", "SF}", "A2",
    "",
    [](StackPtr &sp, VM &vm) {
        IsInit(vm);
        auto c = PopVec<float4>(sp);
        ImGui::ColorEdit4(Label(vm, Pop(sp)), (float *)c.data());
        PushVec(sp, c);
    });

nfr("image", "tex,size,flip", "R:textureF}:2B?", "",
    "",
    [](StackPtr &sp, VM &vm) {
        IsInit(vm);
        auto flip = Pop(sp).True();
        auto sz = PopVec<float2>(sp);
        auto t = GetTexture(Pop(sp));
        ImGui::Image((ImTextureID)(size_t)t.id, ImVec2(sz.x, sz.y), ImVec2(0.0f, flip ? 0.0f : 1.0f),
                                                                    ImVec2(1.0f, flip ? 1.0f : 0.0f));
    });

nfr("image_button", "label,tex,size,bgcol,flip", "SR:textureF}:2F}:4?B?", "B",
    "",
    [](StackPtr &sp, VM &vm) {
        IsInit(vm);
        auto flip = Pop(sp).True();
        auto bgcol = PopVec<float4>(sp);
        auto sz = PopVec<float2>(sp);
        auto t = GetTexture(Pop(sp));
        auto label = Pop(sp);
        auto press =
            ImGui::ImageButton(Label(vm, label), (ImTextureID)(size_t)t.id, ImVec2(sz.x, sz.y),
                               ImVec2(0.0f, flip ? 0.0f : 1.0f),
                               ImVec2(1.0f, flip ? 1.0f : 0.0f),
                               ImVec4(bgcol.x, bgcol.y, bgcol.z, bgcol.w));
        Push(sp, press);
    });

nfr("image_mouseclick", "tex,size", "R:textureF}:2", "F}:2I",
    "",
    [](StackPtr &sp, VM &vm) {
        IsInit(vm);
        auto sz = PopVec<float2>(sp);
        auto t = GetTexture(Pop(sp));
        ImVec2 cursor = ImGui::GetCursorScreenPos();
        ImVec2 size = ImVec2(sz.x, sz.y);
        ImGui::Image((ImTextureID)(size_t)t.id, size, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
        if (ImGui::IsMouseHoveringRect(cursor, cursor + size)) {
            auto pos = (ImGui::GetMousePos() - cursor) / size;
            PushVec<float, 2>(sp, float2(pos.x, pos.y));
            // Create an all-in-one event value similar to gl.button().
            int event = -1;
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) event = 1;
            else if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) event = 0;
            else if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) event = 2;
            Push(sp, event);
        } else {
            PushVec<float, 2>(sp, -float2_1);
            Push(sp, -1);
        }
    });

nfr("treenode_start", "label,flags", "SI", "B",
    "(use im.treenode instead)",
    [](StackPtr &sp, VM &vm) {
        IsInit(vm);
        auto flags = (ImGuiTreeNodeFlags)Pop(sp).intval();
        auto title = Pop(sp);
        bool open = ImGui::TreeNodeEx(Label(vm, title), flags);
        Push(sp, open);
        if (open) NPush(N_TREE);
    });

nfr("treenode_end", "", "", "",
    "",
    [](StackPtr &, VM &vm) {
        IsInit(vm);
        NPop(vm, N_TREE);
    });

nfr("tab_bar_start", "label", "S", "B",
    "(use im.tab_bar instead)",
    [](StackPtr &sp, VM &vm) {
        IsInit(vm);
        auto title = Pop(sp);
        bool open = ImGui::BeginTabBar(Label(vm, title), 0);
        Push(sp, open);
        if (open) NPush(N_TAB_BAR);
    });

nfr("tab_bar_end", "", "", "",
    "",
    [](StackPtr &, VM &vm) {
        IsInit(vm);
        NPop(vm, N_TAB_BAR);
    });

nfr("tab_start", "label,flags", "SI", "B",
    "(use im.tab instead)",
    [](StackPtr &sp, VM &vm) {
        IsInit(vm);
        auto flags = Pop(sp).intval();
        auto title = Pop(sp);
        bool open = ImGui::BeginTabItem(Label(vm, title), nullptr, (ImGuiTabItemFlags)flags);
        Push(sp, open);
        if (open) NPush(N_TAB);
    });

nfr("tab_end", "", "", "",
    "",
    [](StackPtr &, VM &vm) {
        IsInit(vm);
        NPop(vm, N_TAB);
    });

nfr("menu_bar_start", "main", "B", "B",
    "(use im.menu_bar instead)",
    [](StackPtr &sp, VM &vm) {
        IsInit(vm, { N_FRAME, N_NONE });
        auto main = Pop(sp).True();
        bool open = main ? ImGui::BeginMainMenuBar() : ImGui::BeginMenuBar();
        Push(sp, open);
        if (open) NPush(main ? N_MAIN_MENU_BAR: N_MENU_BAR);
    });

nfr("menu_bar_end", "main", "B", "",
    "",
    [](StackPtr &sp, VM &vm) {
        IsInit(vm, { N_FRAME, N_NONE });
        auto main = Pop(sp).True();
        NPop(vm, main ? N_MAIN_MENU_BAR: N_MENU_BAR);
    });

nfr("menu_start", "label,disabled", "SB?", "B",
    "(use im.menu instead)",
    [](StackPtr &sp, VM &vm) {
        IsInit(vm, {});
        RequireMenuNesting(vm);
        auto disabled = Pop(sp).True();
        auto title = Pop(sp);
        bool open = ImGui::BeginMenu(Label(vm, title), !disabled);
        Push(sp, open);
        if (open) NPush(N_MENU);
    });

nfr("menu_end", "", "", "",
    "",
    [](StackPtr &, VM &vm) {
        IsInit(vm, {});
        RequireMenuNesting(vm);
        NPop(vm, N_MENU);
    });

nfr("menu_item", "label,shortcut,disabled", "SS?B?", "B",
    "",
    [](StackPtr &sp, VM &vm) {
        IsInit(vm, { N_MENU, N_POPUP });
        auto disabled = Pop(sp).True();
        auto shortcut = Pop(sp);
        auto title = Pop(sp);
        auto press = ImGui::MenuItem(Label(vm, title),
                                     shortcut.True() ? shortcut.sval()->data() : nullptr,
                                     false,
                                     !disabled);
        Push(sp, press);
    });

nfr("menu_item_toggle", "label,selected,disabled", "SB?B?", "B",
    "",
    [](StackPtr &sp, VM &vm) {
        IsInit(vm, { N_MENU, N_POPUP });
        auto disabled = Pop(sp).True();
        auto selected = Pop(sp).True();
        auto title = Pop(sp);
        ImGui::MenuItem(Label(vm, title), nullptr, &selected, !disabled);
        Push(sp, selected);
    });

nfr("id_start", "label", "Ss", "",
    "an invisble group around some widgets, useful to ensure these widgets are unique"
    " (if they have the same label as widgets in another group that has a different group"
    " label). Use im.id instead",
    [](StackPtr &sp, VM &vm) {
        IsInit(vm);
        auto title = Pop(sp);
        ImGui::PushID(Label(vm, title));
        NPush(N_ID);
    });

nfr("id_start", "label", "I", "",
    "(integer version)",
    [](StackPtr &sp, VM &vm) {
        IsInit(vm);
        auto i = Pop(sp);
        ImGui::PushID(i.intval());
        NPush(N_ID);
    });

nfr("id_start", "label", "A", "",
    "(reference version)",
    [](StackPtr &sp, VM &vm) {
        IsInit(vm);
        auto r = Pop(sp);
        ImGui::PushID((const void *)r.ref());
        NPush(N_ID);
    });

nfr("id_end", "", "", "",
    "",
    [](StackPtr &, VM &vm) {
        IsInit(vm);
        NPop(vm, N_ID);
    });

nfr("child_start", "title,size,flags", "SF}:2I", "",
    "create a self-contained scrolling/clipping region with a window. use im.child instead",
    [](StackPtr &sp, VM &vm) {
        IsInit(vm);
        auto flags = Pop(sp);
        auto sz = PopVec<float2>(sp);
        auto title = Pop(sp);
        ImGui::BeginChild(Label(vm, title), ImVec2(sz.x, sz.y), false, (ImGuiWindowFlags)flags.ival());
        NPush(N_CHILD);
    });

nfr("child_end", "", "", "",
    "",
    [](StackPtr &, VM &vm) {
        IsInit(vm);
        NPop(vm, N_CHILD);
    });

nfr("group_start", "", "", "",
    "lock the horizontal starting position, and capture all contained widgets"
    " into one item. Use im.group instead",
    [](StackPtr &, VM &vm) {
        IsInit(vm);
        ImGui::BeginGroup();
        NPush(N_VGROUP);
    });

nfr("group_end", "", "", "",
    "",
    [](StackPtr &, VM &vm) {
        IsInit(vm);
        NPop(vm, N_VGROUP);
    });

nfr("popup_start", "label,winflags,rmbprevitem", "SIB?", "B",
    "(use im.popup instead)",
    [](StackPtr &sp, VM &vm) {
        IsInit(vm);
        auto rmb = Pop(sp).True();
        auto flags = (ImGuiWindowFlags)Pop(sp).intval();
        auto title = Pop(sp);
        bool open = rmb ? ImGui::BeginPopupContextItem(Label(vm, title))
            : ImGui::BeginPopup(Label(vm, title), flags);
        Push(sp, open);
        if (open) NPush(N_POPUP);
    });

nfr("popup_end", "", "", "",
    "",
    [](StackPtr &, VM &vm) {
        IsInit(vm);
        NPop(vm, N_POPUP);
    });

nfr("popup_open", "label", "S", "",
    "",
    [](StackPtr &sp, VM &vm) {
        IsInit(vm);
        auto title = Pop(sp);
        ImGui::OpenPopup(Label(vm, title));
    });

nfr("close_current_popup", "", "", "",
    "",
    [](StackPtr &, VM &vm) {
        IsInit(vm);
        ImGui::CloseCurrentPopup();
    });

nfr("disabled_start", "disabled", "B", "",
    "(use im.disabled instead)",
    [](StackPtr &sp, VM &vm) {
        IsInit(vm);
        const auto disabled = Pop(sp).True();
        ImGui::BeginDisabled(disabled);
    });

nfr("disabled_end", "", "", "",
    "",
    [](StackPtr &, VM &vm) {
        IsInit(vm);
        ImGui::EndDisabled();
    });

nfr("button_repeat_start", "repeat", "B", "",
    "(use im.button_repeat instead)",
    [](StackPtr &sp, VM &vm) {
        IsInit(vm);
        const auto repeat = Pop(sp).True();
        ImGui::PushButtonRepeat(repeat);
    });

nfr("button_repeat_end", "", "", "",
    "",
    [](StackPtr &, VM &vm) {
        IsInit(vm);
        ImGui::PopButtonRepeat();
    });

nfr("drag_drop_source_start", "flags", "I", "B",
    "(use im.drag_drop_source instead)",
    [](StackPtr &sp, VM &vm) {
        IsInit(vm);
        const auto flags = Pop(sp).intval();
        bool open = ImGui::BeginDragDropSource(flags);
        Push(sp, open);
        if (open) NPush(N_DRAG_DROP_SOURCE);
    });

nfr("drag_drop_source_end", "", "", "",
    "",
    [](StackPtr &, VM &vm) {
        IsInit(vm);
        NPop(vm, N_DRAG_DROP_SOURCE);
    });

nfr("set_drag_drop_payload", "type,data", "SS", "",
    "",
    [](StackPtr &sp, VM &vm) {
        IsInit(vm);
        auto data = Pop(sp).sval()->strv();
        auto type = Pop(sp).sval()->strvnt();
        ImGui::SetDragDropPayload(type.c_str(), data.data(), data.size());
    });

nfr("drag_drop_target_start", "", "", "B",
    "(use im.drag_drop_target instead)",
    [](StackPtr &sp, VM &vm) {
        IsInit(vm);
        bool open = ImGui::BeginDragDropTarget();
        Push(sp, open);
        if (open) NPush(N_DRAG_DROP_TARGET);
    });

nfr("drag_drop_target_end", "", "", "",
    "",
    [](StackPtr &, VM &vm) {
        IsInit(vm);
        NPop(vm, N_DRAG_DROP_TARGET);
    });

nfr("accept_drag_drop_payload", "type,flags", "SI", "S?",
    "",
    [](StackPtr &sp, VM &vm) {
        IsInit(vm);
        const auto flags = Pop(sp).intval();
        auto type = Pop(sp).sval()->strvnt();
        auto *payload = ImGui::AcceptDragDropPayload(type.c_str(), flags);
        if (payload) {
            Push(sp, vm.NewString(string_view { (const char*)payload->Data, (size_t)payload->DataSize }));
        } else {
            Push(sp, NilVal());
        }
    });

nfr("width_start", "width", "F", "",
    "Sets the width of an item: 0 = default, -1 = use full width without label,"
    " any other value is custom width. Use im.width instead",
    [](StackPtr &sp, VM &vm) {
        IsInit(vm);
        auto width = Pop(sp).fltval();
        ImGui::PushItemWidth(width);
        NPush(N_WIDTH);
    });

nfr("width_end", "", "", "", "",
    [](StackPtr &, VM &vm) {
        IsInit(vm);
        NPop(vm, N_WIDTH);
    });

nfr("text_table", "id,num_colums,labels", "SIS]", "", "",
    [](StackPtr &, VM &vm, Value &id, Value &num_colums, Value &labels) {
        IsInit(vm);
        auto nc = num_colums.intval();
        if (!ImGui::BeginTable(id.sval()->strvnt().c_str(), nc,
                               ImGuiTableFlags_SizingFixedFit |
                               ImGuiTableFlags_NoHostExtendX |
                               ImGuiTableFlags_Borders))
            return NilVal();
        for (iint i = 0; i < labels.vval()->len; i++) {
            if (!(i % nc)) ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(i % nc);
            Text(labels.vval()->At(i).sval()->strv());
        }
        ImGui::EndTable();
        return NilVal();
    });

nfr("table_start", "id,num_colums,flags", "SII", "B",
    "(use im.table instead)",
    [](StackPtr &, VM &vm, Value &id, Value &num_colums, Value &flags) {
        IsInit(vm);
        auto nc = num_colums.intval();
        auto visible = ImGui::BeginTable(id.sval()->strvnt().c_str(), nc, flags.intval());
        if (visible) NPush(N_TABLE);
        return Value(visible);
    });

nfr("table_setup_column", "label,flags,init_width_or_weight", "SIF", "",
    "(use im.table instead)",
    [](StackPtr &, VM &vm, Value &label, Value &flags, Value &init) {
        IsInit(vm);
        if (NTop() == N_TABLE)
            ImGui::TableSetupColumn(label.sval()->strvnt().c_str(), flags.intval(), init.fltval());
        return NilVal();
    });

nfr("table_headers_row", "", "", "",
    "",
    [](StackPtr &, VM &vm) {
        IsInit(vm);
        if (NTop() == N_TABLE) ImGui::TableHeadersRow();
    });

nfr("table_next_row", "", "", "",
    "",
    [](StackPtr &, VM &vm) {
        IsInit(vm);
        if (ImGui::TableGetColumnCount()) ImGui::TableNextRow();
    });

nfr("table_next_column", "", "", "",
    "",
    [](StackPtr &, VM &vm) {
        IsInit(vm);
        if (ImGui::TableGetColumnCount()) ImGui::TableNextColumn();
    });

nfr("table_end", "", "", "",
    "",
    [](StackPtr &, VM &vm) {
        IsInit(vm);
        NPop(vm, N_TABLE);
    });

nfr("edit_anything", "value,label", "AkS?", "A1",
    "creates a UI for any lobster reference value, and returns the edited version",
    [](StackPtr &, VM &vm, Value &v, Value &label) {
        IsInit(vm);
        // FIXME: would be good to support structs, but that requires typeinfo, not just len.
        auto &ti = vm.GetTypeInfo(v.True() ? v.ref()->tti : TYPE_ELEM_ANY);
        ValToGUI(vm, &v, &ti, label.True() ? label.sval()->strvnt() : string_view_nt(""), true,
                 false);
        return v;
    });

nfr("graph", "label,values,ishistogram", "SF]I", "",
    "",
    [](StackPtr &, VM &vm, Value &label, Value &vals, Value &histogram) {
        IsInit(vm);
        auto getter = [](void *data, int i) -> float {
            return ((Value *)data)[i].fltval();
        };
        if (histogram.True()) {
            ImGui::PlotHistogram(Label(vm, label), getter, vals.vval()->Elems(),
                (int)vals.vval()->len);
        } else {
            ImGui::PlotLines(Label(vm, label), getter, vals.vval()->Elems(),
                (int)vals.vval()->len);
        }
        return NilVal();
    });

nfr("show_flexbuffer", "value", "S", "",
    "",
    [](StackPtr &, VM &vm, Value &v) {
        IsInit(vm);
        auto sv = v.sval()->strv();
        auto root = flexbuffers::GetRoot((const uint8_t *)sv.data(), sv.size());
        FlexBufferGUI(root, "Stack trace", false);
        return NilVal();
    });

nfr("show_vars", "", "", "",
    "shows an automatic editing UI for each global variable in your program",
    [](StackPtr &, VM &vm) {
        IsInit(vm);
        VarsToGUI(vm);
        return NilVal();
    });

nfr("show_engine_stats", "", "", "",
    "",
    [](StackPtr &, VM &vm) {
        IsInit(vm);
        EngineStatsGUI();
        return NilVal();
    });

}  // AddIMGUI

void AddIMGUIDebug(NativeRegistry & nfr) {

nfr("breakpoint", "condition", "I", "",
    "stops the program in the debugger if passed true."
    " debugger needs --runtime-stack-trace on, and im.init() to have run.",
    [](StackPtr &, VM &vm, Value &c) {
        if (c.True()) {
            auto err = BreakPoint(vm, "Conditional breakpoint hit!");
            if (!err.empty()) vm.Error(err);
        }
        return NilVal();
    });

nfr("breakpoint", "", "", "",
    "stops the program in the debugger always."
    " debugger needs --runtime-stack-trace on, and im.init() to have run.",
    [](StackPtr &, VM &vm) {
        auto err = BreakPoint(vm, "Breakpoint hit!");
        if (!err.empty()) vm.Error(err);
        return NilVal();
    });

}  // AddIMGUIDebug
