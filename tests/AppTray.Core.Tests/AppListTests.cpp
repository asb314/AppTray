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
