#pragma once

#include "Catalog.h"
#include "UserSettings.h"

#include <cstddef>
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
		std::wstring searchText;         // folded name, id, description and category
	};

	// Days during which an app found after the first scan counts as new
	constexpr int NewAppDays = 30;

	std::vector<AppItem> BuildAppItems(const Catalog& catalog, const UserSettings& settings, std::wstring_view now);

	enum class AppFilterKind
	{
		All,        // everything not hidden
		Favorites,  // in the user's order
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

	// Indices into items. Sorted by name; with a search, names starting with the first word
	// come first; favorites keep the user's order.
	std::vector<size_t> FilterAppItems(
		const std::vector<AppItem>& items, const AppFilter& filter, const std::vector<std::wstring>& favorites);

	struct CategoryNode
	{
		std::wstring path; // "Media/Audio"
		std::wstring name; // "Audio"
		int count = 0;     // visible apps in this category and below
		std::vector<CategoryNode> children;
	};

	// Tree of the categories of the visible apps (root has an empty path)
	CategoryNode BuildCategoryTree(const std::vector<AppItem>& items);

	// Counts for the fixed filters, to show or hide them in the UI
	size_t CountAppItems(const std::vector<AppItem>& items, AppFilterKind kind);
}
