#pragma once

#include "AppList.h"
#include "IconLoader.h"
#include "Theme.h"

#include <windows.h>

#include <commctrl.h>

#include <wil/resource.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace apptray
{
	class TrayApp;

	// The flyout next to the tray icon: search box, category tree and app list. It is created
	// when shown and destroyed when it loses focus, so it costs nothing while closed.
	class LauncherWindow
	{
	public:
		explicit LauncherWindow(TrayApp& app);
		~LauncherWindow();
		LauncherWindow(const LauncherWindow&) = delete;
		LauncherWindow& operator=(const LauncherWindow&) = delete;

		void Show(const std::optional<RECT>& anchor);
		void Close();
		// Closes once the current message is done: safe from inside a control's notification
		void RequestClose();
		bool IsOpen() const
		{
			return window_ != nullptr;
		}
		ULONGLONG ClosedAt() const
		{
			return closedAt_;
		}

		// Keyboard shortcuts for the window and its children; true when handled
		bool PreTranslateMessage(const MSG& message);

		// The app items or the user's settings changed
		void RefreshItems();
		void RefreshTheme();

		// Shows a message in the status line until the selection changes
		void SetStatus(std::wstring text);

	private:
		static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
		LRESULT HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

		int Scale(int value) const;
		POINT ComputePosition(const RECT& anchor, int width, int height) const;
		void CreateChildren();
		void ApplyTheme();
		void ApplyFonts();
		void ResetImages();
		void Layout();
		void Paint();
		void PopulateTree();
		void AddCategoryNodes(HTREEITEM parent, const CategoryNode& node);
		HTREEITEM AddTreeNode(HTREEITEM parent, const std::wstring& text, AppFilter filter);
		void ApplyFilter(std::optional<std::wstring> keepSelectedId = std::nullopt);
		void UpdateStatus();
		std::optional<size_t> GetSelectedItem() const;
		std::optional<int> GetSelectedRow() const;
		void SelectRow(int row);
		void MoveSelection(int delta);
		void LaunchSelected(bool asAdmin);
		void ShowContextMenu(std::optional<POINT> screenPoint);
		int GetImageIndex(size_t itemIndex);
		void OnIconReady(std::unique_ptr<IconResult> result);
		LRESULT OnNotify(const NMHDR* header);
		LRESULT OnListCustomDraw(NMLVCUSTOMDRAW* draw) const;
		void OnGetDisplayInfo(NMLVDISPINFOW* info);
		void FocusSearch();
		void CycleFocus(bool backwards);

		TrayApp& app_;
		HWND window_ = nullptr;
		HWND folderSearch_ = nullptr; // filters the category tree
		HWND search_ = nullptr;       // filters the apps
		HWND tree_ = nullptr;
		HWND list_ = nullptr;
		HWND status_ = nullptr;
		RECT folderFrame_{};
		RECT searchFrame_{};
		UINT dpi_ = USER_DEFAULT_SCREEN_DPI;
		Theme theme_;
		wil::unique_hfont font_;
		wil::unique_hfont searchFont_;
		HIMAGELIST images_ = nullptr;
		int iconSize_ = 16;
		std::vector<int> imageOf_; // per item: -1 not requested, -2 pending, else image index
		std::uint64_t generation_ = 0;
		std::unique_ptr<IconWorker> iconWorker_;
		std::vector<AppFilter> treeFilters_; // lParam of the tree nodes
		std::vector<std::wstring> expandedCategories_; // open branches, kept while AppTray runs
		bool populatingTree_ = false;
		AppFilter filter_;
		std::wstring folderFilter_;        // text of the folder search box, trimmed
		bool folderFilterChanged_ = false; // the text changed: pick the first matching folder
		bool noFolderMatch_ = false;
		std::vector<size_t> visible_;
		std::wstring selectedId_; // survives a rebuild of the items (indices do not)
		std::wstring statusOverride_;
		bool wasActive_ = false;
		ULONGLONG closedAt_ = 0;
	};
}
