# World Production Recipe

1. Confirm the active editor world and resolve the exact actors or components you plan to edit.
2. Prefer reversible, rectangular, or bounded operations for spline, landscape, foliage, and partition changes.
3. Capture a before snapshot of transforms, bounds, or world-partition state before writing.
4. Batch related edits in one transaction, but keep unrelated systems in separate passes.
5. Save only after the post-edit query matches the intended result.
