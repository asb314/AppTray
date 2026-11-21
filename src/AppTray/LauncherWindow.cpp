#include "LauncherWindow.h"

#include "PathResolver.h"
#include "StringUtil.h"
#include "Strings.h"
#include "TrayApp.h"
#include "UiCommon.h"

#include <windowsx.h>

#include <algorithm>
#include <format>

#pragma comment(lib, "comctl32.lib")

namespace apptray
{
	namespace
	{
		// Layout at 96 DPI
		constexpr int BaseWidth = 720;
		constexpr int BaseHeight = 480;
		constexpr int Padding = 12;
		constexpr int SearchHeight = 36;
		constexpr int TreeWidth = 200;
		constexpr int StatusHeight = 24;
		constexpr int Gap = 8;
		constexpr int NameColumnPercent = 55;
		constexpr wchar_t MenuGlyph[] = L"\xE700"; // GlobalNavigationButton (three lines)

		constexpr int SearchId = 101;
		constexpr int FolderSearchId = 105;
		constexpr int TreeId = 102;
		constexpr int ListId = 103;
		constexpr int StatusId = 104;

		enum MenuCommand : UINT
		{
			OpenCommand = 1,
			OpenAsAdminCommand,
			OpenFolderCommand,
			OpenHelpCommand,
			FavoriteCommand,
			HideCommand,
			UnhideCommand,
			ExpandCommand,
			CollapseCommand,
			ResetTargetCommand,
			AutostartNoCommand,
			AutostartAlwaysCommand,
			AutostartAskCommand,
			ElevatedCommand,
			ShortcutDesktopCommand,
			ShortcutStartMenuCommand,
			AlternativeCommandBase = 100,
		};

		std::wstring FormatCategory(const std::wstring& category)
		{
			if (category.empty())
			{
				return text::RootCategory.Get();
			}
			std::wstring result;
			for (const wchar_t c : category)
			{
				if (c == L'/')
				{
					result += L" › ";
				}
				else
				{
					result += c;
				}
			}
			return result;
		}

		// Menus treat '&' as an accelerator marker
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

		bool IsSameFilter(const AppFilter& a, const AppFilter& b)
		{
			return a.kind == b.kind && (a.kind != AppFilterKind::Category || EqualsIgnoreCase(a.category, b.category));
		}

		void RegisterLauncherClass(HINSTANCE instance, WNDPROC procedure)
		{
			static bool registered = false;
			if (registered)
			{
				return;
			}
			WNDCLASSEXW windowClass{};
			windowClass.cbSize = sizeof(windowClass);
			windowClass.style = CS_DROPSHADOW;
			windowClass.lpfnWndProc = procedure;
			windowClass.hInstance = instance;
			windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
			windowClass.lpszClassName = LauncherWindowClass;
			registered = RegisterClassExW(&windowClass) != 0;
		}
	}

	LauncherWindow::LauncherWindow(TrayApp& app)
		: app_(app)
	{
	}

	LauncherWindow::~LauncherWindow()
	{
		Close();
	}

	int LauncherWindow::Scale(int value) const
	{
		return ScaleForDpi(value, dpi_);
	}

	POINT LauncherWindow::ComputePosition(const RECT& anchor, int width, int height) const
	{
		HMONITOR monitor = MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST);
		MONITORINFO info{};
		info.cbSize = sizeof(info);
		GetMonitorInfoW(monitor, &info);
		const int left = static_cast<int>(info.rcWork.left);
		const int top = static_cast<int>(info.rcWork.top);
		const int right = static_cast<int>(info.rcWork.right);
		const int bottom = static_cast<int>(info.rcWork.bottom);
		const int gap = Scale(Padding);
		const int centerX = static_cast<int>((anchor.left + anchor.right) / 2);
		const int centerY = static_cast<int>((anchor.top + anchor.bottom) / 2);
		const auto clampX = [&](int x) { return std::clamp(x, left + gap, std::max(left + gap, right - width - gap)); };
		const auto clampY = [&](int y) { return std::clamp(y, top + gap, std::max(top + gap, bottom - height - gap)); };
		const auto makePoint = [](int x, int y) { return POINT{static_cast<LONG>(x), static_cast<LONG>(y)}; };

