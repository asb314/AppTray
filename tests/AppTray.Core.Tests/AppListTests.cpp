#include "AppList.h"
#include "StringUtil.h"

#include <gtest/gtest.h>

using namespace apptray;

namespace
{
	AppEntry MakeEntry(std::wstring id, std::wstring category, std::wstring name, std::wstring firstSeen = L"T0")
	{
		AppEntry entry;
		entry.id = std::move(id);
		entry.category = std::move(category);
		entry.name = std::move(name);
		entry.target = entry.id + L"/app.exe";
		entry.firstSeen = std::move(firstSeen);
		return entry;
	}

	Catalog MakeCatalog()
	{
		Catalog catalog;
		catalog.baselineAt = L"2026-11-01T10:00:00Z";
		catalog.apps = {
			MakeEntry(L"Media/Audio/Música", L"Media/Audio", L"Música Ñandú", catalog.baselineAt),
			MakeEntry(L"Media/Video/Cut", L"Media/Video", L"Cutter", L"2026-11-20T10:00:00Z"),
			MakeEntry(L"Utils/Old", L"Utils", L"Old tool", L"2026-09-01T10:00:00Z"),
			MakeEntry(L"Utils/Setup", L"Utils", L"Setup thing", catalog.baselineAt),
			MakeEntry(L"Utils/Copy_", L"Utils", L"Copy", catalog.baselineAt),
		};
		catalog.apps[3].kind = AppKind::Installer;
		catalog.apps[4].alternate = true;
		return catalog;
	}

	std::vector<std::wstring> NamesOf(const std::vector<AppItem>& items, const std::vector<size_t>& indices)
	{
		std::vector<std::wstring> names;
		for (const size_t index : indices)
		{
			names.push_back(items[index].name);
		}
		return names;
	}
}

TEST(StringUtil, Should_FoldCaseAndAccents)
{
	EXPECT_EQ(FoldForSearch(L"Música ÑANDÚ"), L"musica nandu");
}

TEST(StringUtil, Should_ParseUtcTimestamps)
{
	const std::optional<std::int64_t> a = ParseUtcSeconds(L"2026-11-21T10:00:00Z");
	const std::optional<std::int64_t> b = ParseUtcSeconds(L"2026-11-22T10:00:00Z");
	ASSERT_TRUE(a && b);
	EXPECT_EQ(*b - *a, 86400);
	EXPECT_FALSE(ParseUtcSeconds(L"yesterday").has_value());
}

TEST(AppList, Should_ApplyOverridesAndFlags)
{
	const Catalog catalog = MakeCatalog();
	UserSettings settings;
	settings.favorites = {L"Utils/Old"};
	settings.apps[L"Media/Video/Cut"].name = L"Video cutter";
	settings.apps[L"Media/Video/Cut"].args = L"--fast";
	settings.apps[L"Utils/Old"].hidden = true;

	const std::vector<AppItem> items = BuildAppItems(catalog, settings, L"2026-11-21T10:00:00Z");

	ASSERT_EQ(items.size(), 5u);
	EXPECT_EQ(items[1].name, L"Video cutter");
	EXPECT_EQ(items[1].args, L"--fast");
	EXPECT_TRUE(items[1].isNew);   // found after the first scan, one day ago
	EXPECT_FALSE(items[0].isNew);  // found by the first scan
	EXPECT_FALSE(items[2].isNew);  // found more than 30 days ago
	EXPECT_TRUE(items[2].favorite);
	EXPECT_TRUE(items[2].hidden);
	EXPECT_TRUE(items[3].hiddenByDefault); // installer
	EXPECT_TRUE(items[4].hiddenByDefault); // backup copy
}

TEST(AppList, Should_FilterByKindCategoryAndAccentInsensitiveSearch)
{
	const Catalog catalog = MakeCatalog();
	UserSettings settings;
	settings.apps[L"Utils/Old"].hidden = true;
	const std::vector<AppItem> items = BuildAppItems(catalog, settings, L"2026-11-21T10:00:00Z");

	EXPECT_EQ(NamesOf(items, FilterAppItems(items, {}, {})), (std::vector<std::wstring>{L"Cutter", L"Música Ñandú"}));
	EXPECT_EQ(
		NamesOf(items, FilterAppItems(items, {AppFilterKind::Category, L"Media/Audio", L""}, {})),
		(std::vector<std::wstring>{L"Música Ñandú"}));
	EXPECT_EQ(NamesOf(items, FilterAppItems(items, {AppFilterKind::All, L"", L"MUSICA nan"}, {})),
		(std::vector<std::wstring>{L"Música Ñandú"}));
	EXPECT_EQ(NamesOf(items, FilterAppItems(items, {AppFilterKind::Hidden, L"", L""}, {})),
		(std::vector<std::wstring>{L"Old tool"}));
	EXPECT_EQ(FilterAppItems(items, {AppFilterKind::Installers, L"", L""}, {}).size(), 2u);
	EXPECT_EQ(FilterAppItems(items, {AppFilterKind::New, L"", L""}, {}).size(), 1u);
	// "Media/Aud" is not the category "Media/Audio"
	EXPECT_TRUE(FilterAppItems(items, {AppFilterKind::Category, L"Media/Aud", L""}, {}).empty());
}

