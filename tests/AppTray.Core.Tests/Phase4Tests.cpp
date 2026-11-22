// Phase 4: hotkey, recent apps and usage ranking, initials search, settings that follow a new
// version folder, shortcuts, versions, user.json.bak

#include "AppList.h"
#include "Catalog.h"
#include "CatalogMerger.h"
#include "CommandLine.h"
#include "ExeInspector.h"
#include "FolderScanner.h"
#include "Hotkey.h"
#include "NameTokens.h"
#include "ConsoleCommand.h"
#include "Shortcut.h"
#include "UsageStats.h"
#include "UserSettings.h"

#include "TestTree.h"

#include <windows.h>

#include <objbase.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <format>

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

	const AppEntry* FindEntry(const Catalog& catalog, std::wstring_view id)
	{
		const auto found = std::find_if(
			catalog.apps.begin(), catalog.apps.end(), [&](const AppEntry& entry) { return entry.id == id; });
		return found == catalog.apps.end() ? nullptr : &*found;
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

	std::vector<std::wstring> Search(const std::vector<AppItem>& items, std::wstring search)
	{
		AppFilter filter;
		filter.search = std::move(search);
		return NamesOf(items, FilterAppItems(items, filter, {}));
	}
}

TEST(NameTokens, Should_TidyVersionStrings)
{
	EXPECT_EQ(NormalizeVersion(L"1, 2, 3, 0"), L"1.2.3");
	EXPECT_EQ(NormalizeVersion(L"4.0.0.0"), L"4.0.0");
	EXPECT_EQ(NormalizeVersion(L"0.3.0"), L"0.3.0");
	EXPECT_EQ(NormalizeVersion(L" v25.1.0 (build 7) "), L"25.1.0");
	EXPECT_EQ(NormalizeVersion(L"Version 6.0a (Build 150)"), L"6.0a");
	EXPECT_EQ(NormalizeVersion(L"Release 0.85"), L"0.85");
	EXPECT_EQ(NormalizeVersion(L"2026.10.21 on Python 3.10.11"), L"2026.10.21");
	EXPECT_EQ(NormalizeVersion(L"8.9.8.1"), L"8.9.8.1");
	EXPECT_EQ(NormalizeVersion(L"0.1.3+8909da14b783e9625bfabc7ab2cf16e233"), L"0.1.3");
	EXPECT_EQ(NormalizeVersion(L"10.0.6.906-win-x64+f30bee841311b4063c3c9"), L"10.0.6.906");
	EXPECT_EQ(NormalizeVersion(L"1.2rc1"), L"1.2rc1");
	EXPECT_EQ(NormalizeVersion(L"BadCopy Pro"), L""); // a name: the next source is tried
	EXPECT_EQ(NormalizeVersion(L"0.0.0.0"), L"");
	EXPECT_EQ(NormalizeVersion(L"   "), L"");
	EXPECT_EQ(NormalizeVersion(L"2024a"), L"2024a");
}

TEST(NameTokens, Should_FindTheVersionInAName)
{
	EXPECT_EQ(FindVersionInName(L"QGIS 4.0.0 light"), L"4.0.0");
	EXPECT_EQ(FindVersionInName(L"rufus-4.15p"), L"4.15");
	EXPECT_EQ(FindVersionInName(L"GIMPPortable_3.2.6"), L"3.2.6");
	EXPECT_EQ(FindVersionInName(L"HandBrake_1"), L"");
	EXPECT_EQ(FindVersionInName(L"7-Zip"), L"");
	EXPECT_EQ(FindVersionInName(L"x64 tool 2.5."), L"2.5");
}

TEST(ExeInspector, Should_ReadTheProductVersion)
{
	wchar_t windows[MAX_PATH]{};
	GetWindowsDirectoryW(windows, MAX_PATH);
	const ExeInfo info = InspectExe(std::filesystem::path(windows) / L"System32" / L"cmd.exe");
	ASSERT_FALSE(info.version.empty());
	EXPECT_TRUE(info.version[0] >= L'1' && info.version[0] <= L'9') << info.version;
}

