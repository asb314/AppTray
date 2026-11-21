#include "Catalog.h"
#include "CatalogMerger.h"
#include "JsonFile.h"
#include "PathResolver.h"
#include "UserSettings.h"

#include "TestTree.h"

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>

using namespace apptray;
using apptray::tests::TestTree;

namespace
{
	AppEntry MakeApp(std::wstring id, std::wstring category, std::wstring name)
	{
		AppEntry entry;
		entry.id = std::move(id);
		entry.category = std::move(category);
		entry.name = std::move(name);
		entry.target = entry.id + L"/app.exe";
		return entry;
	}

	std::string ReadText(const std::filesystem::path& file)
	{
		std::ifstream stream(file, std::ios::binary);
		std::stringstream buffer;
		buffer << stream.rdbuf();
		return buffer.str();
	}
}

TEST(CatalogMerger, Should_SetFirstSeenAndBaseline_When_FirstScan)
{
	const Catalog catalog = MergeCatalog({MakeApp(L"B/b", L"B", L"b"), MakeApp(L"A/a", L"A", L"a")}, std::nullopt, {}, L"C:\\Apps", L"T1");

	EXPECT_EQ(catalog.baselineAt, L"T1");
	EXPECT_EQ(catalog.scannedAt, L"T1");
	ASSERT_EQ(catalog.apps.size(), 2u);
	EXPECT_EQ(catalog.apps[0].id, L"A/a"); // sorted by category
	EXPECT_EQ(catalog.apps[0].firstSeen, L"T1");
}

TEST(CatalogMerger, Should_KeepFirstSeenAndMarkMissing_When_Rescanned)
{
	Catalog previous = MergeCatalog(
		{MakeApp(L"A/kept", L"A", L"kept"), MakeApp(L"A/favorite", L"A", L"favorite"), MakeApp(L"A/gone", L"A", L"gone")},
		std::nullopt,
		{},
		L"C:\\Apps",
		L"T1");
	UserSettings settings;
	settings.favorites = {L"A/favorite"};

	const Catalog catalog = MergeCatalog({MakeApp(L"A/kept", L"A", L"kept"), MakeApp(L"A/new", L"A", L"new")}, previous, settings, L"C:\\Apps", L"T2");

	EXPECT_EQ(catalog.baselineAt, L"T1");
	ASSERT_EQ(catalog.apps.size(), 3u);
	const auto find = [&](std::wstring_view id)
	{ return std::find_if(catalog.apps.begin(), catalog.apps.end(), [&](const AppEntry& e) { return e.id == id; }); };
	EXPECT_EQ(find(L"A/kept")->firstSeen, L"T1");
	EXPECT_FALSE(find(L"A/kept")->missing);
	EXPECT_EQ(find(L"A/new")->firstSeen, L"T2");
	EXPECT_TRUE(find(L"A/favorite")->missing);
	EXPECT_EQ(find(L"A/gone"), catalog.apps.end());
}

TEST(CatalogMerger, Should_TreatExpandedSuiteProgramsAsKnown_And_DropTheSuite)
{
	AppEntry suite = MakeApp(L"U/Suite", L"U", L"Suite");
	suite.kind = AppKind::Suite;
	const Catalog previous = MergeCatalog({suite}, std::nullopt, {}, L"C:\\Apps", L"T1");
	UserSettings settings;
	SetExpanded(settings, L"U/Suite", true);

	const Catalog expanded = MergeCatalog(
		{MakeApp(L"U/Suite/a.exe", L"U/Suite", L"a"), MakeApp(L"U/Suite/b.exe", L"U/Suite", L"b")},
		previous, settings, L"C:\\Apps", L"T2");

	ASSERT_EQ(expanded.apps.size(), 2u); // the suite is not kept as "missing"
	EXPECT_EQ(expanded.apps[0].firstSeen, L"T1"); // not new: they were in the suite
	EXPECT_EQ(expanded.apps[1].firstSeen, L"T1");

	// Grouped again: the suite comes back with its programs' date, and a favorite program of
	// the expanded suite does not linger as "missing"
	UserSettings grouped;
	grouped.favorites = {L"U/Suite/a.exe"};
	const Catalog collapsed = MergeCatalog({suite}, expanded, grouped, L"C:\\Apps", L"T3");
	ASSERT_EQ(collapsed.apps.size(), 1u);
	EXPECT_EQ(collapsed.apps[0].firstSeen, L"T1");
}

