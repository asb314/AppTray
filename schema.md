# AppTray — structure

## Layout

```
AppTray.slnx                 solution (VS 2026)
Directory.Build.props        makes every project use the vcpkg bundled with Visual Studio
vcpkg.json                   dependencies, manifest mode (triplet x64-windows-static)
build/AppTray.Common.props   shared compiler/linker settings: C++20, /W4 /WX, static CRT, LTCG
src/AppTray.Core/            static library, no UI: everything testable
src/AppTray/                 the program (Windows subsystem): main.cpp, res/app.manifest
tests/AppTray.Core.Tests/    GoogleTest unit tests for AppTray.Core
```

## AppTray.Core

| File | Responsibility |
|---|---|
| `AppEntry` | One catalog entry (`AppKind`: folder, file, portableApps, installer, suite) |
| `FolderScanner` | Walks the tree and decides what is a category and what is an app |
| `ExeRanker` | Picks the main executable of an app folder (scoring) and spots installers by name |
| `ExeInspector` | Reads PE header, version resource and manifest without running the file |
| `NameTokens` | Tokenizes file/folder names and scores how well they match |
| `Catalog` | `catalog.json` load/save |
| `UserSettings` | `user.json` load/save (keeps unknown keys) |
| `CatalogMerger` | Fresh scan + previous catalog + user data → new catalog |
| `PathResolver` | Absolute ↔ root-relative paths (`/` separators); root resolution |
| `ScanReport` | Markdown report of a scan |
| `JsonFile` | UTF-8 JSON read, atomic tab-indented write |
| `CommandLine` | Argument parsing |
| `Log` | Small rotating UTF-8 log |
| `StringUtil` | UTF-8/UTF-16, case-insensitive comparisons, UTC timestamps |

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
3. Launchable files: `.exe .bat .cmd .jar .lnk`. Installers by whole words of the name
	(`setup`, `install`, `unins000`, `update`, `patch`, `keygen`, `crack`, `redist`, `*.paf.exe`,
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
	`description`, `category`, `target`, `alternatives`, `readme`, `confidence`,
	`requiresAdmin`, `console`, `alternate`, `missing`, `firstSeen`.
- `user.json`: `root`, `startWithWindows`, `startupDelaySeconds`, `startupPromptSeconds`,
	`scan{maxDepth, ignore, skipNames}`, `favorites[]`, `apps{id: {hidden, name, category,
	target, args, expand, autostart, askDefault, elevatedTask}}`.

## Dependencies

| Package | Why |
|---|---|
| nlohmann-json | JSON for the catalog and settings (ordered, keeps user's key order) |
| WIL | RAII wrappers for Win32 handles and COM |
| GoogleTest | Unit tests only |
