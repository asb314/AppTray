#pragma once

#include "Catalog.h"
#include "UsageStats.h"
#include "UserSettings.h"

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace apptray
{
	// One app as the UI shows it: the catalog entry with the user's overrides applied
	struct AppItem
	{
		const AppEntry* entry = nullptr; // points into the Catalog, which must outlive the items
		std::wstring name;
		std::wstring category;
		std::wstring target;             // relative; empty = open the folder (suite)
		std::wstring args;
		bool favorite = false;
		bool hidden = false;             // hidden by the user
		bool hiddenByDefault = false;    // installer or backup copy
		bool isNew = false;              // found after the first scan, within the last 30 days
		AutostartMode autostart = AutostartMode::No;
		bool askDefault = false;         // checked in the logon dialog
		bool elevatedTask = false;       // started through its own elevated scheduled task
		int launchCount = 0;             // opened from AppTray (usage.json)
		std::wstring lastLaunched;       // UTC, empty if never
		std::wstring searchText;         // folded name, id, description and category
		std::wstring initials;           // of the name's words, folded: "fsiv fiv" (FastStone Image Viewer)
	};

	// Days during which an app found after the first scan counts as new
	constexpr int NewAppDays = 30;

	// Apps in the Recent list
	constexpr size_t RecentAppLimit = 15;

	std::vector<AppItem> BuildAppItems(
		const Catalog& catalog, const UserSettings& settings, std::wstring_view now, const UsageStats* usage = nullptr);

	enum class AppFilterKind
	{
		All,        // everything not hidden
		Favorites,  // in the user's order
		Recent,     // last opened first, at most RecentAppLimit
		New,
		Category,   // a category and everything below it
		Installers, // installers and backup copies
		Hidden,     // hidden by the user
	};

	struct AppFilter
	{
		AppFilterKind kind = AppFilterKind::All;
		std::wstring category;
		std::wstring search; // words separated by spaces; all must match (case and accents ignored)
	};

	// Indices into items. A search word matches anywhere in the name, folder, description or
	// category, or the start of the name's initials ("vsc" -> Visual Studio Code). Sorted by name;
	// with a search, names starting with the first word come first, then names whose initials do,
	// and within each group the apps opened most often. Favorites keep the user's order, Recent
	// its time order.
	std::vector<size_t> FilterAppItems(
		const std::vector<AppItem>& items, const AppFilter& filter, const std::vector<std::wstring>& favorites);

	struct CategoryNode
	{
		std::wstring path; // "Media/Audio"
		std::wstring name; // "Audio"
		int count = 0;     // visible apps in this category and below
		std::vector<CategoryNode> children;
	};

	// Tree of the categories of the visible apps (root has an empty path). maxDepth > 0 keeps only
	// that many levels: deeper apps count in (and belong to) their ancestor at the last level.
	CategoryNode BuildCategoryTree(const std::vector<AppItem>& items, int maxDepth = 0);

	// True when the category path contains every word of the search (case and accents ignored,
	// levels as words): "audio edit" matches "Media/Audio/Editor"
	bool MatchesCategorySearch(std::wstring_view path, std::wstring_view search);

	// Keeps the branches that match the search: a matching category keeps all its subcategories,
	// and the categories above it stay so its path shows. An empty search keeps everything.
	CategoryNode FilterCategoryTree(const CategoryNode& tree, std::wstring_view search);

	// Counts for the fixed filters, to show or hide them in the UI
	size_t CountAppItems(const std::vector<AppItem>& items, AppFilterKind kind);

	// What to do at logon: apps set to "always" to open now, apps set to "ask" to offer in the
	// dialog. Apps already running, missing, hidden or without an executable are left out.
	struct AutostartPlan
	{
		std::vector<size_t> open;
		std::vector<size_t> ask;
	};

	AutostartPlan PlanAutostart(const std::vector<AppItem>& items, const std::function<bool(const AppItem&)>& isRunning);
}
