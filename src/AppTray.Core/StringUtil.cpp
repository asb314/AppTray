#include "StringUtil.h"

#include <windows.h>

#include <cwctype>
#include <format>

namespace apptray
{
	std::string ToUtf8(std::wstring_view text)
	{
		if (text.empty())
		{
			return {};
		}
		const int length = WideCharToMultiByte(
			CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
		std::string result(static_cast<size_t>(length), '\0');
		WideCharToMultiByte(
			CP_UTF8,
			0,
			text.data(),
			static_cast<int>(text.size()),
			result.data(),
			length,
			nullptr,
			nullptr);
		return result;
	}

	std::wstring FromUtf8(std::string_view text)
	{
		if (text.empty())
		{
			return {};
		}
		const int length =
			MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
		std::wstring result(static_cast<size_t>(length), L'\0');
		MultiByteToWideChar(
			CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length);
		return result;
	}

	std::wstring ToLower(std::wstring_view text)
	{
		if (text.empty())
		{
			return {};
		}
		std::wstring result(text.size(), L'\0');
		const int length = LCMapStringEx(
			LOCALE_NAME_INVARIANT,
			LCMAP_LOWERCASE,
			text.data(),
			static_cast<int>(text.size()),
			result.data(),
			static_cast<int>(result.size()),
			nullptr,
			nullptr,
			0);
		result.resize(static_cast<size_t>(length));
		return result;
	}

	bool EqualsIgnoreCase(std::wstring_view a, std::wstring_view b)
	{
		return CompareStringOrdinal(
				   a.data(), static_cast<int>(a.size()), b.data(), static_cast<int>(b.size()), TRUE)
			== CSTR_EQUAL;
	}

	bool StartsWithIgnoreCase(std::wstring_view text, std::wstring_view prefix)
	{
		return text.size() >= prefix.size() && EqualsIgnoreCase(text.substr(0, prefix.size()), prefix);
	}

	bool EndsWithIgnoreCase(std::wstring_view text, std::wstring_view suffix)
	{
		return text.size() >= suffix.size()
			&& EqualsIgnoreCase(text.substr(text.size() - suffix.size()), suffix);
	}

	bool ContainsIgnoreCase(std::wstring_view text, std::wstring_view part)
	{
		if (part.empty())
		{
			return true;
		}
		return ToLower(text).find(ToLower(part)) != std::wstring::npos;
	}

	std::wstring Trim(std::wstring_view text)
	{
		size_t first = 0;
		while (first < text.size() && std::iswspace(text[first]))
		{
			++first;
		}
		size_t last = text.size();
		while (last > first && std::iswspace(text[last - 1]))
		{
			--last;
		}
		return std::wstring(text.substr(first, last - first));
	}

	std::wstring FormatUtcNow()
	{
		SYSTEMTIME now{};
		GetSystemTime(&now);
		return std::format(
			L"{:04}-{:02}-{:02}T{:02}:{:02}:{:02}Z",
			now.wYear,
			now.wMonth,
			now.wDay,
			now.wHour,
			now.wMinute,
			now.wSecond);
	}
}
