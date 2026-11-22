// Phase 5: running apps (processes and windows), command-line tools in a console, the user's
// PATH, search keywords

#include "AppList.h"
#include "AppWindows.h"
#include "Catalog.h"
#include "ConsoleCommand.h"
#include "ProcessFinder.h"
#include "UserPath.h"
#include "UserSettings.h"

#include "TestTree.h"

#include <windows.h>

#include <gtest/gtest.h>

#include <algorithm>

using namespace apptray;
using apptray::tests::TestTree;

namespace
{
	constexpr wchar_t Now[] = L"2026-11-22T10:00:00Z";

	AppEntry MakeApp(std::wstring id, std::wstring name, AppKind kind = AppKind::Folder)
	{
		AppEntry entry;
		entry.id = std::move(id);
		entry.name = std::move(name);
		entry.kind = kind;
		entry.target = kind == AppKind::File ? entry.id : entry.id + L"/app.exe";
		return entry;
	}

	std::vector<std::wstring> Search(const std::vector<AppItem>& items, const std::wstring& search)
	{
		AppFilter filter;
		filter.search = search;
		std::vector<std::wstring> names;
		for (const size_t index : FilterAppItems(items, filter, {}))
		{
			names.push_back(items[index].name);
		}
		return names;
	}

	std::filesystem::path GetThisProgram()
	{
		wchar_t self[MAX_PATH]{};
		GetModuleFileNameW(nullptr, self, MAX_PATH);
		return self;
	}

	// HKCU\Software\AppTray.Tests\Environment, deleted when the test ends
	class TestRegistryKey
	{
	public:
		static constexpr wchar_t Key[] = L"Software\\AppTray.Tests\\Environment";

		TestRegistryKey()
		{
			Delete();
		}
		~TestRegistryKey()
		{
			Delete();
		}
		TestRegistryKey(const TestRegistryKey&) = delete;
		TestRegistryKey& operator=(const TestRegistryKey&) = delete;

	private:
		static void Delete()
		{
			RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\AppTray.Tests");
		}
	};
}

TEST(ProcessFinder, Should_FindThisTestProgramByFileAndByFolder)
{
	const std::filesystem::path exe = GetThisProgram();
	const std::vector<RunningProcess> running = GetRunningProcesses();
	const unsigned long self = GetCurrentProcessId();
	const auto hasSelf = [&](const std::vector<unsigned long>& ids)
	{ return std::find(ids.begin(), ids.end(), self) != ids.end(); };

	EXPECT_TRUE(hasSelf(FindAppProcesses(running, exe, false)));
	EXPECT_TRUE(hasSelf(FindAppProcesses(running, exe.parent_path(), true)));
	EXPECT_TRUE(FindAppProcesses(running, exe.parent_path() / L"not-running.exe", false).empty());
	// "C:\Apps\Foo" must not match a process in "C:\Apps\FooBar"
	EXPECT_FALSE(hasSelf(FindAppProcesses(running, exe.parent_path().wstring() + L"Suffix", true)));
}

TEST(AppList, Should_LookForFolderAppsByFolder_AndSingleProgramsByFile)
{
	Catalog catalog;
	catalog.apps = {
		MakeApp(L"Browser/FirefoxPortable", L"Firefox", AppKind::PortableApps),
		MakeApp(L"Media/VLC", L"VLC"),
		MakeApp(L"Network/Download/wget.exe", L"wget", AppKind::File),
		MakeApp(L"Utils/SysinternalsSuite", L"Sysinternals", AppKind::Suite),
	};
	catalog.apps[3].target.clear();
	const std::vector<AppItem> items = BuildAppItems(catalog, {}, Now);

	const std::optional<ProcessScope> firefox = GetProcessScope(items[0]);
	ASSERT_TRUE(firefox.has_value());
	EXPECT_EQ(firefox->path, L"Browser/FirefoxPortable");
	EXPECT_TRUE(firefox->isFolder);
	EXPECT_TRUE(GetProcessScope(items[1])->isFolder);
	const std::optional<ProcessScope> wget = GetProcessScope(items[2]);
	ASSERT_TRUE(wget.has_value());
	EXPECT_EQ(wget->path, L"Network/Download/wget.exe");
	EXPECT_FALSE(wget->isFolder);
	EXPECT_FALSE(GetProcessScope(items[3]).has_value()); // a suite opens its folder
}

