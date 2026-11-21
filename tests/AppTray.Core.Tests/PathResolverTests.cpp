#include "PathResolver.h"

#include <gtest/gtest.h>

using namespace apptray;

TEST(PathResolver, Should_ConvertBothWaysWithForwardSlashes)
{
	const PathResolver resolver(L"C:\\Apps\\");

	EXPECT_EQ(resolver.ToRelative(L"C:\\Apps\\Media\\Tools"), L"Media/Tools");
	EXPECT_EQ(resolver.ToRelative(L"c:\\apps\\Media"), L"Media");
	EXPECT_EQ(resolver.ToRelative(L"C:\\Apps"), L"");
	EXPECT_EQ(resolver.ToAbsolute(L"Media/Tools"), std::filesystem::path(L"C:\\Apps\\Media\\Tools"));
}

TEST(PathResolver, Should_RejectPathsOutsideTheRoot)
{
	const PathResolver resolver(L"C:\\Apps");

	EXPECT_FALSE(resolver.ToRelative(L"C:\\Apps2\\x").has_value());
	EXPECT_FALSE(resolver.ToRelative(L"D:\\Apps\\x").has_value());
	EXPECT_FALSE(resolver.ToAbsolute(L"../Windows/notepad.exe").has_value());
}

TEST(PathResolver, Should_ResolveRootFromCommandLineSettingsOrParentFolder)
{
	const std::filesystem::path exe = L"C:\\Applications\\AppTray";

	EXPECT_EQ(PathResolver::ResolveRoot(L"D:\\Portable", L"..", exe), std::filesystem::path(L"D:\\Portable"));
	EXPECT_EQ(PathResolver::ResolveRoot(std::nullopt, L"..\\Other", exe), std::filesystem::path(L"C:\\Applications\\Other"));
	EXPECT_EQ(PathResolver::ResolveRoot(std::nullopt, std::nullopt, exe), std::filesystem::path(L"C:\\Applications"));
}

TEST(PathResolver, Should_SplitRelativePaths)
{
	EXPECT_EQ(GetParentOfRelative(L"Media/Tools/ffmpeg"), L"Media/Tools");
	EXPECT_EQ(GetParentOfRelative(L"ffmpeg"), L"");
	EXPECT_EQ(GetLeafOfRelative(L"Media/Tools/ffmpeg"), L"ffmpeg");
}
