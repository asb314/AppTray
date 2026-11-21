// Logon apps, elevated task names, running-process detection and the update check

#include "AppList.h"
#include "CommandLine.h"
#include "ElevatedTask.h"
#include "ProcessFinder.h"
#include "UpdateCheck.h"

#include <windows.h>

#include <gtest/gtest.h>

using namespace apptray;

TEST(UpdateCheck, Should_CompareDottedVersionsNumerically)
{
	EXPECT_LT(CompareVersions(L"0.3.0", L"0.3.1"), 0);
	EXPECT_GT(CompareVersions(L"0.10.0", L"0.9.9"), 0);
	EXPECT_EQ(CompareVersions(L"v0.3", L"0.3.0"), 0);
	EXPECT_EQ(CompareVersions(L"1.2.0-beta", L"1.2.0"), 0); // the suffix is ignored
	EXPECT_GT(CompareVersions(L"1.0.0", L""), 0);
}

TEST(UpdateCheck, Should_ReadTagAndPageFromTheLatestReleaseAnswer)
{
	const std::optional<ReleaseInfo> release = ParseLatestRelease(
		R"({"tag_name": "v0.4.2", "html_url": "https://github.com/asb314/AppTray/releases/tag/v0.4.2", "assets": []})");
	ASSERT_TRUE(release.has_value());
	EXPECT_EQ(release->version, L"0.4.2");
	EXPECT_EQ(release->url, L"https://github.com/asb314/AppTray/releases/tag/v0.4.2");
	EXPECT_FALSE(ParseLatestRelease(R"({"message": "Not Found"})").has_value());
	EXPECT_FALSE(ParseLatestRelease("not json").has_value());
}

TEST(AutostartPlan, Should_OpenAlwaysAppsAndAskTheRest_SkippingRunningOnes)
{
	Catalog catalog;
	for (const wchar_t* id : {L"A/always", L"A/ask", L"A/running", L"A/none", L"A/hidden", L"A/suite"})
	{
		AppEntry entry;
		entry.id = id;
		entry.name = id;
		entry.target = std::wstring(id) + L"/app.exe";
		catalog.apps.push_back(entry);
	}
	catalog.apps[5].target.clear(); // a suite opens a folder: never at logon
	UserSettings settings;
	settings.apps[L"A/always"].autostart = AutostartMode::Always;
	settings.apps[L"A/ask"].autostart = AutostartMode::Ask;
	settings.apps[L"A/running"].autostart = AutostartMode::Always;
	settings.apps[L"A/hidden"].autostart = AutostartMode::Always;
	settings.apps[L"A/hidden"].hidden = true;
	settings.apps[L"A/suite"].autostart = AutostartMode::Always;
	const std::vector<AppItem> items = BuildAppItems(catalog, settings, L"2026-11-21T10:00:00Z");

	const AutostartPlan plan =
		PlanAutostart(items, [](const AppItem& item) { return item.entry->id == L"A/running"; });

	ASSERT_EQ(plan.open.size(), 1u);
	EXPECT_EQ(items[plan.open[0]].entry->id, L"A/always");
	ASSERT_EQ(plan.ask.size(), 1u);
	EXPECT_EQ(items[plan.ask[0]].entry->id, L"A/ask");
}

TEST(ProcessFinder, Should_FindThisTestProgramByFileAndByFolder)
{
	wchar_t self[MAX_PATH]{};
	GetModuleFileNameW(nullptr, self, MAX_PATH);
	const std::filesystem::path exe(self);
	const std::vector<std::wstring> running = GetRunningImagePaths();

	EXPECT_TRUE(IsAppRunning(running, exe, false));
	EXPECT_TRUE(IsAppRunning(running, exe.parent_path(), true));
	EXPECT_FALSE(IsAppRunning(running, exe.parent_path() / L"not-running.exe", false));
	// "C:\Apps\Foo" must not match a process in "C:\Apps\FooBar"
	EXPECT_FALSE(IsAppRunning(running, exe.parent_path().wstring() + L"Suffix", true));
}

TEST(ElevatedTask, Should_MakeReadableTaskNamesWithoutForbiddenCharacters)
{
	const std::wstring name = MakeElevatedTaskName(L"Network/Diagnostics/simplewall");
	EXPECT_TRUE(name.starts_with(L"Network - Diagnostics - simplewall (")) << name;
	EXPECT_EQ(name.size(), std::wstring(L"Network - Diagnostics - simplewall (12345678)").size());
	const std::wstring cleaned = MakeElevatedTaskName(L"A\\B:C*D?\"<>|");
	EXPECT_TRUE(cleaned.starts_with(L"A - BCD (")) << cleaned;
}

TEST(ElevatedTask, Should_GiveDifferentIdsDifferentTaskNames_AndTheSameIdOneName)
{
	// Same readable part, different apps: a backup copy next to a subfolder
	EXPECT_NE(MakeElevatedTaskName(L"Net/Tool - copia"), MakeElevatedTaskName(L"Net/Tool/copia"));
	// Ids are compared without case and with either slash: same hash
	const auto hashOf = [](const std::wstring& name) { return name.substr(name.size() - 10); };
	EXPECT_EQ(hashOf(MakeElevatedTaskName(L"Net/Tool")), hashOf(MakeElevatedTaskName(L"net\\TOOL")));
}

TEST(CommandLine, Should_ParsePhase3Options)
{
	const CommandLineOptions options = ParseCommandLine(
		{L"--register-task", L"Network/Diagnostics/simplewall", L"--task-user", L"PC\\me", L"--check-update",
		 L"--startup"});
	EXPECT_EQ(options.registerTask, L"Network/Diagnostics/simplewall");
	EXPECT_EQ(options.taskUser, L"PC\\me");
	EXPECT_TRUE(options.checkUpdate);
	EXPECT_TRUE(options.startup);
	EXPECT_TRUE(options.errors.empty());
}
