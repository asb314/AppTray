#include "NameTokens.h"

#include <gtest/gtest.h>

using namespace apptray;

TEST(NameTokens, Should_DropNoiseDigitsAndVersions)
{
	EXPECT_EQ(NormalizeName(L"picpick_portable"), L"picpick");
	EXPECT_EQ(NormalizeName(L"CrystalDiskInfo9_9_3"), L"crystaldiskinfo");
	EXPECT_EQ(NormalizeName(L"rufus-4.15p"), L"rufus");
	EXPECT_EQ(NormalizeName(L"Q-Dir_x64"), L"dir");
}

TEST(NameTokens, Should_SplitCamelCaseAndAcronyms)
{
	const std::vector<std::wstring> expected{L"hd", L"sentinel"};
	EXPECT_EQ(TokenizeName(L"HDSentinel", true), expected);
	const std::vector<std::wstring> winMerge{L"merge"}; // "win" and the single "u" are dropped
	EXPECT_EQ(TokenizeName(L"WinMergeU", true), winMerge);
}

TEST(NameTokens, Should_StripLaunchAndPortableAppsExtensions)
{
	EXPECT_EQ(StripLaunchExtension(L"GIMPPortable_3.2.6.paf.exe"), L"GIMPPortable_3.2.6");
	EXPECT_EQ(StripLaunchExtension(L"launch_qgis_portable.bat"), L"launch_qgis_portable");
	EXPECT_EQ(StripLaunchExtension(L"readme.txt"), L"readme.txt");
}

TEST(NameTokens, Should_ScoreExactContainedSharedAndPartialMatches)
{
	EXPECT_EQ(ScoreNameMatch(L"hdsentinel_pro_portable", L"HDSentinel.exe"), 100);
	EXPECT_EQ(ScoreNameMatch(L"CrystalDiskInfo9_9_3", L"DiskInfo64.exe"), 70);
	EXPECT_GE(ScoreNameMatch(L"MediaCompress", L"Compress-Media.cmd"), 40);
	EXPECT_GE(ScoreNameMatch(L"QGIS 4.0.0 light", L"launch_qgis_portable.bat"), 40);
	EXPECT_GE(ScoreNameMatch(L"FilePilot", L"FPilot.exe"), 40);
	EXPECT_EQ(ScoreNameMatch(L"UltraEdit 24.0", L"Uedit.exe"), 30);
	EXPECT_EQ(ScoreNameMatch(L"Editor", L"k2pdfopt.exe"), 0);
	EXPECT_EQ(ScoreNameMatch(L"MiniTool Partition Wizard 13", L"7z.exe"), 0);
}