TEST(CatalogMerger, Should_MarkNewApp_When_ItAppearsInsideAKnownFolder)
{
	const Catalog previous = MergeCatalog({MakeApp(L"Tools/Foo", L"Tools", L"Foo")}, std::nullopt, {}, L"C:\\Apps", L"T1");

	// Tools/Foo became a category holding a new app: that app is new (nothing was expanded)
	const Catalog catalog =
		MergeCatalog({MakeApp(L"Tools/Foo/Bar", L"Tools/Foo", L"Bar")}, previous, {}, L"C:\\Apps", L"T2");

	ASSERT_EQ(catalog.apps.size(), 1u);
	EXPECT_EQ(catalog.apps[0].firstSeen, L"T2");
}

TEST(CatalogMerger, Should_RecognizeExpandedSuite_When_IdIsWrittenWithBackslashes)
{
	AppEntry suite = MakeApp(L"U/Suite", L"U", L"Suite");
	suite.kind = AppKind::Suite;
	const Catalog previous = MergeCatalog({suite}, std::nullopt, {}, L"C:\\Apps", L"T1");
	UserSettings settings;
	settings.apps[L"U\\Suite\\"].expand = true; // as a user might type it

	const Catalog catalog =
		MergeCatalog({MakeApp(L"U/Suite/a.exe", L"U/Suite", L"a")}, previous, settings, L"C:\\Apps", L"T2");

	ASSERT_EQ(catalog.apps.size(), 1u); // no ghost "missing" suite
	EXPECT_EQ(catalog.apps[0].firstSeen, L"T1");
}

TEST(Catalog, Should_RoundTripThroughTabIndentedJson)
{
	TestTree tree;
	Catalog catalog;
	catalog.root = L"C:\\Applications";
	catalog.scannedAt = L"2026-11-21T10:00:00Z";
	catalog.baselineAt = L"2026-11-20T10:00:00Z";
	AppEntry entry = MakeApp(L"Média/Ñandú", L"Média", L"Ñandú");
	entry.kind = AppKind::Suite;
	entry.alternatives = {L"Média/Ñandú/x.exe"};
	entry.requiresAdmin = true;
	entry.alternate = true;
	entry.confidence = 70;
	entry.firstSeen = catalog.baselineAt;
	catalog.apps.push_back(entry);
	const std::filesystem::path file = tree.Root() / L"catalog.json";

	std::wstring error;
	ASSERT_TRUE(SaveCatalog(file, catalog, error)) << error;
	const std::optional<Catalog> loaded = LoadCatalog(file, error);

	ASSERT_TRUE(loaded.has_value()) << error;
	EXPECT_EQ(loaded->baselineAt, catalog.baselineAt);
	ASSERT_EQ(loaded->apps.size(), 1u);
	EXPECT_EQ(loaded->apps[0].id, L"Média/Ñandú");
	EXPECT_EQ(loaded->apps[0].kind, AppKind::Suite);
	EXPECT_EQ(loaded->apps[0].alternatives, entry.alternatives);
	EXPECT_TRUE(loaded->apps[0].requiresAdmin);
	EXPECT_TRUE(loaded->apps[0].alternate);
	EXPECT_EQ(loaded->apps[0].confidence, 70);
	EXPECT_NE(ReadText(file).find("\n\t\"apps\""), std::string::npos);
}

