// The UI texts (src\AppTray\Strings.h): every translation of a text with {} must take the same
// arguments, or text::Format throws at run time in that language only

#include "../../src/AppTray/Strings.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string_view>

using namespace apptray;

namespace
{
	size_t CountOf(std::wstring_view text, std::wstring_view part)
	{
		size_t count = 0;
		for (size_t found = text.find(part); found != std::wstring_view::npos; found = text.find(part, found + part.size()))
		{
			++count;
		}
		return count;
	}

	std::wstring FormatWith(const text::Text& format, size_t arguments)
	{
		const std::wstring a = L"a";
		const std::wstring b = L"b";
		const std::wstring c = L"c";
		switch (arguments)
		{
		case 1:
			return text::Format(format, a);
		case 2:
			return text::Format(format, a, b);
		case 3:
			return text::Format(format, a, b, c);
		default:
			return text::Format(format);
		}
	}
}

TEST(Strings, Should_TakeTheSameArgumentsInEveryLanguage)
{
	struct Case
	{
		const text::Text& text;
		size_t arguments;
	};
	const Case cases[] = {
		{text::AboutText, 3},
		{text::AboutApps, 2},
		{text::AboutScanning, 1},
		{text::AboutHotkeyInUse, 1},
		{text::NewVersionMenu, 1},
		{text::NewAppsText, 1},
		{text::RefreshExitCode, 1},
		{text::NeedsElevatedTask, 1},
		{text::NewVersionText, 2},
		{text::UpToDate, 1},
		{text::ShortcutCreatedDesktop, 1},
		{text::ShortcutCreatedStartMenu, 1},
		{text::HotkeyInvalid, 1},
		{text::HotkeyTaken, 1},
		{text::RenamedAppsText, 1},
		{text::RenamedAppsReenable, 1},
		{text::CloseBlocked, 1},
		{text::NoWindowToClose, 1},
		{text::PathAdded, 1},
		{text::PathRemoved, 1},
		{text::KeywordsLabel, 1},
	};
	for (const text::Language language : {text::Language::Spanish, text::Language::English})
	{
		text::SetLanguage(language);
		for (const Case& item : cases)
		{
			const std::wstring_view format = item.text.Get();
			// Only plain {} placeholders, and no stray brace
			EXPECT_EQ(CountOf(format, L"{}"), item.arguments) << std::wstring(format);
			EXPECT_EQ(CountOf(format, L"{"), item.arguments) << std::wstring(format);
			EXPECT_EQ(CountOf(format, L"}"), item.arguments) << std::wstring(format);
			EXPECT_NO_THROW(FormatWith(item.text, item.arguments)) << std::wstring(format);
		}
	}
	text::SetLanguage(text::Language::Spanish);
}

TEST(Strings, Should_FollowTheSetting_OrWindowsLanguage)
{
	EXPECT_EQ(text::ResolveLanguage(L"es"), text::Language::Spanish);
	EXPECT_EQ(text::ResolveLanguage(L"en"), text::Language::English);
	const text::Language windows =
		PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_SPANISH ? text::Language::Spanish : text::Language::English;
	EXPECT_EQ(text::ResolveLanguage(L"auto"), windows);

	text::SetLanguage(text::Language::English);
	EXPECT_STREQ(text::Refresh, L"Refresh");
	text::SetLanguage(text::Language::Spanish);
	EXPECT_STREQ(text::Refresh, L"Refrescar");
}
