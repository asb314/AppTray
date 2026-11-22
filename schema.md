# AppTray — structure

## Layout

```
AppTray.slnx                 solution (VS 2026)
Directory.Build.props        makes every project use the vcpkg bundled with Visual Studio
vcpkg.json                   dependencies, manifest mode (triplet x64-windows-static)
build/AppTray.Common.props   shared compiler/linker settings: C++20, /W4 /WX, static CRT, LTCG
src/AppTray.Core/            static library, no UI: everything testable
src/AppTray/                 the program (Windows subsystem): tray icon, launcher, menus
tests/AppTray.Core.Tests/    GoogleTest unit tests for AppTray.Core (and the UI texts in Strings.h)
tools/Deploy-AppTray.ps1     build + test + install into a folder (closes a running copy)
tools/New-Release.ps1        build + test + zip (+ SHA-256); -Publish tags and creates the GitHub release
tools/New-AppIcon.ps1        draws res/AppTray.ico (PNG frames 16-256 px)
```

## AppTray (UI)

| File | Responsibility |
|---|---|
| `main.cpp` | Entry point: `--scan` (console output), `--check-update`, `--register-task`/`--unregister-task` (elevated copy), `--create-shortcut`, single instance per exe folder (mutex; a second start forwards `--show`/`--exit`), COM and common controls |
| `TrayApp` | The running program: catalog, settings (reloaded when `user.json` changes; `user.json.bak` before each change), usage (`usage.json`), tray menu (favorites, recent apps, running apps, category submenus filled on `WM_INITMENUPOPUP`), global hotkey (`RegisterHotKey`), background refresh, settings moved to renamed app folders, actions on apps (switch to a running one, close its windows, console programs in a console, folder on the user PATH, keywords), shortcuts, logon apps, Start with Windows, daily update check (and on demand), "Acerca de" task dialog |
| `TextPrompt` | Small modal one-line text dialog (OK / Cancel), owned by the launcher: the keywords of an app |
| `StartupPrompt` | The logon dialog: "ask" apps as a check-box list view with a countdown (Abrir / Ninguna) |
| `LauncherWindow` | The flyout: folder search over the category tree (pruned with `FilterCategoryTree`; Favorites, Recent, All, New, categories...), app search over an owner-data list view (scoped to the chosen folder), "running" tag, status line with version, a painted menu button (and F10) that opens the tray menu owned by the launcher (it forwards `WM_INITMENUPOPUP`); created on show, destroyed on deactivation (not while its own keywords dialog is up); keyboard shortcuts in `PreTranslateMessage` |
| `TrayIcon` | `Shell_NotifyIcon` v4 with a GUID (falls back to a plain id if refused): add (again after `TaskbarCreated`), tip, notifications, icon rectangle |
| `IconLoader` | App icons (`SHDefExtractIcon`, shell icon for scripts/shortcuts/folders), menu bitmaps through WIC, background `IconWorker` |
| `Theme` | Light/dark from `AppsUseLightTheme` (or `APPTRAY_THEME`), palette, DWM corners/border, dark menus and controls |
| `UiCommon` | Window messages, DPI helpers, system font, instance name and the tray icon GUID derived from it |
| `Strings.h` | All user-facing texts, Spanish and English (`text::Text`; `text::Format` for the ones with `{}`); the language follows Windows or user.json `language` |

Memory: the launcher window, its image list, fonts and icon thread exist only while it is open.
After loading the catalog, after each refresh and when the launcher closes, the heap is
compacted (`HeapCompact`): ~2.5 MB of private memory after start (6 MB without it), ~5 MB once
the launcher has been used. Icons avoid `SHGetFileInfo` and `IShellLink` on purpose: both load
the shell namespace, which left ~1.5 MB and four threads behind for good. Scripts get cmd.exe's
icon (`.ps1` the PowerShell one), folders the stock folder icon, and `.lnk` files are read
directly (MS-SHLLINK).

Refresh: the tray never scans in its own process. It starts `AppTray.exe --scan --root ...
--data ...` (below-normal priority) and waits for it on a thread; exit code 0 means
catalog.json is new and is reloaded, 3 means the root is missing. The child's threads, heap
and mapped files vanish with it, so the tray's memory does not grow with the tree. Exiting
kills the child; catalog.json is written atomically, so that never leaves a broken file.