TEST(FolderScanner, Should_TakeTheVersionFromTheFolderName_When_TheTargetIsAScript)
{
	TestTree tree;
	tree.Add(L"Image/GIS/QGIS 4.0.0 light/launch_qgis_portable.bat");
	tree.Add(L"Image/GIS/QGIS 4.0.0 light/bin/qgis_core.dll");

	ScanOptions options;
	options.root = tree.Root();
	const ScanResult result = ScanFolders(options);

	ASSERT_EQ(result.apps.size(), 1u);
	EXPECT_EQ(result.apps[0].version, L"4.0.0");
}

TEST(Hotkey, Should_ParseModifiersAndKeys_InEnglishOrSpanish)
{
	const std::optional<Hotkey> space = ParseHotkey(L"Ctrl+Alt+Space");
	ASSERT_TRUE(space.has_value());
	EXPECT_EQ(space->modifiers, static_cast<unsigned int>(MOD_CONTROL | MOD_ALT));
	EXPECT_EQ(space->key, static_cast<unsigned int>(VK_SPACE));
	EXPECT_EQ(FormatHotkey(*space), L"Ctrl+Alt+Espacio");

	const std::optional<Hotkey> spanish = ParseHotkey(L" win + MAYÚS + a ");
	ASSERT_TRUE(spanish.has_value());
	EXPECT_EQ(spanish->modifiers, static_cast<unsigned int>(MOD_WIN | MOD_SHIFT));
	EXPECT_EQ(spanish->key, static_cast<unsigned int>('A'));
	EXPECT_EQ(FormatHotkey(*spanish), L"Mayús+Win+A");
	EXPECT_EQ(FormatHotkey(*spanish, false), L"Shift+Win+A");
	EXPECT_EQ(FormatHotkey(*space, false), L"Ctrl+Alt+Space");

	const std::optional<Hotkey> function = ParseHotkey(L"Ctrl+F12");
	ASSERT_TRUE(function.has_value());
	EXPECT_EQ(function->key, static_cast<unsigned int>(VK_F12));
	EXPECT_EQ(FormatHotkey(*function), L"Ctrl+F12");
}

TEST(Hotkey, Should_RejectUnknownPartsAndKeysWithoutCtrlAltOrWin)
{
	EXPECT_FALSE(ParseHotkey(L"").has_value());
	EXPECT_FALSE(ParseHotkey(L"Space").has_value());
	EXPECT_FALSE(ParseHotkey(L"Shift+A").has_value());
	EXPECT_FALSE(ParseHotkey(L"Ctrl+Hyper+A").has_value());
	EXPECT_FALSE(ParseHotkey(L"Ctrl+F25").has_value());
	EXPECT_FALSE(ParseHotkey(L"Ctrl+").has_value());
	EXPECT_FALSE(ParseHotkey(L"Ctrl+AB").has_value());
}

TEST(UsageStats, Should_RecordMoveAndRoundTrip)
{
	UsageStats stats;
	RecordLaunch(stats, L"Media/Player/VLC", L"2026-11-20T10:00:00Z");
	RecordLaunch(stats, L"media\\player\\vlc", L"2026-11-21T10:00:00Z");
	const AppUsage* vlc = stats.Find(L"Media/Player/VLC");
	ASSERT_NE(vlc, nullptr);
	EXPECT_EQ(vlc->count, 2);
	EXPECT_EQ(vlc->lastLaunched, L"2026-11-21T10:00:00Z");

	RecordLaunch(stats, L"GIS/QGIS 4.1", L"2026-11-19T10:00:00Z");
	RecordLaunch(stats, L"GIS/QGIS 4.0", L"2026-11-18T10:00:00Z");
	EXPECT_TRUE(MoveUsage(stats, L"GIS/QGIS 4.0", L"GIS/QGIS 4.1"));
	EXPECT_FALSE(MoveUsage(stats, L"GIS/QGIS 4.0", L"GIS/QGIS 4.1"));
	EXPECT_EQ(stats.Find(L"GIS/QGIS 4.0"), nullptr);
	ASSERT_NE(stats.Find(L"GIS/QGIS 4.1"), nullptr);
	EXPECT_EQ(stats.Find(L"GIS/QGIS 4.1")->count, 2);
	EXPECT_EQ(stats.Find(L"GIS/QGIS 4.1")->lastLaunched, L"2026-11-19T10:00:00Z");

	TestTree tree;
	const std::filesystem::path file = tree.Root() / L"usage.json";
	std::wstring error;
	ASSERT_TRUE(SaveUsageStats(file, stats, error)) << error;
	const UsageStats loaded = LoadUsageStats(file);
	EXPECT_EQ(loaded.apps.size(), 2u);
	ASSERT_NE(loaded.Find(L"Media/Player/VLC"), nullptr);
	EXPECT_EQ(loaded.Find(L"Media/Player/VLC")->count, 2);
	EXPECT_TRUE(LoadUsageStats(tree.Root() / L"missing.json").apps.empty());
}