TEST(AppList, Should_PutNamesStartingWithTheSearchFirst_And_KeepFavoritesOrder)
{
	Catalog catalog;
	catalog.apps = {
		MakeEntry(L"A/Zip", L"A", L"Zip tool"),
		MakeEntry(L"A/Tools", L"A", L"Archive zip"),
		MakeEntry(L"A/Paint", L"A", L"Paint"),
	};
	UserSettings settings;
	settings.favorites = {L"A/Paint", L"A/Zip"};
	const std::vector<AppItem> items = BuildAppItems(catalog, settings, L"2026-11-21T10:00:00Z");

	EXPECT_EQ(NamesOf(items, FilterAppItems(items, {AppFilterKind::All, L"", L"zip"}, settings.favorites)),
		(std::vector<std::wstring>{L"Zip tool", L"Archive zip"}));
	EXPECT_EQ(NamesOf(items, FilterAppItems(items, {AppFilterKind::Favorites, L"", L""}, settings.favorites)),
		(std::vector<std::wstring>{L"Paint", L"Zip tool"}));
}

TEST(AppList, Should_BuildSortedCategoryTreeWithCounts)
{
	const Catalog catalog = MakeCatalog();
	const std::vector<AppItem> items = BuildAppItems(catalog, {}, L"2026-11-21T10:00:00Z");

	const CategoryNode root = BuildCategoryTree(items);

	EXPECT_EQ(root.count, 3); // the installer and the copy are not counted
	ASSERT_EQ(root.children.size(), 2u);
	EXPECT_EQ(root.children[0].name, L"Media");
	EXPECT_EQ(root.children[0].count, 2);
	ASSERT_EQ(root.children[0].children.size(), 2u);
	EXPECT_EQ(root.children[0].children[0].path, L"Media/Audio");
	EXPECT_EQ(root.children[1].name, L"Utils");
}

TEST(AppList, Should_CutCategoryTreeAtMaxDepth)
{
	Catalog catalog;
	catalog.apps = {
		MakeEntry(L"Media/Audio/Editor/Wave", L"Media/Audio/Editor", L"Wave"),
		MakeEntry(L"Media/Audio/Tag", L"Media/Audio", L"Tag"),
		MakeEntry(L"Media/Player", L"Media", L"Player"),
	};
	const std::vector<AppItem> items = BuildAppItems(catalog, {}, L"2026-11-21T10:00:00Z");

	const CategoryNode top = BuildCategoryTree(items, 1);
	ASSERT_EQ(top.children.size(), 1u);
	EXPECT_EQ(top.children[0].count, 3);       // everything below Media counts in Media
	EXPECT_TRUE(top.children[0].children.empty());

	const CategoryNode two = BuildCategoryTree(items, 2);
	ASSERT_EQ(two.children[0].children.size(), 1u);
	EXPECT_EQ(two.children[0].children[0].path, L"Media/Audio");
	EXPECT_EQ(two.children[0].children[0].count, 2); // Editor's app counts in Audio
	EXPECT_TRUE(two.children[0].children[0].children.empty());

	EXPECT_EQ(BuildCategoryTree(items).children[0].children[0].children.size(), 1u); // all levels
}

