# Performance Triage Guide

1. Record whether you are measuring the editor world or PIE.
2. Capture frame time, FPS, actor/object counts, and memory signals before deep investigation.
3. Pair numerical summaries with at least one viewport or log artifact when the issue is visual or intermittent.
4. Keep snapshots timestamped and comparable so repeated runs can be diffed.
5. Treat one snapshot as evidence, not proof; look for stable regressions across multiple captures.