TEST(AppList, Should_MatchTheStartOfTheInitials)
{
	Catalog catalog;
	catalog.apps = {
		MakeApp(L"Image/FastStone", L"Image", L"FastStone Image Viewer"),
		MakeApp(L"Dev/VSCode", L"Dev", L"Visual Studio Code"),
		MakeApp(L"Dev/Vim", L"Dev", L"Vim"),
	};
	const std::vector<AppItem> items = BuildAppItems(catalog, {}, L"2026-11-21T10:00:00Z");

	EXPECT_EQ(Search(items, L"fsiv"), std::vector<std::wstring>{L"FastStone Image Viewer"});
	EXPECT_EQ(Search(items, L"FIV"), std::vector<std::wstring>{L"FastStone Image Viewer"});
	EXPECT_EQ(Search(items, L"vsc"), std::vector<std::wstring>{L"Visual Studio Code"});
	// One letter is never taken as initials: "v" matches by text only (both contain a v)
	const std::vector<std::wstring> v = Search(items, L"v");
	EXPECT_EQ(v.size(), 3u);
	// "vs": the initials of Visual Studio Code (and its id); Vim has neither
	EXPECT_EQ(Search(items, L"vs"), std::vector<std::wstring>{L"Visual Studio Code"});
}

TEST(AppList, Should_PutNamesStartingWithTheWordBeforeInitialsThatDo)
{
	Catalog catalog;
	catalog.apps = {
		MakeApp(L"Image/FastStone", L"Image", L"FastStone Image Viewer"), // initials "fiv"
		MakeApp(L"Browser/Firefox", L"Browser", L"Firefox"),
	};
	const std::vector<AppItem> items = BuildAppItems(catalog, {}, L"2026-11-21T10:00:00Z");
	EXPECT_EQ(Search(items, L"fi"), (std::vector<std::wstring>{L"Firefox", L"FastStone Image Viewer"}));
}

TEST(AppList, Should_PutTheMostOpenedFirst_OnlyWhileSearching)
{
	Catalog catalog;
	catalog.apps = {
		MakeApp(L"Net/FileZilla", L"Net", L"FileZilla"),
		MakeApp(L"Browser/Firefox", L"Browser", L"Firefox"),
		MakeApp(L"Utils/Profiler", L"Utils", L"Profiler"),
	};
	UsageStats usage;
	for (int i = 0; i < 3; ++i)
	{
		RecordLaunch(usage, L"Browser/Firefox", L"2026-11-21T10:00:00Z");
	}
	RecordLaunch(usage, L"Utils/Profiler", L"2026-11-21T11:00:00Z");
	const std::vector<AppItem> items = BuildAppItems(catalog, {}, L"2026-11-21T12:00:00Z", &usage);

	// "fi": both names start with it; Firefox was opened more. Profiler only contains "fi".
	EXPECT_EQ(Search(items, L"fi"), (std::vector<std::wstring>{L"Firefox", L"FileZilla", L"Profiler"}));
	// No search: plain name order
	EXPECT_EQ(Search(items, L""), (std::vector<std::wstring>{L"FileZilla", L"Firefox", L"Profiler"}));
}