Scanning in parallel: the root's folders are independent subtrees, scanned by up to 8 threads
and merged in folder order (same result as a sequential scan). A cold scan is mostly waiting
for the antivirus to clear each executable opened, and those waits overlap.

Other child processes, same mechanism (`StartChildProcess`): `--check-update` at most once a day,
or at once from "Comprobar nueva versión" (then the answer is always reported) (exit 10 = newer
release; the result is read from `data\update.json`, a failure retries in an hour), and `--create-shortcut <id> --shortcut-in desktop|startmenu` (`IShellLink` loads the
shell namespace, which would stay in the tray's memory). Creating or deleting an elevated task runs `AppTray.exe --register-task <id>` with
`runas` instead (one UAC prompt); `elevatedTask` is written to `user.json` only when that copy
exits with 0.

Logon (`--startup`, from the HKCU Run value): one-shot timers run `RunLogonApps` after
`startupDelaySeconds` and the first update check after 60 s. `PlanAutostart` lists the
"always" apps to open and the "ask" apps for `StartupPrompt`, skipping hidden, missing and
running ones (`ProcessFinder` with `GetProcessScope`, as for the launcher's "running" apps). An app that needs administrator opens through its elevated
task or not at all. The logon run holds a `MenuScope`, so a refresh cannot swap the items
under the dialog.

Renamed app folders (a new version): `MergeCatalog` pairs an app gone from disk with an unknown
one of the same lineage (category, kind, backup flag, `NormalizeName` of the folder name, which
drops the numbers) when there is exactly one of each. The new entry keeps the old `firstSeen`
and gets `renamedFrom`; the old one is not kept as missing. The scan never writes `user.json`,
so the tray does the move after loading the catalog (`MoveRenamedAppSettings`: favorites in
place, override and hand-written keys, target rebased or dropped, elevated task dropped) and
moves the usage. Until user.json can be written, later scans keep `renamedFrom`.

Running apps: when the launcher or the tray menu opens, one process snapshot
(`GetRunningProcesses`) sets `AppItem::running` (`GetProcessScope`: folder and PortableApps
apps by any process inside their folder, others by their executable, suites never); the ids are
kept so a rebuild of the items keeps the flags. Opening an app takes a fresh snapshot: if one of
its processes has a main window (`FindMainWindows`: visible, unowned, titled, no tool window,
front-most first; in a folder app the chosen executable's windows first, the folder's other
programs only when it has none) AppTray activates it instead of starting it, unless as
administrator or as "another copy", and records it as a use. "Close" posts `WM_CLOSE` to those
windows (blocked by UIPI for elevated ones).

Consoles: a target whose executable is a console program (`AppEntry::console` from the scan, or
`ExeInspector` for a target the user chose, skipped on a network root), unless the app's
`console` override says otherwise, starts through `BuildConsoleCommand`: pwsh / powershell
`-NoExit -Command` (the app's arguments after `--%`, untouched) or `cmd /s /k` (operators in
the arguments escaped; `pushd`, which also enters UNC folders), with the folder prepended to
`PATH` and made current inside the command line itself, so it also works through `runas`. Not
at logon, and not for a missing executable (the shell would hide the "not found").
A `.ps1` target (`IsPowerShellScript`) always starts through `BuildScriptCommand`, at logon
too: the same PowerShell command in the script's folder, whatever the `shell` setting, plus
`-ExecutionPolicy Bypass` (that process only; a Group Policy still wins) and the arguments as
PowerShell syntax (no `--%`). Through `ShellExecute` a `.ps1` opens in Notepad. Its shortcuts
point at that console too; `ChooseShortcutFile` takes as the script's any link to pwsh or
powershell with its quoted path in the arguments. "Its folder on the PATH" edits `HKCU\Environment\Path` (`UserPath`: entries compared
expanded, unquoted, without trailing backslash, case-insensitive; the value type is kept),
refreshes the tray's own `PATH` from the registry (`CreateEnvironmentBlock`) and broadcasts
`WM_SETTINGCHANGE "Environment"` on a detached thread. The tray also refreshes its `PATH` when
anyone else broadcasts that change.

Hotkey: `RegisterHotKey` on the tray window (`MOD_NOREPEAT`); `WM_HOTKEY` toggles the launcher
(ignored while a menu or the logon dialog is open). The input that fires it lets the launcher
take the foreground. Re-registered only when `"hotkey"` changes.

A finished refresh never replaces the items while a popup menu is open (`TrayApp::MenuScope`):
its result waits until the menu's command has run. A `user.json` that does not parse is never
overwritten: changes and refreshes are refused until it is fixed. `FolderScanner` also honours
a `std::stop_token` (used by callers that scan in-process).

## AppTray.Core

| File | Responsibility |
|---|---|
| `AppEntry` | One catalog entry (`AppKind`: folder, file, portableApps, installer, suite) |
| `FolderScanner` | Walks the tree and decides what is a category and what is an app |
| `ExeRanker` | Picks the main executable of an app folder (scoring) and spots installers by name |
| `ExeInspector` | Reads PE header, version resource (names, product version) and manifest without running the file |
| `NameTokens` | Tokenizes file/folder names and scores how well they match; tidies versions and finds one in a name |
| `Catalog` | `catalog.json` load/save |
| `UserSettings` | `user.json` load/save (keeps unknown keys; per-app `keywords` and `console` choice, the console `shell`); `SerializeUserSettings` also feeds `user.json.bak` |
| `CatalogMerger` | Fresh scan + previous catalog + user data → new catalog (renamed version folders recognized); `MoveRenamedAppSettings` moves the user's data to them |
| `UsageStats` | `usage.json`: launch count and last launch per app; record, move, load/save |
| `Hotkey` | Parses and formats the global hotkey (`"Ctrl+Alt+Space"`, Spanish key names too) |
| `Shortcut` | `.lnk` files with `IShellLink` (desktop, Start menu\Programs\AppTray), file names without forbidden characters |
| `CatalogRefresher` | Scan + merge + save in one call, thread-safe (used by `--scan` and the tray's background refresh) |
| `AppList` | Catalog + `user.json` + usage → items the UI shows (overrides, favorites, hidden, new, autostart, launches, keywords, running flag); filters (Recent too), accent-insensitive search that also matches initials and keywords, ranking by launches while searching, category tree, logon plan, where to look for an app among processes (`GetProcessScope`) |
| `AppLauncher` | `ShellExecuteEx` launch (normal or `runas`), open folder/document, reveal in Explorer |
| `ElevatedTask` | Task Scheduler (COM) tasks in `\AppTray` (name = readable id + FNV-1a hash): run elevated on demand with the user's interactive token, no trigger; create (for the user passed with `--task-user`), delete, run only if its action still matches the app's exe and arguments; delete orphans (program inside the root and gone) on every create/delete |
| `LogonRegistration` | The HKCU `Run` values of AppTray (`"exe" --startup`): `AppTray` for the first copy, `AppTray (<tree folder>)` for others; set, delete (only this exe's), repair when the exe moved (the one value whose exe is missing and has the same last three path parts; a missing exe's value is never reused for another copy, it may be on an unplugged drive) |
| `ProcessFinder` | Running processes with their image paths (`QueryFullProcessImageName`, works for elevated ones) and the processes of an app (by file, or by folder prefix) |
| `AppWindows` | Main windows of some processes (as the taskbar shows them), activate one, ask one to close |
| `ConsoleCommand` | The console shell (pwsh / Windows PowerShell / cmd) and its command line: open in a folder with it first in `PATH`, optionally run a program, stay open; how a `.ps1` runs (`BuildScriptCommand`) |
| `UserPath` | The user's `PATH` in `HKCU\Environment`: find, add, remove and rebase entries; read/write keeping the value type; announce the change; refresh the process's own `PATH` |
| `UpdateCheck` | Latest GitHub release over WinHTTP, version comparison, `update.json` |
| `Version.h` | Version number and GitHub repository (also used by the `.rc` and `New-Release.ps1`) |
| `PathResolver` | Absolute ↔ root-relative paths (`/` separators); root resolution |
| `ScanReport` | Markdown report of a scan |
| `JsonFile` | UTF-8 JSON read, atomic tab-indented write |
| `CommandLine` | Argument parsing |
| `Log` | Small rotating UTF-8 log |
| `StringUtil` | UTF-8/UTF-16, case-insensitive comparisons, UTC timestamps, accent folding for search |

## Scanner rules

If the root is missing or cannot be listed, the scan reports `rootFound = false` and the
existing catalog is left untouched.

For each folder F (max depth 6):

1. `App\AppInfo\appinfo.ini` → one **portableApps** entry (name, description, `Start=` when it
	points inside the app folder; otherwise the best-ranked exe).
2. Never entered: ignored paths from `user.json`, AppTray's own folder, `.git`, `node_modules`,
	`_originales`, `_anterior`..., and names with the tokens `crack`, `keygen`, `keymaker`,
	`patch`, `activator`. Inside an app folder, resource folders (`locale`, `docs`, `help`,
	`platforms`...) are not searched for executables either; elsewhere they are categories.
3. Launchable files: `.exe .bat .cmd .ps1 .jar .lnk` (scripts rank below a program unless
	named as a launcher: `portable`, `launch`...). Below the root a `.ps1` counts only when
	named like its folder (`IsHelperScript`): other scripts are helpers that would make an
	app's `scripts\` an app of its own; they remain alternatives. Installers by whole words of the name
	(`setup`, `install`, `unins000`, `update`, `patch`, `keygen`, `crack`, `redist`, Spanish
	`instalar`/`instalador`/`desinstalar`, `*.paf.exe`,
	glued "FiddlerSetup"/"SetupAnyDVD" but not "WinSetupFromUSB") or by version info ("… Setup",
	"… Installer", but not "Inno Setup Compiler").
4. F is an **app folder** when it has programs and (a program name matches the folder name,
	or F has DLLs at its top level, or every subfolder below holding an app is a layout or
	helper folder that is itself one app).
5. Otherwise F is a **category**: each loose program is a **file** entry and subfolders are
	evaluated. When the only apps below are layout folders (`bin`, `x64`, `arm64`, `app(s)`,
	`lib`...), F becomes one app (searched 4 levels deep) with those as alternatives. Helper
	folders (`tools`, `scripts`, `plugins`, `python*`, `java`, `runtime`...) join an app only when
	F has programs of its own. A subfolder that held several apps, or apps further down, is
	a category whatever its name ("Media\Tools").
6. A folder with only installers → one **installer** entry (hidden by default in the UI).
7. Folder names ending in `_`, `_1`, ` - copia` → `alternate: true` (deliberate copies).
8. More than 15 top-level programs, none matching the folder and no DLLs → **suite** (opens
	the folder; `expand: true` in `user.json` lists each program, preferring 64-bit variants).

Opening an executable costs a real-time antivirus scan, so the scanner opens as few as it can:
candidates are first ranked by name alone; then at most 4 (within 30 points of the best) get
their PE header and version resource read, and the ranking is redone. Installers found that
way move out and do not count. The manifest (`requiresAdmin`) is read only for the executable
an entry launches. Suites are decided on names alone.

Main executable score (`ExeRanker`): name match with the folder (file name, or 80 % of the
product name, or 60 % of the file description), +20 GUI, −10 console, +60 portable/launch
scripts, −10 other scripts, −30 per helper word (`plugin`, `cli`, `service`, `debug`, `proxy`...),
+10 x64, −40 ARM64, −10 per folder level, small bonus for version info and size.

## Data files

- `catalog.json`: `root`, `scannedAt`, `baselineAt`, `apps[]` with `id`, `kind`, `name`,
	`description`, `version`, `category`, `target`, `alternatives`, `readme`, `confidence`,
	`requiresAdmin`, `console`, `alternate`, `missing`, `firstSeen`, `renamedFrom` (old id of a
	renamed version folder, until the user's data has moved).
- `user.json`: `root`, `startupDelaySeconds`, `startupPromptSeconds`, `checkForUpdates`, `hotkey`,
	`language` (auto/es/en),
	`categoryDepth` (folder levels shown as categories in the launcher tree and the tray menu:
	0 = all, 1 = top level only; deeper apps belong to the last level shown),
	`scan{maxDepth, ignore, skipNames}`, `favorites[]`, `apps{id: {hidden, name, category,
	target, args, expand, autostart (no/always/ask), askDefault, elevatedTask}}`. Start with
	Windows is the HKCU Run value itself, not a setting.
- `user.json.bak`: the previous version of `user.json` (copied before AppTray saves; written
	from memory when a hand edit is noticed, so a broken edit leaves the last good one).
- `usage.json`: `apps{lowercase id: {count, last}}`, written by the tray after each launch.
- `update.json`: `checkedAt`, `latestVersion`, `url`, `notifiedVersion` (announced once).

## Dependencies

| Package | Why |
|---|---|
| nlohmann-json | JSON for the catalog and settings (ordered, keeps user's key order) |
| WIL | RAII wrappers for Win32 handles and COM |
| GoogleTest | Unit tests only |