TEST(AppWindows, Should_FindAVisibleTitledWindowOfTheProcess_AndNotAnHiddenOne)
{
	const HINSTANCE instance = GetModuleHandleW(nullptr);
	WNDCLASSEXW windowClass{};
	windowClass.cbSize = sizeof(windowClass);
	windowClass.lpfnWndProc = &DefWindowProcW;
	windowClass.hInstance = instance;
	windowClass.lpszClassName = L"AppTray.Tests.Window";
	RegisterClassExW(&windowClass);
	const auto create = [&](const wchar_t* title, DWORD style)
	{
		return CreateWindowExW(
			WS_EX_NOACTIVATE, windowClass.lpszClassName, title, style, -32000, -32000, 100, 100, nullptr, nullptr, instance,
			nullptr);
	};
	HWND hidden = create(L"Hidden", WS_OVERLAPPEDWINDOW);
	HWND untitled = create(L"", WS_OVERLAPPEDWINDOW | WS_VISIBLE);
	HWND main = create(L"Main", WS_OVERLAPPEDWINDOW | WS_VISIBLE);
	ASSERT_NE(main, nullptr);

	const std::vector<HWND> windows = FindMainWindows({GetCurrentProcessId()});
	EXPECT_NE(std::find(windows.begin(), windows.end(), main), windows.end());
	EXPECT_EQ(std::find(windows.begin(), windows.end(), hidden), windows.end());
	EXPECT_EQ(std::find(windows.begin(), windows.end(), untitled), windows.end());
	EXPECT_TRUE(FindMainWindows({}).empty());

	// Closing asks the window, as its X does (DefWindowProc destroys it)
	EXPECT_TRUE(RequestWindowClose(main));
	MSG message{};
	while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
	{
		DispatchMessageW(&message);
	}
	EXPECT_FALSE(IsWindow(main));
	DestroyWindow(hidden);
	DestroyWindow(untitled);
}

TEST(ConsoleCommand, Should_OpenCmdInTheFolder_WithItFirstInPath_AndRunTheProgram)
{
	const ConsoleCommand plain = BuildConsoleCommand(ConsoleShell::Cmd, L"C:\\Windows\\System32\\cmd.exe", L"D:\\Apps\\ffmpeg\\bin");
	EXPECT_EQ(plain.program, std::filesystem::path(L"C:\\Windows\\System32\\cmd.exe"));
	EXPECT_EQ(plain.parameters, L"/s /k \"set \"PATH=D:\\Apps\\ffmpeg\\bin;%PATH%\" & pushd \"D:\\Apps\\ffmpeg\\bin\"\"");

	// cmd's operators in the arguments are escaped, except inside quotes
	const ConsoleCommand tool = BuildConsoleCommand(
		ConsoleShell::Cmd, L"cmd.exe", L"D:\\Apps\\ffmpeg\\bin", std::filesystem::path(L"D:\\Apps\\ffmpeg\\bin\\ffmpeg.exe"),
		L"-i a&b.mp4 -f \"x|y\"");
	EXPECT_EQ(
		tool.parameters,
		L"/s /k \"set \"PATH=D:\\Apps\\ffmpeg\\bin;%PATH%\" & pushd \"D:\\Apps\\ffmpeg\\bin\" & "
		L"\"D:\\Apps\\ffmpeg\\bin\\ffmpeg.exe\" -i a^&b.mp4 -f \"x|y\"\"");
}

TEST(ConsoleCommand, Should_QuotePowerShellLiterals_AndTheWholeCommandAsOneArgument)
{
	const ConsoleCommand tool = BuildConsoleCommand(
		ConsoleShell::PowerShell, L"pwsh.exe", L"D:\\Bob's apps\\yt-dlp",
		std::filesystem::path(L"D:\\Bob's apps\\yt-dlp\\yt-dlp.exe"), L"--sub-langs en,es --config \"my file.txt\"");
	EXPECT_EQ(tool.program, std::filesystem::path(L"pwsh.exe"));
	// Single quotes doubled inside PowerShell literals; the arguments after --% (not parsed by
	// PowerShell, so "en,es" stays one argument); double quotes escaped for the command line
	EXPECT_EQ(
		tool.parameters,
		L"-NoExit -NoLogo -Command \"$env:Path = 'D:\\Bob''s apps\\yt-dlp;' + $env:Path; "
		L"Set-Location -LiteralPath 'D:\\Bob''s apps\\yt-dlp'; & 'D:\\Bob''s apps\\yt-dlp\\yt-dlp.exe' "
		L"--% --sub-langs en,es --config \\\"my file.txt\\\"\"");

	// A drive root: its backslash must not swallow anything
	const ConsoleCommand root = BuildConsoleCommand(ConsoleShell::PowerShell, L"pwsh.exe", L"D:\\");
	EXPECT_EQ(root.parameters, L"-NoExit -NoLogo -Command \"$env:Path = 'D:\\;' + $env:Path; Set-Location -LiteralPath 'D:\\'\"");
}

