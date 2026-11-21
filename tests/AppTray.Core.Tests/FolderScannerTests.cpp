#include "FolderScanner.h"

#include "TestTree.h"

#include <gtest/gtest.h>

#include <algorithm>

using namespace apptray;
using apptray::tests::TestTree;

namespace
{
	ScanResult Scan(const TestTree& tree, ScanOptions options = {})
	{
		options.root = tree.Root();
		return ScanFolders(options);
	}

	const AppEntry* Find(const ScanResult& result, std::wstring_view id)
	{
		const auto found = std::find_if(
			result.apps.begin(), result.apps.end(), [&](const AppEntry& entry) { return entry.id == id; });
		return found == result.apps.end() ? nullptr : &*found;
	}
}

TEST(FolderScanner, Should_ListLooseFilesAndAppSubfolders_When_FolderIsACategory)
{
	TestTree tree;
	tree.Add(L"Editor/k2pdfopt.exe");
	tree.Add(L"Editor/Notepad++/notepad++.exe");
	tree.Add(L"Editor/Notepad++/SciLexer.dll");
	tree.Add(L"Editor/Notepad++/updater/GUP.exe");

	const ScanResult result = Scan(tree);

	ASSERT_EQ(result.apps.size(), 2u);
	const AppEntry* loose = Find(result, L"Editor/k2pdfopt.exe");
	ASSERT_NE(loose, nullptr);
	EXPECT_EQ(loose->kind, AppKind::File);
	EXPECT_EQ(loose->category, L"Editor");
	const AppEntry* notepad = Find(result, L"Editor/Notepad++");
	ASSERT_NE(notepad, nullptr);
	EXPECT_EQ(notepad->kind, AppKind::Folder);
	EXPECT_EQ(notepad->category, L"Editor");
	EXPECT_EQ(notepad->target, L"Editor/Notepad++/notepad++.exe");
	ASSERT_EQ(notepad->alternatives.size(), 1u);
	EXPECT_EQ(notepad->alternatives[0], L"Editor/Notepad++/updater/GUP.exe");
}

TEST(FolderScanner, Should_KeepOneApp_When_FolderHasDllsAndSeveralExecutables)
{
	TestTree tree;
	tree.Add(L"Disk/Partition/MiniTool ShadowMaker/core.dll");
	tree.Add(L"Disk/Partition/MiniTool ShadowMaker/AgentService.exe");
	tree.Add(L"Disk/Partition/MiniTool ShadowMaker/system_backup_gui.exe");
	tree.Add(L"Disk/Partition/MiniTool ShadowMaker/x64/wimserv.exe");

	const ScanResult result = Scan(tree);

	ASSERT_EQ(result.apps.size(), 1u);
	EXPECT_EQ(result.apps[0].id, L"Disk/Partition/MiniTool ShadowMaker");
	EXPECT_EQ(result.apps[0].category, L"Disk/Partition");
	EXPECT_EQ(result.apps[0].target, L"Disk/Partition/MiniTool ShadowMaker/system_backup_gui.exe");
}

TEST(FolderScanner, Should_PickLaunchScript_When_AppKeepsProgramsInBin)
{
	TestTree tree;
	tree.Add(L"Image/GIS/QGIS 4.0.0 light/launch_qgis_portable.bat");
	tree.Add(L"Image/GIS/QGIS 4.0.0 light/OSGeo4W.bat");
	tree.Add(L"Image/GIS/QGIS 4.0.0 light/bin/qgis-bin.exe");
	tree.Add(L"Image/GIS/QGIS 4.0.0 light/bin/qgis_core.dll");
	tree.Add(L"Image/GIS/QGIS 4.0.0 light/apps/qgis/crssync.exe");

	const ScanResult result = Scan(tree);

	ASSERT_EQ(result.apps.size(), 1u);
	EXPECT_EQ(result.apps[0].target, L"Image/GIS/QGIS 4.0.0 light/launch_qgis_portable.bat");
}

TEST(FolderScanner, Should_MergeArchitectureFolders_When_ProgramsLiveOnlyThere)
{
	TestTree tree;
	tree.Add(L"Disk/Space/WinDirStat/arm64/WinDirStat.exe");
	tree.Add(L"Disk/Space/WinDirStat/x64/WinDirStat.exe");
	tree.Add(L"Disk/Space/WinDirStat/x86/WinDirStat.exe");

	const ScanResult result = Scan(tree);

	ASSERT_EQ(result.apps.size(), 1u);
	EXPECT_EQ(result.apps[0].id, L"Disk/Space/WinDirStat");
	EXPECT_EQ(result.apps[0].target, L"Disk/Space/WinDirStat/x64/WinDirStat.exe");
	EXPECT_EQ(result.apps[0].alternatives.size(), 2u);
}

