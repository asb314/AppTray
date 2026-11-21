#include "ExeInspector.h"

#include "TestTree.h"

#include <windows.h>

#include <gtest/gtest.h>

using namespace apptray;

namespace
{
	std::filesystem::path GetWindowsDirectoryPath()
	{
		wchar_t buffer[MAX_PATH]{};
		GetWindowsDirectoryW(buffer, MAX_PATH);
		return buffer;
	}
}

TEST(ExeInspector, Should_ReadConsoleSubsystemMachineAndVersion)
{
	const ExeInfo info = InspectExe(GetWindowsDirectoryPath() / L"System32" / L"cmd.exe");
	EXPECT_TRUE(info.isPe);
	EXPECT_TRUE(info.isConsole);
	EXPECT_FALSE(info.isGui);
	EXPECT_EQ(info.machine, IMAGE_FILE_MACHINE_AMD64);
	EXPECT_FALSE(info.fileDescription.empty());
	EXPECT_GT(info.size, 0u);
}

TEST(ExeInspector, Should_DetectGuiProgramThatAsksForElevation)
{
	// regedit's manifest asks for highestAvailable
	const ExeInfo info = InspectExe(GetWindowsDirectoryPath() / L"regedit.exe");
	EXPECT_TRUE(info.isPe);
	EXPECT_TRUE(info.isGui);
	EXPECT_TRUE(info.requiresAdmin);
	EXPECT_FALSE(info.looksLikeInstaller);
}

TEST(ExeInspector, Should_ReturnNotPe_When_FileIsNotAnExecutable)
{
	tests::TestTree tree;
	const ExeInfo info = InspectExe(tree.Add(L"fake.exe", "not a program"));
	EXPECT_FALSE(info.isPe);
	EXPECT_EQ(info.size, 13u);
}
