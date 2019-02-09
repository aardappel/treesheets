
#include "lobster/stdafx.h"

#include "script_interface.h"

#include "lobster/compiler.h"

using namespace lobster;

namespace script {

ScriptInterface *si = nullptr;

void AddTreeSheets(NativeRegistry &natreg) {
    STARTDECL(ts_goto_root) (VM &) { si->GoToRoot(); return Value(); }
    ENDDECL0(ts_goto_root, "", "", "",
             "makes the root of the document the current cell. this is the default at the start"
             "of any script, so this function is only needed to return there.");

    STARTDECL(ts_goto_view) (VM &) { si->GoToView(); return Value(); }
    ENDDECL0(ts_goto_view, "", "", "",
             "makes what the user has zoomed into the current cell.");

    STARTDECL(ts_has_selection) (VM &) { return Value(si->HasSelection()); }
    ENDDECL0(ts_has_selection, "", "", "I",
             "wether there is a selection.");

    STARTDECL(ts_goto_selection) (VM &) { si->GoToSelection(); return Value(); }
    ENDDECL0(ts_goto_selection, "", "", "",
             "makes the current cell the one containing the selection, or does nothing on no"
             "selection.");

    STARTDECL(ts_has_parent) (VM &) { return Value(si->HasParent()); }
    ENDDECL0(ts_has_parent, "", "", "I",
             "wether the current cell has a parent (is the root cell).");

    STARTDECL(ts_goto_parent) (VM &) { si->GoToParent(); return Value(); }
    ENDDECL0(ts_goto_parent, "", "", "",
             "makes the current cell the parent of the current cell, if any.");

    STARTDECL(ts_num_children) (VM &) { return Value(si->NumChildren()); }
    ENDDECL0(ts_num_children, "", "", "I",
             "returns the total number of children of the current cell (rows * columns)."
             "returns 0 if this cell doesn't have a sub-grid at all.");

    STARTDECL(ts_num_columns_rows) (VM &vm) { return Value(ToValueINT(vm, int2(si->NumColumnsRows()))); }
    ENDDECL0(ts_num_columns_rows, "", "", "I}:2",
             "returns the number of columns & rows in the current cell.");

    STARTDECL(ts_selection) (VM &vm) {
        auto b = si->SelectionBox();
        vm.Push(ToValueINT(vm, int2(b.second)));
        return ToValueINT(vm, int2(b.first));
    }
    ENDDECL0(ts_selection, "", "", "I}:2I}:2",
             "returns the (x,y) and (xs,ys) of the current selection, or zeroes if none.");

    STARTDECL(ts_goto_child) (VM &, Value &n) { si->GoToChild(n.intval()); return Value(); }
    ENDDECL1(ts_goto_child, "n", "I", "",
             "makes the current cell the nth child of the current cell.");

    STARTDECL(ts_goto_column_row) (VM &, Value &x, Value &y) {
        si->GoToColumnRow(x.intval(), y.intval());
        return Value();
    }
    ENDDECL2(ts_goto_column_row, "col,row", "II", "",
             "makes the current cell the child at col / row.");

    STARTDECL(ts_get_text) (VM &vm) { return Value(vm.NewString(si->GetText())); }
    ENDDECL0(ts_get_text, "", "", "S",
             "gets the text of the current cell.");

    STARTDECL(ts_set_text) (VM &vm, Value &s) { si->SetText(s.sval()->strv()); s.DECRT(vm); return Value(); }
    ENDDECL1(ts_set_text, "text", "S", "",
             "sets the text of the current cell.");

    STARTDECL(ts_create_grid) (VM &, Value &x, Value &y) {
        si->CreateGrid(x.intval(), y.intval());
        return Value();
    }
    ENDDECL2(ts_create_grid, "cols,rows", "II", "",
             "creates a grid in the current cell if there isn't one yet.");

    STARTDECL(ts_insert_columns) (VM &, Value &x, Value &n) {
        si->InsertColumns(x.intval(), n.intval());
        return Value();
    }
    ENDDECL2(ts_insert_columns, "c,n", "II", "",
             "insert n columns before column c in an existing grid.");

    STARTDECL(ts_insert_rows) (VM &, Value &x, Value &n) {
        si->InsertRows(x.intval(), n.intval());
        return Value();
    }
    ENDDECL2(ts_insert_rows, "r,n", "II", "",
             "insert n rows before row r in an existing grid.");

    STARTDECL(ts_delete) (VM &vm, Value &pos, Value &size) {
        auto p = ValueDecToINT<2>(vm, pos);
        auto s = ValueDecToINT<2>(vm, size);
        si->Delete(p.x, p.y, s.x, s.y);
        return Value();
    }
    ENDDECL2(ts_delete, "pos,size", "I}:2I}:2", "",
             "clears the cells denoted by pos/size. also removes columns/rows if they become"
             "completely empty, or the entire grid.");

    STARTDECL(ts_set_background_color) (VM &vm, Value &col) {
        si->SetBackgroundColor(*(uint *)quantizec(ValueDecToFLT<3>(vm, col)).data());
        return Value();
    }
    ENDDECL1(ts_set_background_color, "col", "F}:4", "",
             "sets the background color of the current cell");

    STARTDECL(ts_set_text_color) (VM &vm, Value &col) {
        si->SetTextColor(*(uint *)quantizec(ValueDecToFLT<3>(vm, col)).data());
        return Value();
    }
    ENDDECL1(ts_set_text_color, "col", "F}:4", "",
             "sets the text color of the current cell");

    STARTDECL(ts_set_relative_size) (VM &, Value &s) {
        si->SetRelativeSize(geom::clamp(s.intval(), -10, 10));
        return Value();
    }
    ENDDECL1(ts_set_relative_size, "s", "I", "",
             "sets the relative size (0 is normal, -1 is smaller etc.) of the current cell");

    STARTDECL(ts_set_style_bits) (VM &, Value &s) {
        si->SetStyle(s.intval());
        return Value();
    }
    ENDDECL1(ts_set_style_bits, "s", "I", "",
             "sets one or more styles (bold = 1, italic = 2, fixed = 4, underline = 8,"
             " strikethru = 16) on the current cell.");

    STARTDECL(ts_set_status_message) (VM &vm, Value &s) {
        si->SetStatusMessage(s.sval()->strv());
        s.DECRT(vm);
        return Value();
    }
    ENDDECL1(ts_set_status_message, "msg", "S", "",
             "sets the status message in TreeSheets.");

    STARTDECL(ts_get_filename_from_user) (VM &vm, Value &is_save) {
        return Value(vm.NewString(si->GetFileNameFromUser(is_save.True())));
    }
    ENDDECL1(ts_get_filename_from_user, "is_save", "I", "S",
             "gets a filename using a file dialog. empty string if cancelled.");

}

NativeRegistry natreg;  // FIXME: global.

string InitLobster(ScriptInterface *_si, const char *exefilepath, const char *auxfilepath,
                   bool from_bundle, ScriptLoader sl) {
    si = _si;
    min_output_level = OUTPUT_PROGRAM;
    string err = "";
    try {
        InitPlatform(exefilepath, auxfilepath, from_bundle, sl);
        RegisterBuiltin(natreg, "treesheets", AddTreeSheets);
        RegisterCoreLanguageBuiltins(natreg);
    } catch (string &s) {
        err = s;
    }
    return err;
}

string RunLobster(const char *filename, const char *code, bool dump_builtins) {
    string err = "";
    try {
        string bytecode;
        Compile(natreg, filename, code, bytecode, nullptr, nullptr, dump_builtins);
        VM vm(natreg, filename, bytecode, nullptr, nullptr, vector<string>()); 
        vm.EvalProgram(); 
    } catch (string &s) {
        err = s;
    }
    return err;
}

}
