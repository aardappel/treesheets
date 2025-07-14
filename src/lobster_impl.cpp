
#include "lobster/stdafx.h"

#include "script_interface.h"

#include "lobster/compiler.h"

using namespace lobster;

namespace script {

ScriptInterface *si = nullptr;

void AddTreeSheets(NativeRegistry &nfr) {
nfr("goto_root", "", "", "",
    "makes the root of the document the current cell. this is the default at the start"
    "of any script, so this function is only needed to return there.",
    [](StackPtr &sp, VM &) { si->GoToRoot(); return Value(); });

nfr("goto_view", "", "", "",
    "makes what the user has zoomed into the current cell.",
    [](StackPtr &sp, VM &) { si->GoToView(); return Value(); });

nfr("has_selection", "", "", "I",
    "whether there is a selection.",
    [](StackPtr &sp, VM &) { return Value(si->HasSelection()); });

nfr("goto_selection", "", "", "",
    "makes the current cell the one selected, or the first of a selection.",
    [](StackPtr &sp, VM &) { si->GoToSelection(); return Value(); });

nfr("has_parent", "", "", "I",
    "whether the current cell has a parent (is the root cell).",
    [](StackPtr &sp, VM &) { return Value(si->HasParent()); });

nfr("goto_parent", "", "", "",
    "makes the current cell the parent of the current cell, if any.",
    [](StackPtr &sp, VM &) { si->GoToParent(); return Value(); });

nfr("num_children", "", "", "I",
    "returns the total number of children of the current cell (rows * columns)."
    "returns 0 if this cell doesn't have a sub-grid at all.",
    [](StackPtr &sp, VM &) { return Value(si->NumChildren()); });

nfr("num_columns_rows", "", "", "I}:2",
    "returns the number of columns & rows in the current cell.",
    [](StackPtr &sp, VM &vm) { PushVec(sp, int2(si->NumColumnsRows())); });

nfr("selection", "", "", "I}:2I}:2",
    "returns the (xs,ys) and (x,y) of the current selection, or zeroes if none.",
    [](StackPtr &sp, VM &vm) {
        auto b = si->SelectionBox();
        PushVec(sp, int2(b.second));
        PushVec(sp, int2(b.first));
    });

nfr("goto_child", "n", "I", "",
    "makes the current cell the nth child of the current cell.",
    [](StackPtr &sp, VM &, Value &n) { si->GoToChild(n.intval()); return Value(); });

nfr("goto_column_row", "col,row", "II", "",
    "makes the current cell the child at col / row.",
    [](StackPtr &sp, VM &, Value &x, Value &y) {
        si->GoToColumnRow(x.intval(), y.intval());
        return Value();
    });

nfr("get_text", "", "", "S",
    "gets the text of the current cell.",
    [](StackPtr &sp, VM &vm) { return Value(vm.NewString(si->GetText())); });

nfr("set_text", "text", "S", "",
    "sets the text of the current cell.",
    [](StackPtr &sp, VM &, Value &s) { si->SetText(s.sval()->strv()); return Value(); });

nfr("create_grid", "cols,rows", "II", "",
    "creates a grid in the current cell if there isn't one yet.",
    [](StackPtr &sp, VM &, Value &x, Value &y) {
        si->CreateGrid(x.intval(), y.intval());
        return Value();
    });

nfr("insert_column", "c", "I", "",
    "insert n columns before column c in an existing grid.",
    [](StackPtr &sp, VM &, Value &x) {
        si->InsertColumn(x.intval());
        return Value();
    });

nfr("insert_row", "r", "I", "",
    "insert n rows before row r in an existing grid.",
    [](StackPtr &sp, VM &, Value &x) {
        si->InsertRow(x.intval());
        return Value();
    });

nfr("delete", "pos,size", "I}:2I}:2", "",
    "clears the cells denoted by pos/size. also removes columns/rows if they become"
    "completely empty, or the entire grid.",
    [](StackPtr &sp, VM &vm) {
        auto s = PopVec<int2>(sp);
        auto p = PopVec<int2>(sp);
        si->Delete(p.x, p.y, s.x, s.y);
    });

nfr("set_background_color", "col", "F}:4", "",
    "sets the background color of the current cell",
    [](StackPtr &sp, VM &vm) {
        auto col = PopVec<float3>(sp);
        si->SetBackgroundColor(*(uint32_t *)quantizec(col, 0.0f).data());
    });

nfr("set_text_color", "col", "F}:4", "",
    "sets the text color of the current cell",
    [](StackPtr &sp, VM &vm) {
        auto col = PopVec<float3>(sp);
        si->SetTextColor(*(uint32_t *)quantizec(col, 0.0f).data());
    });

nfr("set_text_filtered", "filtered", "B", "", "sets the text filtered of the current cell",
    [](StackPtr &sp, VM &vm, Value &filtered) {
        si->SetTextFiltered(filtered.True());
        return Value();
    });

nfr("is_text_filtered", "", "", "B", "whether the text of the current cell is filtered",
    [](StackPtr &sp, VM &vm) { return Value(si->IsTextFiltered()); });

nfr("set_border_color", "col", "F}:4", "", "sets the border color of the current grid",
    [](StackPtr &sp, VM &vm) {
        auto col = PopVec<float3>(sp);
        si->SetBorderColor(*(uint32_t *)quantizec(col, 0.0f).data());
    });

nfr("set_relative_size", "s", "I", "",
    "sets the relative size (0 is normal, -1 is smaller etc.) of the current cell",
    [](StackPtr &sp, VM &, Value &s) {
        si->SetRelativeSize(geom::clamp(s.intval(), -10, 10));
        return Value();
    });

nfr("set_style_bits", "s", "I", "",
    "sets one or more styles (bold = 1, italic = 2, fixed = 4, underline = 8,"
    " strikethru = 16) on the current cell.",
    [](StackPtr &sp, VM &, Value &s) {
        si->SetStyle(s.intval());
        return Value();
    });

nfr("set_status_message", "msg", "S", "",
    "sets the status message in TreeSheets.",
    [](StackPtr &sp, VM &vm, Value &s) {
        si->SetStatusMessage(s.sval()->strv());
        return Value();
    });

nfr("get_filename_from_user", "is_save", "I", "S",
    "gets a filename using a file dialog. empty string if cancelled.",
    [](StackPtr &sp, VM &vm, Value &is_save) {
        return Value(vm.NewString(si->GetFileNameFromUser(is_save.True())));
    });

nfr("get_filename", "", "", "S",
    "gets the current documents file name.",
    [](StackPtr &sp, VM &vm) {
        return Value(vm.NewString(si->GetFileName()));
    });

nfr("load_document", "filename", "S", "B",
    "loads a document, and makes it the active one. returns false if failed.",
    [](StackPtr &sp, VM &vm, Value &filename) {
        return Value(si->LoadDocument(filename.sval()->data()));
    });

nfr("set_window_size", "width,height", "II", "", "resizes the window",
    [](StackPtr &sp, VM &vm, Value &w, Value &h) {
        si->SetWindowSize(w.intval(), h.intval());
        return Value();
    });

nfr("last_edit", "", "", "I",
    "gets the timestamp of the last edit in milliseconds since the Unix/C epoch",
    [](StackPtr &sp, VM &vm) { return Value(si->LastEdit()); });
}

NativeRegistry natreg;  // FIXME: global.

string InitLobster(ScriptInterface *_si, const char *exefilepath, const char *auxfilepath,
                   bool from_bundle, FileLoader sl) {
    si = _si;
    min_output_level = OUTPUT_PROGRAM;
    string err;
    try {
        InitPlatform(exefilepath, auxfilepath, from_bundle, sl);
        RegisterBuiltin(natreg, "ts", "treesheets", AddTreeSheets);
        RegisterCoreLanguageBuiltins(natreg);
    } catch (string &s) { err = s; }
    return err;
}

string RunLobster(std::string_view filename, std::string_view code, bool dump_builtins) {
    string err;
    try {
        string bytecode;
        Compile(natreg, filename, code, bytecode, nullptr, nullptr,
                false, RUNTIME_ASSERT, nullptr, 0, true);
        auto ret = RunTCC(natreg, bytecode, filename, nullptr, {}, TraceMode::OFF, false, err,
                          RUNTIME_ASSERT, true, false);
    } catch (string &s) {
        err = s;
    }
    return err;
}

void TSDumpBuiltinDoc() { DumpBuiltinDoc(natreg, true); }

}  // namespace script

namespace lobster {

FileLoader EnginePreInit(NativeRegistry &nfr) {
    // nfr.DoneRegistering();
    return DefaultLoadFile;
}

}  // namespace lobster

extern "C" void GLFrame(StackPtr, VM &) {}
string BreakPoint(lobster::VM &vm, string_view reason) { return {}; }