TEST(AppList, Should_ListRecentAppsLastOpenedFirst_WithoutHiddenOnes)
{
	Catalog catalog;
	for (int i = 0; i < 20; ++i)
	{
		catalog.apps.push_back(MakeApp(std::format(L"A/app{:02}", i), L"A", std::format(L"app{:02}", i)));
	}
	UsageStats usage;
	for (int i = 0; i < 18; ++i)
	{
		RecordLaunch(usage, std::format(L"A/app{:02}", i), std::format(L"2026-11-21T10:00:{:02}Z", i));
	}
	UserSettings settings;
	SetHidden(settings, L"A/app17", true);
	const std::vector<AppItem> items = BuildAppItems(catalog, settings, L"2026-11-21T12:00:00Z", &usage);

	AppFilter recent;
	recent.kind = AppFilterKind::Recent;
	const std::vector<std::wstring> names = NamesOf(items, FilterAppItems(items, recent, {}));
	ASSERT_EQ(names.size(), RecentAppLimit);
	EXPECT_EQ(names.front(), L"app16"); // app17 is hidden
	EXPECT_EQ(names.back(), L"app02");
	EXPECT_EQ(CountAppItems(items, AppFilterKind::Recent), RecentAppLimit);
}

TEST(CatalogMerger, Should_RecognizeANewVersionInARenamedFolder)
{
	AppEntry oldQgis = MakeApp(L"Image/GIS/QGIS 4.0.0 light", L"Image/GIS", L"QGIS 4.0.0 light");
	const Catalog previous = MergeCatalog({oldQgis, MakeApp(L"Image/GIS/Other", L"Image/GIS", L"Other")}, std::nullopt, {}, L"C:\\Apps", L"T1");
	UserSettings settings;
	ToggleFavorite(settings, L"Image/GIS/QGIS 4.0.0 light");

	const Catalog catalog = MergeCatalog(
		{MakeApp(L"Image/GIS/QGIS 4.1.2 light", L"Image/GIS", L"QGIS 4.1.2 light"), MakeApp(L"Image/GIS/Other", L"Image/GIS", L"Other")},
		previous, settings, L"C:\\Apps", L"T2");

	ASSERT_EQ(catalog.apps.size(), 2u); // the old folder is not kept as missing
	const AppEntry* qgis = FindEntry(catalog, L"Image/GIS/QGIS 4.1.2 light");
	ASSERT_NE(qgis, nullptr);
	EXPECT_EQ(qgis->firstSeen, L"T1"); // not "new"
	EXPECT_EQ(qgis->renamedFrom, L"Image/GIS/QGIS 4.0.0 light");

	// Until the tray moves the favorite, the next scans keep pointing at the old id
	const Catalog again = MergeCatalog(
		{MakeApp(L"Image/GIS/QGIS 4.1.2 light", L"Image/GIS", L"QGIS 4.1.2 light")}, catalog, settings, L"C:\\Apps", L"T3");
	EXPECT_EQ(FindEntry(again, L"Image/GIS/QGIS 4.1.2 light")->renamedFrom, L"Image/GIS/QGIS 4.0.0 light");
	// Once it moved, the pointer goes
	UserSettings movedSettings = settings;
	MoveRenamedAppSettings(movedSettings, again);
	const Catalog later = MergeCatalog(
		{MakeApp(L"Image/GIS/QGIS 4.1.2 light", L"Image/GIS", L"QGIS 4.1.2 light")}, again, movedSettings, L"C:\\Apps", L"T4");
	EXPECT_TRUE(FindEntry(later, L"Image/GIS/QGIS 4.1.2 light")->renamedFrom.empty());
}

