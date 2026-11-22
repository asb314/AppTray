#include "ExeRanker.h"

#include <windows.h>

#include <gtest/gtest.h>

#include <initializer_list>

using namespace apptray;

namespace
{
	ExeInfo MakeGuiInfo(std::uint16_t machine = IMAGE_FILE_MACHINE_AMD64, std::wstring productName = {})
	{
		ExeInfo info;
		info.isPe = true;
		info.isGui = true;
		info.machine = machine;
		info.productName = std::move(productName);
		return info;
	}

	ExeInfo MakeConsoleInfo(std::uint16_t machine = IMAGE_FILE_MACHINE_AMD64)
	{
		ExeInfo info;
		info.isPe = true;
		info.isConsole = true;
		info.machine = machine;
		return info;
	}

	Candidate Make(std::wstring fileName, ExeInfo info = {}, int depth = 0, std::wstring relativeDir = {})
	{
		Candidate candidate;
		candidate.fileName = std::move(fileName);
		candidate.info = std::move(info);
		candidate.depth = depth;
		candidate.relativeDir = std::move(relativeDir);
		return candidate;
	}

	std::wstring PickBest(std::wstring_view folder, std::vector<Candidate> candidates)
	{
		RankCandidates(folder, candidates);
		return candidates.front().fileName;
	}
}

TEST(ExeRanker, Should_PreferExactNameOverPluginsAndUploaders)
{
	EXPECT_EQ(
		PickBest(L"picpick_portable", {Make(L"picpick_plugin.exe", MakeGuiInfo()), Make(L"picpick_uploader.exe", MakeGuiInfo()), Make(L"picpick.exe", MakeGuiInfo())}),
		L"picpick.exe");
}

TEST(ExeRanker, Should_PreferNative64BitOver32BitAndArm)
{
	EXPECT_EQ(
		PickBest(
			L"CrystalDiskInfo9_9_3",
			{Make(L"DiskInfo32.exe", MakeGuiInfo(IMAGE_FILE_MACHINE_I386)),
			 Make(L"DiskInfoA64.exe", MakeGuiInfo(IMAGE_FILE_MACHINE_ARM64)),
			 Make(L"DiskInfo64.exe", MakeGuiInfo(IMAGE_FILE_MACHINE_AMD64))}),
		L"DiskInfo64.exe");
}

TEST(ExeRanker, Should_PenalizeHelpersCachesAndProxies)
{
	EXPECT_EQ(
		PickBest(L"Meshmixer", {Make(L"DADispatcherService.exe", MakeGuiInfo()), Make(L"senddmp.exe", MakeConsoleInfo()), Make(L"meshmixer.exe", MakeGuiInfo())}),
		L"meshmixer.exe");
	EXPECT_EQ(PickBest(L"VLC", {Make(L"vlc-cache-gen.exe", MakeConsoleInfo()), Make(L"vlc.exe", MakeGuiInfo())}), L"vlc.exe");
	EXPECT_EQ(
		PickBest(L"WinMerge", {Make(L"WinMerge32BitPluginProxy.exe", MakeGuiInfo()), Make(L"WinMergeU.exe", MakeGuiInfo())}),
		L"WinMergeU.exe");
	EXPECT_EQ(PickBest(L"SABnzbd", {Make(L"SABnzbd-console.exe", MakeConsoleInfo()), Make(L"SABnzbd.exe", MakeGuiInfo())}), L"SABnzbd.exe");
}

TEST(ExeRanker, Should_PreferProgramOverDebugScript)
{
	EXPECT_EQ(PickBest(L"Blender", {Make(L"blender_debug_cycles.cmd"), Make(L"blender.exe", MakeGuiInfo())}), L"blender.exe");
	// "startup" is not the word "start"
	EXPECT_EQ(PickBest(L"Blender", {Make(L"blender_factory_startup.cmd"), Make(L"blender.exe", MakeGuiInfo())}), L"blender.exe");
}

