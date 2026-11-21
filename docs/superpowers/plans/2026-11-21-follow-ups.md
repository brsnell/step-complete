# Follow-ups after the naming work

Status: done 2026-11-21 (PRs #14, #15).

## 1. CI off Node 20, plus a stale comment

GitHub warns about actions that run on Node 20. Bump each to its newest major, all on Node 24:
`actions/checkout` v7, `actions/cache/{restore,save}` v6, `actions/upload-artifact` v7,
`actions/download-artifact` v8, `softprops/action-gh-release` v3. Every input the workflows use
is unchanged in those majors (checked against each `action.yml`). `jurplel/install-qt-action@v4`
and `lukka/run-vcpkg@v11` already run on Node 24.

`ilammy/msvc-dev-cmd` has no Node 24 release. Decision: replace it with a local composite action,
`.github/actions/msvc-env`, which runs `vcvarsall.bat x64` and copies the variables it changes
into `GITHUB_ENV` (rather than forcing the old action onto Node 24 or living with the warning).

Also: `tests/app/test_measurements_panel.cpp` named the old `m_updating` member in a comment.

Check: CI green on all platforms with no Node 20 annotations; a manual `release.yml` run, since CI
never exercises its upload steps (the publish job runs on tags only).

## 2. PMI redraws at the right size after a scaling change

`PmiScene` fixes the device-pixel ratio in `setModel` (line widths, arrowheads, label pitch,
declutter gap), so PMI keeps the old size after the window moves to a screen with different
scaling. Calling `setModel` again would reset visibility, highlight and the semantic-labels
toggle. Add `PmiScene::setDevicePixelRatio(double)`, which rebuilds the presentations through
the same private `build()` as `setModel` and restores that state; `MainWindow` calls it on
`OcctViewport::devicePixelRatioChanged`, then declutters and redraws. Tests in
`tests/render/test_pmi_scene.cpp`.