TEST(CatalogMerger, Should_NotPairRenamedFolders_When_ItIsAmbiguousOrInAnotherCategory)
{
	const Catalog previous = MergeCatalog(
		{MakeApp(L"Dev/Python 3.12", L"Dev", L"Python 3.12"), MakeApp(L"Net/Tool 1.0", L"Net", L"Tool 1.0")}, std::nullopt, {},
		L"C:\\Apps", L"T1");
	UserSettings settings;
	ToggleFavorite(settings, L"Dev/Python 3.12");
	ToggleFavorite(settings, L"Net/Tool 1.0");

	const Catalog catalog = MergeCatalog(
		{MakeApp(L"Dev/Python 3.13", L"Dev", L"Python 3.13"), MakeApp(L"Dev/Python 3.14", L"Dev", L"Python 3.14"),
		 MakeApp(L"Utils/Tool 2.0", L"Utils", L"Tool 2.0")},
		previous, settings, L"C:\\Apps", L"T2");

	// Two candidates for Python, Tool moved to another category: new apps plus missing ones
	EXPECT_EQ(FindEntry(catalog, L"Dev/Python 3.13")->firstSeen, L"T2");
	EXPECT_TRUE(FindEntry(catalog, L"Dev/Python 3.13")->renamedFrom.empty());
	EXPECT_TRUE(FindEntry(catalog, L"Utils/Tool 2.0")->renamedFrom.empty());
	ASSERT_NE(FindEntry(catalog, L"Dev/Python 3.12"), nullptr);
	EXPECT_TRUE(FindEntry(catalog, L"Dev/Python 3.12")->missing);
	EXPECT_TRUE(FindEntry(catalog, L"Net/Tool 1.0")->missing);
}

TEST(CatalogMerger, Should_MoveFavoritesAndOverridesToTheRenamedFolder)
{
	AppEntry renamed = MakeApp(L"GIS/QGIS 4.1 light", L"GIS", L"QGIS 4.1 light");
	renamed.target = L"GIS/QGIS 4.1 light/launch.bat";
	renamed.alternatives = {L"GIS/QGIS 4.1 light/bin/qgis-bin.exe"};
	renamed.renamedFrom = L"GIS/QGIS 4.0 light";
	Catalog catalog;
	catalog.apps = {renamed};

	UserSettings settings;
	settings.favorites = {L"First", L"GIS/QGIS 4.0 light", L"Last"};
	SetTargetOverride(settings, L"GIS/QGIS 4.0 light", L"GIS/QGIS 4.0 light/bin/qgis-bin.exe");
	SetAutostart(settings, L"GIS/QGIS 4.0 light", AutostartMode::Ask);
	SetElevatedTask(settings, L"GIS/QGIS 4.0 light", true);
	settings.document = Json::parse(R"({"apps": {"GIS/QGIS 4.0 light": {"note": "mine"}}})");

	const std::vector<RenamedApp> moved = MoveRenamedAppSettings(settings, catalog);

	ASSERT_EQ(moved.size(), 1u);
	EXPECT_EQ(moved[0].to, L"GIS/QGIS 4.1 light");
	EXPECT_TRUE(moved[0].elevatedTaskDropped);
	EXPECT_EQ(settings.favorites, (std::vector<std::wstring>{L"First", L"GIS/QGIS 4.1 light", L"Last"}));
	EXPECT_FALSE(settings.HasUserData(L"GIS/QGIS 4.0 light"));
	const AppOverride* item = settings.FindOverride(L"GIS/QGIS 4.1 light");
	ASSERT_NE(item, nullptr);
	EXPECT_EQ(item->target, L"GIS/QGIS 4.1 light/bin/qgis-bin.exe");
	EXPECT_EQ(item->autostart, AutostartMode::Ask);
	EXPECT_FALSE(item->elevatedTask);
	EXPECT_TRUE(settings.document["apps"].contains("GIS/QGIS 4.1 light"));
	EXPECT_FALSE(settings.document["apps"].contains("GIS/QGIS 4.0 light"));
	EXPECT_TRUE(MoveRenamedAppSettings(settings, catalog).empty()); // nothing left to move
}

TEST(CatalogMerger, Should_DropAnotherExecutable_When_TheNewFolderDoesNotHaveIt)
{
	AppEntry renamed = MakeApp(L"Tools/Foo 2", L"Tools", L"Foo 2");
	renamed.renamedFrom = L"Tools/Foo 1";
	Catalog catalog;
	catalog.apps = {renamed};
	UserSettings settings;
	SetTargetOverride(settings, L"Tools/Foo 1", L"Tools/Foo 1/old-only.exe");
	SetHidden(settings, L"Tools/Foo 1", true);

	MoveRenamedAppSettings(settings, catalog);

	const AppOverride* item = settings.FindOverride(L"Tools/Foo 2");
	ASSERT_NE(item, nullptr);
	EXPECT_FALSE(item->target.has_value());
	EXPECT_TRUE(item->hidden);
}

