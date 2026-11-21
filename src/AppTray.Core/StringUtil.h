#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace apptray
{
	std::string ToUtf8(std::wstring_view text);
	std::wstring FromUtf8(std::string_view text);

	// Locale-independent lowercase (invariant culture), good for file names
	std::wstring ToLower(std::wstring_view text);

	bool EqualsIgnoreCase(std::wstring_view a, std::wstring_view b);
	bool StartsWithIgnoreCase(std::wstring_view text, std::wstring_view prefix);
	bool EndsWithIgnoreCase(std::wstring_view text, std::wstring_view suffix);
	bool ContainsIgnoreCase(std::wstring_view text, std::wstring_view part);

	std::wstring Trim(std::wstring_view text);

	// Current UTC time as ISO 8601 without fractions: "2026-11-21T12:34:56Z"
	std::wstring FormatUtcNow();

	// Seconds since 1601-01-01 UTC for "2026-11-21T12:34:56Z"; nullopt when malformed
	std::optional<std::int64_t> ParseUtcSeconds(std::wstring_view iso);

	// Lowercase without accents, for searching: "Música Ñandú" -> "musica nandu"
	std::wstring FoldForSearch(std::wstring_view text);
}