TEST(ExeRanker, Should_PreferPortableLaunchScript)
{
	EXPECT_EQ(PickBest(L"Shotcut", {Make(L"shotcut.exe", MakeGuiInfo()), Make(L"Shotcut portable.cmd")}), L"Shotcut portable.cmd");
}

TEST(ExeRanker, Should_LaunchPowerShellScripts_AfterTheProgram)
{
	EXPECT_EQ(GetLaunchType(L"AdminShares.ps1"), LaunchType::PowerShellScript);
	EXPECT_EQ(GetLaunchType(L"WINUTIL.PS1"), LaunchType::PowerShellScript);
	EXPECT_EQ(GetLaunchType(L"profile.ps1xml"), LaunchType::None);
	EXPECT_EQ(GetLaunchType(L"module.psm1"), LaunchType::None);
	EXPECT_EQ(PickBest(L"Tool", {Make(L"build.ps1"), Make(L"Tool.exe", MakeGuiInfo())}), L"Tool.exe");
	// A script named like its folder still loses to the program, as .bat and .cmd do
	EXPECT_EQ(PickBest(L"Tool", {Make(L"Tool.ps1"), Make(L"Tool.exe", MakeGuiInfo())}), L"Tool.exe");
}

TEST(ExeRanker, Should_UseProductNameWhenFileNameSaysNothing)
{
	EXPECT_EQ(
		PickBest(
			L"Exact Audio Copy",
			{Make(L"CheckLog.exe", MakeGuiInfo(IMAGE_FILE_MACHINE_I386, L"CheckLog")),
			 Make(L"EAC.exe", MakeGuiInfo(IMAGE_FILE_MACHINE_I386, L"Exact Audio Copy"))}),
		L"EAC.exe");
	EXPECT_EQ(
		PickBest(L"MiniTool Partition Wizard 13", {Make(L"7z.exe", MakeConsoleInfo()), Make(L"partitionwizard.exe", MakeGuiInfo())}),
		L"partitionwizard.exe");
}

TEST(ExeRanker, Should_GuessArchitectureFromFolderWhenNotInspected)
{
	EXPECT_EQ(
		PickBest(
			L"WinDirStat",
			{Make(L"WinDirStat.exe", {}, 1, L"arm64"), Make(L"WinDirStat.exe", {}, 1, L"x86"), Make(L"WinDirStat.exe", {}, 1, L"x64")}),
		L"WinDirStat.exe");
	std::vector<Candidate> candidates{
		Make(L"WinDirStat.exe", {}, 1, L"arm64"), Make(L"WinDirStat.exe", {}, 1, L"x86"), Make(L"WinDirStat.exe", {}, 1, L"x64")};
	RankCandidates(L"WinDirStat", candidates);
	EXPECT_EQ(candidates.front().relativeDir, L"x64");
	EXPECT_EQ(candidates.back().relativeDir, L"arm64");
}

TEST(ExeRanker, Should_RecognizeInstallerNames)
{
	for (const wchar_t* name :
		 {L"setup.exe", L"unins000.exe", L"Firebird-2.5.9-Setup.exe", L"GIMPPortable_3.2.6.paf.exe",
		  L"harddisksentinelupdate.exe", L"Keygen.exe", L"ib64_install.exe", L"http annalyzer FiddlerSetup.exe",
		  L"SetupVirtualCloneDrive5450.exe", L"innosetup-7.1.0-x64.exe", L"PATCH FOR SLYSOFT PRODUCTS.EXE",
		  L"FastCopy5.11.2_installer.exe", L"instalar.ps1", L"Desinstalar Yazi.bat", L"instalador.exe"})
	{
		EXPECT_TRUE(IsInstallerName(name)) << name;
	}
	// Whole words only: these are programs
	for (const wchar_t* name :
		 {L"picpick.exe", L"notepad++.exe", L"FirefoxPortable.exe", L"launch_qgis_portable.bat",
		  L"WinSetupFromUSB_1-10_x64.exe", L"xdelta-patcher.exe", L"dispatcher.exe", L"DADispatcherService.exe"})
	{
		EXPECT_FALSE(IsInstallerName(name)) << name;
	}
}