		if (anchor.top >= bottom) // taskbar at the bottom
		{
			return makePoint(clampX(centerX - width / 2), bottom - height - gap);
		}
		if (anchor.bottom <= top) // at the top
		{
			return makePoint(clampX(centerX - width / 2), top + gap);
		}
		if (anchor.left >= right) // at the right
		{
			return makePoint(right - width - gap, clampY(centerY - height / 2));
		}
		if (anchor.right <= left) // at the left
		{
			return makePoint(left + gap, clampY(centerY - height / 2));
		}
		// Inside the work area (overflow flyout, cursor): above the point
		return makePoint(clampX(centerX - width / 2), clampY(static_cast<int>(anchor.top) - height - gap));
	}

	void LauncherWindow::Show(const std::optional<RECT>& anchor)
	{
		if (window_ != nullptr)
		{
			SetForegroundWindow(window_);
			return;
		}
		RECT anchorRect{};
		if (anchor)
		{
			anchorRect = *anchor;
		}
		else
		{
			POINT cursor{};
			GetCursorPos(&cursor);
			anchorRect = {cursor.x, cursor.y, cursor.x + 1, cursor.y + 1};
		}

		RegisterLauncherClass(app_.Instance(), &LauncherWindow::WindowProc);
		dpi_ = GetDpiForRect(anchorRect);
		const int width = Scale(BaseWidth);
		const int height = Scale(BaseHeight);
		const POINT position = ComputePosition(anchorRect, width, height);
		CreateWindowExW(
			WS_EX_TOOLWINDOW,
			LauncherWindowClass,
			text::AppName,
			WS_POPUP | WS_CLIPCHILDREN,
			position.x,
			position.y,
			width,
			height,
			nullptr,
			nullptr,
			app_.Instance(),
			this);
		if (window_ == nullptr)
		{
			return;
		}
		dpi_ = GetDpiForWindow(window_);

		// Each opening starts fresh: both searches empty, at Favorites (or All), so typing a name
		// right away searches every app and never a folder left from the last time
		filter_.kind = CountAppItems(app_.Items(), AppFilterKind::Favorites) > 0 ? AppFilterKind::Favorites
																				 : AppFilterKind::All;
		filter_.category.clear();
		filter_.search.clear();
		folderFilter_.clear();
		statusOverride_.clear();
		wasActive_ = false;

		iconWorker_ = std::make_unique<IconWorker>(window_, WM_APP_ICON);
		CreateChildren();
		ApplyTheme();
		ApplyFonts();
		ResetImages();
		Layout();
		PopulateTree();
		ApplyFilter();

		ShowWindow(window_, SW_SHOW);
		SetForegroundWindow(window_);
		FocusSearch();
	}

	void LauncherWindow::Close()
	{
		if (window_ != nullptr)
		{
			DestroyWindow(window_);
		}
	}

	void LauncherWindow::RequestClose()
	{
		if (window_ != nullptr)
		{
			PostMessageW(window_, WM_CLOSE, 0, 0);
		}
	}

	void LauncherWindow::CreateChildren()
	{
		const HINSTANCE instance = app_.Instance();
		folderSearch_ = CreateWindowExW(
			0, WC_EDITW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, window_,
			reinterpret_cast<HMENU>(static_cast<INT_PTR>(FolderSearchId)), instance, nullptr);
		SendMessageW(folderSearch_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(text::FolderSearchCue.Get()));

		search_ = CreateWindowExW(
			0, WC_EDITW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, window_,
			reinterpret_cast<HMENU>(static_cast<INT_PTR>(SearchId)), instance, nullptr);
		SendMessageW(search_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(text::SearchCue.Get()));

		tree_ = CreateWindowExW(
			0, WC_TREEVIEWW, L"",
			// TVS_LINESATROOT: without it the top-level categories show no expand arrow
			WS_CHILD | WS_VISIBLE | WS_TABSTOP | TVS_HASBUTTONS | TVS_LINESATROOT | TVS_SHOWSELALWAYS | TVS_FULLROWSELECT
				| TVS_TRACKSELECT | TVS_NOHSCROLL,
			0, 0, 0, 0, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(TreeId)), instance, nullptr);
		TreeView_SetExtendedStyle(tree_, TVS_EX_DOUBLEBUFFER, TVS_EX_DOUBLEBUFFER);

		list_ = CreateWindowExW(
			0, WC_LISTVIEWW, L"",
			WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL | LVS_SHOWSELALWAYS
				| LVS_NOCOLUMNHEADER | LVS_SHAREIMAGELISTS,
			0, 0, 0, 0, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ListId)), instance, nullptr);
		ListView_SetExtendedListViewStyle(list_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
		LVCOLUMNW column{};
		column.mask = LVCF_WIDTH;
		column.cx = 100;
		ListView_InsertColumn(list_, 0, &column);
		ListView_InsertColumn(list_, 1, &column);

		status_ = CreateWindowExW(
			0, WC_STATICW, L"", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS | SS_CENTERIMAGE,
			0, 0, 0, 0, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(StatusId)), instance, nullptr);
	}

	void LauncherWindow::ApplyTheme()
	{
		theme_.Reload();
		theme_.ApplyToWindow(window_);
		theme_.ApplyToControl(tree_);
		theme_.ApplyToControl(list_);
		const ThemePalette& palette = theme_.Palette();
		ListView_SetBkColor(list_, palette.window);
		ListView_SetTextBkColor(list_, palette.window);
		ListView_SetTextColor(list_, palette.text);
		TreeView_SetBkColor(tree_, palette.window);
		TreeView_SetTextColor(tree_, palette.text);
		InvalidateRect(window_, nullptr, TRUE);
	}

	void LauncherWindow::ApplyFonts()
	{
		font_ = CreateMessageFont(dpi_);
		searchFont_ = CreateMessageFont(dpi_, 115);
		for (HWND control : {tree_, list_, status_})
		{
			SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_.get()), TRUE);
		}
		for (HWND box : {folderSearch_, search_})
		{
			SendMessageW(box, WM_SETFONT, reinterpret_cast<WPARAM>(searchFont_.get()), TRUE);
		}
		iconFont_.reset(CreateFontW(
			-Scale(16), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe MDL2 Assets"));
		TreeView_SetItemHeight(tree_, Scale(26));
	}

	void LauncherWindow::ResetImages()
	{
		iconSize_ = GetSystemMetricsForDpi(SM_CXSMICON, dpi_);
		if (images_ != nullptr)
		{
			ImageList_Destroy(images_);
		}
		images_ = ImageList_Create(iconSize_, iconSize_, ILC_COLOR32, 64, 64);
		SHSTOCKICONINFO stock{};
		stock.cbSize = sizeof(stock);
		if (SUCCEEDED(SHGetStockIconInfo(SIID_APPLICATION, SHGSI_ICON | SHGSI_SMALLICON, &stock)))
		{
			ImageList_AddIcon(images_, stock.hIcon);
			DestroyIcon(stock.hIcon);
		}
		ListView_SetImageList(list_, images_, LVSIL_SMALL);
		++generation_;
		imageOf_.assign(app_.Items().size(), -1);
		if (iconWorker_)
		{
			iconWorker_->Clear();
		}
	}

	void LauncherWindow::Layout()
	{
		RECT client{};
		GetClientRect(window_, &client);
		const int padding = Scale(Padding);
		const int treeRight = padding + Scale(TreeWidth);
		const int listLeft = treeRight + Scale(Gap);
		const int listWidth = client.right - padding - listLeft;
		// Folder search above the tree, app search above the list, menu button at the end
		const int searchBottom = padding + Scale(SearchHeight);
		folderFrame_ = {padding, padding, treeRight, searchBottom};
		menuButton_ = {client.right - padding - Scale(SearchHeight), padding, client.right - padding, searchBottom};
		searchFrame_ = {listLeft, padding, menuButton_.left - Scale(Gap), searchBottom};

		// Each edit box sits inside the rounded frame painted by the window, vertically centered
		int editHeight = Scale(22);
		if (HDC dc = GetDC(search_))
		{
			HGDIOBJ old = SelectObject(dc, searchFont_.get());
			TEXTMETRICW metrics{};
			GetTextMetricsW(dc, &metrics);
			SelectObject(dc, old);
			ReleaseDC(search_, dc);
			editHeight = metrics.tmHeight + Scale(2);
		}
		const int inner = Scale(10);
		for (const auto& [box, frame] : {std::pair{folderSearch_, folderFrame_}, std::pair{search_, searchFrame_}})
		{
			const int frameHeight = frame.bottom - frame.top;
			MoveWindow(
				box, frame.left + inner, frame.top + (frameHeight - editHeight) / 2, frame.right - frame.left - 2 * inner,
				editHeight, TRUE);
		}

		const int top = searchFrame_.bottom + Scale(Gap);
		const int statusTop = client.bottom - padding / 2 - Scale(StatusHeight);
		const int bottom = statusTop - Scale(4);
		MoveWindow(tree_, padding, top, treeRight - padding, bottom - top, TRUE);
		MoveWindow(list_, listLeft, top, listWidth, bottom - top, TRUE);
		MoveWindow(status_, padding + Scale(4), statusTop, client.right - 2 * padding - Scale(4), Scale(StatusHeight), TRUE);

		const int usable = listWidth - GetSystemMetricsForDpi(SM_CXVSCROLL, dpi_) - Scale(4);
		const int nameWidth = usable * NameColumnPercent / 100;
		ListView_SetColumnWidth(list_, 0, nameWidth);
		ListView_SetColumnWidth(list_, 1, usable - nameWidth);
	}

	void LauncherWindow::Paint()
	{
		PAINTSTRUCT paint{};
		HDC dc = BeginPaint(window_, &paint);
		const ThemePalette& palette = theme_.Palette();
		wil::unique_hpen pen(CreatePen(PS_SOLID, 1, palette.border));
		HGDIOBJ oldPen = SelectObject(dc, pen.get());
		HGDIOBJ oldBrush = SelectObject(dc, theme_.SurfaceBrush());
		const int radius = Scale(8);
		for (const RECT& frame : {folderFrame_, searchFrame_})
		{
			RoundRect(dc, frame.left, frame.top, frame.right, frame.bottom, radius, radius);
		}
		// The menu button: same frame, filled with the border colour while the mouse is over it
		wil::unique_hbrush hotBrush(CreateSolidBrush(palette.border));
		SelectObject(dc, menuButtonHot_ ? hotBrush.get() : theme_.SurfaceBrush());
		RoundRect(dc, menuButton_.left, menuButton_.top, menuButton_.right, menuButton_.bottom, radius, radius);
		SelectObject(dc, oldBrush);
		SelectObject(dc, oldPen);
		HGDIOBJ oldFont = SelectObject(dc, iconFont_.get());
		SetBkMode(dc, TRANSPARENT);
		SetTextColor(dc, palette.text);
		RECT glyph = menuButton_;
		DrawTextW(dc, MenuGlyph, -1, &glyph, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		SelectObject(dc, oldFont);
		EndPaint(window_, &paint);
	}

	HTREEITEM LauncherWindow::AddTreeNode(HTREEITEM parent, const std::wstring& text, AppFilter filter)
	{
		treeFilters_.push_back(std::move(filter));
		std::wstring label = text;
		TVINSERTSTRUCTW insert{};
		insert.hParent = parent;
		insert.hInsertAfter = TVI_LAST;
		insert.item.mask = TVIF_TEXT | TVIF_PARAM;
		insert.item.pszText = label.data();
		insert.item.lParam = static_cast<LPARAM>(treeFilters_.size() - 1);
		return TreeView_InsertItem(tree_, &insert);
	}

	void LauncherWindow::AddCategoryNodes(HTREEITEM parent, const CategoryNode& node)
	{
		for (const CategoryNode& child : node.children)
		{
			AppFilter filter;
			filter.kind = AppFilterKind::Category;
			filter.category = child.path;
			const HTREEITEM item = AddTreeNode(parent, std::format(L"{}  ({})", child.name, child.count), filter);
			AddCategoryNodes(item, child);
			// While filtering folders, the folders above a match open so its path shows; a match
			// itself stays closed (its subfolders are one click away) to keep the list short
			const bool wasOpen = std::any_of(
				expandedCategories_.begin(), expandedCategories_.end(),
				[&](const std::wstring& path) { return EqualsIgnoreCase(path, child.path); });
			const bool open = folderFilter_.empty() ? wasOpen : !MatchesCategorySearch(child.path, folderFilter_);
			if (open)
			{
				TreeView_Expand(tree_, item, TVE_EXPAND);
			}
		}
	}

	void LauncherWindow::PopulateTree()
	{
		populatingTree_ = true;
		TreeView_DeleteAllItems(tree_);
		treeFilters_.clear();
		const std::vector<AppItem>& items = app_.Items();
		const bool filteringFolders = !folderFilter_.empty();

		const auto addFixed = [&](AppFilterKind kind, const wchar_t* label, bool always)
		{
			const size_t count = CountAppItems(items, kind);
			if (!filteringFolders && (always || count > 0))
			{
				AppFilter filter;
				filter.kind = kind;
				AddTreeNode(TVI_ROOT, std::format(L"{}  ({})", label, count), filter);
			}
		};
		addFixed(AppFilterKind::Favorites, text::Favorites, true);
		addFixed(AppFilterKind::Recent, text::Recent, false);
		addFixed(AppFilterKind::All, text::All, true);
		addFixed(AppFilterKind::New, text::New, false);
		AddCategoryNodes(
			TVI_ROOT, FilterCategoryTree(BuildCategoryTree(items, app_.Settings().categoryDepth), folderFilter_));
		addFixed(AppFilterKind::Installers, text::InstallersAndCopies, false);
		addFixed(AppFilterKind::Hidden, text::Hidden, false);

		// Select the node of the current filter (walking the whole tree). A category cut away by
		// categoryDepth falls back to its deepest ancestor still shown; anything else to "All".
		HTREEITEM selected = nullptr;
		HTREEITEM ancestor = nullptr;
		size_t ancestorLength = 0;
		HTREEITEM all = nullptr;
		HTREEITEM firstMatch = nullptr; // first folder that matches the folder search itself
		const auto visit = [&](auto& self, HTREEITEM item) -> void
		{
			for (; item != nullptr; item = TreeView_GetNextSibling(tree_, item))
			{
				TVITEMW data{};
				data.mask = TVIF_PARAM;
				data.hItem = item;
				TreeView_GetItem(tree_, &data);
				const AppFilter& filter = treeFilters_[static_cast<size_t>(data.lParam)];
				if (selected == nullptr && IsSameFilter(filter, filter_))
				{
					selected = item;
				}
				if (filteringFolders && firstMatch == nullptr && filter.kind == AppFilterKind::Category
					&& MatchesCategorySearch(filter.category, folderFilter_))
				{
					firstMatch = item;
				}
				if (filter.kind == AppFilterKind::Category && filter_.kind == AppFilterKind::Category
					&& filter.category.size() > ancestorLength
					&& StartsWithIgnoreCase(filter_.category, filter.category + L"/"))
				{
					ancestor = item;
					ancestorLength = filter.category.size();
				}
				if (filter.kind == AppFilterKind::All)
				{
					all = item;
				}
				self(self, TreeView_GetChild(tree_, item));
			}
		};
		visit(visit, TreeView_GetRoot(tree_));
		const auto categoryOf = [&](HTREEITEM item)
		{
			TVITEMW data{};
			data.mask = TVIF_PARAM;
			data.hItem = item;
			TreeView_GetItem(tree_, &data);
			return treeFilters_[static_cast<size_t>(data.lParam)].category;
		};
		noFolderMatch_ = false;
		if (filteringFolders)
		{
			// Typing a folder name picks the first folder that matches, so its apps show at once;
			// a folder the user picked meanwhile stays picked through refreshes and favorite changes
			if (folderFilterChanged_ || selected == nullptr || filter_.kind != AppFilterKind::Category)
			{
				selected = firstMatch;
				filter_.kind = AppFilterKind::Category;
				filter_.category = firstMatch != nullptr ? categoryOf(firstMatch) : std::wstring{};
			}
			noFolderMatch_ = firstMatch == nullptr;
			folderFilterChanged_ = false;
		}
		else if (selected == nullptr && ancestor != nullptr)
		{
			selected = ancestor;
			filter_.category = categoryOf(ancestor);
		}
		else if (selected == nullptr)
		{
			selected = all;
			filter_.kind = AppFilterKind::All;
			filter_.category.clear();
		}
		if (selected != nullptr)
		{
			TreeView_SelectItem(tree_, selected);
			TreeView_EnsureVisible(tree_, selected);
		}
		populatingTree_ = false;
	}

	void LauncherWindow::ApplyFilter(std::optional<std::wstring> keepSelectedId)
	{
		const std::vector<AppItem>& items = app_.Items();
		AppFilter effective = filter_;
		// The app search stays inside the chosen folder (or hidden apps, or installers); from
		// Favorites, Recent or New it looks everywhere, so typing a name right after opening finds it
		if (!effective.search.empty()
			&& (effective.kind == AppFilterKind::Favorites || effective.kind == AppFilterKind::Recent
				|| effective.kind == AppFilterKind::New))
		{
			effective.kind = AppFilterKind::All;
		}
		// No folder matches: the app search (if any) looks everywhere instead of showing nothing
		if (noFolderMatch_ && !effective.search.empty())
		{
			effective.kind = AppFilterKind::All;
			effective.category.clear();
		}
		visible_ = noFolderMatch_ && effective.search.empty() ? std::vector<size_t>{}
															  : FilterAppItems(items, effective, app_.Settings().favorites);
		// Icons of rows no longer shown wait behind: drop them so the visible ones come first
		if (iconWorker_)
		{
			iconWorker_->Clear();
			std::replace(imageOf_.begin(), imageOf_.end(), -2, -1);
		}
		ListView_SetItemCountEx(list_, static_cast<int>(visible_.size()), 0);

		int row = visible_.empty() ? -1 : 0;
		if (keepSelectedId)
		{
			for (size_t i = 0; i < visible_.size(); ++i)
			{
				if (items[visible_[i]].entry->id == *keepSelectedId)
				{
					row = static_cast<int>(i);
					break;
				}
			}
		}
		if (row >= 0)
		{
			SelectRow(row);
		}
		InvalidateRect(list_, nullptr, TRUE);
		UpdateStatus();
	}

	std::optional<int> LauncherWindow::GetSelectedRow() const
	{
		const int row = ListView_GetNextItem(list_, -1, LVNI_SELECTED);
		if (row < 0 || static_cast<size_t>(row) >= visible_.size())
		{
			return std::nullopt;
		}
		return row;
	}

	std::optional<size_t> LauncherWindow::GetSelectedItem() const
	{
		const std::optional<int> row = GetSelectedRow();
		if (!row)
		{
			return std::nullopt;
		}
		const size_t index = visible_[static_cast<size_t>(*row)];
		if (index >= app_.Items().size())
		{
			return std::nullopt;
		}
		return index;
	}

	void LauncherWindow::SelectRow(int row)
	{
		ListView_SetItemState(list_, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
		ListView_SetItemState(list_, row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		ListView_EnsureVisible(list_, row, FALSE);
	}

	void LauncherWindow::MoveSelection(int delta)
	{
		if (visible_.empty())
		{
			return;
		}
		const int current = GetSelectedRow().value_or(-1);
		const int last = static_cast<int>(visible_.size()) - 1;
		SelectRow(std::clamp(current + delta, 0, last));
	}

	void LauncherWindow::UpdateStatus()
	{
		std::wstring text;
		if (!statusOverride_.empty())
		{
			text = statusOverride_;
		}
		else if (const std::optional<size_t> index = GetSelectedItem())
		{
			const AppItem& item = app_.Items()[*index];
			const std::wstring what = item.entry->description.empty() ? item.name : item.entry->description;
			const std::wstring where = item.target.empty() ? std::wstring(text::OpensFolder) : item.target;
			text = item.entry->version.empty() ? std::format(L"{}  ·  {}", what, where)
											   : std::format(L"{}  ·  v{}  ·  {}", what, item.entry->version, where);
		}
		else if (noFolderMatch_)
		{
			text = text::NoFolderMatches;
		}
		else if (!filter_.search.empty())
		{
			text = text::NoMatches;
		}
		else
		{
			text = app_.DescribeCatalog() + L"  ·  " + text::Keys.Get();
		}
		SetWindowTextW(status_, text.c_str());
	}

	void LauncherWindow::SetStatus(std::wstring text)
	{
		statusOverride_ = std::move(text);
		if (window_ != nullptr)
		{
			UpdateStatus();
		}
	}

	void LauncherWindow::LaunchSelected(bool asAdmin)
	{
		if (const std::optional<size_t> index = GetSelectedItem())
		{
			app_.LaunchItem(*index, asAdmin);
		}
	}

	void LauncherWindow::ShowContextMenu(std::optional<POINT> screenPoint)
	{
		const std::optional<size_t> selected = GetSelectedItem();
		if (!selected)
		{
			return;
		}
		// A scan finishing while the menu is open waits until the command below has run
		const TrayApp::MenuScope menuScope = app_.EnterMenu();
		const size_t index = *selected;
		const AppItem& item = app_.Items()[index];
		const AppEntry& entry = *item.entry;
		const bool isSuite = item.target.empty();

		POINT point{};
		if (screenPoint)
		{
			point = *screenPoint;
		}
		else
		{
			RECT rect{};
			ListView_GetItemRect(list_, *GetSelectedRow(), &rect, LVIR_LABEL);
			point = {rect.left, rect.bottom};
			ClientToScreen(list_, &point);
		}

		HMENU menu = CreatePopupMenu();
		AppendMenuW(menu, MF_STRING, OpenCommand, text::Open);
		SetMenuDefaultItem(menu, OpenCommand, FALSE);
		AppendMenuW(menu, MF_STRING | (isSuite ? MF_GRAYED : 0), OpenAsAdminCommand, text::OpenAsAdmin);
		AppendMenuW(menu, MF_STRING, OpenFolderCommand, text::OpenFolder);
		if (!entry.readme.empty())
		{
			AppendMenuW(menu, MF_STRING, OpenHelpCommand, text::OpenHelp);
		}
		HMENU shortcut = CreatePopupMenu();
		AppendMenuW(shortcut, MF_STRING, ShortcutDesktopCommand, text::ShortcutOnDesktop);
		AppendMenuW(shortcut, MF_STRING, ShortcutStartMenuCommand, text::ShortcutInStartMenu);
		AppendMenuW(
			menu, MF_POPUP | (entry.missing ? MF_GRAYED : 0), reinterpret_cast<UINT_PTR>(shortcut), text::CreateShortcut);
		AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
		AppendMenuW(menu, MF_STRING, FavoriteCommand, item.favorite ? text::RemoveFavorite : text::AddFavorite);

		const AppOverride* userOverride = app_.Settings().FindOverride(entry.id);
		const bool hasTargetOverride = userOverride != nullptr && userOverride->target.has_value();
		if (!entry.alternatives.empty() || hasTargetOverride)
		{
			HMENU alternatives = CreatePopupMenu();
			const std::wstring prefix = entry.id + L"/";
			const auto addChoice = [&](const std::wstring& relative, UINT id)
			{
				const std::wstring label = relative.starts_with(prefix) ? relative.substr(prefix.size()) : relative;
				AppendMenuW(alternatives, MF_STRING | (relative == item.target ? MF_CHECKED : 0), id, EscapeMenuText(label).c_str());
			};
			if (!entry.target.empty())
			{
				addChoice(entry.target, AlternativeCommandBase);
			}
			for (size_t i = 0; i < entry.alternatives.size(); ++i)
			{
				addChoice(entry.alternatives[i], AlternativeCommandBase + 1 + static_cast<UINT>(i));
			}
			if (hasTargetOverride)
			{
				AppendMenuW(alternatives, MF_SEPARATOR, 0, nullptr);
				AppendMenuW(alternatives, MF_STRING, ResetTargetCommand, text::ResetExecutable);
			}
			AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(alternatives), text::UseOtherExecutable);
		}

		// The scanner's category (not a renamed one) is the id of the suite this program came from
		const bool inExpandedSuite = entry.kind != AppKind::Suite && app_.Settings().IsExpanded(entry.category);
		if (entry.kind == AppKind::Suite)
		{
			AppendMenuW(menu, MF_STRING, ExpandCommand, text::ExpandSuite);
		}
		else if (inExpandedSuite)
		{
			AppendMenuW(menu, MF_STRING, CollapseCommand, text::CollapseSuite);
		}
		// Logon behaviour and elevation (programs only, not a suite's folder)
		AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
		HMENU logon = CreatePopupMenu();
		const auto addMode = [&](AutostartMode mode, UINT id, const wchar_t* label)
		{ AppendMenuW(logon, MF_STRING | (item.autostart == mode ? MF_CHECKED : 0), id, label); };
		addMode(AutostartMode::No, AutostartNoCommand, text::AutostartNo);
		addMode(AutostartMode::Always, AutostartAlwaysCommand, text::AutostartAlways);
		addMode(AutostartMode::Ask, AutostartAskCommand, text::AutostartAsk);
		AppendMenuW(menu, MF_POPUP | (isSuite ? MF_GRAYED : 0), reinterpret_cast<UINT_PTR>(logon), text::AtLogon);
		const bool isProgram = !isSuite && EndsWithIgnoreCase(item.target, L".exe");
		// Copied: a finished elevation can rebuild the items while the menu is open
		const bool elevated = item.elevatedTask;
		AppendMenuW(
			menu, MF_STRING | (elevated ? MF_CHECKED : 0) | (isProgram ? 0 : MF_GRAYED), ElevatedCommand,
			text::RunWithoutUac);
		AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
		AppendMenuW(menu, MF_STRING, item.hidden ? UnhideCommand : HideCommand, item.hidden ? text::Unhide : text::Hide);

		const UINT command = static_cast<UINT>(
			TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, window_, nullptr));
		DestroyMenu(menu);

		switch (command)
		{
		case 0:
			break;
		case OpenCommand:
			app_.LaunchItem(index, false);
			break;
		case OpenAsAdminCommand:
			app_.LaunchItem(index, true);
			break;
		case OpenFolderCommand:
			app_.OpenItemFolder(index);
			break;
		case OpenHelpCommand:
			app_.OpenItemReadme(index);
			break;
		case ShortcutDesktopCommand:
			app_.CreateItemShortcut(index, ShortcutPlace::Desktop);
			break;
		case ShortcutStartMenuCommand:
			app_.CreateItemShortcut(index, ShortcutPlace::StartMenu);
			break;
		case FavoriteCommand:
			app_.ToggleItemFavorite(index);
			break;
		case HideCommand:
			app_.SetItemHidden(index, true);
			break;
		case UnhideCommand:
			app_.SetItemHidden(index, false);
			break;
		case ExpandCommand:
			app_.SetSuiteExpanded(entry.id, true);
			break;
		case CollapseCommand:
			app_.SetSuiteExpanded(entry.category, false);
			break;
		case ResetTargetCommand:
			app_.SetItemTarget(index, std::nullopt);
			break;
		case AutostartNoCommand:
			app_.SetItemAutostart(index, AutostartMode::No);
			break;
		case AutostartAlwaysCommand:
			app_.SetItemAutostart(index, AutostartMode::Always);
			break;
		case AutostartAskCommand:
			app_.SetItemAutostart(index, AutostartMode::Ask);
			break;
		case ElevatedCommand:
			app_.SetItemElevated(index, !elevated);
			break;
		default:
			if (command == AlternativeCommandBase)
			{
				app_.SetItemTarget(index, std::nullopt);
			}
			else if (command > AlternativeCommandBase && command - AlternativeCommandBase - 1 < entry.alternatives.size())
			{
				app_.SetItemTarget(index, entry.alternatives[command - AlternativeCommandBase - 1]);
			}
			break;
		}
	}

	void LauncherWindow::ShowAppTrayMenu()
	{
		RECT button = menuButton_;
		MapWindowPoints(window_, nullptr, reinterpret_cast<POINT*>(&button), 2);
		app_.ShowTrayMenu({button.right, button.bottom}, window_, &button);
		menuClosedAt_ = GetTickCount64();
		// The command may have closed the launcher (an app opened, Exit)
		if (window_ != nullptr && menuButtonHot_)
		{
			menuButtonHot_ = false;
			InvalidateRect(window_, &menuButton_, FALSE);
		}
	}

	int LauncherWindow::GetImageIndex(size_t itemIndex)
	{
		if (itemIndex >= imageOf_.size())
		{
			return 0;
		}
		if (imageOf_[itemIndex] >= 0)
		{
			return imageOf_[itemIndex];
		}
		if (imageOf_[itemIndex] == -1 && iconWorker_)
		{
			imageOf_[itemIndex] = -2;
			if (const std::optional<std::filesystem::path> path = app_.ResolveTarget(app_.Items()[itemIndex]))
			{
				iconWorker_->Request(generation_, itemIndex, *path, iconSize_);
			}
		}
		return 0;
	}

	void LauncherWindow::OnIconReady(std::unique_ptr<IconResult> result)
	{
		if (result->icon == nullptr)
		{
			return;
		}
		if (result->generation == generation_ && result->key < imageOf_.size() && images_ != nullptr)
		{
			// A rejected icon keeps the stock one (0); -1 would mean "ask again" on every repaint
			imageOf_[result->key] = std::max(0, ImageList_AddIcon(images_, result->icon));
			const int top = ListView_GetTopIndex(list_);
			ListView_RedrawItems(list_, top, top + ListView_GetCountPerPage(list_));
		}
		DestroyIcon(result->icon);
	}

	void LauncherWindow::OnGetDisplayInfo(NMLVDISPINFOW* info)
	{
		const size_t row = static_cast<size_t>(info->item.iItem);
		if (row >= visible_.size() || visible_[row] >= app_.Items().size())
		{
			return;
		}
		const size_t index = visible_[row];
		const AppItem& item = app_.Items()[index];
		if ((info->item.mask & LVIF_TEXT) != 0 && info->item.pszText != nullptr && info->item.cchTextMax > 0)
		{
			std::wstring text;
			if (info->item.iSubItem == 0)
			{
				text = item.entry->missing ? std::format(L"{}  ({})", item.name, text::NotFound) : item.name;
			}
			else
			{
				text = item.isNew ? std::format(L"{}  ·  {}", text::NewTag, FormatCategory(item.category))
								  : FormatCategory(item.category);
			}
			wcsncpy_s(info->item.pszText, static_cast<size_t>(info->item.cchTextMax), text.c_str(), _TRUNCATE);
		}
		if ((info->item.mask & LVIF_IMAGE) != 0 && info->item.iSubItem == 0)
		{
			info->item.iImage = GetImageIndex(index);
		}
	}

	LRESULT LauncherWindow::OnListCustomDraw(NMLVCUSTOMDRAW* draw) const
	{
		switch (draw->nmcd.dwDrawStage)
		{
		case CDDS_PREPAINT:
			return CDRF_NOTIFYITEMDRAW;
		case CDDS_ITEMPREPAINT:
			return CDRF_NOTIFYSUBITEMDRAW;
		case CDDS_ITEMPREPAINT | CDDS_SUBITEM:
		{
			const size_t row = static_cast<size_t>(draw->nmcd.dwItemSpec);
			const bool missing = row < visible_.size() && visible_[row] < app_.Items().size()
				&& app_.Items()[visible_[row]].entry->missing;
			const ThemePalette& palette = theme_.Palette();
			draw->clrText = draw->iSubItem == 1 || missing ? palette.secondaryText : palette.text;
			return CDRF_NEWFONT;
		}
		default:
			return CDRF_DODEFAULT;
		}
	}

	LRESULT LauncherWindow::OnNotify(const NMHDR* header)
	{
		if (header->hwndFrom == tree_ && header->code == TVN_ITEMEXPANDEDW && !populatingTree_ && folderFilter_.empty())
		{
			// Remember open branches for the next time the tree is filled (window reopened, refresh)
			const auto* change = reinterpret_cast<const NMTREEVIEWW*>(header);
			const size_t filterIndex = static_cast<size_t>(change->itemNew.lParam);
			if (filterIndex < treeFilters_.size() && treeFilters_[filterIndex].kind == AppFilterKind::Category)
			{
				const std::wstring& path = treeFilters_[filterIndex].category;
				std::erase_if(expandedCategories_, [&](const std::wstring& saved) { return EqualsIgnoreCase(saved, path); });
				if ((change->action & TVE_EXPAND) != 0)
				{
					expandedCategories_.push_back(path);
				}
			}
			return 0;
		}
		if (header->hwndFrom == tree_ && header->code == TVN_SELCHANGEDW && !populatingTree_)
		{
			const auto* change = reinterpret_cast<const NMTREEVIEWW*>(header);
			const size_t filterIndex = static_cast<size_t>(change->itemNew.lParam);
			if (filterIndex < treeFilters_.size())
			{
				const std::wstring search = filter_.search;
				filter_ = treeFilters_[filterIndex];
				filter_.search = search;
				statusOverride_.clear();
				ApplyFilter();
			}
			return 0;
		}
		if (header->hwndFrom != list_)
		{
			return 0;
		}
		switch (header->code)
		{
		case LVN_GETDISPINFOW:
			OnGetDisplayInfo(reinterpret_cast<NMLVDISPINFOW*>(const_cast<NMHDR*>(header)));
			return 0;
		case LVN_ODFINDITEMW:
			return -1; // typing goes to the search box instead
		case LVN_ITEMCHANGED:
		{
			const auto* change = reinterpret_cast<const NMLISTVIEW*>(header);
			if ((change->uChanged & LVIF_STATE) != 0 && (change->uNewState & LVIS_SELECTED) != 0)
			{
				if (const std::optional<size_t> index = GetSelectedItem())
				{
					selectedId_ = app_.Items()[*index].entry->id;
				}
				statusOverride_.clear();
				UpdateStatus();
			}
			return 0;
		}
		case LVN_ITEMACTIVATE:
			LaunchSelected(IsKeyDown(VK_CONTROL));
			return 0;
		case NM_RCLICK:
		{
			POINT cursor{};
			GetCursorPos(&cursor);
			LVHITTESTINFO hit{};
			hit.pt = cursor;
			ScreenToClient(list_, &hit.pt);
			const int row = ListView_HitTest(list_, &hit);
			if (row >= 0)
			{
				SelectRow(row);
				ShowContextMenu(cursor);
			}
			return 0;
		}
		case NM_CUSTOMDRAW:
			return OnListCustomDraw(reinterpret_cast<NMLVCUSTOMDRAW*>(const_cast<NMHDR*>(header)));
		default:
			return 0;
		}
	}

	void LauncherWindow::FocusSearch()
	{
		SetFocus(search_);
		SendMessageW(search_, EM_SETSEL, 0, -1);
	}

	void LauncherWindow::CycleFocus(bool backwards)
	{
		const HWND order[] = {folderSearch_, tree_, search_, list_};
		const HWND focus = GetFocus();
		size_t current = 0;
		for (size_t i = 0; i < std::size(order); ++i)
		{
			if (order[i] == focus)
			{
				current = i;
			}
		}
		const size_t next = backwards ? (current + std::size(order) - 1) % std::size(order) : (current + 1) % std::size(order);
		SetFocus(order[next]);
	}

	bool LauncherWindow::PreTranslateMessage(const MSG& message)
	{
		if (window_ == nullptr || (message.hwnd != window_ && !IsChild(window_, message.hwnd)))
		{
			return false;
		}
		if (message.message == WM_KEYDOWN || message.message == WM_SYSKEYDOWN)
		{
			const bool control = IsKeyDown(VK_CONTROL);
			const bool shift = IsKeyDown(VK_SHIFT);
			const bool inSearch = message.hwnd == search_;
			const bool inFolderSearch = message.hwnd == folderSearch_;
			switch (message.wParam)
			{
			case VK_ESCAPE:
				// Clears the box being typed in first, then the other, then closes
				if (inFolderSearch && GetWindowTextLengthW(folderSearch_) > 0)
				{
					SetWindowTextW(folderSearch_, L"");
				}
				else if (GetWindowTextLengthW(search_) > 0)
				{
					SetWindowTextW(search_, L"");
					FocusSearch();
				}
				else if (GetWindowTextLengthW(folderSearch_) > 0)
				{
					SetWindowTextW(folderSearch_, L"");
				}
				else
				{
					Close();
				}
				return true;
			case VK_RETURN:
				// From the folder side, Enter moves on to the apps of the chosen folder
				if ((inFolderSearch || message.hwnd == tree_) && !control)
				{
					SetFocus(visible_.empty() ? search_ : list_);
				}
				else
				{
					LaunchSelected(control);
				}
				return true;
			case VK_DOWN:
			case VK_UP:
				if (inFolderSearch)
				{
					// Walk the (pruned, open) tree without leaving the folder box
					const HTREEITEM current = TreeView_GetSelection(tree_);
					const HTREEITEM next = current == nullptr
						? TreeView_GetRoot(tree_)
						: (message.wParam == VK_DOWN ? TreeView_GetNextVisible(tree_, current)
													 : TreeView_GetPrevVisible(tree_, current));
					if (next != nullptr)
					{
						TreeView_SelectItem(tree_, next);
					}
					return true;
				}
				[[fallthrough]];
			case VK_PRIOR:
			case VK_NEXT:
				if (inSearch)
				{
					const int page = std::max(1, ListView_GetCountPerPage(list_) - 1);
					const int delta = message.wParam == VK_DOWN ? 1
						: message.wParam == VK_UP                ? -1
						: message.wParam == VK_NEXT              ? page
																 : -page;
					MoveSelection(delta);
					return true;
				}
				return false;
			case VK_F5:
				app_.StartRefresh();
				return true;
			case VK_F3:
				FocusSearch();
				return true;
			case VK_APPS:
				ShowContextMenu(std::nullopt);
				return true;
			case VK_F10:
				if (control || IsKeyDown(VK_MENU))
				{
					return false;
				}
				if (shift)
				{
					ShowContextMenu(std::nullopt);
				}
				else
				{
					ShowAppTrayMenu();
				}
				return true;
			case VK_TAB:
				CycleFocus(shift);
				return true;
			case 'F':
				if (control && shift)
				{
					SetFocus(folderSearch_);
					SendMessageW(folderSearch_, EM_SETSEL, 0, -1);
					return true;
				}
				if (control)
				{
					FocusSearch();
					return true;
				}
				return false;
			case 'D':
				if (control)
				{
					if (const std::optional<size_t> index = GetSelectedItem())
					{
						app_.ToggleItemFavorite(*index);
					}
					return true;
				}
				return false;
			case 'E':
				if (control)
				{
					if (const std::optional<size_t> index = GetSelectedItem())
					{
						app_.OpenItemFolder(*index);
					}
					return true;
				}
				return false;
			default:
				return false;
			}
		}
		// Typing in the tree goes to the folder search, typing in the list to the app search
		if (message.message == WM_CHAR && (message.hwnd == list_ || message.hwnd == tree_)
			&& (message.wParam >= L' ' || message.wParam == VK_BACK))
		{
			const HWND box = message.hwnd == tree_ ? folderSearch_ : search_;
			SetFocus(box);
			SendMessageW(box, EM_SETSEL, static_cast<WPARAM>(-1), -1);
			SendMessageW(box, WM_CHAR, message.wParam, message.lParam);
			return true;
		}
		return false;
	}

	void LauncherWindow::RefreshItems()
	{
		if (window_ == nullptr)
		{
			return;
		}
		// The rows still hold indices into the old items: keep the selection by id
		const std::wstring selectedId = selectedId_;
		visible_.clear();
		ListView_SetItemCountEx(list_, 0, 0);
		if (images_ != nullptr)
		{
			ImageList_SetImageCount(images_, 1);
		}
		++generation_;
		imageOf_.assign(app_.Items().size(), -1);
		if (iconWorker_)
		{
			iconWorker_->Clear();
		}
		PopulateTree();
		ApplyFilter(selectedId);
	}

	void LauncherWindow::RefreshTheme()
	{
		if (window_ != nullptr)
		{
			ApplyTheme();
			for (HWND control : {folderSearch_, search_, tree_, list_, status_})
			{
				InvalidateRect(control, nullptr, TRUE);
			}
		}
	}

	LRESULT CALLBACK LauncherWindow::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
	{
		LauncherWindow* self = nullptr;
		if (message == WM_NCCREATE)
		{
			self = static_cast<LauncherWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
			self->window_ = window;
			SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
		}
		else
		{
			self = reinterpret_cast<LauncherWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
		}
		if (self != nullptr)
		{
			return self->HandleMessage(window, message, wParam, lParam);
		}
		return DefWindowProcW(window, message, wParam, lParam);
	}

	LRESULT LauncherWindow::HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
	{
		switch (message)
		{
		case WM_ERASEBKGND:
		{
			RECT client{};
			GetClientRect(window, &client);
			FillRect(reinterpret_cast<HDC>(wParam), &client, theme_.WindowBrush());
			return 1;
		}
		case WM_PAINT:
			Paint();
			return 0;
		case WM_SIZE:
			if (search_ != nullptr)
			{
				Layout();
			}
			return 0;
		case WM_CTLCOLOREDIT:
		{
			HDC dc = reinterpret_cast<HDC>(wParam);
			SetTextColor(dc, theme_.Palette().text);
			SetBkColor(dc, theme_.Palette().surface);
			return reinterpret_cast<LRESULT>(theme_.SurfaceBrush());
		}
		case WM_CTLCOLORSTATIC:
		{
			HDC dc = reinterpret_cast<HDC>(wParam);
			SetTextColor(dc, theme_.Palette().secondaryText);
			SetBkColor(dc, theme_.Palette().window);
			return reinterpret_cast<LRESULT>(theme_.WindowBrush());
		}
		case WM_COMMAND:
			if (reinterpret_cast<HWND>(lParam) == folderSearch_ && HIWORD(wParam) == EN_CHANGE)
			{
				folderFilter_ = Trim(GetWindowTextString(folderSearch_)); // spaces alone filter nothing
				folderFilterChanged_ = true;
				statusOverride_.clear();
				PopulateTree();
				ApplyFilter();
				return 0;
			}
			if (reinterpret_cast<HWND>(lParam) == search_ && HIWORD(wParam) == EN_CHANGE)
			{
				filter_.search = GetWindowTextString(search_);
				statusOverride_.clear();
				ApplyFilter();
			}
			return 0;
		case WM_NOTIFY:
			return OnNotify(reinterpret_cast<const NMHDR*>(lParam));
		case WM_LBUTTONDOWN:
		{
			// A click on the button while its menu is open closes the menu, and Windows passes that
			// click on to the launcher: it must not open the menu again
			constexpr ULONGLONG ReopenGuardMilliseconds = 300;
			const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
			if (PtInRect(&menuButton_, point) && GetTickCount64() - menuClosedAt_ > ReopenGuardMilliseconds)
			{
				ShowAppTrayMenu();
			}
			return 0;
		}
		case WM_MOUSEMOVE:
		{
			const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
			const bool hot = PtInRect(&menuButton_, point) != FALSE;
			if (hot != menuButtonHot_)
			{
				menuButtonHot_ = hot;
				InvalidateRect(window, &menuButton_, FALSE);
				if (hot)
				{
					TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0};
					TrackMouseEvent(&track);
				}
			}
			return 0;
		}
		case WM_MOUSELEAVE:
			if (menuButtonHot_)
			{
				menuButtonHot_ = false;
				InvalidateRect(window, &menuButton_, FALSE);
			}
			return 0;
		case WM_INITMENUPOPUP:
			// Category submenus of the tray menu opened from the menu button
			app_.FillCategoryMenu(reinterpret_cast<HMENU>(wParam));
			return 0;
		case WM_APP_ICON:
			OnIconReady(std::unique_ptr<IconResult>(reinterpret_cast<IconResult*>(lParam)));
			return 0;
		case WM_ACTIVATE:
			// Clicking anywhere else, or the app just launched taking focus, closes the flyout.
			// Only once it has been active: without foreground rights (started from another
			// process) Windows may never activate it, and it must not vanish on its own.
			if (LOWORD(wParam) != WA_INACTIVE)
			{
				wasActive_ = true;
			}
			else if (wasActive_)
			{
				PostMessageW(window, WM_CLOSE, 0, 0);
			}
			return 0;
		case WM_DPICHANGED:
		{
			dpi_ = HIWORD(wParam);
			const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
			SetWindowPos(
				window, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
				suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
			ApplyFonts();
			ResetImages();
			Layout();
			InvalidateRect(list_, nullptr, TRUE);
			return 0;
		}
		case WM_CLOSE:
			DestroyWindow(window);
			return 0;
		case WM_DESTROY:
		{
			iconWorker_.reset(); // does not wait; nothing more gets posted after this
			// Results already queued would be dropped with the window, leaking their icons
			MSG pending{};
			while (PeekMessageW(&pending, window, WM_APP_ICON, WM_APP_ICON, PM_REMOVE))
			{
				std::unique_ptr<IconResult> result(reinterpret_cast<IconResult*>(pending.lParam));
				if (result && result->icon != nullptr)
				{
					DestroyIcon(result->icon);
				}
			}
			if (images_ != nullptr)
			{
				ImageList_Destroy(images_);
				images_ = nullptr;
			}
			imageOf_.clear();
			imageOf_.shrink_to_fit();
			visible_.clear();
			visible_.shrink_to_fit();
			treeFilters_.clear();
			treeFilters_.shrink_to_fit();
			font_.reset();
			searchFont_.reset();
			iconFont_.reset();
			menuButtonHot_ = false;
			folderSearch_ = search_ = tree_ = list_ = status_ = nullptr;
			window_ = nullptr;
			closedAt_ = GetTickCount64();
			return 0;
		}
		case WM_NCDESTROY:
			SetWindowLongPtrW(window, GWLP_USERDATA, 0);
			app_.OnLauncherClosed();
			break;
		default:
			break;
		}
		return DefWindowProcW(window, message, wParam, lParam);
	}
}