TEST(FolderScanner, Should_ReadAppInfo_When_FolderIsPortableAppsFormat)
{
	TestTree tree;
	tree.Add(L"Browser/FirefoxPortable/FirefoxPortable.exe");
	tree.Add(
		L"Browser/FirefoxPortable/App/AppInfo/appinfo.ini",
		"[Details]\r\nName=Firefox, Portable Edition\r\nDescription=Web browser\r\n[Control]\r\nStart=FirefoxPortable.exe\r\n");
	tree.Add(L"Browser/FirefoxPortable/App/Firefox64/firefox.exe");
	tree.Add(L"Browser/FirefoxPortable/Data/profile/helper.exe");

	const ScanResult result = Scan(tree);

	ASSERT_EQ(result.apps.size(), 1u);
	const AppEntry& entry = result.apps[0];
	EXPECT_EQ(entry.kind, AppKind::PortableApps);
	EXPECT_EQ(entry.name, L"Firefox, Portable Edition");
	EXPECT_EQ(entry.description, L"Web browser");
	EXPECT_EQ(entry.target, L"Browser/FirefoxPortable/FirefoxPortable.exe");
	EXPECT_EQ(entry.category, L"Browser");
}

TEST(FolderScanner, Should_MarkInstallerFolder_When_ItHoldsOnlyInstallers)
{
	TestTree tree;
	tree.Add(L"DB/Firebird/Firebird-2.5.9-Setup.exe");
	tree.Add(L"DB/Firebird/fb_install.exe");

	const ScanResult result = Scan(tree);

	ASSERT_EQ(result.apps.size(), 1u);
	EXPECT_EQ(result.apps[0].id, L"DB/Firebird");
	EXPECT_EQ(result.apps[0].kind, AppKind::Installer);
}

TEST(FolderScanner, Should_MarkBackupCopies_But_NotVersionSuffixes)
{
	TestTree tree;
	tree.Add(L"Network/Remote/mRemoteNG_/mRemoteNG.exe");
	tree.Add(L"Media/Video/HandBrake_1/HandBrake.exe");
	tree.Add(L"Disk/Health/CrystalDiskInfo9_9_3/DiskInfo64.exe");

	const ScanResult result = Scan(tree);

	ASSERT_NE(Find(result, L"Network/Remote/mRemoteNG_"), nullptr);
	EXPECT_TRUE(Find(result, L"Network/Remote/mRemoteNG_")->alternate);
	ASSERT_NE(Find(result, L"Media/Video/HandBrake_1"), nullptr);
	EXPECT_TRUE(Find(result, L"Media/Video/HandBrake_1")->alternate);
	ASSERT_NE(Find(result, L"Disk/Health/CrystalDiskInfo9_9_3"), nullptr);
	EXPECT_FALSE(Find(result, L"Disk/Health/CrystalDiskInfo9_9_3")->alternate);
}

TEST(FolderScanner, Should_SkipCracksIgnoredPathsAndItsOwnFolder)
{
	TestTree tree;
	tree.Add(L"DB/IB Expert/Crack/hk.exe");
	tree.Add(L"DB/IB Expert/Software/IBExpert.exe");
	tree.Add(L".claude/tool.exe");
	tree.Add(L"temp/thing.exe");
	tree.Add(L"AppTray/AppTray.exe");
	tree.Add(L"Tools/Foo/Foo.exe");

	ScanOptions options;
	options.ignorePaths = {L".claude", L"temp"};
	options.selfDirectory = tree.Root() / L"AppTray";
	const ScanResult result = Scan(tree, options);

	ASSERT_EQ(result.apps.size(), 2u);
	EXPECT_NE(Find(result, L"DB/IB Expert/Software"), nullptr);
	EXPECT_NE(Find(result, L"Tools/Foo"), nullptr);
	const auto skipped = [&](std::wstring_view id)
	{ return std::find(result.skippedFolders.begin(), result.skippedFolders.end(), id) != result.skippedFolders.end(); };
	EXPECT_TRUE(skipped(L"DB/IB Expert/Crack"));
	EXPECT_TRUE(skipped(L".claude"));
	EXPECT_TRUE(skipped(L"temp"));
	EXPECT_TRUE(skipped(L"AppTray"));
}

