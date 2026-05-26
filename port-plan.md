# Phase 4 Step 1-2 Status

- Timestamp: 2026-05-25T19:12:33-07:00
- Summary: Step 1-2 completed; `ssd-port-f2` was rooted at the pinned f2-bump SHA, `testchipip` cherry-picked cleanly, and the superproject now points at the ported `testchipip` commit.
- Port branch root SHA: `5845cf1700a38569bbe47d8cb11c24fa772fa84a`
- Submodule SHAs after init:
  - `generators/testchipip`: `5dca05bef9a9d7b135e18543379bc782df80ce40`
  - `sims/firesim`: `4a04e7a5f4094723bf392664b5ff9bbd8c088137`
  - `software/firemarshal`: `21119e5ce922ff9302f0bc9d2d7349c77f7ac064`
- `testchipip` cherry-pick outcome: clean
- Source `testchipip` commit: `450a527`
- Resulting `testchipip` commit on f2 base: `f86a977`
- Superproject bump commit: `ce3bab5a`
- Note: recursive submodule init left `sims/firesim/utils/fireperf/FlameGraph/` as untracked content; it was not staged or committed.