TEST(UserSettings, Should_ReturnDefaults_When_FileIsMissing)
{
	TestTree tree;
	std::wstring error;
	const UserSettings settings = LoadUserSettings(tree.Root() / L"user.json", error);
	EXPECT_TRUE(error.empty());
	EXPECT_EQ(settings.startupDelaySeconds, 30);
	EXPECT_EQ(settings.scan.ignore.size(), 3u);
	EXPECT_TRUE(settings.favorites.empty());
}

TEST(UserSettings, Should_KeepUnknownKeys_When_Saved)
{
	TestTree tree;
	const std::filesystem::path file = tree.Add(
		L"user.json",
		"{\n\t// written by hand\n\t\"custom\": 5,\n\t\"favorites\": [\"A/a\"],\n"
		"\t\"apps\": {\"A/a\": {\"hidden\": true, \"note\": \"mine\", \"autostart\": \"ask\"}}\n}\n");

	std::wstring error;
	UserSettings settings = LoadUserSettings(file, error);
	ASSERT_TRUE(error.empty()) << error;
	EXPECT_TRUE(settings.IsFavorite(L"A/a"));
	EXPECT_TRUE(settings.apps[L"A/a"].hidden);
	EXPECT_EQ(settings.apps[L"A/a"].autostart, AutostartMode::Ask);

	settings.favorites.push_back(L"B/b");
	settings.apps[L"A/a"].hidden = false;
	ASSERT_TRUE(SaveUserSettings(file, settings, error)) << error;

	const std::optional<Json> json = ReadJsonFile(file, error);
	ASSERT_TRUE(json.has_value()) << error;
	EXPECT_EQ((*json)["custom"], 5);
	EXPECT_EQ((*json)["apps"]["A/a"]["note"], "mine");
	EXPECT_FALSE((*json)["apps"]["A/a"].contains("hidden"));
	EXPECT_EQ((*json)["favorites"].size(), 2u);
}

TEST(UserSettings, Should_KeepHandWrittenKeys_When_AllKnownFieldsAreCleared)
{
	TestTree tree;
	const std::filesystem::path file =
		tree.Add(L"user.json", "{\"apps\": {\"A/a\": {\"hidden\": true, \"note\": \"needs licence\"}}}");
	std::wstring error;
	UserSettings settings = LoadUserSettings(file, error);

	SetHidden(settings, L"A/a", false); // the override is now empty and leaves the map
	ASSERT_TRUE(SaveUserSettings(file, settings, error)) << error;

	const std::optional<Json> json = ReadJsonFile(file, error);
	ASSERT_TRUE(json.has_value()) << error;
	EXPECT_EQ((*json)["apps"]["A/a"]["note"], "needs licence");
	EXPECT_FALSE((*json)["apps"]["A/a"].contains("hidden"));
}

TEST(UserSettings, Should_MatchIdsIgnoringCase)
{
	UserSettings settings;
	settings.apps[L"media/foo"].expand = true;

	EXPECT_TRUE(settings.IsExpanded(L"Media/Foo"));
	EXPECT_TRUE(settings.HasUserData(L"MEDIA/FOO"));
	SetExpanded(settings, L"Media/Foo", false); // clears the user's key, does not add another
	EXPECT_TRUE(settings.apps.empty());

	settings.favorites = {L"tools\\vlc"};
	EXPECT_TRUE(settings.IsFavorite(L"Tools/VLC"));
	ToggleFavorite(settings, L"Tools/VLC"); // removes the user's entry instead of adding a twin
	EXPECT_TRUE(settings.favorites.empty());
}

TEST(PathResolver, Should_NormalizeIdsWrittenEitherWay)
{
	EXPECT_EQ(NormalizeRelative(L"\\Media\\Tools\\"), L"Media/Tools");
	EXPECT_TRUE(IsSameRelative(L"media\\tools", L"Media/Tools/"));
	EXPECT_FALSE(IsSameRelative(L"Media/Tool", L"Media/Tools"));
}
