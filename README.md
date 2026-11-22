# AppTray

A tiny native tray launcher for a folder tree of portable Windows apps. It scans the tree,
works out which executable is the real program of each folder, and keeps a JSON catalog with
paths relative to the tree, so the whole folder can move to another drive. Click the tray icon
to search, browse by category, keep favorites and launch.

The interface is in Spanish when Windows is in Spanish and in English otherwise;
`"language": "es"` or `"en"` in `user.json` forces one (`"auto"` is the default).

> Status: **phase 5** — tray icon, launcher, tray menu, apps at logon, elevated apps without a
> UAC prompt, a global hotkey, recent apps, search by initials and keywords, settings that follow
> an app to its new version folder, shortcuts, app versions, running apps (switch to their
> window, close), command-line tools in a console and app folders on the PATH work.

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
	one next to "Open AppTray". If another program holds the key, AppTray says so (only in the
	log when started at logon, where a second AppTray copy would complain every time) and tries
	again each time you open the menu or the launcher.
- **Left click**: the launcher. Two search boxes, case and accents ignored:
	- **Folder search** (left, Ctrl+Shift+F) prunes the category tree to the folders whose path
		contains every word ("audio edit" finds Media › Audio › Editor), keeping the folders
		above each match open so its path shows, and picks the first match.
	- **App search** (right, Ctrl+F) looks inside the chosen folder and its subfolders (from
		Favorites, Recent, New or All it looks everywhere). Each app shows its folder path. A
		word also matches the start of the name's initials: "vsc" finds Visual Studio Code,
		"fsiv" FastStone Image Viewer. Right click › "Keywords…" gives an app more words to be
		found by ("retouch" for GIMP; `"keywords"` in `user.json`). Names or keywords starting
		with the first word come first, then names whose initials do, and within each group the
		apps you open most.
	- **Running apps** show "running" next to their folder and in the status line (same test
		as at logon, below). Enter switches to the app's window (restored if minimized) instead
		of starting a second copy; Shift+Enter, or right click › "Open another copy", starts
		another. Right click › "Close" asks its windows to close, as their X button does (the
		app may still ask to save). In a folder app the windows of its chosen executable come
		first; the folder's other programs only count when that one has none (a launcher's real
		program). An app with no window (only in the tray, or just a helper process) starts as
		usual, so a single-instance app shows itself. Switching counts as opening it for Recent
		and the ranking. The state is read when the launcher or the tray menu opens.
	- **Command-line programs** (console executables such as ffmpeg or yt-dlp, which would
		flash a window that closes at once) open inside a console that stays open, in their
		folder and with that folder first in `PATH`: you see their output and can type the next
		command. `"shell"` in `user.json` picks it: `"powershell"` (default: PowerShell 7 when
		installed, else Windows PowerShell) or `"cmd"`. With Windows Terminal as the default
		terminal it opens there. Its arguments (`"args"`) reach it as written. Scripts (`.bat`,
		`.cmd`) open as always, and logon apps start directly. A console program that opens a
		window of its own (Blender) would leave the console behind: right click › "Open in a
		console" turns this off (or on) for that app (`"console": false|true` in `user.json`).
		Right click › "Open a console here" opens one in any app's folder without running
		anything.
	- **PowerShell scripts** (`.ps1`): loose ones in the root are listed as their own entry,
		and one named like its folder as that folder's program (`WinUtil\winutil.ps1`). Other
		scripts below the root are taken for helpers (build, install, activate) and only show
		as another executable of their app: give a script of yours its own folder named like it.
		Opened directly, Windows would show them in Notepad, so AppTray runs them in a
		PowerShell console that stays open, in their folder, whatever `"shell"` says, also at
		logon. The execution policy is bypassed for that console only, so a script unzipped
		from a download runs too, unless a Group Policy sets the policy; a script that relaunches
		itself (elevated, say) must pass `-ExecutionPolicy Bypass` on to its new process. List
		only scripts you trust, as with any program. Their `"args"` are PowerShell syntax
		(`status -Name 'a b'`). "Open as administrator" runs the console elevated; "Open without
		UAC" is for executables only. A shortcut to a script opens that same console.
	- Right click › "Its folder on the PATH" adds the folder of the app's executable (a
		suite's folder) to your user `PATH` (`HKCU\Environment`), or takes it out; the check
		mark shows whether it is there, also when added by hand. Consoles opened from then on,
		and programs AppTray starts, find its programs by name. When a refresh recognizes a
		renamed version folder, `PATH` entries inside the old folder move to the new one.
	- **Recent** lists the last 15 apps opened from AppTray (launcher or tray menu; the logon
		apps do not count). The status line shows the selected app's description, version (from
		its executable, or from the folder name for scripts) and target.
	- Enter opens (from the folder side it moves to the apps; a running app: its window),
		Shift+Enter opens another copy, Ctrl+Enter opens as administrator, Ctrl+D toggles a
		favorite, Ctrl+E opens the folder, F5 rescans, Esc clears
		a search or closes, Tab moves between the boxes, the tree and the list. Typing in the tree
		goes to the folder search, typing in the list to the app search. The button at the top
		right (or F10) opens the tray icon's menu (Refresh, Edit settings, Exit...) without
		leaving the launcher. Right click an app for
		more: another executable of the same app, show a suite's programs separately, hide, help,
		a shortcut on the desktop or in the Start menu (`Programs\AppTray`, so Windows search
		finds the app; a shortcut of the same name that opens something else is kept and the new
		one is named "Name (2)"; it starts the program directly, so an app with "Open without UAC"
		shows its UAC prompt from there), what to do at logon, open without a UAC prompt, and the
		running-app, console, PATH and keywords commands above.