TEST(ConsoleCommand, Should_ReadTheShellSetting_AndFindItsProgram)
{
	EXPECT_EQ(ParseConsoleShell(L"cmd"), ConsoleShell::Cmd);
	EXPECT_EQ(ParseConsoleShell(L" CMD "), ConsoleShell::Cmd);
	EXPECT_EQ(ParseConsoleShell(L"powershell"), ConsoleShell::PowerShell);
	EXPECT_EQ(ParseConsoleShell(L"anything"), ConsoleShell::PowerShell);

	EXPECT_EQ(FindShellProgram(ConsoleShell::Cmd).filename(), std::filesystem::path(L"cmd.exe"));
	const std::filesystem::path powershell = FindShellProgram(ConsoleShell::PowerShell);
	EXPECT_TRUE(powershell.filename() == L"pwsh.exe" || powershell.filename() == L"powershell.exe") << powershell;
}

TEST(UserPath, Should_FindAnEntry_IgnoringCaseQuotesTrailingBackslashAndVariables)
{
	wchar_t temp[MAX_PATH]{};
	GetEnvironmentVariableW(L"TEMP", temp, MAX_PATH);
	const std::wstring list = L"C:\\Tools;\"D:\\Apps\\ffmpeg\\bin\\\";%TEMP%\\cli";

	EXPECT_TRUE(ContainsPathEntry(list, L"c:\\tools\\"));
	EXPECT_TRUE(ContainsPathEntry(list, L"D:\\Apps\\FFmpeg\\bin"));
	EXPECT_TRUE(ContainsPathEntry(list, std::wstring(temp) + L"\\cli"));
	EXPECT_FALSE(ContainsPathEntry(list, L"D:\\Apps\\ffmpeg"));
	EXPECT_FALSE(ContainsPathEntry(list, L""));
}

TEST(UserPath, Should_AddAtTheEndOnce_AndRemoveEveryCopy)
{
	EXPECT_EQ(AddPathEntry(L"", L"D:\\A"), L"D:\\A");
	EXPECT_EQ(AddPathEntry(L"C:\\X", L"D:\\A"), L"C:\\X;D:\\A");
	EXPECT_EQ(AddPathEntry(L"C:\\X;", L"D:\\A"), L"C:\\X;D:\\A");
	EXPECT_EQ(AddPathEntry(L"C:\\X;d:\\a\\", L"D:\\A"), L"C:\\X;d:\\a\\"); // already there

	EXPECT_EQ(RemovePathEntry(L"D:\\A;C:\\X;d:\\a\\;C:\\Y", L"D:\\A"), L"C:\\X;C:\\Y");
	EXPECT_EQ(RemovePathEntry(L"C:\\X;D:\\A;", L"D:\\A"), L"C:\\X;"); // the trailing ; stays
	EXPECT_EQ(RemovePathEntry(L"C:\\X", L"D:\\A"), L"C:\\X");
}

TEST(UserPath, Should_MoveEntriesIntoARenamedFolder_OnlyInsideIt)
{
	const std::wstring list = L"C:\\X;D:\\Apps\\ffmpeg 7.1\\bin;D:\\Apps\\ffmpeg 7.1;D:\\Apps\\ffmpeg 7.10\\bin";
	EXPECT_EQ(
		RebasePathEntries(list, L"D:\\Apps\\ffmpeg 7.1", L"D:\\Apps\\ffmpeg 8.0"),
		L"C:\\X;D:\\Apps\\ffmpeg 8.0\\bin;D:\\Apps\\ffmpeg 8.0;D:\\Apps\\ffmpeg 7.10\\bin");
	EXPECT_EQ(RebasePathEntries(L"C:\\X", L"D:\\Old", L"D:\\New"), L"C:\\X");
	// Quotes and a trailing backslash stay as written
	EXPECT_EQ(
		RebasePathEntries(L"\"D:\\Apps\\ffmpeg 7.1\\bin\\\";C:\\X", L"D:\\Apps\\ffmpeg 7.1", L"D:\\Apps\\ffmpeg 8.0"),
		L"\"D:\\Apps\\ffmpeg 8.0\\bin\\\";C:\\X");
}

