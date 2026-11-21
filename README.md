# AppTray

A tiny native tray launcher for a folder tree of portable Windows apps. It scans the tree,
works out which executable is the real program of each folder, and keeps a JSON catalog with
paths relative to the tree, so the whole folder can move to another drive. Click the tray icon
to search, browse by category, keep favorites and launch.

> Status: **phase 4** — tray icon, launcher, tray menu, apps at logon, elevated apps without a
> UAC prompt, a global hotkey, recent apps, search by initials, settings that follow an app to
> its new version folder, shortcuts and app versions work.

## Build

Requirements:
- Visual Studio 2026 with the "Desktop development with C++" workload (toolset v145,
	Windows SDK 10.0.26100 or later).
- vcpkg: nothing to install. `Directory.Build.props` points the build at the vcpkg bundled
	with Visual Studio (older standalone clones do not know the VS 2026 toolset). To use another
	vcpkg, set `APPTRAY_VCPKG` to its `scripts\buildsystems\msbuild` folder. Dependencies
	(nlohmann-json, WIL, GoogleTest) are restored on the first build into `vcpkg_installed\`;
	that first build needs internet and takes a couple of minutes.

Open `AppTray.slnx` and build, or from a Developer PowerShell:

```powershell
msbuild AppTray.slnx /p:Configuration=Release /p:Platform=x64 /m
x64\Release\AppTray.Core.Tests.exe
```

The output is a single `AppTray.exe` (about 1 MB, static CRT): no runtime to install.

To build, test and install in one go (closes a running copy first, keeps its `data\`):

```powershell
pwsh -File tools\Deploy-AppTray.ps1 -Destination D:\Portable\AppTray -Start
```

Releases: bump `src\AppTray.Core\Version.h`, commit and push, then

```powershell
pwsh -File tools\New-Release.ps1             # build, test, dist\AppTray-<version>-win-x64.zip + .sha256
pwsh -File tools\New-Release.ps1 -Publish    # same, plus tag v<version> and the GitHub release
```

`-Publish` takes the GitHub token from Git Credential Manager (the account that pushes).

## Use

Download the zip from [Releases](https://github.com/asb314/AppTray/releases) (or build it) and put
`AppTray.exe` in its own folder inside the tree it should list, e.g.
`D:\Portable\AppTray\AppTray.exe`; the tree is the parent folder. Start it: an icon appears in
the notification area (on Windows 11 it may start in the hidden overflow; drag it to the taskbar).

- **Ctrl+Alt+Space**, from any program: opens or closes the launcher. `"hotkey"` in `user.json`
	changes it (`"Win+Shift+A"`, `"Ctrl+F12"`, Spanish names such as `"Mayús"` and `"Espacio"`
	work too) or turns it off (`""`); it needs Ctrl, Alt or Win. The tray menu shows the active
	one next to "Abrir AppTray". If another program holds the key, AppTray says so (only in the
	log when started at logon, where a second AppTray copy would complain every time) and tries
	again each time you open the menu or the launcher.
- **Left click**: the launcher. Two search boxes, case and accents ignored:
	- **Folder search** (left, Ctrl+Shift+F) prunes the category tree to the folders whose path
		contains every word ("audio edit" finds Media › Audio › Editor), keeping the folders
		above each match open so its path shows, and picks the first match.
	- **App search** (right, Ctrl+F) looks inside the chosen folder and its subfolders (from
		Favorites, Recent, New or All it looks everywhere). Each app shows its folder path. A
		word also matches the start of the name's initials: "vsc" finds Visual Studio Code,
		"fsiv" FastStone Image Viewer. Names (or initials) starting with the first word come
		first, and among them the apps you open most.
	- **Recent** lists the last 15 apps opened from AppTray (launcher or tray menu; the logon
		apps do not count). The status line shows the selected app's description, version (from
		its executable, or from the folder name for scripts) and target.
	- Enter opens (from the folder side it moves to the apps), Ctrl+Enter opens as
		administrator, Ctrl+D toggles a favorite, Ctrl+E opens the folder, F5 rescans, Esc clears
		a search or closes, Tab moves between the boxes, the tree and the list. Typing in the tree
		goes to the folder search, typing in the list to the app search. Right click an app for
		more: another executable of the same app, show a suite's programs separately, hide, help,
		a shortcut on the desktop or in the Start menu (`Programs\AppTray`, so Windows search
		finds the app; a shortcut of the same name that opens something else is kept and the new
		one is named "Name (2)"; it starts the program directly, so an app with "Open without UAC"
		shows its UAC prompt from there), what to do at logon, open without a UAC prompt.
- **Right click**: favorites, the last 5 apps opened (other than favorites), categories as menus,
	Refresh, Edit settings, Start with Windows, Exit, and "New version … available" when there
	is one.
- The launcher closes when it loses focus and is destroyed then: idle, AppTray keeps only the
	tray icon. Private memory is about 2.5 MB after start and settles around 5 MB once the
	launcher has been used (common controls and theme data stay loaded); no CPU, no timers, no
	folder watching.
- A second start of the same `AppTray.exe` does not open another icon: it shows the launcher
	of the running one (`--show`), or closes it (`--exit`).

### At logon

"Start with Windows" (tray menu) adds `"…\AppTray.exe" --startup` to
`HKCU\Software\Microsoft\Windows\CurrentVersion\Run`; no administrator needed. If the folder
moves, the next start of AppTray fixes the value. With `--startup`, AppTray waits
`startupDelaySeconds` (30) and then, for each app (right click › "Al iniciar sesión"):
- **Abrir siempre** — opened, unless it is already running.
- **Preguntar** — listed in a small dialog with a checkbox each and a countdown
	(`startupPromptSeconds`, 15): when it ends the checked ones open. Touching a checkbox stops
	the countdown. "Abrir" remembers the checks as next logon's defaults; "Ninguna" or closing
	opens nothing and changes nothing.
- **No abrir** (default).

"Already running" means a process whose executable is the app's (or, for folder apps such as
PortableApps launchers, any executable inside the app's folder), so FirefoxPortable is not
started twice when firefox.exe runs. A copy of the same program installed elsewhere does not
count. Apps that need administrator are skipped at logon (with a notification) unless they
have "Open without UAC".

### Open without UAC

"Abrir sin UAC (tarea elevada)" creates a scheduled task in the `\AppTray` folder (named after
the app's folder path plus a short hash) that runs the app elevated, with no trigger: AppTray
starts it on demand, from the launcher or at logon, and Windows shows no UAC prompt. Creating
or deleting the task takes one UAC prompt (AppTray runs an elevated copy of itself with
`--register-task` / `--unregister-task`). It works on administrator accounts (the usual
Windows setup); for a standard user the task cannot elevate.

The task stores the executable and arguments of that moment. If they change (another
executable chosen, a rescan that picks a new one, `args` edited), AppTray does not run the
stale task: it opens the app normally and asks you to turn "Abrir sin UAC" off and on. At
logon it opens nothing rather than show a UAC prompt.

Security: anyone who can write to the app's folder can replace its executable and get it run
elevated without a prompt. That is the same trade-off as simplewall's own "Skip UAC" task;
use it only for apps in folders that only you can write to.

### A new version of an app

Many app folders carry their version in the name (`QGIS 4.0.0 light`). When a refresh finds that
folder gone and a new one in the same category whose name only differs in the numbers
(`QGIS 4.1.0 light`), and there is exactly one of each, it is the same app: it is not listed as
new, and your favorite (same place in the list), hidden flag, logon choice, arguments, name and
category move to it, with a notification. Another executable you chose inside the old folder
follows to the same file in the new one if it exists there. "Open without UAC" is turned off,
because the task runs the old executable: turn it on again. Every time AppTray creates or
deletes a task (elevated anyway), it also deletes the `\AppTray` tasks whose program was in the
tree and is gone, so the old one goes then. Anything less clear-cut (two new folders, another
category) stays a new app plus a "no encontrada" one.

### New versions of AppTray

A minute after it starts, and then at most once a day when you open the launcher or the tray
menu, AppTray asks GitHub for the latest release (a child `AppTray.exe --check-update`, one
HTTPS request, nothing else sent; offline, it tries again an hour later). A newer version shows a notification once and adds a
"New version … available" entry at the top of the tray menu, which opens the release page.
Nothing is downloaded or installed: replace `AppTray.exe` by hand (or with the deploy script).
`"checkForUpdates": false` in `user.json` turns it off.

Command line:

```powershell
AppTray.exe                              # tray icon
AppTray.exe --show                       # tray icon and open the launcher
AppTray.exe --exit                       # close the running AppTray
AppTray.exe --scan                       # scan and write data\catalog.json, then exit
AppTray.exe --scan --report report.md    # same, plus a Markdown report to review the result
AppTray.exe --scan --root D:\Portable --data D:\Temp\apptray   # other tree / data folder
AppTray.exe --scan --verbose             # log every folder and executable touched (diagnostics)
AppTray.exe --startup                    # tray icon, then the logon apps (used by the Run value)
AppTray.exe --check-update               # ask GitHub for the latest release, write data\update.json
AppTray.exe --register-task <id>         # create the elevated task of an app (run elevated)
AppTray.exe --unregister-task <id>       # delete it (run elevated)
AppTray.exe --create-shortcut <id> [--shortcut-in desktop|startmenu]   # a .lnk to the app
```

A scan of ~400 folders / 260 apps takes about 0.1 s once the antivirus has seen the files. A
cold scan (first run, or after the antivirus forgot them) can take much longer, because every
executable AppTray opens gets scanned by real-time protection: AppTray opens as few as it can
and scans the top-level folders in parallel. The tray app refreshes by running
`AppTray.exe --scan` as a child process, so scanning never grows the tray's memory; the catalog
is only rebuilt when you ask (Refresh, F5). Shortcuts are written by a child
`AppTray.exe --create-shortcut` too (the shell's link object would stay loaded in the tray).
Exit codes: 0 ok, 1 failed, 2 bad arguments, 3 root not found (`--scan`), 10 a newer version
exists (`--check-update`).

`data\` (next to the exe) holds:
- `catalog.json` — generated by every scan; do not edit.
- `user.json` — your choices (favorites, hidden apps, another executable, arguments,
	autostart, scan options). Created with defaults on first use; edit freely ("Edit settings"
	in the tray menu), AppTray reloads it when it changes and keeps unknown keys.
	`"categoryDepth"` sets how many folder levels become categories: `0` (default) the whole
	tree, `1` only the top-level folders (each holding all its apps), `2` one sublevel...
	`"hotkey"` is the global shortcut (default `"Ctrl+Alt+Space"`, `""` = none).
	Per app, under `"apps"`: `"autostart": "always"|"ask"` (absent = no), `"askDefault"`
	(checkbox state in the logon dialog), `"elevatedTask"` (set by "Open without UAC"; do not
	set it by hand), `"args"`, `"target"`, `"hidden"`, `"name"`, `"category"`, `"expand"`.
- `user.json.bak` — the previous version of `user.json`, written before each change (by AppTray,
	or by hand: then from what AppTray had loaded). If an edit goes wrong, copy it back.
- `usage.json` — how many times and when each app was opened from AppTray (Recent, ranking).
- `update.json` — last update check (time, latest version, release page).
- `AppTray.log`.

## Gotchas

- `AppTray.exe` is a GUI program: PowerShell does not wait for it. Use
	`Start-Process -Wait AppTray.exe --scan` or pipe it (`AppTray.exe --scan | Out-Null`) when
	the order matters.
- Folders whose name contains `crack`, `keygen`, `keymaker`, `patch` or `activator` are never
	listed (see the report's "Skipped folders").
- `requiresAdmin` comes from the executable's manifest. Programs that elevate themselves at run
	time (simplewall, for one) declare `asInvoker` and are not flagged: turn on "Open without
	UAC" for them or they will show their own UAC prompt at logon.
- The tray icon is registered with a GUID derived from the exe's folder, so Windows keeps its
	place in the taskbar across restarts. If Windows refuses the GUID (a stale registration of
	an exe that moved), AppTray falls back to a plain icon id.
- On keyboards with AltGr (Spanish and most European layouts) AltGr is Ctrl+Alt: a hotkey such
	as Ctrl+Alt+E would take the € key from every program. Ctrl+Alt+Space, the default, is safe.
- Light/dark follows Windows' app mode. `APPTRAY_THEME=light` or `dark` forces one (testing).
	Dark context menus and list/tree colours use uxtheme features that Windows does not document
	(the same ones Notepad++ and Explorer++ use); without them AppTray falls back to light menus.

## License

[MIT](LICENSE)