namespace
{
	void AddSysinternals(TestTree& tree)
	{
		for (const wchar_t* tool :
			 {L"procexp", L"autoruns", L"tcpview", L"handle", L"listdlls", L"psexec", L"pslist", L"sigcheck", L"strings"})
		{
			tree.Add(std::format(L"Utils/Process/SysinternalsSuite/{}.exe", tool));
			tree.Add(std::format(L"Utils/Process/SysinternalsSuite/{}64.exe", tool));
		}
	}
}

TEST(FolderScanner, Should_MakeSuite_When_ManyUnrelatedProgramsShareAFolder)
{
	TestTree tree;
	AddSysinternals(tree);

	const ScanResult result = Scan(tree);

	ASSERT_EQ(result.apps.size(), 1u);
	EXPECT_EQ(result.apps[0].kind, AppKind::Suite);
	EXPECT_TRUE(result.apps[0].target.empty());
	EXPECT_EQ(result.apps[0].alternatives.size(), 18u);
}

TEST(FolderScanner, Should_NotMakeSuite_When_FolderHasItsOwnDlls)
{
	TestTree tree;
	AddSysinternals(tree);
	tree.Add(L"Utils/Process/SysinternalsSuite/shared.dll");

	const ScanResult result = Scan(tree);

	ASSERT_EQ(result.apps.size(), 1u);
	EXPECT_EQ(result.apps[0].kind, AppKind::Folder);
	EXPECT_FALSE(result.apps[0].target.empty());
}

TEST(FolderScanner, Should_ListEachProgramOnce_When_SuiteIsExpanded)
{
	TestTree tree;
	AddSysinternals(tree);

	ScanOptions options;
	options.expandIds = {L"Utils/Process/SysinternalsSuite"};
	const ScanResult result = Scan(tree, options);

	ASSERT_EQ(result.apps.size(), 9u);
	EXPECT_NE(Find(result, L"Utils/Process/SysinternalsSuite/procexp64.exe"), nullptr);
	EXPECT_EQ(Find(result, L"Utils/Process/SysinternalsSuite/procexp.exe"), nullptr);
	EXPECT_EQ(result.apps[0].category, L"Utils/Process/SysinternalsSuite");
}

TEST(FolderScanner, Should_KeepSubfolderToolsInsideApp_When_FolderHasDlls)
{
	TestTree tree;
	tree.Add(L"Media/Audio/Ripper/Exact Audio Copy/EAC.exe");
	tree.Add(L"Media/Audio/Ripper/Exact Audio Copy/cdrdao.dll");
	tree.Add(L"Media/Audio/Ripper/Exact Audio Copy/CDRDAO/cdrdao.exe");
	tree.Add(L"Media/Audio/Ripper/Exact Audio Copy/Flac/flac.exe");
	tree.Add(L"Utils/System/SwitchOff/swoff.exe");
	tree.Add(L"Utils/System/SwitchOff/scripts/Open calculator.cmd");

	const ScanResult result = Scan(tree);

	ASSERT_EQ(result.apps.size(), 2u);
	ASSERT_NE(Find(result, L"Media/Audio/Ripper/Exact Audio Copy"), nullptr);
	EXPECT_EQ(Find(result, L"Media/Audio/Ripper/Exact Audio Copy")->target, L"Media/Audio/Ripper/Exact Audio Copy/EAC.exe");
	ASSERT_NE(Find(result, L"Utils/System/SwitchOff"), nullptr);
	EXPECT_EQ(Find(result, L"Utils/System/SwitchOff")->target, L"Utils/System/SwitchOff/swoff.exe");
}

TEST(FolderScanner, Should_ReportRootNotFound_When_RootIsMissing)
{
	ScanOptions options;
	options.root = std::filesystem::temp_directory_path() / L"AppTrayTests" / L"does-not-exist";

	const ScanResult result = ScanFolders(options);

	EXPECT_FALSE(result.rootFound);
	EXPECT_TRUE(result.apps.empty());
}

TEST(FolderScanner, Should_StopEarly_When_StopIsRequested)
{
	TestTree tree;
	tree.Add(L"Tools/Foo/Foo.exe");
	std::stop_source stop;
	stop.request_stop();

	ScanOptions options;
	options.stop = stop.get_token();
	const ScanResult result = Scan(tree, options);

	EXPECT_TRUE(result.rootFound);
	EXPECT_TRUE(result.cancelled);
	EXPECT_TRUE(result.apps.empty());
}