TEST(UserPath, Should_ReadAndWriteTheRegistryValue_KeepingItsType)
{
	TestRegistryKey key;
	std::wstring error;
	const std::optional<UserPath> missing = ReadUserPath(error, TestRegistryKey::Key);
	ASSERT_TRUE(missing.has_value()) << error;
	EXPECT_EQ(missing->list, L"");

	UserPath path{L"%USERPROFILE%\\bin;D:\\Apps\\ffmpeg\\bin", true};
	ASSERT_TRUE(WriteUserPath(path, error, TestRegistryKey::Key)) << error;
	const std::optional<UserPath> read = ReadUserPath(error, TestRegistryKey::Key);
	ASSERT_TRUE(read.has_value()) << error;
	EXPECT_EQ(read->list, path.list); // not expanded
	EXPECT_TRUE(read->expandable);

	path.expandable = false;
	ASSERT_TRUE(WriteUserPath(path, error, TestRegistryKey::Key)) << error;
	EXPECT_FALSE(ReadUserPath(error, TestRegistryKey::Key)->expandable);
}

TEST(AppList, Should_FindAppsByTheirKeywords_AndRankAKeywordStartLikeAName)
{
	Catalog catalog;
	catalog.apps = {
		MakeApp(L"Image/Editor/GIMP", L"GIMP"),
		MakeApp(L"Image/Viewer/Fotor", L"Fotor"),
		MakeApp(L"Image/Viewer/XnView", L"XnView"),
	};
	UserSettings settings;
	settings.apps[L"Image/Viewer/XnView"].keywords = L"Fotos, visor";
	settings.apps[L"Image/Editor/GIMP"].keywords = L"retoque";
	const std::vector<AppItem> items = BuildAppItems(catalog, settings, Now);

	EXPECT_EQ(items[2].keywords, L"fotos visor");
	EXPECT_EQ(Search(items, L"retoque"), std::vector<std::wstring>{L"GIMP"});
	EXPECT_EQ(Search(items, L"VISOR"), std::vector<std::wstring>{L"XnView"});
	// "fot": Fotor's name and XnView's keyword both start with it, ahead of names that only contain it
	EXPECT_EQ(Search(items, L"fot"), (std::vector<std::wstring>{L"Fotor", L"XnView"}));
}

TEST(UserSettings, Should_KeepKeywordsAndTheShell_AndDropEmptyKeywords)
{
	TestTree tree;
	const std::filesystem::path file = tree.Root() / L"user.json";
	std::wstring error;
	UserSettings settings = LoadUserSettings(file, error);
	EXPECT_EQ(settings.shell, L"powershell");

	settings.shell = L"cmd";
	SetKeywords(settings, L"Image/GIMP", L"  retoque fotos ");
	ASSERT_TRUE(SaveUserSettings(file, settings, error)) << error;
	UserSettings loaded = LoadUserSettings(file, error);
	EXPECT_EQ(loaded.shell, L"cmd");
	ASSERT_NE(loaded.FindOverride(L"Image/GIMP"), nullptr);
	EXPECT_EQ(loaded.FindOverride(L"Image/GIMP")->keywords, L"retoque fotos");
	EXPECT_TRUE(loaded.HasUserData(L"Image/GIMP"));

	SetKeywords(loaded, L"Image/GIMP", L"   ");
	EXPECT_EQ(loaded.FindOverride(L"Image/GIMP"), nullptr); // nothing else was set: the entry goes
}

TEST(UserSettings, Should_KeepTheConsoleChoice_AndDropItWhenBackToDefault)
{
	TestTree tree;
	const std::filesystem::path file = tree.Root() / L"user.json";
	std::wstring error;
	UserSettings settings = LoadUserSettings(file, error);
	SetConsoleChoice(settings, L"Image/3D/Blender", false);
	ASSERT_TRUE(SaveUserSettings(file, settings, error)) << error;
	UserSettings loaded = LoadUserSettings(file, error);
	ASSERT_NE(loaded.FindOverride(L"Image/3D/Blender"), nullptr);
	EXPECT_EQ(loaded.FindOverride(L"Image/3D/Blender")->console, std::optional<bool>(false));

	Catalog catalog;
	catalog.apps = {MakeApp(L"Image/3D/Blender", L"Blender")};
	EXPECT_EQ(BuildAppItems(catalog, loaded, Now)[0].console, std::optional<bool>(false));

	SetConsoleChoice(loaded, L"Image/3D/Blender", std::nullopt);
	EXPECT_EQ(loaded.FindOverride(L"Image/3D/Blender"), nullptr);
}

TEST(UserSettings, Should_ReadKeywordsWrittenAsAnArray)
{
	TestTree tree;
	const std::filesystem::path file =
		tree.Add(L"user.json", R"({"apps": {"Image/GIMP": {"keywords": ["retoque", "fotos"]}}})");
	std::wstring error;
	const UserSettings settings = LoadUserSettings(file, error);
	ASSERT_TRUE(error.empty()) << error;
	ASSERT_NE(settings.FindOverride(L"Image/GIMP"), nullptr);
	EXPECT_EQ(settings.FindOverride(L"Image/GIMP")->keywords, L"retoque fotos");
}
