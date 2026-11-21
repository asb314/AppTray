#include "TrayApp.h"

#include "AppLauncher.h"
#include "CatalogMerger.h"
#include "CommandLine.h"
#include "ElevatedTask.h"
#include "Hotkey.h"
#include "IconLoader.h"
#include "JsonFile.h"
#include "Log.h"
#include "LogonRegistration.h"
#include "ProcessFinder.h"
#include "StartupPrompt.h"
#include "Version.h"
#include "Strings.h"
#include "StringUtil.h"
#include "Theme.h"
#include "UiCommon.h"
#include "resource.h"

#include <commctrl.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <format>

namespace fs = std::filesystem;

namespace apptray
{
	namespace
	{
		enum TrayCommand : UINT
		{
			OpenLauncherCommand = 1,
			RefreshCommand,
			SettingsCommand,
			StartWithWindowsCommand,
			NewVersionCommand,
			ExitCommand,
			AppCommandBase = 10000,
		};

		constexpr ULONGLONG ReopenGuardMilliseconds = 400;
		constexpr UINT_PTR LogonTimerId = 1;
		constexpr UINT_PTR UpdateTimerId = 2;
		constexpr UINT UpdateCheckDelayMilliseconds = 60 * 1000; // not while the logon apps start
		constexpr int LauncherHotkeyId = 1;
		constexpr size_t TrayRecentLimit = 5; // recent apps in the tray menu (not favorites)

		// Parsing JSON and scanning leave freed blocks committed in the heap; give them back so
		// the idle tray keeps a small footprint
		void ReleaseFreeMemory()
		{
			HeapCompact(GetProcessHeap(), 0);
			SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
		}

		FILETIME GetWriteTime(const fs::path& file)
		{
			WIN32_FILE_ATTRIBUTE_DATA data{};
			if (!GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &data))
			{
				return {};
			}
			return data.ftLastWriteTime;
		}

		std::wstring EscapeMenuText(const std::wstring& text)
		{
			std::wstring result;
			for (const wchar_t c : text)
			{
				result += c;
				if (c == L'&')
				{
					result += L'&';
				}
			}
			return result;
		}

		// "21/11 12:23" in local time for an ISO UTC timestamp
		std::wstring FormatLocalTime(const std::wstring& isoUtc)
		{
			const std::optional<std::int64_t> seconds = ParseUtcSeconds(isoUtc);
			if (!seconds)
			{
				return {};
			}
			ULARGE_INTEGER ticks{};
			ticks.QuadPart = static_cast<ULONGLONG>(*seconds) * 10'000'000ULL;
			const FILETIME utc{ticks.LowPart, ticks.HighPart};
			SYSTEMTIME utcTime{};
			SYSTEMTIME localTime{};
			if (!FileTimeToSystemTime(&utc, &utcTime) || !SystemTimeToTzSpecificLocalTime(nullptr, &utcTime, &localTime))
			{
				return {};
			}
			return std::format(L"{:02}/{:02} {:02}:{:02}", localTime.wDay, localTime.wMonth, localTime.wHour, localTime.wMinute);
		}

		fs::path GetModulePath()
		{
			std::wstring buffer(MAX_PATH, L'\0');
			for (;;)
			{
				const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
				if (length < buffer.size())
				{
					buffer.resize(length);
					return buffer;
				}
				buffer.resize(buffer.size() * 2);
			}
		}

		// Where to open the launcher: the tray icon when Windows reports it on the taskbar;
		// otherwise (icon in the hidden overflow area, or unknown) the tray end of the taskbar
		std::optional<RECT> ChooseLauncherAnchor(const std::optional<RECT>& iconRect)
		{
			APPBARDATA taskbar{};
			taskbar.cbSize = sizeof(taskbar);
			if (SHAppBarMessage(ABM_GETTASKBARPOS, &taskbar) == 0)
			{
				return iconRect;
			}
			RECT overlap{};
			if (iconRect && !IsRectEmpty(&*iconRect) && IntersectRect(&overlap, &*iconRect, &taskbar.rc))
			{
				return iconRect;
			}
			const RECT& bar = taskbar.rc;
			if (taskbar.uEdge == ABE_LEFT || taskbar.uEdge == ABE_RIGHT)
			{
				return RECT{bar.left, bar.bottom - 1, bar.right, bar.bottom};
			}
			return RECT{bar.right - 1, bar.top, bar.right, bar.bottom};
		}

