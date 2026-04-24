# Animation Smoke Checklist

1. Query the target animation asset and confirm skeleton, duration, frame count, notifies, and curves.
2. If editing an Anim Blueprint state machine, take a before snapshot of states, transitions, and entry state.
3. Apply one focused edit at a time and recompile after each structural change.
4. Re-query the modified asset and confirm the changed fields are reflected in the result.
5. Run a lightweight editor or PIE validation pass and capture warnings, logs, or screenshots when behavior changes.
