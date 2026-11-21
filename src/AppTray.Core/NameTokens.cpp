#include "NameTokens.h"

#include "StringUtil.h"

#include <windows.h>

#include <algorithm>
#include <array>

namespace apptray
{
	namespace
	{
		// Words that say nothing about which program a file is
		constexpr std::array NoiseWords{
			std::wstring_view{L"portable"}, std::wstring_view{L"win"},      std::wstring_view{L"amd"},
			std::wstring_view{L"arm"},      std::wstring_view{L"bit"},      std::wstring_view{L"bits"},
			std::wstring_view{L"app"},      std::wstring_view{L"pro"},      std::wstring_view{L"free"},
			std::wstring_view{L"full"},     std::wstring_view{L"final"},    std::wstring_view{L"edition"},
			std::wstring_view{L"version"},  std::wstring_view{L"ver"},      std::wstring_view{L"rev"},
			std::wstring_view{L"multilang"}, std::wstring_view{L"multilingual"},
			std::wstring_view{L"english"},  std::wstring_view{L"eng"},      std::wstring_view{L"esp"},
			std::wstring_view{L"the"},      std::wstring_view{L"and"},      std::wstring_view{L"for"},
			std::wstring_view{L"incl"},     std::wstring_view{L"paf"},      std::wstring_view{L"exe"},
			std::wstring_view{L"beta"},     std::wstring_view{L"alpha"},    std::wstring_view{L"setup"},
			std::wstring_view{L"release"},  std::wstring_view{L"stable"},
		};

		bool IsDigit(wchar_t c)
		{
			return c >= L'0' && c <= L'9';
		}

		bool IsLetter(wchar_t c)
		{
			return IsCharAlphaW(c) != FALSE;
		}

		bool IsUpper(wchar_t c)
		{
			return IsCharUpperW(c) != FALSE;
		}

		bool IsLower(wchar_t c)
		{
			return IsCharLowerW(c) != FALSE;
		}

		bool IsNoise(std::wstring_view token)
		{
			return std::find(NoiseWords.begin(), NoiseWords.end(), token) != NoiseWords.end();
		}

		bool ContainsToken(const std::vector<std::wstring>& tokens, std::wstring_view token)
		{
			return std::find(tokens.begin(), tokens.end(), token) != tokens.end();
		}

		std::vector<std::wstring> KeepLongTokens(std::vector<std::wstring> tokens, size_t minLength)
		{
			std::erase_if(tokens, [minLength](const std::wstring& t) { return t.size() < minLength; });
			return tokens;
		}
	}

	std::wstring StripLaunchExtension(std::wstring_view fileName)
	{
		std::wstring_view stem = fileName;
		for (const std::wstring_view extension : {L".exe", L".bat", L".cmd", L".jar", L".lnk"})
		{
			if (EndsWithIgnoreCase(stem, extension))
			{
				stem.remove_suffix(extension.size());
				break;
			}
		}
		if (EndsWithIgnoreCase(stem, L".paf"))
		{
			stem.remove_suffix(4);
		}
		return std::wstring(stem);
	}

	std::vector<std::wstring> SplitNameWords(std::wstring_view name, bool splitCamelCase)
	{
		std::vector<std::wstring> raw;
		std::wstring current;
		const auto flush = [&]()
		{
			if (!current.empty())
			{
				raw.push_back(ToLower(current));
				current.clear();
			}
		};

		for (size_t i = 0; i < name.size(); ++i)
		{
			const wchar_t c = name[i];
			const bool letter = IsLetter(c);
			const bool digit = IsDigit(c);
			if (!letter && !digit)
			{
				flush();
				continue;
			}
			if (!current.empty())
			{
				const wchar_t previous = current.back();
				const bool kindChanged = IsDigit(previous) != digit;
				bool camelBoundary = false;
				if (splitCamelCase && letter && IsUpper(c) && IsLetter(previous))
				{
					const bool nextIsLower = i + 1 < name.size() && IsLower(name[i + 1]);
					camelBoundary = IsLower(previous) || (IsUpper(previous) && nextIsLower);
				}
				if (kindChanged || camelBoundary)
				{
					flush();
				}
			}
			current.push_back(c);
		}
		flush();

		std::erase_if(
			raw, [](const std::wstring& word) { return std::all_of(word.begin(), word.end(), IsDigit); });
		return raw;
	}

	std::vector<std::wstring> TokenizeName(std::wstring_view name, bool splitCamelCase)
	{
		std::vector<std::wstring> tokens = SplitNameWords(name, splitCamelCase);
		std::erase_if(tokens, [](const std::wstring& word) { return word.size() < 2 || IsNoise(word); });
		return tokens;
	}

	std::wstring NormalizeName(std::wstring_view name)
	{
		std::wstring joined;
		for (const std::wstring& token : TokenizeName(name, false))
		{
			joined += token;
		}
		return joined;
	}

