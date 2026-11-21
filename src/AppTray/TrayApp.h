#pragma once

#include "AppLauncher.h"
#include "AppList.h"
#include "Catalog.h"
#include "Hotkey.h"
#include "LauncherWindow.h"
#include "PathResolver.h"
#include "Shortcut.h"
#include "TrayIcon.h"
#include "UpdateCheck.h"
#include "UsageStats.h"
#include "UserSettings.h"

#include <windows.h>

#include <wil/resource.h>

#include <atomic>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace apptray
{
	struct AppPaths
	{
		std::filesystem::path exeDirectory;
		std::filesystem::path dataDirectory;
		std::optional<std::wstring> rootOverride; // --root
		std::wstring instanceName;                // title of the hidden tray window
	};

	// The running tray program: owns the catalog, the user's settings, the tray icon, the
	// launcher window and the background scan. Everything runs on the UI thread except the scan
	// and the icon extraction, which post their results back.
	class TrayApp
	{
	public:
		TrayApp(HINSTANCE instance, AppPaths paths);
		~TrayApp();
		TrayApp(const TrayApp&) = delete;
		TrayApp& operator=(const TrayApp&) = delete;

		// Message loop; returns the exit code. atLogon: started by Windows (--startup), so open the
		// logon apps after the configured delay.
		int Run(bool showLauncher, bool atLogon);

		// Used by the launcher window
		HINSTANCE Instance() const
		{
			return instance_;
		}
		const std::vector<AppItem>& Items() const
		{
			return items_;
		}
		const UserSettings& Settings() const
		{
			return settings_;
		}
		std::optional<std::filesystem::path> ResolvePath(const std::wstring& relative) const;
		std::optional<std::filesystem::path> ResolveTarget(const AppItem& item) const;
		std::wstring DescribeCatalog() const;
		bool IsScanning() const
		{
			return scanning_;
		}

		// The tray icon's menu. owner: the launcher window when its menu button opens it (the
		// launcher stays open and forwards WM_INITMENUPOPUP to FillCategoryMenu); nullptr from the
		// tray icon. exclude: the button, in screen coordinates, which the menu must not cover.
		void ShowTrayMenu(POINT point, HWND owner = nullptr, const RECT* exclude = nullptr);
		void FillCategoryMenu(HMENU menu);

		void LaunchItem(size_t index, bool asAdmin);
		void OpenItemFolder(size_t index);
		void OpenItemReadme(size_t index);
		void ToggleItemFavorite(size_t index);
		void SetItemHidden(size_t index, bool hidden);
		void SetItemTarget(size_t index, std::optional<std::wstring> target);
		void SetSuiteExpanded(const std::wstring& id, bool expanded);
		void SetItemAutostart(size_t index, AutostartMode mode);
		void SetItemElevated(size_t index, bool enabled);
		void CreateItemShortcut(size_t index, ShortcutPlace place);
		void StartRefresh();
		void OnLauncherClosed();

		// While a popup menu runs its modal loop, a scan that finishes must not replace the items
		// the menu was built from: its result waits until the scope ends (after the command ran)
		class MenuScope
		{
		public:
			explicit MenuScope(TrayApp& app);
			~MenuScope();
			MenuScope(const MenuScope&) = delete;
			MenuScope& operator=(const MenuScope&) = delete;

		private:
			TrayApp& app_;
		};
		[[nodiscard]] MenuScope EnterMenu()
		{
			return MenuScope(*this);
		}

	private:
		struct Notice
		{
			std::wstring title;
			std::wstring text;
			bool warning = false;
		};

		void LeaveMenu();
		bool CanChangeSettings();
		static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
		LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

		void LoadData();
		bool ReloadSettingsIfChanged();
		void SaveSettings();
		void SaveUsage();
		std::optional<Notice> ApplyRenamedApps();
		void RebuildItems();
		void UpdateTip();
		void ToggleLauncher();
		void ShowLauncher();
		void AppendAppMenuItem(HMENU menu, size_t index, UINT id);
		HBITMAP GetMenuBitmap(size_t index);
		void OpenSettingsFile();
		void OnScanDone(DWORD exitCode);
		void ReportError(std::wstring_view title, std::wstring_view text);
		bool StartChildProcess(const std::wstring& arguments, UINT doneMessage, std::jthread& waiter);
		LaunchResult StartItem(const AppItem& item, bool asAdmin, bool atLogon = false);
		void RunLogonApps();
		void ToggleStartWithWindows();
		void CheckForUpdates();
		void CheckForUpdatesNow();
		void OnUpdateCheckDone(DWORD exitCode);
		void ApplyUpdateState(bool announce);
		void OnElevatedTaskDone(DWORD exitCode);
		void OnShortcutDone(DWORD exitCode);
		void RegisterLauncherHotkey(bool warn);
		void ReportInfo(std::wstring_view title, std::wstring_view text);
		void ShowNotice(const Notice& notice);

		HINSTANCE instance_;
		AppPaths paths_;
		std::filesystem::path root_;
		bool rootIsRemote_ = false;
		std::filesystem::path catalogFile_;
		std::filesystem::path settingsFile_;
		std::filesystem::path settingsBackupFile_; // user.json.bak: the version before the last change
		std::filesystem::path usageFile_;
		std::filesystem::path exePath_; // this AppTray.exe, started with --scan for refreshes
		HWND window_ = nullptr;
		UINT taskbarCreatedMessage_ = 0;
		HICON trayIconHandle_ = nullptr;
		TrayIcon trayIcon_;
		LauncherWindow launcher_;

		Catalog catalog_;
		bool hasCatalog_ = false;
		UserSettings settings_;
		FILETIME settingsWriteTime_{};
		bool settingsBroken_ = false; // user.json did not parse: never overwrite it
		UsageStats usage_;
		std::vector<AppItem> items_;

		std::jthread scanThread_;
		std::atomic<bool> scanning_ = false;
		bool refreshAgain_ = false;   // asked while a scan was running (with newer settings)
		int menuDepth_ = 0;
		std::optional<DWORD> pendingScanExit_; // exit code of a scan that ended during a menu
		std::optional<DWORD> pendingTaskExit_; // same for the elevated --register-task copy
		bool exiting_ = false;

		std::jthread updateThread_;            // waits for "AppTray.exe --check-update"
		bool updateChecking_ = false;
		bool manualUpdateCheck_ = false;       // "Comprobar nueva versión": always report the answer
		ULONGLONG nextUpdateCheck_ = 0;        // GetTickCount64 from which a check is due
		std::optional<ReleaseInfo> newRelease_; // newer than this AppTray

		std::jthread elevationThread_;         // waits for the elevated --register-task copy
		std::wstring pendingTaskId_;
		bool pendingTaskEnable_ = false;

		std::jthread shortcutThread_;          // waits for "AppTray.exe --create-shortcut"
		std::optional<std::wstring> pendingShortcutName_;
		ShortcutPlace pendingShortcutPlace_ = ShortcutPlace::Desktop;

		std::optional<std::wstring> hotkeyTried_; // user.json "hotkey" last registered (or refused)
		std::wstring hotkeyLabel_;                // as the tray menu shows it; empty = none active
		std::optional<Hotkey> hotkeyWaiting_;     // refused (another program had it): retried quietly

		std::optional<Notice> startupNotice_; // raised before the tray icon exists, shown once it does

		// Tray menu state, valid while the menu is open
		std::unordered_map<HMENU, std::wstring> categoryMenus_;
		std::unordered_map<std::wstring, wil::unique_hbitmap> menuBitmaps_;
	};
}
