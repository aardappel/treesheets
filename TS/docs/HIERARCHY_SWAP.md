## Command Surface
- **Menu / Shortcut:** Organization → Hierarchy Swap (F8), defined in [`tsframe.h`](../src/tsframe.h#L333-L348). The label reads “Hierarchy Swap” and the tooltip explains that all cells with the selected text are swapped with their parents at the current level (or above). The menu entry lives under the “Organization” cascade, grouped with flattening and hierarchify commands.
- **Action ID:** `A_HSWAP` in [`main.cpp`](../src/main.cpp#L130-L180). This ID connects the menu, keyboard shortcut, and dispatcher switch statement; it is also used in the toolbar accelerator table.
- **Validation and dispatch:** `Document::Action()` at [`document.h`](../src/document.h#L1860-L1920) enforces:
  - The selected cell has a parent and grandparent (minimum depth requirement) and therefore the command is disabled at the document root.
  - Both parent and grandparent grids are 1×N or N×1 (unidimensional constraint). If either grid fails the shape test, the action aborts before any structural edits occur.
  - An undo step is recorded before calling the swap, so the operation is fully reversible.
  - The returned `Selection` is installed, and the layout is reset plus canvas refresh is requested to reflect the new tree topology.
- **Failure modes (user-facing):**
  - No grandparent → “Cannot move this cell up in the hierarchy.”
  - Parent grid not 1×N or N×1 → “Can only move this cell from a Nx1 or 1xN grid.”
  - Grandparent grid not 1×N or N×1 → “Can only move this cell into a Nx1 or 1xN grid.”
  - Selection has no grid ancestry because the document is a single flat row/column → the menu item will be disabled due to the grandparent check.
- **Selection rule:** The operation is invoked on the currently selected cell; the returned selection usually points to the promoted/merged tag at the grandparent level. If merges occur, the earliest tag inserted into the target grid becomes the selection anchor and is reused for subsequent merges.
- **UI invariants to watch:** because `HierarchySwap` runs inside a single undo step, the canvas redraw and layout reset happen once per keypress, even when multiple promotions/merges occur via `goto lookformore`.

## Code Map
| Area | Location | Role |
| --- | --- | --- |
| Grid-level search | [`Grid::FindExact`](../src/grid.h#L867-L888) | Recursively finds the first cell whose text matches the selected tag. Walks depth-first through a child grid. |
| Main algorithm | [`Grid::HierarchySwap`](../src/grid.h#L889-L938) | Promotes each match, rebuilds its parent chain under it, merges like-tag peers, and restarts the search until no matches remain. |
| Parent cleanup | [`Grid::DeleteTagParent`](../src/grid.h#L940-L964) | Removes the promoted node from its old location, deleting empty 1×1 ancestors on the way up. |
| Merge helpers | [`Grid::MergeTagCell`](../src/grid.h#L966-L991) and [`Grid::MergeTagAll`](../src/grid.h#L993-L1000) | Merge promoted cells (and their grids) when the target level already contains the same tag. |
| Parent pointer fix-up | [`Grid::ReParent`](../src/grid.h#L1010-L1014) | Retargets parent pointers whenever a grid is transplanted. |
| Cell-level match | [`Cell::FindExact`](../src/cell.h#L484-L503) | Base-case exact-text comparison used by `Grid::FindExact`. |

### How these pieces cooperate
1. `Document::Action()` validates shape/depth, then calls `HierarchySwap` on the grandparent grid, passing the selected cell text.
2. `HierarchySwap` iterates each direct child cell of that grid that has a subgrid, scanning it with `FindExact`.
3. When a match is found, the ancestor chain is reversed into nested children (using `ReParent` for correctness), original containers are removed (`DeleteTagParent`), and the promoted tag is merged (`MergeTagCell`/`MergeTagAll`).
4. The search restarts (`goto lookformore`) so newly created structure is considered; the returned `Selection` points to the first merged tag at the target grid.

## Algorithm (from the current implementation)
The following is a line-by-line translation of the active codepath, emphasizing what actually happens rather than a simplified mental model.

1. **Search scope:** Start in the grandparent grid of the selected cell (the grid that owns the parent’s parent). Iterate each direct child that has a grid. The current child being scanned is referenced by `cell` inside the function.
2. **Find first match:** Use `Grid::FindExact(tag)` to locate the first cell in that child grid whose text equals the selected tag (case-sensitive). If none, continue to the next sibling with a grid.
3. **Build reversed chain:** For the found cell `f`, walk its parent chain up to (but not including) the grandparent cell that owns the running grid. For each ancestor `p`:
   - If `p->text` matches the tag, set `done = true` to stop after this promotion and avoid infinite swaps through same-named ancestors.
   - Clone `p` into a new cell attached under `f`, transfer `f`’s current grid to that clone, call `ReParent`, and give `f` a fresh 1×1 grid containing the clone. This makes every ancestor become a child nested under the promoted tag, preserving the original ordering from nearest to farthest parent.
4. **Detach the original chain:** Call `DeleteTagParent` repeatedly while walking upward, deleting empty 1×1 ancestors and cleaning up the spot where the match used to live. This pruning happens before any merge so that empties are not left behind in the original branch.
5. **Merge at target level:**
   - If the target grid was empty, place `f` there and mark it as the selection.
   - Otherwise call `MergeTagCell`, which either merges `f` into an existing like-named cell (combining grids via `MergeTagAll` when both have grids) or appends it if no duplicate exists. The first merged/added cell becomes the returned selection, and subsequent merges fold into that.
6. **Restart search:** `goto lookformore` restarts the sweep so newly created structure is also scanned. The loop ends when no further matches remain or `done` was set because an ancestor already matched the tag (the ancestor-match guard).
7. **Return selection:** The function returns a `Selection` pointing to the promoted/merged tag at the target level. The caller re-applies the selection and refreshes layout/UI.

### Behavioral Notes
- **Grid shape:** The operation only runs on 1×N or N×1 grids (checked before calling the algorithm). This keeps parent/child inversion unambiguous and ensures `DeleteCells` can safely collapse rows/columns when null slots appear.
- **Exact text match:** Matching is literal and case-sensitive (`Cell::FindExact`), so “Red” ≠ “red”. Hidden formatting (bold/italic) does not affect the match because only `text.t` is compared.
- **Search order:** Because the search restarts after every promotion, newly merged structures can be processed in subsequent passes; order is depth-first within each child grid. This restart is why multi-match merges can happen within one keypress.
- **Ancestor protection:** If an ancestor already has the same tag, `done` stops further promotions after that chain is processed to prevent cycling the same text upward forever. That means repeated-tag chains only promote once, even when additional matches exist deeper in the tree.
- **Merge semantics:** When the target grid already contains the tag, children from both structures are merged under the surviving tag cell. Subgrid merging preserves existing rows/columns as appended sibling rows, and `MergeTagAll` will recursively merge duplicate-tag grandchildren as well. If a promoted match brings in a cloned ancestor with the same name but no grid, `MergeTagCell` short-circuits on the first match and leaves only one copy.
- **Two-level hops:** Each keypress works against the selected cell’s grandparent grid, so very deep matches may need multiple presses to bubble all the way to the top-level grid where siblings live. The “press-count” tables below assume this two-level stride.
- **Undo/redo alignment:** An undo point is established before running the algorithm; all structural edits (promote, delete, merge) live inside that single undo step. Redo will replay the full set of promotions, merges, and deletions.
- **Selection stability:** The first promoted/merged cell at the target grid is returned as the new selection; subsequent merges do not change that pointer. This stability matters for keyboard users repeating swaps.
- **Empty-shell cleanup:** Because `DeleteTagParent` prunes empty 1×1 ancestors, the final tree omits placeholder shells that lost all children during promotion. When the grid has multiple rows, empty rows are physically removed via `DeleteCells`.
- **Grid ownership:** `ReParent` is called every time a grid is re-attached so parent pointers remain accurate for all transplanted children. This invariant is critical for subsequent operations such as copy/paste or further swaps.
- **Shape preservation for bystanders:** Cells in the grandparent grid that do not contain a matching tag remain in place (apart from row removal when a null slot is deleted). Use this property to predict stable ordering of unrelated siblings.

### Implementation Walkthrough (annotated pseudocode)
Below is an exact-structure pseudocode sketch that matches the current C++ implementation, including the restart logic:

```
HierarchySwap(tag):
    selcell = nullptr
    done = false
lookformore:
    for each cell in this grid where cell has a subgrid:
        found = cell->grid->FindExact(tag)
        if not found: continue

        // Reverse the ancestor chain into children under `found`
        for p = found->parent; p != cell; p = p->parent:
            if p->text == tag: done = true
            clone = new Cell(found, p)
            clone->text = p->text
            clone->grid = found->grid
            if clone->grid: clone->grid->ReParent(clone)
            found->grid = new Grid(1, 1)
            found->grid->cell = found
            *found->grid->cells = clone

        // Remove the original chain (prunes empties)
        for r = found; r && r != cell; r = r->parent->grid->DeleteTagParent(r, cell, found);

        // Merge or insert at the target level
        if !cells[0]:
            *cells = found
            selcell = found
        else:
            MergeTagCell(found, selcell)

        if !done: goto lookformore
    return Selection(this, selcell)
```
Key takeaways from this structure:
- The `goto` intentionally restarts the `for` so the modified grid is scanned anew.
- `done` only flips when a same-named ancestor exists; otherwise every match reachable from the scanning grid will be processed.
- The merge target is always the grid on which `HierarchySwap` is called (the grandparent grid chosen by the caller).
- The loop condition `p != cell` stops cloning at the grid’s owning cell (the grandparent), so only ancestors strictly below that owning cell are nested under the promoted tag.

## Examples
The following scenarios are constructed directly against the algorithm above:

### 1) Baseline: Single Match Promotion
**Before** (select "Alice", grandparent grid owns `Project`):
```
Projects
   Project A
      Alice
   Project B
      Bob
```

**After F8 on "Alice":**
```
Projects
   Project B
      Bob
   Alice
      Project A
```
- The `Alice` branch moves to the grandparent grid. `Project A` becomes a child under `Alice`. Other siblings stay put.
- No merge occurs because only one `Alice` exists. Undo reverses the promotion cleanly.
- Plain text before: `Projects\n  Project A\n    Alice\n  Project B\n    Bob\n`
- Plain text after: `Projects\n  Project B\n    Bob\n  Alice\n    Project A\n`

### 2) Multiple Matches at Different Depths
**Before** (grandparent grid is `Colors`):
```
Colors
   Warm
      Red
      Orange
   Cool
      Blue
   Mixed
      Purple
         Red
```

**After F8 on either "Red":**
```
Colors
   Warm
      Orange
   Cool
      Blue
   Red
      Warm
      Mixed
         Purple
```
- First match promotes `Warm` under `Red`, leaving `Orange` behind.
- Second match promotes `Mixed → Purple` under another `Red`.
- The two `Red` results merge at the `Colors` level; children from both chains are preserved.
- Merge order is deterministic because the scan restarts after each promotion: the first child grid containing a match is processed fully before later siblings are scanned again.
- Plain text (pre-swap): `colors\n  warm\n    red\n    orange\n  cool\n    blue\n  mixed\n    purple\n      red\n`
- Plain text (post-swap): `colors\n  warm\n    orange\n  cool\n    blue\n  red\n    warm\n    mixed\n      purple\n`
- XML (pre) mirrors the hierarchical listing; XML (post) reflects the merged `Red` node with `Warm` and `Mixed` children.

### 3) Single-Path with Repeated Tags
**Before** (select the deeper "Tag", grandparent grid is the root):
```
Root
   Tag
      Tag
         Item
```

**After F8 on inner "Tag":**
```
Root
   Tag
      Tag
         Item
```
- The inner match is promoted to the root grid.
- The original outer tag is preserved as a child under the promoted tag (the ancestor clone step).
- Because an ancestor shared the tag, `done` stops further passes after this promotion. That guard prevents repeatedly flipping the two tags back and forth.
- Variants to test: add a third nested `Tag` to confirm only one promotion occurs when a same-named ancestor exists.

### 4) Grid Merge with Existing Hierarchy 
**Before** (select any `tag` cell; grandparent grid is `main`):
```
main
   branch1
      tag
         a
         b
         c
   branch2
      tag
         d
         e
         f
```

**After F8 on `tag`:**
```
main
   tag
      branch1
         a
         b
         c
      branch2
         d
         e
         f
```
- Each `tag` is promoted; their parent chains (`branch1`, `branch2`) become children under the promoted tags.
- The promoted tags merge at the `main` level, combining both sub-branches under one `tag`.
- Empty intermediate grids do not survive because `DeleteTagParent` prunes 1×1 shells.
- Regression hint: if a third `tag` existed under `branch3`, it would also merge into the single top-level `tag` during the same keypress because the search restarts until no matches remain.

### 5) Deep Match with Mixed Siblings (depth-dependent passes)
This example illustrates the “two-level hop” rule: each keypress processes the current selection’s grandparent grid. Deeper matches can require multiple presses to reach the shared ancestor where merging occurs.

**Before (same starting state for all runs):**
```
Departments
   Sales
      Q4
         Jamie
   Support
      Jamie
   Engineering
      Backend
         Team A
            Jamie
```

**If you press F8 on the shallow Jamie (under `Sales → Q4`):**
- *After the first press (scope = `Sales` grid):*
  ```
  Departments
     Sales
        Jamie
           Q4
     Support
        Jamie
     Engineering
        Backend
           Team A
              Jamie
  ```
- *After the second press (scope = `Departments` grid):*
  ```
  Departments
     Jamie
        Sales
           Q4
        Support
        Engineering
           Backend
              Team A
  ```
  - First press bubbles the match two levels (grandparent = `Sales`).
  - Second press runs at `Departments`, finds all three `Jamie` matches, promotes each, and merges them at the top level.

**If you press F8 on the mid-depth Jamie (under `Support`):**
```
Departments
   Jamie
      Sales
         Q4
      Support
      Engineering
         Backend
            Team A
```
- One press is enough because the selected cell’s grandparent grid is already `Departments`, so all three matches are discovered and merged in a single pass. The scan restarts after each promotion, but all matches reside directly in the processed scope, so the merge completes immediately.

**If you press F8 on the deep Jamie (under `Engineering → Backend → Team A`):**
- *After one press (scope = `Backend` grid):*
  ```
  Departments
     Sales
        Q4
           Jamie
     Support
        Jamie
     Engineering
        Backend
           Jamie
              Team A
  ```
- *After two presses (scope = `Engineering` grid):*
  ```
  Departments
     Sales
        Q4
           Jamie
     Support
        Jamie
     Engineering
        Jamie
           Backend
              Team A
  ```
- *After three presses (scope = `Departments` grid):*
  ```
  Departments
     Jamie
        Sales
           Q4
        Support
        Engineering
           Backend
              Team A
  ```
- Each press operates two levels up from the current selection, so the deep match must be swapped three times to reach the shared `Departments` grid where the merge can occur. At that point, all `Jamie` nodes are coalesced.

**Press counts by depth:**
| Selected `Jamie` | Initial depth relative to `Departments` | Presses to merge all three | Why |
| --- | --- | --- | --- |
| Shallow (`Sales → Q4 → Jamie`) | Great-grandchild | 2 | First press moves into the parent’s parent (`Sales`); second press runs in `Departments` and merges everything. |
| Mid-depth (`Support → Jamie`) | Grandchild | 1 | Already two levels below `Departments`; one pass finds all matches. |
| Deep (`Engineering → Backend → Team A → Jamie`) | Great-great-grandchild | 3 | Needs three hops (Backend → Engineering → Departments) because each swap uses the current grandparent grid. |

**Alternate representations for automated checks:**
- Pre-swap plain text: `Departments\n  Sales\n    Q4\n      Jamie\n  Support\n    Jamie\n  Engineering\n    Backend\n      Team A\n        Jamie\n`
- Post-merge plain text (after mid-depth or second shallow press or third deep press): `Departments\n  Jamie\n    Sales\n      Q4\n    Support\n    Engineering\n      Backend\n        Team A\n`
- XML versions can mirror these structures to diff serialized `.cts` files.

### 6) Flat Sibling Merge (single pass, no depth hops)
This mirrors a shallow multi-match merge with no ancestor reuse beyond the shared parent. The grandparent grid is the document root; its only child with a subgrid is `Root`.

**Before** (select any `Tag`; grandparent grid = document root, scanning the `Root` grid):
```
<doc root>
   Root
      Tag
      Tag
      Tag
      Other
```

**After one F8 on any `Tag`:**
```
<doc root>
   Root
      Other
   Tag
      Root
```
- Each `Tag` is promoted out of `Root` and merged at the document root grid in a single pass because the restart (`goto lookformore`) continues scanning until no matches remain.
- The ancestor-clone step adds a `Root` child under every promoted `Tag`, but because clones share the same text and carry no grids, `MergeTagCell` collapses them into a single `Root` child on the surviving `Tag`.
- `Root` keeps only the non-matching `Other` child because `DeleteTagParent` removes each `Tag` row from its grid but does not delete the grid itself (it is not 1×1).
- Regression tip: if one of the `Tag` nodes had its own grid, that grid would merge into the surviving `Tag` as well via `MergeTagAll`; duplicates without grids are dropped as shown here.

### 7) Partial Empty Parents (slot deletion)
This showcases how null slots are deleted when a promoted child leaves behind an empty 1×1 grid, while non-empty siblings remain.

**Before** (select the first `Target`; grandparent grid = `Main`’s parent):
```
Main
   Holder1
      Target
      Sibling
   Holder2
      Target
```

**After F8 on `Target`:**
```
Main
   Holder1
      Sibling
   Target
      Holder1
      Holder2
```
- The first promotion clones `Holder1` under the new `Target` and removes the original `Target` row, leaving `Holder1` with only `Sibling`.
- The second promotion clones `Holder2` under a second `Target`; because `Holder2`’s grid becomes empty (1×1), `DeleteTagParent` removes that grid and returns the `Holder2` cell to the caller.
- `MergeTagCell` folds the second promoted `Target` into the first, merging the two cloned parents (`Holder1`, `Holder2`) under the surviving `Target` while keeping the `Holder1` branch with `Sibling` intact.
- This example is a good probe for the `DeleteCells` path that removes a null slot from a multi-row grid and for the merge helper when one parent clone already exists.

## Practical Testing Checklist
Use these focused checks to validate behavior after any code change touching the swap logic:
- Verify swaps only run when both parent and grandparent grids are 1×N or N×1 (exercise all three error messages).
- Confirm merged results keep every child grid (use Examples 2 and 4 to see that no payload is dropped during merges).
- Exercise the ancestor-tag guard by creating a chain with repeated tags (Example 3) and ensure only one promotion occurs.
- Walk the depth-dependent paths (Example 5) to ensure press counts still match the two-level-hop rule.
- Ensure undo works: perform a swap and Ctrl+Z to restore; redo should reapply the promotion without divergence.
- Serialize to `.cts` and diff the XML snippets above to confirm structural equivalence in file form.

## FAQ (code-grounded)
- **Why restart with `goto`?** The grid topology changes during promotion and merge. Restarting ensures newly attached grids are eligible for immediate scanning without complex iterator management.
- **Why prune empty 1×1 grids?** Promotion often hollows out ancestor shells. Cleaning them avoids empty visual rows/columns and keeps selection paths short.
- **Why does an ancestor tag stop further passes?** Without `done`, a repeated tag could ping-pong upward indefinitely. The guard enforces a single promotion when the chain already contains the tag.
- **Can swaps occur across unrelated branches?** No. The scope is the selected cell’s grandparent grid; only descendants of each child grid under that grandparent are scanned.
- **What happens when both merge candidates have grids?** `MergeTagAll` merges every cell from the source grid into the destination grid, preserving ordering; if one side lacks a grid, the other grid is kept intact.

## Quick Regression Matrix (who to press where)
| Scenario | Selection | Expected presses | Expected top-level result |
| --- | --- | --- | --- |
| Single match | `Alice` under `Project A` | 1 | `Alice` at Projects with `Project A` child |
| Two matches, differing depths | Any `Red` | 1 | Single `Red` at `Colors` with `Warm` and `Mixed → Purple` children |
| Repeated ancestor tag | Inner `Tag` | 1 | Two nested `Tag` nodes under `Root`, no further passes |
| Parallel tag merges | Any `tag` under `branch1/2` | 1 | Single `tag` under `main` with both branches merged |
| Depth-varied siblings | Shallow `Jamie` | 2 | Single `Jamie` under `Departments` with all departments beneath |
| Depth-varied siblings | Mid `Jamie` | 1 | Same as above |
| Depth-varied siblings | Deep `Jamie` | 3 | Same as above |