		const CategoryNode* FindCategory(const CategoryNode& node, const std::wstring& path)
		{
			if (EqualsIgnoreCase(node.path, path))
			{
				return &node;
			}
			for (const CategoryNode& child : node.children)
			{
				if (EqualsIgnoreCase(path, child.path) || StartsWithIgnoreCase(path, child.path + L"/"))
				{
					return FindCategory(child, path);
				}
			}
			return nullptr;
		}
	}

	TrayApp::TrayApp(HINSTANCE instance, AppPaths paths)
		: instance_(instance), paths_(std::move(paths)), launcher_(*this)
	{
		catalogFile_ = paths_.dataDirectory / L"catalog.json";
		settingsFile_ = paths_.dataDirectory / L"user.json";
		settingsBackupFile_ = paths_.dataDirectory / L"user.json.bak";
		usageFile_ = paths_.dataDirectory / L"usage.json";
		exePath_ = GetModulePath();
		LoadData();
		ReleaseFreeMemory();
	}

	TrayApp::~TrayApp()
	{
		// The scan stops at the next folder; its result is dropped (the window is gone)
		scanThread_.request_stop();
		if (scanThread_.joinable())
		{
			scanThread_.join();
		}
		if (trayIconHandle_ != nullptr)
		{
			DestroyIcon(trayIconHandle_);
		}
	}

	void TrayApp::LoadData()
	{
		std::wstring error;
		std::error_code existsError;
		const bool hadSettings = fs::exists(settingsFile_, existsError);
		settings_ = LoadUserSettings(settingsFile_, error);
		settingsBroken_ = !error.empty();
		if (settingsBroken_)
		{
			WriteLog(L"user.json: " + error);
		}
		else if (!hadSettings)
		{
			SaveSettings(); // a real file to open with "Edit settings"
		}
		settingsWriteTime_ = GetWriteTime(settingsFile_);
		root_ = PathResolver::ResolveRoot(paths_.rootOverride, settings_.root, paths_.exeDirectory);
		rootIsRemote_ = GetDriveTypeW(root_.root_path().c_str()) == DRIVE_REMOTE;
		usage_ = LoadUsageStats(usageFile_);

		if (fs::exists(catalogFile_, existsError))
		{
			std::wstring catalogError;
			if (std::optional<Catalog> catalog = LoadCatalog(catalogFile_, catalogError))
			{
				catalog_ = std::move(*catalog);
				hasCatalog_ = true;
			}
			else
			{
				WriteLog(L"catalog.json: " + catalogError);
			}
		}
		if (std::optional<Notice> notice = ApplyRenamedApps()) // in case the last run could not
		{
			ShowNotice(*notice); // kept until the tray icon exists
		}
		RebuildItems();
	}

	void TrayApp::RebuildItems()
	{
		items_ = BuildAppItems(catalog_, settings_, FormatUtcNow(), &usage_);
	}

	void TrayApp::SaveUsage()
	{
		std::wstring error;
		if (!SaveUsageStats(usageFile_, usage_, error))
		{
			WriteLog(L"usage.json: " + error);
		}
	}

	// Apps the scan found in a renamed folder (a new version) take the user's data along: their
	// usage always, favorites and overrides once user.json can be written (until then the scans
	// keep renamedFrom). Returns the notice for the user; the caller shows it.
	std::optional<TrayApp::Notice> TrayApp::ApplyRenamedApps()
	{
		bool pending = false;
		bool usageMoved = false;
		for (const AppEntry& entry : catalog_.apps)
		{
			if (!entry.renamedFrom.empty() && !entry.missing)
			{
				pending = true;
				usageMoved = MoveUsage(usage_, entry.renamedFrom, entry.id) || usageMoved;
			}
		}
		if (usageMoved)
		{
			SaveUsage();
		}
		if (!pending)
		{
			return std::nullopt;
		}
		if (window_ != nullptr)
		{
			ReloadSettingsIfChanged(); // LoadData (no window yet) has just read it
		}
		if (settingsBroken_)
		{
			return std::nullopt;
		}
		const std::vector<RenamedApp> moved = MoveRenamedAppSettings(settings_, catalog_);
		if (moved.empty())
		{
			return std::nullopt;
		}
		SaveSettings();
		std::wstring names;
		std::wstring reenable;
		for (const RenamedApp& app : moved)
		{
			WriteLog(std::format(L"renamed: {} -> {}", app.from, app.to));
			names += (names.empty() ? L"" : L", ") + app.name;
			if (app.elevatedTaskDropped)
			{
				reenable += (reenable.empty() ? L"" : L", ") + app.name;
			}
		}
		std::wstring message = std::format(text::RenamedAppsText, names);
		if (!reenable.empty())
		{
			message += std::format(text::RenamedAppsReenable, reenable);
		}
		return Notice{text::RenamedAppsTitle, std::move(message), !reenable.empty()};
	}

	// A tray notification; before the icon exists (LoadData runs in the constructor) the last one
	// waits for Run
	void TrayApp::ShowNotice(const Notice& notice)
	{
		if (window_ == nullptr)
		{
			startupNotice_ = notice;
			return;
		}
		trayIcon_.ShowNotification(notice.title, notice.text, notice.warning);
	}

	bool TrayApp::ReloadSettingsIfChanged()
	{
		const FILETIME writeTime = GetWriteTime(settingsFile_);
		if (CompareFileTime(&writeTime, &settingsWriteTime_) == 0)
		{
			return false;
		}
		settingsWriteTime_ = writeTime;
		if (!settingsBroken_)
		{
			// What the file held before this edit (AppTray has it in memory): if the edit goes
			// wrong, user.json.bak is the last good version
			std::wstring backupError;
			if (!WriteJsonFile(settingsBackupFile_, SerializeUserSettings(settings_), backupError))
			{
				WriteLog(L"user.json.bak: " + backupError);
			}
		}
		std::wstring error;
		UserSettings loaded = LoadUserSettings(settingsFile_, error);
		if (!error.empty())
		{
			// Keep working with the last good settings, but never save over the user's file
			settingsBroken_ = true;
			ReportError(text::SettingsBrokenTitle, error);
			return false;
		}
		settingsBroken_ = false;
		settings_ = std::move(loaded);
		root_ = PathResolver::ResolveRoot(paths_.rootOverride, settings_.root, paths_.exeDirectory);
		rootIsRemote_ = GetDriveTypeW(root_.root_path().c_str()) == DRIVE_REMOTE;
		RebuildItems();
		UpdateTip();
		launcher_.RefreshItems();
		RegisterLauncherHotkey(true); // registers again only when "hotkey" changed
		return true;
	}

	bool TrayApp::CanChangeSettings()
	{
		ReloadSettingsIfChanged();
		if (settingsBroken_)
		{
			ReportError(text::SettingsBrokenTitle, text::SettingsFixFirst);
			return false;
		}
		return true;
	}

	void TrayApp::SaveSettings()
	{
		if (settingsBroken_)
		{
			return;
		}
		// The version being replaced stays as user.json.bak
		std::error_code existsError;
		if (fs::exists(settingsFile_, existsError)
			&& !CopyFileW(settingsFile_.c_str(), settingsBackupFile_.c_str(), FALSE))
		{
			WriteLog(std::format(L"user.json.bak: error {}", GetLastError()));
		}
		std::wstring error;
		if (!SaveUserSettings(settingsFile_, settings_, error))
		{
			ReportError(text::SettingsBrokenTitle, error);
		}
		settingsWriteTime_ = GetWriteTime(settingsFile_);
	}

	std::optional<fs::path> TrayApp::ResolvePath(const std::wstring& relative) const
	{
		return PathResolver(root_).ToAbsolute(relative);
	}

	std::optional<fs::path> TrayApp::ResolveTarget(const AppItem& item) const
	{
		// A suite has no target: its folder stands for it (icon, "open")
		return ResolvePath(item.target.empty() ? item.entry->id : item.target);
	}

	std::wstring TrayApp::DescribeCatalog() const
	{
		if (!hasCatalog_)
		{
			return text::Refreshing;
		}
		const size_t visible = CountAppItems(items_, AppFilterKind::All);
		std::wstring description = std::format(L"{} apps  ·  {}", visible, FormatLocalTime(catalog_.scannedAt));
		if (scanning_)
		{
			description += L"  ·  ";
			description += text::Refreshing;
		}
		return description;
	}

	void TrayApp::UpdateTip()
	{
		std::wstring tip = std::format(L"{} · {} apps", text::AppName, CountAppItems(items_, AppFilterKind::All));
		if (scanning_)
		{
			tip += L" · ";
			tip += text::Refreshing;
		}
		trayIcon_.SetTip(tip);
	}

	void TrayApp::ReportError(std::wstring_view title, std::wstring_view text)
	{
		WriteLog(std::format(L"{}: {}", title, text));
		if (launcher_.IsOpen())
		{
			launcher_.SetStatus(std::format(L"{}: {}", title, text));
		}
		else
		{
			ShowNotice({std::wstring(title), std::wstring(text), true});
		}
	}

	void TrayApp::ReportInfo(std::wstring_view title, std::wstring_view text)
	{
		WriteLog(std::format(L"{}: {}", title, text));
		if (launcher_.IsOpen())
		{
			launcher_.SetStatus(std::wstring(text));
		}
		else
		{
			ShowNotice({std::wstring(title), std::wstring(text), false});
		}
	}

	// The one way an app gets started: its elevated task when it has one (no UAC prompt),
	// otherwise the shell (a suite opens its folder). A missing exe skips the task, so the
	// shell reports "not found" instead of the task failing out of sight.
	LaunchResult TrayApp::StartItem(const AppItem& item, bool asAdmin, bool atLogon)
	{
		const std::optional<fs::path> path = ResolveTarget(item);
		if (!path)
		{
			return {false, false, ERROR_PATH_NOT_FOUND};
		}
		std::error_code missing;
		if (item.elevatedTask && !item.target.empty() && fs::is_regular_file(*path, missing))
		{
			std::wstring error;
			const ElevatedTaskRun run = RunElevatedTask(MakeElevatedTaskName(item.entry->id), *path, item.args, error);
			if (run == ElevatedTaskRun::Started)
			{
				WriteLog(L"started (elevated task) " + path->wstring());
				return {true, false, 0};
			}
			WriteLog(std::format(L"elevated task of {} not used: {}", item.name, error));
			if (run == ElevatedTaskRun::Stale)
			{
				ReportError(text::TaskFailed, std::format(L"{}: {}", item.name, text::ElevatedTaskStale));
			}
			if (atLogon)
			{
				// The task exists to avoid a UAC prompt at logon: never fall back to one
				if (run != ElevatedTaskRun::Stale)
				{
					ReportError(text::TaskFailed, std::format(L"{}: {}", item.name, error));
				}
				return {false, true, 0};
			}
		}
		const LaunchResult result = item.target.empty() ? OpenWithShell(*path) : LaunchProgram(*path, item.args, asAdmin);
		if (result.started)
		{
			WriteLog(std::format(L"started {}{}", path->wstring(), asAdmin ? L" (admin)" : L""));
		}
		return result;
	}

	void TrayApp::LaunchItem(size_t index, bool asAdmin)
	{
		if (index >= items_.size())
		{
			return;
		}
		const AppItem& item = items_[index];
		const LaunchResult result = StartItem(item, asAdmin);
		if (result.started)
		{
			// Recent list and search ranking. The item is updated in place: a menu may hold the items.
			const std::wstring now = FormatUtcNow();
			RecordLaunch(usage_, item.entry->id, now);
			items_[index].launchCount += 1;
			items_[index].lastLaunched = now;
			SaveUsage();
			launcher_.RequestClose(); // called from list-view notifications: never destroy it now
		}
		else if (!result.cancelled)
		{
			ReportError(text::LaunchFailed, std::format(L"{}: {}", item.name, DescribeLaunchError(result)));
		}
	}

	void TrayApp::OpenItemFolder(size_t index)
	{
		if (index >= items_.size())
		{
			return;
		}
		const AppItem& item = items_[index];
		const std::optional<fs::path> path = ResolveTarget(item);
		if (!path)
		{
			return;
		}
		std::error_code error;
		const LaunchResult result = fs::is_regular_file(*path, error) ? RevealInExplorer(*path) : OpenWithShell(*path);
		if (result.started)
		{
			launcher_.RequestClose();
		}
		else if (!result.cancelled)
		{
			ReportError(text::LaunchFailed, DescribeLaunchError(result));
		}
	}

	void TrayApp::OpenItemReadme(size_t index)
	{
		if (index >= items_.size() || items_[index].entry->readme.empty())
		{
			return;
		}
		if (const std::optional<fs::path> path = ResolvePath(items_[index].entry->readme))
		{
			const LaunchResult result = OpenWithShell(*path);
			if (result.started)
			{
				launcher_.RequestClose();
			}
			else if (!result.cancelled)
			{
				ReportError(text::LaunchFailed, DescribeLaunchError(result));
			}
		}
	}

	void TrayApp::ToggleItemFavorite(size_t index)
	{
		if (index >= items_.size())
		{
			return;
		}
		const std::wstring id = items_[index].entry->id;
		if (!CanChangeSettings())
		{
			return;
		}
		ToggleFavorite(settings_, id);
		SaveSettings();
		RebuildItems();
		launcher_.RefreshItems();
	}

	void TrayApp::SetItemHidden(size_t index, bool hidden)
	{
		if (index >= items_.size())
		{
			return;
		}
		const std::wstring id = items_[index].entry->id;
		if (!CanChangeSettings())
		{
			return;
		}
		SetHidden(settings_, id, hidden);
		SaveSettings();
		RebuildItems();
		UpdateTip();
		launcher_.RefreshItems();
	}

	void TrayApp::SetItemTarget(size_t index, std::optional<std::wstring> target)
	{
		if (index >= items_.size())
		{
			return;
		}
		const std::wstring id = items_[index].entry->id;
		if (!CanChangeSettings())
		{
			return;
		}
		SetTargetOverride(settings_, id, std::move(target));
		SaveSettings();
		RebuildItems();
		menuBitmaps_.clear();
		launcher_.RefreshItems();
		if (items_[index].elevatedTask)
		{
			// The task still points at the old exe
			ReportError(text::TaskFailed, std::format(L"{}: {}", items_[index].name, text::ElevatedTaskStale));
		}
	}

	void TrayApp::SetSuiteExpanded(const std::wstring& id, bool expanded)
	{
		if (!CanChangeSettings())
		{
			return;
		}
		SetExpanded(settings_, id, expanded);
		SaveSettings();
		StartRefresh(); // the scanner decides what an expanded suite turns into
	}

	void TrayApp::StartRefresh()
	{
		if (exiting_)
		{
			return;
		}
		if (scanning_)
		{
			// The running scan has older settings (a suite just expanded, say): scan again after it
			refreshAgain_ = true;
			return;
		}
		ReloadSettingsIfChanged();
		if (settingsBroken_)
		{
			// The scan reads user.json too (ignored folders, expanded suites): it must be valid
			ReportError(text::SettingsBrokenTitle, text::SettingsFixBeforeRefresh);
			return;
		}
		if (scanThread_.joinable())
		{
			scanThread_.join();
		}

		// The scan runs in a child process ("AppTray.exe --scan"): its threads, heap and loaded
		// files go away with it, so the tray stays small however big the tree is. Writing
		// catalog.json is atomic, so killing the child on exit never leaves a broken file.
		const std::wstring arguments = std::format(
			L"--scan --root {} --data {}", QuoteArgument(root_.wstring()), QuoteArgument(paths_.dataDirectory.wstring()));
		if (!StartChildProcess(arguments, WM_APP_SCAN_DONE, scanThread_))
		{
			ReportError(text::RefreshFailed, DescribeLaunchError({false, false, GetLastError()}));
			return;
		}
		scanning_ = true;
		UpdateTip();
		launcher_.SetStatus(text::Refreshing);
	}

	// Starts "AppTray.exe <arguments>" at below-normal priority and a thread that posts
	// doneMessage(exit code) when it ends. Exiting AppTray stops the thread and kills the child.
	bool TrayApp::StartChildProcess(const std::wstring& arguments, UINT doneMessage, std::jthread& waiter)
	{
		std::wstring commandLine = QuoteArgument(exePath_.wstring()) + L" " + arguments;
		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		PROCESS_INFORMATION process{};
		if (!CreateProcessW(
				exePath_.c_str(), commandLine.data(), nullptr, nullptr, FALSE, BELOW_NORMAL_PRIORITY_CLASS, nullptr,
				paths_.exeDirectory.c_str(), &startup, &process))
		{
			return false;
		}
		CloseHandle(process.hThread);
		wil::unique_handle child(process.hProcess);
		if (waiter.joinable())
		{
			waiter.join();
		}
		HWND window = window_;
		waiter = std::jthread(
			[window, doneMessage, child = std::move(child)](std::stop_token stop)
			{
				wil::unique_event stopped(CreateEventW(nullptr, TRUE, FALSE, nullptr));
				const std::stop_callback onStop(stop, [&] { SetEvent(stopped.get()); });
				const HANDLE waits[] = {child.get(), stopped.get()};
				if (WaitForMultipleObjects(2, waits, FALSE, INFINITE) != WAIT_OBJECT_0)
				{
					TerminateProcess(child.get(), 1); // AppTray is exiting
					WaitForSingleObject(child.get(), 5000);
					return;
				}
				DWORD exitCode = 1;
				GetExitCodeProcess(child.get(), &exitCode);
				PostMessageW(window, doneMessage, static_cast<WPARAM>(exitCode), 0);
			});
		return true;
	}

	void TrayApp::SetItemAutostart(size_t index, AutostartMode mode)
	{
		if (index >= items_.size())
		{
			return;
		}
		const std::wstring id = items_[index].entry->id;
		const bool wasAsk = items_[index].autostart == AutostartMode::Ask;
		if (!CanChangeSettings())
		{
			return;
		}
		SetAutostart(settings_, id, mode);
		if (mode == AutostartMode::Ask && !wasAsk)
		{
			SetAskDefault(settings_, id, true); // newly offered: checked until the user says otherwise
		}
		SaveSettings();
		RebuildItems();
		launcher_.RefreshItems();
		const AppItem& item = items_[index];
		if (mode != AutostartMode::No && item.entry->requiresAdmin && !item.elevatedTask)
		{
			ReportError(text::LogonTitle, std::format(text::NeedsElevatedTask, item.name));
		}
	}

	// Creating or deleting the task needs elevation: AppTray starts a copy of itself with "runas"
	// (one UAC prompt) and records the change only when that copy succeeds
	void TrayApp::SetItemElevated(size_t index, bool enabled)
	{
		if (index >= items_.size() || !pendingTaskId_.empty()) // one elevation at a time
		{
			return;
		}
		if (!CanChangeSettings())
		{
			return;
		}
		const std::wstring id = items_[index].entry->id;
		// The elevated copy may run as another account (over-the-shoulder UAC): it gets this user
		const std::wstring parameters = std::format(
			L"{} {} --task-user {} --root {} --data {}", enabled ? L"--register-task" : L"--unregister-task",
			QuoteArgument(id), QuoteArgument(GetCurrentUserName()), QuoteArgument(root_.wstring()),
			QuoteArgument(paths_.dataDirectory.wstring()));
		SHELLEXECUTEINFOW info{};
		info.cbSize = sizeof(info);
		info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
		info.hwnd = window_;
		info.lpVerb = L"runas";
		info.lpFile = exePath_.c_str();
		info.lpParameters = parameters.c_str();
		info.lpDirectory = paths_.exeDirectory.c_str();
		info.nShow = SW_HIDE;
		if (!ShellExecuteExW(&info) || info.hProcess == nullptr)
		{
			const DWORD error = GetLastError();
			if (error != ERROR_CANCELLED)
			{
				ReportError(text::TaskFailed, DescribeLaunchError({false, false, error}));
			}
			return;
		}
		pendingTaskId_ = id;
		pendingTaskEnable_ = enabled;
		if (elevationThread_.joinable())
		{
			elevationThread_.join();
		}
		HWND window = window_;
		elevationThread_ = std::jthread(
			[window, process = wil::unique_handle(info.hProcess)](std::stop_token stop)
			{
				// The elevated copy cannot be killed from here; on exit just stop waiting
				wil::unique_event stopped(CreateEventW(nullptr, TRUE, FALSE, nullptr));
				const std::stop_callback onStop(stop, [&] { SetEvent(stopped.get()); });
				const HANDLE waits[] = {process.get(), stopped.get()};
				if (WaitForMultipleObjects(2, waits, FALSE, INFINITE) != WAIT_OBJECT_0)
				{
					return;
				}
				DWORD exitCode = 1;
				GetExitCodeProcess(process.get(), &exitCode);
				PostMessageW(window, WM_APP_TASK_DONE, static_cast<WPARAM>(exitCode), 0);
			});
	}

	void TrayApp::OnElevatedTaskDone(DWORD exitCode)
	{
		if (menuDepth_ > 0)
		{
			pendingTaskExit_ = exitCode; // the open menu holds references to the items
			return;
		}
		const std::wstring id = std::exchange(pendingTaskId_, std::wstring{});
		if (exiting_ || id.empty())
		{
			return;
		}
		if (exitCode != 0)
		{
			ReportError(text::TaskFailed, L"detalles en AppTray.log");
			return;
		}
		if (!CanChangeSettings())
		{
			return;
		}
		SetElevatedTask(settings_, id, pendingTaskEnable_);
		SaveSettings();
		RebuildItems();
		launcher_.RefreshItems();
	}

	// The .lnk is written by a child "AppTray.exe --create-shortcut": IShellLink would leave the
	// shell namespace loaded in the tray
	void TrayApp::CreateItemShortcut(size_t index, ShortcutPlace place)
	{
		if (index >= items_.size())
		{
			return;
		}
		if (pendingShortcutName_) // one at a time
		{
			ReportInfo(text::CreateShortcut, text::ShortcutBusy);
			return;
		}
		const AppItem& item = items_[index];
		const std::wstring arguments = std::format(
			L"--create-shortcut {} --shortcut-in {} --root {} --data {}", QuoteArgument(item.entry->id), ToString(place),
			QuoteArgument(root_.wstring()), QuoteArgument(paths_.dataDirectory.wstring()));
		if (!StartChildProcess(arguments, WM_APP_SHORTCUT_DONE, shortcutThread_))
		{
			ReportError(text::ShortcutFailed, DescribeLaunchError({false, false, GetLastError()}));
			return;
		}
		pendingShortcutName_ = item.name;
		pendingShortcutPlace_ = place;
	}

	void TrayApp::OnShortcutDone(DWORD exitCode)
	{
		if (shortcutThread_.joinable())
		{
			shortcutThread_.join();
		}
		const std::optional<std::wstring> name = std::exchange(pendingShortcutName_, std::nullopt);
		if (exiting_ || !name)
		{
			return;
		}
		if (exitCode != 0)
		{
			ReportError(text::ShortcutFailed, L"detalles en AppTray.log");
		}
		else if (pendingShortcutPlace_ == ShortcutPlace::StartMenu)
		{
			ReportInfo(text::ShortcutCreatedTitle, std::format(text::ShortcutCreatedStartMenu, *name));
		}
		else
		{
			ReportInfo(text::ShortcutCreatedTitle, std::format(text::ShortcutCreatedDesktop, *name));
		}
	}

	// user.json "hotkey" (Ctrl+Alt+Space by default) opens and closes the launcher from any
	// program. A key another program holds is retried quietly each time the menu or the launcher
	// opens; warn = false (at logon, where a second AppTray would complain every time) only logs it.
	void TrayApp::RegisterLauncherHotkey(bool warn)
	{
		if (window_ == nullptr)
		{
			return;
		}
		if (hotkeyTried_ == settings_.hotkey)
		{
			if (hotkeyWaiting_
				&& RegisterHotKey(window_, LauncherHotkeyId, hotkeyWaiting_->modifiers | MOD_NOREPEAT, hotkeyWaiting_->key))
			{
				hotkeyLabel_ = FormatHotkey(*hotkeyWaiting_);
				hotkeyWaiting_.reset();
				WriteLog(L"hotkey registered: " + hotkeyLabel_);
			}
			return;
		}
		hotkeyTried_ = settings_.hotkey;
		hotkeyWaiting_.reset();
		if (!hotkeyLabel_.empty())
		{
			UnregisterHotKey(window_, LauncherHotkeyId);
			hotkeyLabel_.clear();
		}
		if (settings_.hotkey.empty())
		{
			return;
		}
		const std::optional<Hotkey> hotkey = ParseHotkey(settings_.hotkey);
		if (!hotkey)
		{
			ReportError(text::HotkeyTitle, std::format(text::HotkeyInvalid, settings_.hotkey));
			return;
		}
		if (!RegisterHotKey(window_, LauncherHotkeyId, hotkey->modifiers | MOD_NOREPEAT, hotkey->key))
		{
			hotkeyWaiting_ = hotkey;
			const std::wstring message = std::format(text::HotkeyTaken, FormatHotkey(*hotkey));
			if (warn)
			{
				ReportError(text::HotkeyTitle, message);
			}
			else
			{
				WriteLog(std::format(L"{}: {}", text::HotkeyTitle, message));
			}
			return;
		}
		hotkeyLabel_ = FormatHotkey(*hotkey);
	}

	// At logon (after the delay): open the "always" apps that are not running, then offer the
	// "ask" ones in the dialog. The same rules StartupApps.ps1 had, for any app in the catalog.
	void TrayApp::RunLogonApps()
	{
		ReloadSettingsIfChanged();
		const MenuScope scope = EnterMenu(); // items stay put through the dialog
		const std::vector<std::wstring> running = GetRunningImagePaths();
		const AutostartPlan plan = PlanAutostart(
			items_,
			[&](const AppItem& item)
			{
				// A folder app counts as running when anything from its folder runs (launchers)
				const bool isFolder = item.entry->kind == AppKind::Folder || item.entry->kind == AppKind::PortableApps;
				const std::optional<fs::path> path = ResolvePath(isFolder ? item.entry->id : item.target);
				return path && IsAppRunning(running, *path, isFolder);
			});
		WriteLog(std::format(L"logon: {} to open, {} to ask", plan.open.size(), plan.ask.size()));

		const auto start = [&](size_t index)
		{
			const AppItem& item = items_[index];
			// Without its elevated task an admin app would show a UAC prompt at every logon
			if (item.entry->requiresAdmin && !item.elevatedTask)
			{
				WriteLog(L"logon: skipped " + item.name + L" (needs \"Abrir sin UAC\")");
				trayIcon_.ShowNotification(text::LogonTitle, std::format(text::NeedsElevatedTask, item.name), true);
				return;
			}
			const LaunchResult result = StartItem(item, false, true);
			if (!result.started && !result.cancelled)
			{
				WriteLog(std::format(L"logon: {} failed: {}", item.name, DescribeLaunchError(result)));
			}
		};
		for (const size_t index : plan.open)
		{
			start(index);
		}
		if (plan.ask.empty())
		{
			return;
		}

		std::vector<StartupChoice> choices;
		for (const size_t index : plan.ask)
		{
			choices.push_back({index, items_[index].name, items_[index].askDefault});
		}
		const std::optional<std::vector<StartupChoice>> chosen =
			ShowStartupPrompt(instance_, std::move(choices), settings_.startupPromptSeconds);
		if (!chosen)
		{
			WriteLog(L"logon: \"Ninguna\" (saved choice unchanged)");
			return;
		}
		// The accepted choice becomes next logon's default
		if (CanChangeSettings())
		{
			for (const StartupChoice& choice : *chosen)
			{
				SetAskDefault(settings_, items_[choice.itemIndex].entry->id, choice.checked);
			}
			SaveSettings();
		}
		for (const StartupChoice& choice : *chosen)
		{
			if (choice.checked)
			{
				start(choice.itemIndex);
			}
		}
		RebuildItems();
		launcher_.RefreshItems();
	}

	void TrayApp::ToggleStartWithWindows()
	{
		std::wstring error;
		// Unchecked while another copy is registered: checking it registers this one instead
		if (!SetRegisteredAtLogon(!IsRegisteredAtLogon(exePath_), exePath_, error))
		{
			ReportError(text::StartWithWindows, error);
		}
	}

	// Once a day at most, a child "AppTray.exe --check-update" asks GitHub for the latest release
	void TrayApp::CheckForUpdates()
	{
		constexpr std::int64_t DaySeconds = 24 * 60 * 60;
		if (!settings_.checkForUpdates || updateChecking_ || exiting_ || GetTickCount64() < nextUpdateCheck_)
		{
			return;
		}
		nextUpdateCheck_ = GetTickCount64() + DaySeconds * 1000;
		// A check done in the last day (by this or an earlier run) is enough until it is a day old
		const std::optional<UpdateState> state = LoadUpdateState(paths_.dataDirectory / L"update.json");
		const std::optional<std::int64_t> checked = state ? ParseUtcSeconds(state->checkedAt) : std::nullopt;
		const std::optional<std::int64_t> now = ParseUtcSeconds(FormatUtcNow());
		if (checked && now && *now >= *checked && *now - *checked < DaySeconds)
		{
			nextUpdateCheck_ = GetTickCount64() + static_cast<ULONGLONG>(DaySeconds - (*now - *checked)) * 1000;
			ApplyUpdateState();
			return;
		}
		const std::wstring arguments = L"--check-update --data " + QuoteArgument(paths_.dataDirectory.wstring());
		updateChecking_ = StartChildProcess(arguments, WM_APP_UPDATE_DONE, updateThread_);
	}

	void TrayApp::OnUpdateCheckDone(DWORD exitCode)
	{
		updateChecking_ = false;
		if (updateThread_.joinable())
		{
			updateThread_.join();
		}
		if (!exiting_ && (exitCode == 0 || exitCode == static_cast<DWORD>(ExitCodeUpdateAvailable)))
		{
			ApplyUpdateState();
		}
		else
		{
			// Offline or GitHub unreachable: try again in an hour rather than tomorrow
			constexpr ULONGLONG Hour = 60ULL * 60 * 1000;
			nextUpdateCheck_ = GetTickCount64() + Hour;
		}
	}

	// Reads update.json: a newer version adds a menu entry, and is announced once
	void TrayApp::ApplyUpdateState()
	{
		const fs::path file = paths_.dataDirectory / L"update.json";
		std::optional<UpdateState> state = LoadUpdateState(file);
		if (!state || state->latestVersion.empty() || CompareVersions(state->latestVersion, APPTRAY_VERSION_WIDE) <= 0)
		{
			newRelease_.reset();
			return;
		}
		newRelease_ = ReleaseInfo{state->latestVersion, state->url};
		if (state->notifiedVersion != state->latestVersion)
		{
			trayIcon_.ShowNotification(
				text::NewVersionTitle, std::format(text::NewVersionText, state->latestVersion, APPTRAY_VERSION_WIDE), false);
			state->notifiedVersion = state->latestVersion;
			std::wstring error;
			SaveUpdateState(file, *state, error);
		}
	}

	TrayApp::MenuScope::MenuScope(TrayApp& app)
		: app_(app)
	{
		++app_.menuDepth_;
	}

	TrayApp::MenuScope::~MenuScope()
	{
		app_.LeaveMenu();
	}

	void TrayApp::LeaveMenu()
	{
		if (--menuDepth_ == 0 && pendingScanExit_)
		{
			const DWORD exitCode = *pendingScanExit_;
			pendingScanExit_.reset();
			OnScanDone(exitCode);
		}
		if (menuDepth_ == 0 && pendingTaskExit_)
		{
			const DWORD exitCode = *pendingTaskExit_;
			pendingTaskExit_.reset();
			OnElevatedTaskDone(exitCode);
		}
	}

	void TrayApp::OnScanDone(DWORD exitCode)
	{
		if (exiting_)
		{
			return;
		}
		if (menuDepth_ > 0)
		{
			pendingScanExit_ = exitCode; // applied when the menu is gone
			return;
		}
		scanning_ = false;
		if (scanThread_.joinable())
		{
			scanThread_.join();
		}
		launcher_.SetStatus({});
		if (exitCode == static_cast<DWORD>(ExitCodeRootMissing))
		{
			refreshAgain_ = false; // the folder is not there: scanning again would fail the same way
			ReportError(text::RootMissingTitle, root_.wstring());
			UpdateTip();
			return;
		}
		std::wstring error;
		std::optional<Catalog> loaded = exitCode == 0 ? LoadCatalog(catalogFile_, error) : std::nullopt;
		if (!loaded)
		{
			// A queued rescan (newer settings) still runs below; only a missing root drops it
			ReportError(
				text::RefreshFailed,
				error.empty() ? std::format(L"código {} (detalles en AppTray.log)", exitCode) : error);
			UpdateTip();
			if (refreshAgain_)
			{
				refreshAgain_ = false;
				StartRefresh();
			}
			return;
		}

		// New = first seen by this scan (regrouped suites and renamed folders keep their old date),
		// never on the first
		const bool hadCatalog = hasCatalog_;
		catalog_ = std::move(*loaded);
		hasCatalog_ = true;
		std::optional<Notice> notice = ApplyRenamedApps();
		const size_t newApps = !hadCatalog ? 0
										   : static_cast<size_t>(std::count_if(
												 catalog_.apps.begin(),
												 catalog_.apps.end(),
												 [&](const AppEntry& entry)
												 { return entry.firstSeen == catalog_.scannedAt && !entry.missing; }));
		menuBitmaps_.clear();
		RebuildItems();
		ReleaseFreeMemory();
		UpdateTip();
		launcher_.RefreshItems();
		WriteLog(std::format(L"refresh: {} apps, {} new", catalog_.apps.size(), newApps));
		// One notification: a second one would replace the first while it is still on screen
		if (newApps > 0)
		{
			const std::wstring newText = std::format(text::NewAppsText, newApps);
			if (notice)
			{
				notice->text += L". " + newText;
			}
			else
			{
				notice = Notice{text::NewAppsTitle, newText, false};
			}
		}
		if (notice)
		{
			ShowNotice(*notice);
		}
		if (refreshAgain_)
		{
			refreshAgain_ = false;
			StartRefresh();
		}
	}

	void TrayApp::OpenSettingsFile()
	{
		std::error_code existsError;
		if (!fs::exists(settingsFile_, existsError))
		{
			SaveSettings();
		}
		LaunchResult result = OpenWithShell(settingsFile_);
		if (!result.started && !result.cancelled)
		{
			// No program is associated with .json
			result = LaunchProgram(L"notepad.exe", QuoteArgument(settingsFile_.wstring()), false);
		}
		if (!result.started && !result.cancelled)
		{
			ReportError(text::LaunchFailed, DescribeLaunchError(result));
		}
	}

	void TrayApp::OnLauncherClosed()
	{
		// The window, its image list and its icon thread are gone: return their memory
		ReleaseFreeMemory();
	}

	void TrayApp::ToggleLauncher()
	{
		if (launcher_.IsOpen())
		{
			launcher_.Close();
			return;
		}
		// The click on the icon deactivates (and closes) an open flyout before it arrives here
		if (GetTickCount64() - launcher_.ClosedAt() < ReopenGuardMilliseconds)
		{
			return;
		}
		ShowLauncher();
	}

	void TrayApp::ShowLauncher()
	{
		ReloadSettingsIfChanged();
		RegisterLauncherHotkey(false); // retries a hotkey another program held
		CheckForUpdates(); // at most once a day
		launcher_.Show(ChooseLauncherAnchor(trayIcon_.GetRect()));
	}

	HBITMAP TrayApp::GetMenuBitmap(size_t index)
	{
		// Menus are built on the UI thread: reading icons from a share that hangs would freeze it
		if (rootIsRemote_)
		{
			return nullptr;
		}
		const std::optional<fs::path> path = ResolveTarget(items_[index]);
		if (!path)
		{
			return nullptr;
		}
		const int size = GetSystemMetricsForDpi(SM_CXSMICON, GetDpiForWindow(window_));
		const std::wstring key = std::format(L"{}|{}", path->wstring(), size);
		auto found = menuBitmaps_.find(key);
		if (found == menuBitmaps_.end())
		{
			wil::unique_hicon icon(ExtractAppIcon(*path, size));
			found = menuBitmaps_.emplace(key, wil::unique_hbitmap(CreateMenuBitmap(icon.get(), size))).first;
		}
		return found->second.get();
	}

	void TrayApp::AppendAppMenuItem(HMENU menu, size_t index, UINT id)
	{
		std::wstring label = EscapeMenuText(items_[index].name);
		MENUITEMINFOW item{};
		item.cbSize = sizeof(item);
		item.fMask = MIIM_ID | MIIM_STRING | MIIM_BITMAP;
		item.wID = id;
		item.dwTypeData = label.data();
		item.hbmpItem = GetMenuBitmap(index);
		InsertMenuItemW(menu, static_cast<UINT>(GetMenuItemCount(menu)), TRUE, &item);
	}

	void TrayApp::FillCategoryMenu(HMENU menu)
	{
		const auto found = categoryMenus_.find(menu);
		if (found == categoryMenus_.end() || GetMenuItemCount(menu) > 0)
		{
			return;
		}
		const std::wstring path = found->second;
		const CategoryNode tree = BuildCategoryTree(items_, settings_.categoryDepth);
		const CategoryNode* node = FindCategory(tree, path);
		if (node == nullptr)
		{
			AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, text::Empty);
			return;
		}
		for (const CategoryNode& child : node->children)
		{
			HMENU submenu = CreatePopupMenu();
			categoryMenus_.emplace(submenu, child.path);
			AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(submenu), EscapeMenuText(child.name).c_str());
		}
		// Apps of this category, and of deeper folders that have no submenu of their own (the
		// tree is cut at categoryDepth)
		AppFilter filter;
		filter.kind = AppFilterKind::Category;
		filter.category = path;
		for (const size_t index : FilterAppItems(items_, filter, settings_.favorites))
		{
			const std::wstring& category = items_[index].category;
			const bool inSubmenu = std::any_of(
				node->children.begin(), node->children.end(), [&](const CategoryNode& child)
				{ return EqualsIgnoreCase(category, child.path) || StartsWithIgnoreCase(category, child.path + L"/"); });
			if (!inSubmenu)
			{
				AppendAppMenuItem(menu, index, AppCommandBase + static_cast<UINT>(index));
			}
		}
		if (GetMenuItemCount(menu) == 0)
		{
			AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, text::Empty);
		}
	}

	void TrayApp::ShowTrayMenu(POINT point)
	{
		ReloadSettingsIfChanged();
		RegisterLauncherHotkey(false); // retries a hotkey another program held
		CheckForUpdates(); // at most once a day
		const MenuScope menuScope = EnterMenu(); // items stay put until the command has run
		categoryMenus_.clear();
		HMENU menu = CreatePopupMenu();
		if (newRelease_)
		{
			const std::wstring label = std::format(text::NewVersionMenu, newRelease_->version);
			AppendMenuW(menu, MF_STRING, NewVersionCommand, label.c_str());
			AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
		}
		const std::wstring openLabel =
			hotkeyLabel_.empty() ? std::wstring(text::OpenLauncher) : std::format(L"{}\t{}", text::OpenLauncher, hotkeyLabel_);
		AppendMenuW(menu, MF_STRING, OpenLauncherCommand, openLabel.c_str());
		SetMenuDefaultItem(menu, OpenLauncherCommand, FALSE);
		AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

		AppFilter favorites;
		favorites.kind = AppFilterKind::Favorites;
		const std::vector<size_t> favoriteItems = FilterAppItems(items_, favorites, settings_.favorites);
		if (favoriteItems.empty())
		{
			AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, text::NoFavorites);
		}
		for (const size_t index : favoriteItems)
		{
			AppendAppMenuItem(menu, index, AppCommandBase + static_cast<UINT>(index));
		}
		AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

		// The last apps opened that are not favorites already
		AppFilter recent;
		recent.kind = AppFilterKind::Recent;
		size_t recentShown = 0;
		for (const size_t index : FilterAppItems(items_, recent, settings_.favorites))
		{
			if (!items_[index].favorite && recentShown < TrayRecentLimit)
			{
				AppendAppMenuItem(menu, index, AppCommandBase + static_cast<UINT>(index));
				++recentShown;
			}
		}
		if (recentShown > 0)
		{
			AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
		}

		// Categories open lazily (WM_INITMENUPOPUP), so only the icons of what is seen are read
		for (const CategoryNode& category : BuildCategoryTree(items_, settings_.categoryDepth).children)
		{
			HMENU submenu = CreatePopupMenu();
			categoryMenus_.emplace(submenu, category.path);
			AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(submenu), EscapeMenuText(category.name).c_str());
		}
		AppFilter all;
		for (const size_t index : FilterAppItems(items_, all, settings_.favorites))
		{
			if (items_[index].category.empty())
			{
				AppendAppMenuItem(menu, index, AppCommandBase + static_cast<UINT>(index));
			}
		}
		AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
		AppendMenuW(menu, MF_STRING | (scanning_ ? MF_GRAYED : 0), RefreshCommand, scanning_ ? text::Refreshing : text::Refresh);
		AppendMenuW(menu, MF_STRING, SettingsCommand, text::EditSettings);
		AppendMenuW(menu, MF_STRING | (IsRegisteredAtLogon(exePath_) ? MF_CHECKED : 0), StartWithWindowsCommand, text::StartWithWindows);
		AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
		AppendMenuW(menu, MF_STRING, ExitCommand, text::Exit);

		// Without the foreground the menu would not close when clicking elsewhere
		SetForegroundWindow(window_);
		const UINT alignment = GetSystemMetrics(SM_MENUDROPALIGNMENT) != 0 ? TPM_RIGHTALIGN : TPM_LEFTALIGN;
		const UINT command = static_cast<UINT>(TrackPopupMenuEx(
			menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | alignment, point.x, point.y, window_, nullptr));
		PostMessageW(window_, WM_NULL, 0, 0);
		DestroyMenu(menu);
		categoryMenus_.clear();

		switch (command)
		{
		case 0:
			break;
		case OpenLauncherCommand:
			ShowLauncher();
			break;
		case RefreshCommand:
			StartRefresh();
			break;
		case StartWithWindowsCommand:
			ToggleStartWithWindows();
			break;
		case NewVersionCommand:
			if (newRelease_ && !newRelease_->url.empty())
			{
				OpenWithShell(newRelease_->url);
			}
			break;
		case SettingsCommand:
			OpenSettingsFile();
			break;
		case ExitCommand:
			DestroyWindow(window_);
			break;
		default:
			if (command >= AppCommandBase)
			{
				LaunchItem(command - AppCommandBase, false);
			}
			break;
		}
	}

	int TrayApp::Run(bool showLauncher, bool atLogon)
	{
		WNDCLASSEXW windowClass{};
		windowClass.cbSize = sizeof(windowClass);
		windowClass.lpfnWndProc = &TrayApp::WindowProc;
		windowClass.hInstance = instance_;
		windowClass.lpszClassName = TrayWindowClass;
		RegisterClassExW(&windowClass);

		// A hidden top-level window (not message-only: those miss "TaskbarCreated")
		CreateWindowExW(
			WS_EX_TOOLWINDOW, TrayWindowClass, paths_.instanceName.c_str(), WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance_, this);
		if (window_ == nullptr)
		{
			return 1;
		}
		taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");
		// If this copy runs elevated, a normal second start must still be able to reach it
		ChangeWindowMessageFilterEx(window_, WM_APP_SHOW, MSGFLT_ALLOW, nullptr);
		ChangeWindowMessageFilterEx(window_, WM_APP_EXIT, MSGFLT_ALLOW, nullptr);
		ChangeWindowMessageFilterEx(window_, taskbarCreatedMessage_, MSGFLT_ALLOW, nullptr);
		LoadIconMetric(instance_, MAKEINTRESOURCEW(IDI_APPTRAY), LIM_SMALL, &trayIconHandle_);
		Theme::ApplyMenuMode(Theme::IsSystemDark());
		trayIcon_.Add(window_, WM_APP_TRAY, trayIconHandle_, text::AppName, GetTrayIconGuid(paths_.instanceName));
		UpdateTip();
		if (startupNotice_)
		{
			ShowNotice(*std::exchange(startupNotice_, std::nullopt));
		}
		RegisterLauncherHotkey(!atLogon);
		RepairLogonRegistration(exePath_); // the folder may have moved since it was registered
		if (atLogon)
		{
			SetTimer(window_, LogonTimerId, static_cast<UINT>(std::max(0, settings_.startupDelaySeconds)) * 1000, nullptr);
		}
		SetTimer(window_, UpdateTimerId, UpdateCheckDelayMilliseconds, nullptr);
		WriteLog(std::format(L"started: root {}, {} apps", root_.wstring(), catalog_.apps.size()));

		if (!hasCatalog_)
		{
			StartRefresh();
		}
		if (showLauncher)
		{
			ShowLauncher();
		}

		MSG message{};
		while (GetMessageW(&message, nullptr, 0, 0) > 0)
		{
			if (!launcher_.PreTranslateMessage(message))
			{
				TranslateMessage(&message);
				DispatchMessageW(&message);
			}
		}
		return static_cast<int>(message.wParam);
	}

	LRESULT CALLBACK TrayApp::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
	{
		TrayApp* self = nullptr;
		if (message == WM_NCCREATE)
		{
			self = static_cast<TrayApp*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
			self->window_ = window;
			SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
		}
		else
		{
			self = reinterpret_cast<TrayApp*>(GetWindowLongPtrW(window, GWLP_USERDATA));
		}
		if (self != nullptr)
		{
			return self->HandleMessage(message, wParam, lParam);
		}
		return DefWindowProcW(window, message, wParam, lParam);
	}

	LRESULT TrayApp::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
	{
		if (message == taskbarCreatedMessage_ && taskbarCreatedMessage_ != 0)
		{
			// Explorer restarted: the icon must be added again
			trayIcon_.Add(window_, WM_APP_TRAY, trayIconHandle_, text::AppName, GetTrayIconGuid(paths_.instanceName));
			UpdateTip();
			return 0;
		}
		switch (message)
		{
		case WM_APP_TRAY:
			switch (LOWORD(lParam))
			{
			case NIN_SELECT:
			case NIN_KEYSELECT:
				ToggleLauncher();
				break;
			case WM_CONTEXTMENU:
				ShowTrayMenu({GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam)});
				break;
			default:
				break;
			}
			return 0;
		case WM_APP_SCAN_DONE:
			OnScanDone(static_cast<DWORD>(wParam));
			return 0;
		case WM_APP_UPDATE_DONE:
			OnUpdateCheckDone(static_cast<DWORD>(wParam));
			return 0;
		case WM_APP_TASK_DONE:
			OnElevatedTaskDone(static_cast<DWORD>(wParam));
			return 0;
		case WM_APP_SHORTCUT_DONE:
			OnShortcutDone(static_cast<DWORD>(wParam));
			return 0;
		case WM_HOTKEY:
			// Not while a menu or the logon dialog runs its modal loop
			if (wParam == LauncherHotkeyId && menuDepth_ == 0)
			{
				if (launcher_.IsOpen())
				{
					launcher_.Close();
				}
				else
				{
					ShowLauncher();
				}
			}
			return 0;
		case WM_TIMER:
			// One-shot timers: nothing ticks while AppTray sits idle
			KillTimer(window_, wParam);
			if (wParam == LogonTimerId && menuDepth_ > 0)
			{
				SetTimer(window_, LogonTimerId, 2000, nullptr); // a menu holds the items: after it
			}
			else if (wParam == LogonTimerId)
			{
				RunLogonApps();
			}
			else if (wParam == UpdateTimerId)
			{
				CheckForUpdates();
			}
			return 0;
		case WM_APP_SHOW:
			ShowLauncher();
			return 0;
		case WM_APP_EXIT:
			DestroyWindow(window_);
			return 0;
		case WM_INITMENUPOPUP:
			FillCategoryMenu(reinterpret_cast<HMENU>(wParam));
			return 0;
		case WM_SETTINGCHANGE:
			if (lParam != 0 && wcscmp(reinterpret_cast<const wchar_t*>(lParam), L"ImmersiveColorSet") == 0)
			{
				Theme::ApplyMenuMode(Theme::IsSystemDark());
				launcher_.RefreshTheme();
			}
			break;
		case WM_DESTROY:
			exiting_ = true;
			scanThread_.request_stop(); // the process ends now, not when a long scan finishes
			if (!hotkeyLabel_.empty())
			{
				UnregisterHotKey(window_, LauncherHotkeyId);
			}
			launcher_.Close();
			trayIcon_.Remove();
			WriteLog(L"exit");
			PostQuitMessage(0);
			return 0;
		default:
			break;
		}
		return DefWindowProcW(window_, message, wParam, lParam);
	}
}