- **Right click**: favorites, the last 5 apps opened (other than favorites), "Running" (the apps
	open now: a click switches to them), categories as menus,
	Refresh, Edit settings, Start with Windows, Check for a new version, About (version, the
	hotkey, links to this repository and to the data folder), Exit, and "New version …
	available" when there is one.
- The launcher closes when it loses focus and is destroyed then: idle, AppTray keeps only the
	tray icon. Private memory is about 2.5 MB after start and settles around 5 MB once the
	launcher has been used (common controls and theme data stay loaded); no CPU, no timers, no
	folder watching.
- A second start of the same `AppTray.exe` does not open another icon: it shows the launcher
	of the running one (`--show`), or closes it (`--exit`).

### At logon

"Start with Windows" (tray menu) adds `"…\AppTray.exe" --startup` to
`HKCU\Software\Microsoft\Windows\CurrentVersion\Run` (value `AppTray`; another copy gets its own,
see below); no administrator needed. If the folder moves, the next start of AppTray fixes the
value: the one whose exe is missing and has the same last folders
(`Juegos\AppTray\AppTray.exe`), so a new drive letter is fine and another copy's value is never
taken. With `--startup`, AppTray waits
`startupDelaySeconds` (30) and then, for each app (right click › "At logon"):
- **Always open** — opened, unless it is already running.
- **Ask** — listed in a small dialog with a checkbox each and a countdown
	(`startupPromptSeconds`, 15): when it ends the checked ones open. Touching a checkbox stops
	the countdown. "Open" remembers the checks as next logon's defaults; "None" or closing
	opens nothing and changes nothing.
- **Don't open** (default).

"Already running" means a process whose executable is the app's (or, for folder apps such as
PortableApps launchers, any executable inside the app's folder), so FirefoxPortable is not
started twice when firefox.exe runs. A copy of the same program installed elsewhere does not
count. Apps that need administrator are skipped at logon (with a notification) unless they
have "Open without UAC".

### Another tree (games, a USB drive...)