TEST(CatalogMerger, Should_KeepTheNewOverride_When_TheNewFolderHasOneAlready)
{
	AppEntry renamed = MakeApp(L"Tools/Foo 2", L"Tools", L"Foo 2");
	renamed.renamedFrom = L"Tools/Foo 1";
	Catalog catalog;
	catalog.apps = {renamed};
	UserSettings settings;
	SetHidden(settings, L"Tools/Foo 1", true);
	SetAutostart(settings, L"Tools/Foo 2", AutostartMode::Always);

	MoveRenamedAppSettings(settings, catalog);

	EXPECT_EQ(settings.FindOverride(L"Tools/Foo 1"), nullptr);
	ASSERT_NE(settings.FindOverride(L"Tools/Foo 2"), nullptr);
	EXPECT_FALSE(settings.FindOverride(L"Tools/Foo 2")->hidden);
	EXPECT_EQ(settings.FindOverride(L"Tools/Foo 2")->autostart, AutostartMode::Always);
}

TEST(CatalogMerger, Should_ReportADroppedElevatedTask_When_TheNewOverrideWins)
{
	AppEntry renamed = MakeApp(L"Tools/Foo 2", L"Tools", L"Foo 2");
	renamed.renamedFrom = L"Tools/Foo 1";
	Catalog catalog;
	catalog.apps = {renamed};
	UserSettings settings;
	SetElevatedTask(settings, L"Tools/Foo 1", true);
	SetHidden(settings, L"Tools/Foo 2", true);

	const std::vector<RenamedApp> moved = MoveRenamedAppSettings(settings, catalog);

	ASSERT_EQ(moved.size(), 1u);
	EXPECT_TRUE(moved[0].elevatedTaskDropped);
	EXPECT_FALSE(settings.FindOverride(L"Tools/Foo 2")->elevatedTask);
}

TEST(Catalog, Should_KeepVersionAndRenamedFrom)
{
	TestTree tree;
	Catalog catalog;
	AppEntry entry = MakeApp(L"A/a 2", L"A", L"a");
	entry.version = L"2.0.1";
	entry.renamedFrom = L"A/a 1";
	catalog.apps = {entry};
	const std::filesystem::path file = tree.Root() / L"catalog.json";
	std::wstring error;
	ASSERT_TRUE(SaveCatalog(file, catalog, error)) << error;
	const std::optional<Catalog> loaded = LoadCatalog(file, error);
	ASSERT_TRUE(loaded.has_value()) << error;
	EXPECT_EQ(loaded->apps[0].version, L"2.0.1");
	EXPECT_EQ(loaded->apps[0].renamedFrom, L"A/a 1");
}

TEST(UserSettings, Should_DefaultTheHotkey_And_KeepAnEmptyOne)
{
	TestTree tree;
	const std::filesystem::path file = tree.Root() / L"user.json";
	std::wstring error;
	UserSettings settings = LoadUserSettings(file, error);
	EXPECT_EQ(settings.hotkey, L"Ctrl+Alt+Space");
	EXPECT_EQ(SerializeUserSettings(settings)["hotkey"], "Ctrl+Alt+Space");

	settings.hotkey.clear(); // turned off
	ASSERT_TRUE(SaveUserSettings(file, settings, error)) << error;
	EXPECT_EQ(LoadUserSettings(file, error).hotkey, L"");
}

TEST(UserSettings, Should_DefaultTheLanguageToAuto_And_KeepAChoice)
{
	TestTree tree;
	const std::filesystem::path file = tree.Root() / L"user.json";
	std::wstring error;
	UserSettings settings = LoadUserSettings(file, error);
	EXPECT_EQ(settings.language, L"auto");

	settings.language = L"en";
	ASSERT_TRUE(SaveUserSettings(file, settings, error)) << error;
	EXPECT_EQ(LoadUserSettings(file, error).language, L"en");
}

