// This example script exports a TreeSheet to JSON.

// That is not super-useful, since TreeSheets are not key-value pairs, so instead it exports
// as a recursive set of arrays with strings instead.

// In each array, the first element is cell text, followed by the children.
// For each child, if it has no children it is just a string, otherwise an array.

// Also a nice simple example on how to recurse through a TreeSheet in script.

import std

let out = []

def add_text(s):
    out.push("\"")
    // \ and " need to be escaped in JSON:
    out.push(escape_string(s, "\\\"", "\\", ""))
    out.push("\"")

def add_indent(indent):
    out.push(concat_string(map(indent): "  ", ""))

def add_cell(indent) -> void:
    out.push("[\n")
    add_indent(indent + 1)
    add_text(ts.get_text())
    for(ts.num_children()) i:
        out.push(",\n")
        ts.goto_child(i)
        add_indent(indent + 1)
        if ts.num_children():
            add_cell(indent + 1)
        else:
            add_text(ts.get_text())
        ts.goto_parent()
    out.push("\n")
    add_indent(indent)
    out.push("]")

let filename = ts.get_filename_from_user(true)
if filename.length:
    ts.goto_root()  // This the default, here just for clarity.
    add_cell(0)
    let ok = write_file(filename, concat_string(out, ""), true, true)
    ts.set_status_message("JSON export of {filename} " + if ok: "succesful" else: "failed!")
else:
    ts.set_status_message("export cancelled")
