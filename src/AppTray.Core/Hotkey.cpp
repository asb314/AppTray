#include "Hotkey.h"

#include "StringUtil.h"

#include <windows.h>

#include <format>
#include <vector>

namespace apptray
{
	namespace
	{
		bool IsOneOf(std::wstring_view word, std::initializer_list<std::wstring_view> names)
		{
			for (const std::wstring_view name : names)
			{
				if (EqualsIgnoreCase(word, name))
				{
					return true;
				}
			}
			return false;
		}

		std::optional<unsigned int> ParseKey(std::wstring_view name)
		{
			if (name.empty())
			{
				return std::nullopt;
			}
			if (name.size() == 1)
			{
				const wchar_t c = name[0];
				if (c >= L'a' && c <= L'z')
				{
					return static_cast<unsigned int>(c - L'a' + L'A');
				}
				if ((c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9'))
				{
					return static_cast<unsigned int>(c);
				}
				return std::nullopt;
			}
			if (IsOneOf(name, {L"Space", L"Espacio"}))
			{
				return VK_SPACE;
			}
			if ((name[0] == L'F' || name[0] == L'f') && name.size() <= 3)
			{
				int number = 0;
				for (const wchar_t c : name.substr(1))
				{
					if (c < L'0' || c > L'9')
					{
						return std::nullopt;
					}
					number = number * 10 + (c - L'0');
				}
				if (number >= 1 && number <= 24)
				{
					return static_cast<unsigned int>(VK_F1 + number - 1);
				}
			}
			return std::nullopt;
		}
	}

	std::optional<Hotkey> ParseHotkey(std::wstring_view text)
	{
		std::vector<std::wstring> parts;
		size_t start = 0;
		for (;;)
		{
			const size_t plus = text.find(L'+', start);
			parts.push_back(Trim(text.substr(start, plus == std::wstring_view::npos ? std::wstring_view::npos : plus - start)));
			if (plus == std::wstring_view::npos)
			{
				break;
			}
			start = plus + 1;
		}
		if (parts.size() < 2)
		{
			return std::nullopt;
		}

		Hotkey hotkey;
		for (size_t i = 0; i + 1 < parts.size(); ++i)
		{
			const std::wstring& part = parts[i];
			if (IsOneOf(part, {L"Ctrl", L"Control", L"Ctl"}))
			{
				hotkey.modifiers |= MOD_CONTROL;
			}
			else if (IsOneOf(part, {L"Alt"}))
			{
				hotkey.modifiers |= MOD_ALT;
			}
			else if (IsOneOf(part, {L"Shift", L"Mayús", L"Mayus", L"Mayúsculas"}))
			{
				hotkey.modifiers |= MOD_SHIFT;
			}
			else if (IsOneOf(part, {L"Win", L"Windows"}))
			{
				hotkey.modifiers |= MOD_WIN;
			}
			else
			{
				return std::nullopt;
			}
		}
		const std::optional<unsigned int> key = ParseKey(parts.back());
		if (!key || (hotkey.modifiers & (MOD_CONTROL | MOD_ALT | MOD_WIN)) == 0)
		{
			return std::nullopt;
		}
		hotkey.key = *key;
		return hotkey;
	}

	std::wstring FormatHotkey(const Hotkey& hotkey, bool spanishNames)
	{
		std::wstring text;
		if ((hotkey.modifiers & MOD_CONTROL) != 0)
		{
			text += L"Ctrl+";
		}
		if ((hotkey.modifiers & MOD_ALT) != 0)
		{
			text += L"Alt+";
		}
		if ((hotkey.modifiers & MOD_SHIFT) != 0)
		{
			text += spanishNames ? L"Mayús+" : L"Shift+";
		}
		if ((hotkey.modifiers & MOD_WIN) != 0)
		{
			text += L"Win+";
		}
		if (hotkey.key == VK_SPACE)
		{
			text += spanishNames ? L"Espacio" : L"Space";
		}
		else if (hotkey.key >= VK_F1 && hotkey.key <= VK_F24)
		{
			text += std::format(L"F{}", hotkey.key - VK_F1 + 1);
		}
		else
		{
			text += static_cast<wchar_t>(hotkey.key);
		}
		return text;
	}
}