TEST(CommandLine, Should_ParseShortcutCreation)
{
	const CommandLineOptions options =
		ParseCommandLine({L"--create-shortcut", L"Media/Player/VLC", L"--shortcut-in", L"startmenu"});
	EXPECT_TRUE(options.errors.empty());
	EXPECT_EQ(options.createShortcut, L"Media/Player/VLC");
	EXPECT_EQ(options.shortcutIn, L"startmenu");
}

TEST(Shortcut, Should_MakeValidFileNamesAndParsePlaces)
{
	EXPECT_EQ(MakeShortcutFileName(L"Foo: bar?"), L"Foo bar.lnk");
	EXPECT_EQ(MakeShortcutFileName(L"A/B\\C*"), L"ABC.lnk");
	EXPECT_EQ(MakeShortcutFileName(L"Notepad++ ..."), L"Notepad++.lnk");
	EXPECT_EQ(MakeShortcutFileName(L"???"), L"App.lnk");
	EXPECT_EQ(MakeShortcutFileName(L"Con"), L"Con_.lnk");
	EXPECT_EQ(MakeShortcutFileName(L"COM1"), L"COM1_.lnk");
	EXPECT_EQ(MakeShortcutFileName(L"Company"), L"Company.lnk");
	EXPECT_EQ(ParseShortcutPlace(L"Desktop"), ShortcutPlace::Desktop);
	EXPECT_EQ(ParseShortcutPlace(L"startmenu"), ShortcutPlace::StartMenu);
	EXPECT_FALSE(ParseShortcutPlace(L"taskbar").has_value());
}

TEST(Shortcut, Should_WriteALinkFile)
{
	const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	TestTree tree;
	wchar_t windows[MAX_PATH]{};
	GetWindowsDirectoryW(windows, MAX_PATH);
	ShortcutSpec spec;
	spec.file = tree.Root() / L"cmd.lnk";
	spec.target = std::filesystem::path(windows) / L"System32" / L"cmd.exe";
	spec.arguments = L"/k echo hi";
	spec.workingDirectory = spec.target.parent_path();
	spec.description = L"Command prompt";
	std::wstring error;
	EXPECT_TRUE(WriteShortcut(spec, error)) << error;
	EXPECT_TRUE(std::filesystem::is_regular_file(spec.file));

	// The same target reuses the file; another target never replaces it
	EXPECT_EQ(ChooseShortcutFile(tree.Root(), L"cmd.lnk", spec.target), spec.file);
	const std::filesystem::path notepad = std::filesystem::path(windows) / L"System32" / L"notepad.exe";
	EXPECT_EQ(ChooseShortcutFile(tree.Root(), L"cmd.lnk", notepad), tree.Root() / L"cmd (2).lnk");

	// A script's shortcut opens PowerShell: it is the script's when its path is in the arguments
	const std::filesystem::path script = tree.Root() / L"Bob's tool.ps1";
	ShortcutSpec scriptLink;
	scriptLink.file = tree.Root() / L"Tool.lnk";
	scriptLink.target = std::filesystem::path(windows) / L"System32" / L"WindowsPowerShell" / L"v1.0" / L"powershell.exe";
	scriptLink.arguments = L"-NoExit -Command \"& " + QuoteForPowerShell(script.wstring()) + L" old args\"";
	EXPECT_TRUE(WriteShortcut(scriptLink, error)) << error;
	EXPECT_EQ(ChooseShortcutFile(tree.Root(), L"Tool.lnk", script), scriptLink.file);
	EXPECT_EQ(ChooseShortcutFile(tree.Root(), L"Tool.lnk", tree.Root() / L"other.ps1"), tree.Root() / L"Tool (2).lnk");
	EXPECT_EQ(ChooseShortcutFile(tree.Root(), L"cmd.lnk", script), tree.Root() / L"cmd (2).lnk");
	if (SUCCEEDED(com))
	{
		CoUninitialize();
	}
}
