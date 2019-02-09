// This example script exports a TreeSheet to JSON.

// That is not super-useful, since TreeSheets are not key-value pairs, so instead it exports
// as a recursive set of arrays with strings instead.

// In each array, the first element is cell text, followed by the children.
// For each child, if it has no children it is just a string, otherwise an array.

// Also a nice simple example on how to recurse through a TreeSheet in script.

include "std.lobster"

out := []

def add_text(s):
    out.push("\"")
    // \ and " need to be escaped in JSON:
    out.push(escapestring(s, "\\\"", "\\", ""))
    out.push("\"")

def add_indent(indent):
    out.push(concatstring(map(indent): "  ", ""))

def add_cell(indent):
    out.push("[\n")
    add_indent(indent + 1)
    add_text(ts_get_text())
    for(ts_num_children()) i:
        out.push(",\n")
        ts_goto_child(i)
        add_indent(indent + 1)
        if ts_num_children():
            add_cell(indent + 1)
        else:
            add_text(ts_get_text())
        ts_goto_parent()
    out.push("\n")
    add_indent(indent)
    out.push("]")

fn := ts_get_filename_from_user(true)
if fn.length:
    ts_goto_root()  // This the default, here just for clarity.
    add_cell(0)
    ok := write_file(fn, concatstring(out, ""))
    ts_set_status_message("JSON export " + if ok: "succesful" else: "failed!")
else:
    ts_set_status_message("export cancelled")
