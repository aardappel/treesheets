// This example script prints a statistics line.

// Also a nice simple example on how to recurse through a TreeSheet in script.

import std

var cells = 0
var chars = 0

def count_cell() -> void:
    cells++
    chars += length(ts.get_text())
    for(ts.num_children()) i:
        ts.goto_child(i)
        count_cell()
        ts.goto_parent()

ts.goto_selection()
let size, start = ts.selection()
ts.goto_parent()
let g = ts.num_columns_rows()
for(ts.num_children()) j:
    if j % g[0] >= start[0] and j % g[0] < start[0] + size[0] and j / g[0] >= start[1] and j / g[0] < start[1] + size[1]:
        ts.goto_child(j)
        count_cell()
        ts.goto_parent()
ts.set_status_message("Selection contains {cells} cell(s) with {chars} character(s)")
