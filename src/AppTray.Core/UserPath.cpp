#include "UserPath.h"

#include "StringUtil.h"

#include <windows.h>

#include <userenv.h>

#include <wil/resource.h>

#include <format>
#include <vector>

#pragma comment(lib, "userenv.lib")

namespace apptray
{
	namespace
	{
		std::vector<std::wstring> SplitList(std::wstring_view list)
		{
			std::vector<std::wstring> entries;
			size_t start = 0;
			for (;;)
			{
				const size_t end = list.find(L';', start);
				entries.emplace_back(list.substr(start, end == std::wstring_view::npos ? std::wstring_view::npos : end - start));
				if (end == std::wstring_view::npos)
				{
					return entries;
				}
				start = end + 1;
			}
		}

		std::wstring JoinList(const std::vector<std::wstring>& entries)
		{
			std::wstring list;
			for (size_t i = 0; i < entries.size(); ++i)
			{
				list += (i == 0 ? L"" : L";") + entries[i];
			}
			return list;
		}

		// The folder an entry names, comparable: without spaces around, quotes or trailing
		// backslashes, and with %variables% expanded (case kept)
		std::wstring CleanEntry(std::wstring_view entry)
		{
			std::wstring clean = Trim(entry);
			if (clean.size() >= 2 && clean.front() == L'"' && clean.back() == L'"')
			{
				clean = Trim(std::wstring_view(clean).substr(1, clean.size() - 2));
			}
			if (clean.find(L'%') != std::wstring::npos)
			{
				const DWORD size = ExpandEnvironmentStringsW(clean.c_str(), nullptr, 0);
				if (size > 0)
				{
					std::wstring expanded(size, L'\0');
					const DWORD written = ExpandEnvironmentStringsW(clean.c_str(), expanded.data(), size);
					if (written > 0 && written <= size)
					{
						expanded.resize(written - 1);
						clean = std::move(expanded);
					}
				}
			}
			while (!clean.empty() && (clean.back() == L'\\' || clean.back() == L'/'))
			{
				clean.pop_back();
			}
			return clean;
		}
	}

	bool ContainsPathEntry(std::wstring_view list, std::wstring_view directory)
	{
		const std::wstring wanted = CleanEntry(directory);
		if (wanted.empty())
		{
			return false;
		}
		for (const std::wstring& entry : SplitList(list))
		{
			if (EqualsIgnoreCase(CleanEntry(entry), wanted))
			{
				return true;
			}
		}
		return false;
	}

	std::wstring AddPathEntry(std::wstring_view list, std::wstring_view directory)
	{
		if (CleanEntry(directory).empty() || ContainsPathEntry(list, directory))
		{
			return std::wstring(list);
		}
		std::wstring result(list);
		if (Trim(result).empty())
		{
			result.clear();
		}
		else if (result.back() != L';')
		{
			result += L';';
		}
		return result + std::wstring(directory);
	}

	std::wstring RemovePathEntry(std::wstring_view list, std::wstring_view directory)
	{
		const std::wstring unwanted = CleanEntry(directory);
		if (unwanted.empty())
		{
			return std::wstring(list);
		}
		std::vector<std::wstring> kept;
		for (std::wstring& entry : SplitList(list))
		{
			if (!EqualsIgnoreCase(CleanEntry(entry), unwanted))
			{
				kept.push_back(std::move(entry));
			}
		}
		return JoinList(kept);
	}

