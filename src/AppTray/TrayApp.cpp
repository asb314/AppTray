#include "TrayApp.h"

#include "AppLauncher.h"
#include "CommandLine.h"
#include "IconLoader.h"
#include "Log.h"
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
			ExitCommand,
			AppCommandBase = 10000,
		};

		constexpr ULONGLONG ReopenGuardMilliseconds = 400;

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
		RebuildItems();
	}

	void TrayApp::RebuildItems()
	{
		items_ = BuildAppItems(catalog_, settings_, FormatUtcNow());
	}

	bool TrayApp::ReloadSettingsIfChanged()
	{
		const FILETIME writeTime = GetWriteTime(settingsFile_);
		if (CompareFileTime(&writeTime, &settingsWriteTime_) == 0)
		{
			return false;
		}
		settingsWriteTime_ = writeTime;
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
			trayIcon_.ShowNotification(title, text, true);
		}
	}

	void TrayApp::LaunchItem(size_t index, bool asAdmin)
	{
		if (index >= items_.size())
		{
			return;
		}
		const AppItem& item = items_[index];
		const std::optional<fs::path> path = ResolveTarget(item);
		if (!path)
		{
			ReportError(text::LaunchFailed, item.name);
			return;
		}
		const LaunchResult result = item.target.empty() ? OpenWithShell(*path) : LaunchProgram(*path, item.args, asAdmin);
		if (result.started)
		{
			WriteLog(std::format(L"launched {}{}", path->wstring(), asAdmin ? L" (admin)" : L""));
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
		std::wstring commandLine = std::format(
			L"{} --scan --root {} --data {}", QuoteArgument(exePath_.wstring()), QuoteArgument(root_.wstring()),
			QuoteArgument(paths_.dataDirectory.wstring()));
		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		PROCESS_INFORMATION process{};
		if (!CreateProcessW(
				exePath_.c_str(), commandLine.data(), nullptr, nullptr, FALSE, BELOW_NORMAL_PRIORITY_CLASS, nullptr,
				paths_.exeDirectory.c_str(), &startup, &process))
		{
			ReportError(text::RefreshFailed, DescribeLaunchError({false, false, GetLastError()}));
			return;
		}
		CloseHandle(process.hThread);
		wil::unique_handle child(process.hProcess);
		scanning_ = true;
		UpdateTip();
		launcher_.SetStatus(text::Refreshing);

		HWND window = window_;
		scanThread_ = std::jthread(
			[window, child = std::move(child)](std::stop_token stop)
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
				PostMessageW(window, WM_APP_SCAN_DONE, static_cast<WPARAM>(exitCode), 0);
			});
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

		// New = first seen by this scan (regrouped suites keep their old date), never on the first
		const bool hadCatalog = hasCatalog_;
		catalog_ = std::move(*loaded);
		hasCatalog_ = true;
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
		if (newApps > 0)
		{
			trayIcon_.ShowNotification(
				text::NewAppsTitle, std::format(L"{} apps nuevas desde el último refresco", newApps), false);
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
		const CategoryNode tree = BuildCategoryTree(items_);
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
		AppFilter filter;
		filter.kind = AppFilterKind::Category;
		filter.category = path;
		for (const size_t index : FilterAppItems(items_, filter, settings_.favorites))
		{
			if (CompareStringOrdinal(items_[index].category.c_str(), -1, path.c_str(), -1, TRUE) == CSTR_EQUAL)
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
		const MenuScope menuScope = EnterMenu(); // items stay put until the command has run
		categoryMenus_.clear();
		HMENU menu = CreatePopupMenu();
		AppendMenuW(menu, MF_STRING, OpenLauncherCommand, text::OpenLauncher);
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

		// Categories open lazily (WM_INITMENUPOPUP), so only the icons of what is seen are read
		for (const CategoryNode& category : BuildCategoryTree(items_).children)
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

	int TrayApp::Run(bool showLauncher)
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
		trayIcon_.Add(window_, WM_APP_TRAY, trayIconHandle_, text::AppName);
		UpdateTip();
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
			trayIcon_.Add(window_, WM_APP_TRAY, trayIconHandle_, text::AppName);
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