One AppTray lists one tree. For another one, put a second copy of `AppTray.exe` in its own folder
inside that tree (`D:\Juegos\AppTray\AppTray.exe`, or `tools\Deploy-AppTray.ps1 -Destination
D:\Juegos\AppTray -Start`); it keeps its own `data\` (catalog, favorites, Recent), its own icon and
its own settings. Each copy is independent:
- Give it another hotkey in its `user.json` (`"hotkey": "Ctrl+Alt+Shift+Space"`): the default
	one belongs to the first copy, and the second says "used by another program" until you do.
- "Start with Windows" works per copy: the first registered copy uses the Run value `AppTray`,
	the others `AppTray (<folder of their tree>)`.
- The icon's tooltip names the tree ("AppTray · Juegos · 120 apps"), and About shows its path.
- Scan options (`"scan"` › `"ignore"`, `"skipNames"`) are per copy too: for games, skipping
	folders such as `_CommonRedist` keeps redistributables out.

A copy outside the tree can point at it with `"root"` in its `user.json`, but then the first
copy's scan would list that copy's folder as an app: inside the tree it skips itself.

### Open without UAC

"Open without UAC (elevated task)" creates a scheduled task in the `\AppTray` folder (named after
the app's folder path plus a short hash) that runs the app elevated, with no trigger: AppTray
starts it on demand, from the launcher or at logon, and Windows shows no UAC prompt. Creating
or deleting the task takes one UAC prompt (AppTray runs an elevated copy of itself with
`--register-task` / `--unregister-task`). It works on administrator accounts (the usual
Windows setup); for a standard user the task cannot elevate.

The task stores the executable and arguments of that moment. If they change (another
executable chosen, a rescan that picks a new one, `args` edited), AppTray does not run the
stale task: it opens the app normally and asks you to turn "Open without UAC" off and on. At
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
category) stays a new app plus a "not found" one.

### New versions of AppTray

A minute after it starts, and then at most once a day when you open the launcher or the tray
menu, AppTray asks GitHub for the latest release (a child `AppTray.exe --check-update`, one
HTTPS request, nothing else sent; offline, it tries again an hour later). A newer version shows a notification once and adds a
"New version … available" entry at the top of the tray menu, which opens the release page.
Nothing is downloaded or installed: replace `AppTray.exe` by hand (or with the deploy script).
`"checkForUpdates": false` in `user.json` turns it off. "Check for a new version" in the menu
asks right away (even with `checkForUpdates` off) and always answers: up to date, a newer
version, or GitHub unreachable.

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
	`"hotkey"` is the global shortcut (default `"Ctrl+Alt+Space"`, `""` = none). `"language"`:
	`"auto"` (Windows' language), `"es"` or `"en"`. `"shell"`: the console for command-line
	programs, `"powershell"` (default) or `"cmd"`.
	Per app, under `"apps"`: `"autostart": "always"|"ask"` (absent = no), `"askDefault"`
	(checkbox state in the logon dialog), `"elevatedTask"` (set by "Open without UAC"; do not
	set it by hand), `"args"`, `"target"`, `"hidden"`, `"name"`, `"category"`, `"expand"`,
	`"keywords"` (words separated by spaces; an array of words works too), `"console"`
	(`false`: a console program opens directly; `true`: any executable opens in a console).
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
- "Close" cannot reach an app that runs as administrator while AppTray does not (Windows drops
	the message); AppTray says so. Switching to a window needs the foreground, which AppTray has
	right after your click or key; if Windows refuses it, the app's taskbar button flashes.
- A `PATH` change reaches consoles opened afterwards. A terminal already open keeps its old
	environment (Windows Terminal reloads it for new tabs once it gets the change notice).
- On keyboards with AltGr (Spanish and most European layouts) AltGr is Ctrl+Alt: a hotkey such
	as Ctrl+Alt+E would take the € key from every program. Ctrl+Alt+Space, the default, is safe.
- Light/dark follows Windows' app mode. `APPTRAY_THEME=light` or `dark` forces one (testing).
	Dark context menus and list/tree colours use uxtheme features that Windows does not document
	(the same ones Notepad++ and Explorer++ use); without them AppTray falls back to light menus.

## License

[MIT](LICENSE)