TEST(FolderScanner, Should_FindProgram_When_ItSitsDeepInLayoutFolders)
{
	TestTree tree;
	tree.Add(L"Tools/Foo/app/bin/x64/foo.exe");

	const ScanResult result = Scan(tree);

	EXPECT_TRUE(result.rootFound);
	ASSERT_EQ(result.apps.size(), 1u);
	EXPECT_EQ(result.apps[0].id, L"Tools/Foo");
	EXPECT_EQ(result.apps[0].target, L"Tools/Foo/app/bin/x64/foo.exe");
}

TEST(FolderScanner, Should_KeepCategory_When_SubfoldersHaveHelperNames)
{
	TestTree tree;
	// A loose program next to a "Tools" category that holds two apps
	tree.Add(L"Media/PortableXE.exe");
	tree.Add(L"Media/Tools/ffmpeg/bin/ffmpeg.exe");
	tree.Add(L"Media/Tools/MediaInfo/MediaInfo.exe");
	// Two language runtimes side by side
	tree.Add(L"Dev/Languages/Python312/python.exe");
	tree.Add(L"Dev/Languages/Java/bin/javaw.exe");

	const ScanResult result = Scan(tree);

	EXPECT_EQ(result.apps.size(), 5u);
	EXPECT_NE(Find(result, L"Media/PortableXE.exe"), nullptr);
	EXPECT_NE(Find(result, L"Media/Tools/ffmpeg"), nullptr);
	EXPECT_NE(Find(result, L"Media/Tools/MediaInfo"), nullptr);
	EXPECT_NE(Find(result, L"Dev/Languages/Python312"), nullptr);
	EXPECT_NE(Find(result, L"Dev/Languages/Java"), nullptr);
}

TEST(FolderScanner, Should_LeaveOutIgnoredFolders_When_TheyAreInsideAnApp)
{
	TestTree tree;
	tree.Add(L"Editor/Foo/foo.exe");
	tree.Add(L"Editor/Foo/foo.dll");
	tree.Add(L"Editor/Foo/old/foo.exe");

	ScanOptions options;
	options.ignorePaths = {L"Editor\\Foo\\old"};
	const ScanResult result = Scan(tree, options);

	ASSERT_EQ(result.apps.size(), 1u);
	EXPECT_EQ(result.apps[0].target, L"Editor/Foo/foo.exe");
	EXPECT_TRUE(result.apps[0].alternatives.empty());
}

TEST(FolderScanner, Should_WalkResourceNamedCategories_But_NotInsideApps)
{
	TestTree tree;
	tree.Add(L"Docs/Manuskript/manuskript.exe");
	tree.Add(L"Editor/Bar/bar.exe");
	tree.Add(L"Editor/Bar/bar.dll");
	tree.Add(L"Editor/Bar/docs/viewer.exe");

	const ScanResult result = Scan(tree);

	ASSERT_EQ(result.apps.size(), 2u);
	EXPECT_NE(Find(result, L"Docs/Manuskript"), nullptr);
	EXPECT_TRUE(Find(result, L"Editor/Bar")->alternatives.empty());
}

TEST(FolderScanner, Should_IgnorePortableAppsStart_When_ItPointsOutsideTheApp)
{
	TestTree tree;
	tree.Add(L"Shared/launcher.exe");
	tree.Add(L"Browser/FooPortable/FooPortable.exe");
	tree.Add(L"Browser/FooPortable/App/AppInfo/appinfo.ini", "[Control]\r\nStart=..\\..\\Shared\\launcher.exe\r\n");

	const ScanResult result = Scan(tree);

	const AppEntry* entry = Find(result, L"Browser/FooPortable");
	ASSERT_NE(entry, nullptr);
	EXPECT_EQ(entry->target, L"Browser/FooPortable/FooPortable.exe");
}

TEST(FolderScanner, Should_ListRootShortcutsAndLooseInstallers)
{
	TestTree tree;
	tree.Add(L"JDownloader 2.lnk");
	tree.Add(L"Network/rustdesk-1.4.2-x86_64.exe");
	tree.Add(L"Network/http annalyzer FiddlerSetup.exe");
	tree.Add(L"Network/API/Bruno/Bruno.exe");

	const ScanResult result = Scan(tree);

	ASSERT_EQ(result.apps.size(), 4u);
	ASSERT_NE(Find(result, L"JDownloader 2.lnk"), nullptr);
	EXPECT_EQ(Find(result, L"JDownloader 2.lnk")->category, L"");
	EXPECT_EQ(Find(result, L"Network/rustdesk-1.4.2-x86_64.exe")->kind, AppKind::File);
	EXPECT_EQ(Find(result, L"Network/http annalyzer FiddlerSetup.exe")->kind, AppKind::Installer);
	EXPECT_EQ(Find(result, L"Network/API/Bruno")->category, L"Network/API");
}
