#include "lobster/stdafx.h"

#include "aggregate.h"
#include "script_interface.h"

#include "lobster/compiler.h"
#include "lobster/tonative.h"

using namespace lobster;

namespace lobster {

// TreeSheets only runs the JIT with JitOptions::mir left at its default (false), i.e. via the
// libtcc backend (RunTCC, in liblobster). The MIR backend (mirbind.cpp) vendors its own large
// external MIR/c2mir sources that TreeSheets doesn't build, so provide a stub to satisfy RunC's
// (tonative.h) unconditional reference to RunMIR at link time; it must never actually run.
bool RunMIR(const char *, const char *, string &error, const void **, const char **,
            const JitOptions &, function<bool(void **)>) {
    error = "RunMIR: MIR backend not built into TreeSheets";
    return false;
}

}  // namespace lobster

namespace script {

ScriptInterface *si = nullptr;

BuiltinGroup treesheets_builtins;
#define BUILTIN_GROUP treesheets_builtins
#define BUILTIN_SYM(name) builtin_ts_##name

BUILTIN(goto_root, "", "", "",
    "makes the root of the document the current cell. this is the default at the start "
    "of any script, so this function is only needed to return there.")
(VM &) {
    si->GoToRoot();
}

BUILTIN(goto_view, "", "", "", "makes what the user has zoomed into the current cell")
(VM &) {
    si->GoToView();
}

BUILTIN(has_selection, "", "", "B", "whether there is a selection")
(VM &) {
    return (iint)si->HasSelection();
}

BUILTIN(goto_selection, "", "", "",
    "makes the current cell the one selected, or the first of a selection")
(VM &) {
    si->GoToSelection();
}

BUILTIN(has_parent, "", "", "B", "whether the current cell has a parent (is the root cell)")
(VM &) {
    return (iint)si->HasParent();
}

BUILTIN(goto_parent, "", "", "", "makes the current cell the parent of the current cell, if any")
(VM &) {
    si->GoToParent();
}

BUILTIN(num_children, "", "", "I",
    "returns the total number of children of the current cell (rows * columns). "
    "returns 0 if this cell doesn't have a sub-grid at all.")
(VM &) {
    return (iint)si->NumChildren();
}

BUILTIN(num_columns_rows, "", "", "I}:2",
    "returns the number of columns and rows in the current cell")
(VM &) {
    return ToVec<iint2>(int2(si->NumColumnsRows()));
}

BUILTIN_OUTS(selection, "", "", "I}:2I}:2",
    "returns the (xs,ys) and (x,y) of the current selection, or zeroes if none")
(VM &, iint2 *out0, iint2 *out1) {
    auto b = si->SelectionBox();
    *out0 = ToVec<iint2>(int2(b.second));
    *out1 = ToVec<iint2>(int2(b.first));
}

BUILTIN(goto_child, "n", "I", "",
    "makes the current cell the nth child of the current cell. it is a runtime error if the "
    "current cell has no sub-grid, or n is not in 0..num_children() - 1.")
(VM &vm, iint n) {
    auto num = si->NumChildren();
    if (n < 0 || n >= num) {
        vm.BuiltinError(num == 0 ? cat("ts.goto_child: the current cell has no sub-grid")
                                 : cat("ts.goto_child: child ", n, " is invalid, it must be 0..",
                                       num - 1));
    }
    si->GoToChild((int)n);
}

BUILTIN(goto_column_row, "col,row", "II", "",
    "makes the current cell the child at col / row. it is a runtime error if the current cell "
    "has no sub-grid, or col / row is outside of it (see num_columns_rows()).")
(VM &vm, iint x, iint y) {
    auto [cols, rows] = si->NumColumnsRows();
    if (x < 0 || x >= cols || y < 0 || y >= rows) {
        vm.BuiltinError(cols == 0 ? cat("ts.goto_column_row: the current cell has no sub-grid")
                                  : cat("ts.goto_column_row: column ", x, ", row ", y,
                                        " is invalid, the grid has ", cols, " columns and ", rows,
                                        " rows"));
    }
    si->GoToColumnRow((int)x, (int)y);
}

BUILTIN(get_text, "", "", "S", "gets the text of the current cell.")
(VM &vm) {
    return vm.NewString(si->GetText());
}

BUILTIN(get_note, "", "", "S", "gets the note of the current cell.")
(VM &vm) {
    return vm.NewString(si->GetNote());
}

BUILTIN(set_text, "text", "S", "", "sets the text of the current cell")
(VM &, LString *s) {
    si->SetText(s->strv());
}

BUILTIN(set_note, "text", "S", "", "sets the note of the current cell")
(VM &, LString *s) {
    si->SetNote(s->strv());
}

// Raises a runtime error unless a cols x rows grid is one a script may create.
static void CheckNewGridSize(VM &vm, const char *builtin, iint cols, iint rows) {
    if (cols < 1 || rows < 1 || cols > max_new_grid_cells || rows > max_new_grid_cells ||
        cols * rows > max_new_grid_cells) {
        vm.BuiltinError(cat(builtin, ": a ", cols, " x ", rows, " grid is invalid, cols and rows "
                            "must be at least 1 and cols * rows at most ", max_new_grid_cells));
    }
}

BUILTIN(create_grid, "cols,rows", "II", "",
    "creates a grid in the current cell if there is not one yet. cols and rows must be at "
    "least 1, and cols * rows at most 65536, or this is a runtime error.")
(VM &vm, iint x, iint y) {
    CheckNewGridSize(vm, "ts.create_grid", x, y);
    si->CreateGrid((int)x, (int)y);
}

BUILTIN(insert_column, "c", "I", "", "inserts a column before column c in an existing grid")
(VM &, iint x) {
    si->InsertColumn((int)x);
}

BUILTIN(insert_row, "r", "I", "", "inserts a row before row r in an existing grid")
(VM &, iint x) {
    si->InsertRow((int)x);
}

BUILTIN(delete, "position,size", "I}:2I}:2", "",
    "clears the cells denoted by position/size. also removes columns/rows if they become "
    "completely empty, or the entire grid.")
(VM &, iint2 position, iint2 size) {
    auto p = ToVec<int2>(position);
    auto s = ToVec<int2>(size);
    si->Delete(p.x, p.y, s.x, s.y);
}

BUILTIN(set_background_color, "color", "F}:4", "", "sets the background color of the current cell")
(VM &, double4 color) {
    auto col = ToVec<float3>(color);
    si->SetBackgroundColor(*(uint32_t *)quantizec(col, 0.0f).data());
}

BUILTIN(set_text_color, "color", "F}:4", "", "sets the text color of the current cell")
(VM &, double4 color) {
    auto col = ToVec<float3>(color);
    si->SetTextColor(*(uint32_t *)quantizec(col, 0.0f).data());
}

BUILTIN(set_text_filtered, "filtered", "B", "", "sets the text filtered of the current cell")
(VM &, iint filtered) {
    si->SetTextFiltered(filtered != 0);
}

BUILTIN(is_text_filtered, "", "", "B", "whether the text of the current cell is filtered")
(VM &) {
    return (iint)si->IsTextFiltered();
}

BUILTIN(set_border_color, "color", "F}:4", "", "sets the border color of the current grid")
(VM &, double4 color) {
    auto col = ToVec<float3>(color);
    si->SetBorderColor(*(uint32_t *)quantizec(col, 0.0f).data());
}

BUILTIN(get_relative_size, "", "", "I", "returns the relative text size of the current cell")
(VM &) {
    return (iint)si->GetRelativeSize();
}

BUILTIN(set_relative_size, "size", "I", "",
    "sets the relative size (0 is normal, -1 is smaller etc.) of the current cell")
(VM &, iint s) {
    si->SetRelativeSize(geom::clamp((int)s, -10, 10));
}

BUILTIN(set_style_bits, "stylebits", "I", "",
    "sets one or more styles (bold = 1, italic = 2, fixed = 4, underline = 8,"
    " strikethru = 16) on the current cell")
(VM &, iint s) {
    si->SetStyle((int)s);
}

BUILTIN(get_style_bits, "", "", "I", "returns the stylebits of the current cell")
(VM &) {
    return (iint)si->GetStyle();
}

BUILTIN(set_text_alignment, "alignment", "I", "",
    "sets the text alignment of the current cell (automatic = 0, left = 1, center = 2,"
    " right = 3). Automatic aligns right-to-left text (such as Arabic or Hebrew) right,"
    " and all other text left")
(VM &vm, iint a) {
    if (a < 0 || a > 3) {
        vm.BuiltinError(
            cat("ts.set_text_alignment: alignment ", a, " is invalid, it must be 0..3"));
    }
    si->SetTextAlignment((int)a);
}

BUILTIN(get_text_alignment, "", "", "I",
    "returns the text alignment of the current cell (see set_text_alignment)")
(VM &) {
    return (iint)si->GetTextAlignment();
}

BUILTIN(set_vertical_alignment, "alignment", "I", "",
    "sets the vertical alignment of the current cell's text and grid in the height of its row"
    " (automatic = 0, top = 1, middle = 2, bottom = 3). Automatic is top, except for cells"
    " without a grid in line style, which are centered")
(VM &vm, iint a) {
    if (a < 0 || a > 3) {
        vm.BuiltinError(
            cat("ts.set_vertical_alignment: alignment ", a, " is invalid, it must be 0..3"));
    }
    si->SetVerticalAlignment((int)a);
}

BUILTIN(get_vertical_alignment, "", "", "I",
    "returns the vertical alignment of the current cell (see set_vertical_alignment)")
(VM &) {
    return (iint)si->GetVerticalAlignment();
}

BUILTIN(set_status_message, "message", "S", "", "sets the status message in TreeSheets")
(VM &, LString *s) {
    si->SetStatusMessage(s->strv());
}

BUILTIN(agent_result, "result", "S", "",
    "reports a value back to the connected agent for the current eval request, if any")
(VM &, LString *s) {
    si->SetAgentResult(s->strv());
}

BUILTIN(get_filename_from_user, "is_save", "I", "S",
    "gets a filename using a file dialog. empty string if cancelled.")
(VM &vm, iint is_save) {
    return vm.NewString(si->GetFileNameFromUser(is_save != 0));
}

BUILTIN(get_filename, "", "", "S", "gets the current documents file name")
(VM &vm) {
    return vm.NewString(si->GetFileName());
}

BUILTIN(load_document, "filename", "S", "B",
    "loads a document, and makes it the active one. returns false if failed.")
(VM &, LString *filename) {
    return (iint)si->LoadDocument(filename->data());
}

BUILTIN(new_document, "cols,rows", "II", "",
    "opens a new, unsaved document in a new tab with a root grid of the given size, and makes "
    "it the active one. cols and rows must be at least 1, and cols * rows at most 65536, or "
    "this is a runtime error.")
(VM &vm, iint cols, iint rows) {
    CheckNewGridSize(vm, "ts.new_document", cols, rows);
    si->NewDocument((int)cols, (int)rows);
}

BUILTIN(save_document, "saveas", "I", "B",
    "saves the current document to disk, same as the Save (saveas=false) / Save As "
    "(saveas=true) menu actions. if the document has no filename yet, always shows a save "
    "dialog. returns false if the save failed or the dialog was cancelled.")
(VM &, iint saveas) {
    return (iint)si->SaveDocument(saveas != 0);
}

BUILTIN(save_document_as, "filename", "S", "B",
    "saves the current document to the given filename, without ever showing a save dialog "
    "(unlike save_document(true)). appends .cts if filename has no extension, and makes this "
    "the document's filename for subsequent save_document() calls. returns false if failed.")
(VM &, LString *filename) {
    return (iint)si->SaveDocumentAs(filename->data());
}

BUILTIN(set_window_size, "width,height", "II", "", "resizes the window")
(VM &, iint w, iint h) {
    si->SetWindowSize((int)w, (int)h);
}

BUILTIN(get_last_edit, "", "", "I",
    "gets the timestamp of the last edit in milliseconds since the Unix/C epoch")
(VM &) {
    return (iint)si->GetLastEdit();
}

BUILTIN(get_current_time, "", "", "I",
    "gets the current timestamp in milliseconds since the Unix/C epoch")
(VM &) {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    return (iint)milliseconds;
}

BUILTIN(is_tag, "", "", "B", "whether the current cell text is a tag")
(VM &) {
    return (iint)si->IsTag();
}

BUILTIN(has_image, "", "", "B", "whether the current cell has an image")
(VM &) {
    return (iint)si->HasImage();
}

BUILTIN(get_column_width, "", "", "I", "get the column width of the current cell")
(VM &) {
    return (iint)si->GetColWidth();
}

BUILTIN(set_column_width, "width", "I", "", "set the column width of the current cell")
(VM &, iint w) {
    si->SetColWidth((int)w);
}

BUILTIN(remove_image, "", "", "", "remove image in the current cell")
(VM &) {
    si->RemoveImage();
}

BUILTIN(set_image, "filename", "S", "B", "set image for the current cell")
(VM &, LString *filename) {
    return (iint)si->SetImage(filename->data());
}

BUILTIN(set_image_display_scale, "scale", "I", "", "set display scale (in integer percentage)")
(VM &, iint w) {
    si->SetImageDisplayScale((int)w);
}

BUILTIN(undo, "", "", "B", "undoes the last edit, if any. returns whether there was one.")
(VM &) {
    return (iint)si->Undo();
}

BUILTIN(redo, "", "", "B", "redoes the last undone edit, if any. returns whether there was one.")
(VM &) {
    return (iint)si->Redo();
}

BUILTIN(is_grid, "", "", "B", "whether the current cell has a sub-grid")
(VM &) {
    return (iint)si->IsGrid();
}

BUILTIN(get_cell_type, "", "", "I",
    "returns the evaluation type of the current cell: 0 = data, 1 = operation, "
    "2 = variable assign, 3 = horizontal view, 4 = variable read, 5 = vertical view")
(VM &) {
    return (iint)si->GetCellType();
}

BUILTIN(is_folded, "", "", "B", "whether the current cell's grid is folded (collapsed)")
(VM &) {
    return (iint)si->IsFolded();
}

BUILTIN(set_folded, "folded", "B", "", "folds or unfolds the current cell's grid")
(VM &, iint folded) {
    si->SetFolded(folded != 0);
}

BUILTIN(get_background_color, "", "", "F}:4", "gets the background color of the current cell")
(VM &) {
    uint32_t c = si->GetBackgroundColor();
    return ToVec<double4>(color2vec(*(byte4 *)&c));
}

BUILTIN(get_text_color, "", "", "F}:4", "gets the text color of the current cell")
(VM &) {
    uint32_t c = si->GetTextColor();
    return ToVec<double4>(color2vec(*(byte4 *)&c));
}

BUILTIN(get_border_color, "", "", "F}:4", "gets the border color of the current grid")
(VM &) {
    uint32_t c = si->GetBorderColor();
    return ToVec<double4>(color2vec(*(byte4 *)&c));
}

BUILTIN(get_version, "", "", "S", "returns the TreeSheets version string")
(VM &vm) {
    return vm.NewString(si->GetVersion());
}

BUILTIN(find_exact, "text", "S", "B",
    "searches the subtree of the current cell (including itself) for a cell whose text exactly "
    "equals the given string, and makes it current if found. returns whether a match was found.")
(VM &, LString *text) {
    return (iint)si->FindExact(text->strv());
}

BUILTIN(copy_current, "", "", "",
    "deep-clones the current cell, including its subtree, into an in-process scripting "
    "clipboard, for use with paste_into_current()")
(VM &) {
    si->CopyCurrent();
}

BUILTIN(paste_into_current, "", "", "B",
    "pastes the contents of the scripting clipboard (see copy_current()) into the current "
    "cell, the same way a manual paste would. returns false if nothing has been copied yet, "
    "or the current cell has no parent (is the root).")
(VM &) {
    return (iint)si->PasteIntoCurrent();
}

BUILTIN(get_subtree_text, "format", "I", "S",
    "exports the subtree of the current cell to text in one call: 0 = plain indented text, "
    "1 = csv, 2 = xml. much cheaper than manually walking the subtree with goto_child/get_text "
    "in a loop.")
(VM &vm, iint format) {
    return vm.NewString(si->GetSubtreeText((int)format));
}

BUILTIN(set_cell_type, "type", "I", "",
    "sets the evaluation type of the current cell (see get_cell_type), same as the Program "
    "menu items. like there, a cell only becomes an operation (1) if its text is one, "
    "otherwise it becomes data. any other type than 0..5 is a runtime error.")
(VM &vm, iint type) {
    if (type < 0 || type > 5) {
        vm.BuiltinError(cat("ts.set_cell_type: type ", type, " is invalid, it must be 0..5"));
    }
    si->SetCellType((int)type);
}

BUILTIN(evaluate, "format", "I", "S",
    "evaluates the current cell the way Program > Run evaluates the whole document (which is "
    "what this does on the root cell): fills in the result views in its grid, and returns "
    "the result in the given format (see get_subtree_text), or an empty string if there is "
    "none. can be undone like any other edit.")
(VM &vm, iint format) {
    return vm.NewString(si->Evaluate((int)format));
}

BUILTIN(grid_count, "", "", "I",
    "returns the number of cells in the grid of the current cell whose text is a number. "
    "cells with other text and empty cells are skipped, as is anything inside sub-grids "
    "(only the direct children count). this is also what grid_sum, grid_min, grid_max, "
    "grid_avg and grid_median aggregate.")
(VM &) {
    return (iint)si->GridNumbers().size();
}

BUILTIN(grid_sum, "", "", "F",
    "returns the sum of the numbers in the grid of the current cell (see grid_count), "
    "or 0 if there are none")
(VM &) {
    return aggregate::Sum(si->GridNumbers());
}

BUILTIN(grid_min, "", "", "F",
    "returns the smallest number in the grid of the current cell (see grid_count), "
    "or 0 if there are none")
(VM &) {
    return aggregate::Min(si->GridNumbers());
}

BUILTIN(grid_max, "", "", "F",
    "returns the largest number in the grid of the current cell (see grid_count), "
    "or 0 if there are none")
(VM &) {
    return aggregate::Max(si->GridNumbers());
}

BUILTIN(grid_avg, "", "", "F",
    "returns the average of the numbers in the grid of the current cell (see grid_count), "
    "or 0 if there are none")
(VM &) {
    return aggregate::Avg(si->GridNumbers());
}

BUILTIN(grid_median, "", "", "F",
    "returns the median of the numbers in the grid of the current cell (see grid_count), "
    "i.e. the middle one when sorted, or the average of the two middle ones if their count "
    "is even. 0 if there are none.")
(VM &) {
    return aggregate::Median(si->GridNumbers());
}

#undef BUILTIN_GROUP
#undef BUILTIN_SYM

NativeRegistry natreg;  // FIXME: global.

string InitLobster(ScriptInterface *_si, const char *exefilepath, const char *auxfilepath,
                   bool from_bundle, FileLoader sl) {
    si = _si;
    min_output_level = OUTPUT_PROGRAM;
    string err;
    try {
        RegisterBuiltin(natreg, "ts", "treesheets", treesheets_builtins);
        RegisterCoreLanguageBuiltins(natreg);
        // Finalizes registration, filling in the JIT symbol table RunTCC/RunMIR link the
        // generated code against; RunTCC segfaults walking that table if this is skipped.
        EnginePreInit(natreg);
        InitPlatform(exefilepath, auxfilepath, from_bundle, sl);
    } catch (string &s) { err = s; }
    return err;
}

string RunLobster(std::string_view filename, std::string_view code, bool dump_builtins) {
    (void)dump_builtins;
    string err;
    try {
        CompileOptions opts;
        string c_codegen;
        err = Compile(natreg, filename, code, opts, c_codegen);
        if (err.empty()) {
            RunOptions ropts;
            RunJIT(natreg, filename, c_codegen, {}, opts, ropts, err);
        }
    } catch (string &s) {
        err = s;
    }
    return err;
}

void TSDumpBuiltinDoc() { DumpBuiltinDoc(natreg, true); }

}  // namespace script