TEST(AppList, Should_PruneCategoryTreeKeepingPathsAndSubtrees)
{
	Catalog catalog;
	catalog.apps = {
		MakeEntry(L"Media/Audio/Editor/Wave", L"Media/Audio/Editor", L"Wave"),
		MakeEntry(L"Media/Audio/Tags/Tag", L"Media/Audio/Tags", L"Tag"),
		MakeEntry(L"Media/Video/Editor/Cut", L"Media/Video/Editor", L"Cut"),
		MakeEntry(L"Editor/LaTeX/TeX", L"Editor/LaTeX", L"TeX"),
		MakeEntry(L"Utils/Zip", L"Utils", L"Zip"),
	};
	const CategoryNode tree = BuildCategoryTree(BuildAppItems(catalog, {}, L"2026-11-21T10:00:00Z"));

	const CategoryNode edit = FilterCategoryTree(tree, L"EDIT");
	ASSERT_EQ(edit.children.size(), 2u); // Editor (matches) and Media (holds matches); not Utils
	EXPECT_EQ(edit.children[0].path, L"Editor");
	ASSERT_EQ(edit.children[0].children.size(), 1u); // a matching category keeps its subcategories
	EXPECT_EQ(edit.children[0].children[0].path, L"Editor/LaTeX");
	const CategoryNode& media = edit.children[1];
	ASSERT_EQ(media.children.size(), 2u); // Audio and Video, each only with its Editor
	EXPECT_EQ(media.children[0].children.size(), 1u);
	EXPECT_EQ(media.children[0].children[0].path, L"Media/Audio/Editor");

	// Words across levels: only the audio editor
	const CategoryNode audioEditor = FilterCategoryTree(tree, L"audio edit");
	ASSERT_EQ(audioEditor.children.size(), 1u);
	ASSERT_EQ(audioEditor.children[0].children.size(), 1u);
	ASSERT_EQ(audioEditor.children[0].children[0].children.size(), 1u);
	EXPECT_EQ(audioEditor.children[0].children[0].children[0].path, L"Media/Audio/Editor");

	// Written as a path, with either slash
	EXPECT_EQ(FilterCategoryTree(tree, L"media/audio").children.size(), 1u);
	EXPECT_TRUE(MatchesCategorySearch(L"Media/Audio/Editor", L"Media\\Audio"));

	EXPECT_TRUE(FilterCategoryTree(tree, L"nothing-like-this").children.empty());
	EXPECT_EQ(FilterCategoryTree(tree, L"  ").children.size(), tree.children.size());
	EXPECT_TRUE(MatchesCategorySearch(L"Música/Edición", L"musica edicion"));
}

TEST(UserSettings, Should_ToggleFavoritesAndDropEmptyOverrides)
{
	UserSettings settings;
	ToggleFavorite(settings, L"A/a");
	EXPECT_TRUE(settings.IsFavorite(L"A/a"));
	ToggleFavorite(settings, L"A/a");
	EXPECT_FALSE(settings.IsFavorite(L"A/a"));

	SetHidden(settings, L"A/a", true);
	SetTargetOverride(settings, L"A/a", L"A/a/other.exe");
	EXPECT_TRUE(settings.apps.contains(L"A/a"));
	SetHidden(settings, L"A/a", false);
	SetTargetOverride(settings, L"A/a", std::nullopt);
	EXPECT_FALSE(settings.apps.contains(L"A/a"));
}

TEST(AppList, Should_ListAutostartApps_AlwaysBeforeAsk_WithoutHiddenOnes)
{
	Catalog catalog = MakeCatalog();
	UserSettings settings;
	SetAutostart(settings, L"Media/Video/Cut", AutostartMode::Ask);
	SetAutostart(settings, L"Utils/Old", AutostartMode::Always);
	SetAutostart(settings, L"Utils/Setup", AutostartMode::Always); // an installer still counts
	SetAutostart(settings, L"Media/Audio/Música", AutostartMode::Always);
	SetHidden(settings, L"Media/Audio/Música", true); // hidden: never opened at logon
	catalog.apps[2].missing = true;                    // missing: listed, so it can be turned off
	catalog.apps.push_back(MakeEntry(L"Utils/Suite", L"Utils", L"Suite", catalog.baselineAt));
	catalog.apps.back().target.clear();                // a suite opens a folder: never at logon
	SetAutostart(settings, L"Utils/Suite", AutostartMode::Always);
	const std::vector<AppItem> items = BuildAppItems(catalog, settings, L"2026-11-21T12:00:00Z");

	AppFilter autostart;
	autostart.kind = AppFilterKind::Autostart;
	EXPECT_EQ(
		NamesOf(items, FilterAppItems(items, autostart, {})),
		(std::vector<std::wstring>{L"Old tool", L"Setup thing", L"Cutter"}));
	EXPECT_EQ(CountAppItems(items, AppFilterKind::Autostart), 3u);

	autostart.search = L"cut";
	EXPECT_EQ(NamesOf(items, FilterAppItems(items, autostart, {})), (std::vector<std::wstring>{L"Cutter"}));
}
