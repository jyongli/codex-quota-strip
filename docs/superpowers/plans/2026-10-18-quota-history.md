# Quota change history

Goal: record genuine quota changes in a user-selected local directory and view them as a list or chart on Windows, Linux and macOS, retaining built-in Chinese/English UI.

Decisions:
- Record an initial baseline, then only quota/window/reset/source changes. Unknown values are blank, never zero; deltas are percentage points and reset across account/window cycles.
- UTF-8 daily CSV files contain observation/recording timestamps, remaining percentages, deltas, source and reset times. No account credentials, prompts or conversation contents.
- Enabled by default, directory defaults to the companion configuration directory/history. Settings provide an enable switch, directory chooser and viewer entry; tray/menu also opens the viewer.
- Existing files remain in their original directory after changing paths. No automatic deletion. Viewer reads at most the most recent 30 UTC files, 2000 rows and 2 MiB, only on open/manual refresh. No extra query timer or WebView.
- Logging failures are separate from quota/query state. New accepted snapshots trigger writes; repainting does not. Account changes reset the comparison baseline.

Ownership: main owns source/QuotaHistory.h, HistoryTests.cpp, native/NativeCore.*, runtime tests, build wiring, documentation and integration. Windows worker owns QuotaStrip.cpp, QuotaSettings.h, source/Localization.h, SettingsTests.cpp and HistoryUiTests.cpp. Native UI worker owns LinuxApp.cpp, MacApp.mm, native/Localization.h. Workers do not revert other changes or delegate.

Checks: shared real-file tests first fail then pass (selected directory, changes, missing values, reset/account boundaries, disabling, bounded reads, malformed/truncated file recovery and errors). Windows real headless/controlled UI fixtures validate writes and list/chart/path settings without screen capture. Native engine/fake-server tests validate successful writes and account resets; hosted platform UI smoke validates settings and both viewer modes. Run existing affected tests. Build artifacts and local installation use the tested commit; do not publish a Release as part of this feature request.