	std::wstring RebasePathEntries(std::wstring_view list, std::wstring_view from, std::wstring_view to)
	{
		const std::wstring oldFolder = CleanEntry(from);
		const std::wstring newFolder = CleanEntry(to);
		if (oldFolder.empty() || newFolder.empty())
		{
			return std::wstring(list);
		}
		std::vector<std::wstring> entries = SplitList(list);
		bool changed = false;
		const auto isInside = [&](std::wstring_view path)
		{
			return StartsWithIgnoreCase(path, oldFolder)
				&& (path.size() == oldFolder.size() || path[oldFolder.size()] == L'\\' || path[oldFolder.size()] == L'/');
		};
		for (std::wstring& entry : entries)
		{
			// As written (quotes, trailing backslash) when the text itself names the folder;
			// rebuilt from the expanded form when only that does (%variables%)
			std::wstring raw = Trim(entry);
			const bool quoted = raw.size() >= 2 && raw.front() == L'"' && raw.back() == L'"';
			if (quoted)
			{
				raw = raw.substr(1, raw.size() - 2);
			}
			if (isInside(raw))
			{
				const std::wstring moved = newFolder + raw.substr(oldFolder.size());
				entry = quoted ? L"\"" + moved + L"\"" : moved;
				changed = true;
				continue;
			}
			const std::wstring clean = CleanEntry(entry);
			if (isInside(clean))
			{
				entry = newFolder + clean.substr(oldFolder.size());
				changed = true;
			}
		}
		return changed ? JoinList(entries) : std::wstring(list);
	}

	std::optional<UserPath> ReadUserPath(std::wstring& error, const wchar_t* key)
	{
		constexpr DWORD flags = RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND;
		UserPath path;
		DWORD size = 0;
		LSTATUS status = RegGetValueW(HKEY_CURRENT_USER, key, L"Path", flags, nullptr, nullptr, &size);
		if (status == ERROR_FILE_NOT_FOUND)
		{
			return path; // neither the key nor the value: an empty PATH
		}
		for (int attempt = 0; attempt < 4 && status == ERROR_SUCCESS; ++attempt)
		{
			std::wstring buffer(size / sizeof(wchar_t) + 1, L'\0');
			size = static_cast<DWORD>(buffer.size() * sizeof(wchar_t));
			DWORD type = 0;
			status = RegGetValueW(HKEY_CURRENT_USER, key, L"Path", flags, &type, buffer.data(), &size);
			if (status == ERROR_SUCCESS)
			{
				buffer.resize(wcsnlen(buffer.c_str(), buffer.size()));
				path.list = std::move(buffer);
				path.expandable = type == REG_EXPAND_SZ;
				return path;
			}
			if (status == ERROR_MORE_DATA)
			{
				status = ERROR_SUCCESS; // it grew meanwhile: size holds the new length
			}
		}
		error = std::format(L"HKCU\\{}\\Path: error {}", key, status);
		return std::nullopt;
	}

	bool WriteUserPath(const UserPath& path, std::wstring& error, const wchar_t* key)
	{
		wil::unique_hkey handle;
		LSTATUS status =
			RegCreateKeyExW(HKEY_CURRENT_USER, key, 0, nullptr, 0, KEY_SET_VALUE, nullptr, handle.put(), nullptr);
		if (status == ERROR_SUCCESS)
		{
			status = RegSetValueExW(
				handle.get(), L"Path", 0, path.expandable ? REG_EXPAND_SZ : REG_SZ,
				reinterpret_cast<const BYTE*>(path.list.c_str()), static_cast<DWORD>((path.list.size() + 1) * sizeof(wchar_t)));
		}
		if (status != ERROR_SUCCESS)
		{
			error = std::format(L"HKCU\\{}\\Path: error {}", key, status);
			return false;
		}
		return true;
	}

	void AnnounceEnvironmentChange()
	{
		SendMessageTimeoutW(
			HWND_BROADCAST, WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(L"Environment"), SMTO_ABORTIFHUNG, 5000, nullptr);
	}

	bool RefreshProcessPath()
	{
		wil::unique_handle token;
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE | TOKEN_IMPERSONATE, token.put()))
		{
			return false;
		}
		void* block = nullptr;
		if (!CreateEnvironmentBlock(&block, token.get(), FALSE))
		{
			return false;
		}
		bool updated = false;
		for (const wchar_t* variable = static_cast<const wchar_t*>(block); *variable != L'\0'; variable += wcslen(variable) + 1)
		{
			if (StartsWithIgnoreCase(variable, L"Path="))
			{
				updated = SetEnvironmentVariableW(L"Path", variable + 5) != FALSE;
				break;
			}
		}
		DestroyEnvironmentBlock(block);
		return updated;
	}
}
