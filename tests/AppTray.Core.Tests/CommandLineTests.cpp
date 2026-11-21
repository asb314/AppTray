#include "CommandLine.h"

#include <windows.h>

#include <shellapi.h>

#include <gtest/gtest.h>

using namespace apptray;

TEST(CommandLine, Should_ParseSwitchesAndValues)
{
	const CommandLineOptions options =
		ParseCommandLine({L"--scan", L"--root", L"C:\\Apps", L"--REPORT", L"r.md", L"--verbose"});

	EXPECT_TRUE(options.scan);
	EXPECT_TRUE(options.verbose);
	EXPECT_FALSE(options.startup);
	EXPECT_EQ(options.root, L"C:\\Apps");
	EXPECT_EQ(options.report, L"r.md");
	EXPECT_TRUE(options.errors.empty());
}

TEST(CommandLine, Should_NotTakeAnOptionAsValue_When_ValueIsMissing)
{
	const CommandLineOptions options = ParseCommandLine({L"--scan", L"--report", L"--verbose"});

	EXPECT_FALSE(options.report.has_value());
	EXPECT_TRUE(options.verbose);
	ASSERT_EQ(options.errors.size(), 1u);
	EXPECT_NE(options.errors[0].find(L"--report"), std::wstring::npos);
}

TEST(CommandLine, Should_QuoteArgumentsSoTheyParseBackUnchanged)
{
	for (const wchar_t* value : {L"D:\\", L"C:\\Program Files\\App\\", L"a\"b", L"plain", L"x\\\\\"y"})
	{
		const std::wstring line = L"app.exe " + QuoteArgument(value) + L" --next";
		int count = 0;
		LPWSTR* parsed = CommandLineToArgvW(line.c_str(), &count);
		ASSERT_NE(parsed, nullptr);
		ASSERT_EQ(count, 3) << line;
		EXPECT_EQ(std::wstring(parsed[1]), value) << line;
		EXPECT_EQ(std::wstring(parsed[2]), L"--next") << line;
		LocalFree(parsed);
	}
}

TEST(CommandLine, Should_ReportUnknownArgumentsAndTrailingOptionWithoutValue)
{
	const CommandLineOptions options = ParseCommandLine({L"-scan", L"--data"});

	EXPECT_FALSE(options.scan);
	EXPECT_FALSE(options.data.has_value());
	EXPECT_EQ(options.errors.size(), 2u);
}