	int ScoreNameMatch(std::wstring_view folderName, std::wstring_view fileName)
	{
		const std::wstring stem = StripLaunchExtension(fileName);
		const std::wstring a = NormalizeName(folderName);
		const std::wstring b = NormalizeName(stem);
		if (a.empty() || b.empty())
		{
			return 0;
		}
		if (a == b)
		{
			return 100;
		}
		if (std::min(a.size(), b.size()) >= 3
			&& (a.find(b) != std::wstring::npos || b.find(a) != std::wstring::npos))
		{
			return 70;
		}

		const std::vector<std::wstring> folderTokens = KeepLongTokens(TokenizeName(folderName, true), 3);
		const std::vector<std::wstring> fileTokens = KeepLongTokens(TokenizeName(stem, true), 3);
		int shared = 0;
		for (const std::wstring& token : fileTokens)
		{
			if (ContainsToken(folderTokens, token))
			{
				++shared;
			}
		}
		if (shared > 0)
		{
			return std::min(40 + 10 * (shared - 1), 60);
		}

		// A long token of one side inside the other: "UltraEdit" ~ "Uedit", "emed64_editor" ~ "emedhtml"
		std::vector<std::wstring> folderLong = KeepLongTokens(TokenizeName(folderName, true), 4);
		for (std::wstring& token : KeepLongTokens(TokenizeName(folderName, false), 4))
		{
			folderLong.push_back(std::move(token));
		}
		for (const std::wstring& token : folderLong)
		{
			if (b.find(token) != std::wstring::npos)
			{
				return 30;
			}
		}
		std::vector<std::wstring> fileLong = KeepLongTokens(TokenizeName(stem, true), 4);
		for (std::wstring& token : KeepLongTokens(TokenizeName(stem, false), 4))
		{
			fileLong.push_back(std::move(token));
		}
		for (const std::wstring& token : fileLong)
		{
			if (a.find(token) != std::wstring::npos)
			{
				return 30;
			}
		}
		return 0;
	}

	std::wstring NormalizeVersion(std::wstring_view raw)
	{
		// "1, 2, 3, 0" (old resource compilers) -> "1.2.3.0"
		std::wstring text;
		for (size_t i = 0; i < raw.size(); ++i)
		{
			if (raw[i] != L',')
			{
				text.push_back(raw[i]);
				continue;
			}
			text.push_back(L'.');
			while (i + 1 < raw.size() && raw[i + 1] == L' ')
			{
				++i;
			}
		}
		text = Trim(text);
		// "Version 6.0a", "Release 0.85", "v2.1": the word goes when a number follows
		for (const std::wstring_view word : {L"version", L"release", L"ver", L"rel", L"v"})
		{
			if (StartsWithIgnoreCase(text, word))
			{
				const std::wstring after = Trim(std::wstring_view(text).substr(word.size()));
				if (!after.empty() && IsDigit(after[0]))
				{
					text = after;
					break;
				}
			}
		}

		// The leading dotted number, plus a short tag stuck to it ("6.0a", "4.15p", "1.2rc1").
		// Build data ("+8909da14...", "-win-x64", " (build 7)", " on Python 3.10") is dropped.
		size_t numberEnd = 0;
		while (numberEnd < text.size() && (IsDigit(text[numberEnd]) || text[numberEnd] == L'.'))
		{
			++numberEnd;
		}
		std::wstring number = text.substr(0, numberEnd);
		while (!number.empty() && number.back() == L'.')
		{
			number.pop_back();
		}
		if (number.empty())
		{
			return {}; // a name ("BadCopy Pro"), not a version
		}
		size_t tagEnd = numberEnd;
		while (tagEnd < text.size() && (IsLetter(text[tagEnd]) || IsDigit(text[tagEnd])))
		{
			++tagEnd;
		}
		constexpr size_t MaxTagLength = 6;
		const std::wstring tag = tagEnd - numberEnd <= MaxTagLength ? text.substr(numberEnd, tagEnd - numberEnd) : L"";

		std::vector<std::wstring> parts;
		size_t start = 0;
		for (size_t dot = number.find(L'.'); dot != std::wstring::npos; dot = number.find(L'.', start))
		{
			parts.push_back(number.substr(start, dot - start));
			start = dot + 1;
		}
		parts.push_back(number.substr(start));
		const bool allZero = std::all_of(parts.begin(), parts.end(), [](const std::wstring& part)
			{ return std::all_of(part.begin(), part.end(), [](wchar_t c) { return c == L'0'; }); });
		if (allZero && tag.empty())
		{
			return {};
		}
		// "1.2.3.0" -> "1.2.3": a fourth part that is zero says nothing
		while (parts.size() > 3 && parts.back() == L"0")
		{
			parts.pop_back();
		}
		std::wstring version;
		for (const std::wstring& part : parts)
		{
			version += (version.empty() ? L"" : L".") + part;
		}
		return version + tag;
	}

	std::wstring FindVersionInName(std::wstring_view name)
	{
		size_t i = 0;
		while (i < name.size())
		{
			if (!IsDigit(name[i]))
			{
				++i;
				continue;
			}
			// Digits, then any ".digits" groups
			const size_t start = i;
			bool dotted = false;
			while (i < name.size() && IsDigit(name[i]))
			{
				++i;
				if (i + 1 < name.size() && name[i] == L'.' && IsDigit(name[i + 1]))
				{
					dotted = true;
					++i;
				}
			}
			if (dotted)
			{
				return std::wstring(name.substr(start, i - start));
			}
		}
		return {};
	}
}
