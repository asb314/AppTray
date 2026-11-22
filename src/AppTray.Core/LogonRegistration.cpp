#include "LogonRegistration.h"

#include "CommandLine.h"
#include "StringUtil.h"

#include <windows.h>

#include <shellapi.h>

#include <wil/resource.h>

#include <algorithm>
#include <format>
#include <optional>
#include <vector>

namespace fs = std::filesystem;

namespace apptray
{
	namespace
	{
		constexpr wchar_t BaseValueName[] = L"AppTray";

		struct RunValue
		{
			std::wstring name;
			std::wstring command;
		};

		std::wstring MakeCommand(const fs::path& exe)
		{
			return QuoteArgument(exe.wstring()) + L" --startup";
		}

		// The program part of a registered command
		std::optional<fs::path> GetCommandProgram(const std::wstring& command)
		{
			if (Trim(command).empty())
			{
				return std::nullopt; // CommandLineToArgvW would answer with this process's own path
			}
			int count = 0;
			LPWSTR* parts = CommandLineToArgvW(command.c_str(), &count);
			if (parts == nullptr)
			{
				return std::nullopt;
			}
			std::optional<fs::path> program;
			if (count > 0)
			{
				program = fs::path(parts[0]);
			}
			LocalFree(parts);
			return program;
		}

		bool StartsExe(const RunValue& value, const fs::path& exe)
		{
			const std::optional<fs::path> program = GetCommandProgram(value.command);
			if (!program)
			{
				return false;
			}
			if (EqualsIgnoreCase(program->lexically_normal().wstring(), exe.lexically_normal().wstring()))
			{
				return true;
			}
			std::error_code error;
			return fs::equivalent(*program, exe, error); // the same file through another path (subst, short name)
		}

		// The last folders and the file name, without the drive: what survives a moved tree or a
		// new drive letter ("Juegos\AppTray\AppTray.exe")
		std::wstring GetPathTail(const fs::path& path)
		{
			std::vector<std::wstring> parts;
			for (const fs::path& part : path.lexically_normal().relative_path())
			{
				if (!part.empty())
				{
					parts.push_back(part.wstring());
				}
			}
			std::wstring tail;
			for (size_t i = parts.size() > 3 ? parts.size() - 3 : 0; i < parts.size(); ++i)
			{
				tail += (tail.empty() ? L"" : L"\\") + parts[i];
			}
			return tail;
		}

		bool StartsMissingExe(const RunValue& value)
		{
			const std::optional<fs::path> program = GetCommandProgram(value.command);
			std::error_code error;
			return program && !fs::exists(*program, error);
		}

		bool IsAppTrayValueName(std::wstring_view name)
		{
			return EqualsIgnoreCase(name, BaseValueName) || StartsWithIgnoreCase(name, L"AppTray (");
		}

		// "AppTray" and "AppTray (...)" values; other programs' values are never touched
		std::vector<RunValue> ReadAppTrayValues(const wchar_t* key)
		{
			std::vector<RunValue> values;
			wil::unique_hkey handle;
			if (RegOpenKeyExW(HKEY_CURRENT_USER, key, 0, KEY_QUERY_VALUE, handle.put()) != ERROR_SUCCESS)
			{
				return values;
			}
			for (DWORD index = 0;; ++index)
			{
				wchar_t name[256]{};
				DWORD nameLength = static_cast<DWORD>(std::size(name));
				wchar_t data[2048]{};
				DWORD dataSize = sizeof(data);
				DWORD type = 0;
				const LSTATUS status = RegEnumValueW(
					handle.get(), index, name, &nameLength, nullptr, &type, reinterpret_cast<BYTE*>(data), &dataSize);
				if (status == ERROR_NO_MORE_ITEMS)
				{
					break;
				}
				if (status == ERROR_MORE_DATA)
				{
					continue; // a name or command that long is not AppTray's
				}
				if (status != ERROR_SUCCESS)
				{
					break;
				}
				if ((type == REG_SZ || type == REG_EXPAND_SZ) && IsAppTrayValueName(std::wstring_view(name, nameLength)))
				{
					std::wstring command(data, wcsnlen(data, dataSize / sizeof(wchar_t)));
					if (type == REG_EXPAND_SZ)
					{
						// Written by hand or by a tool: "%USERPROFILE%\..." as Windows will run it
						wchar_t expanded[2048]{};
						const DWORD length = ExpandEnvironmentStringsW(command.c_str(), expanded, static_cast<DWORD>(std::size(expanded)));
						if (length > 0 && length <= std::size(expanded))
						{
							command = expanded;
						}
					}
					values.push_back({std::wstring(name, nameLength), std::move(command)});
				}
			}
			return values;
		}

		LSTATUS WriteValue(const wchar_t* key, const std::wstring& name, const fs::path& exe)
		{
			const std::wstring command = MakeCommand(exe);
			return RegSetKeyValueW(
				HKEY_CURRENT_USER, key, name.c_str(), REG_SZ, command.c_str(),
				static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
		}

		// "AppTray" when free, else "AppTray (<label>)", then "AppTray (<label> 2)"... A value whose
		// exe is missing is never reused: it may be a copy on a drive that is not plugged in.
		std::wstring ChooseValueName(const std::vector<RunValue>& values, std::wstring_view label)
		{
			const auto isFree = [&](const std::wstring& name)
			{
				return std::none_of(
					values.begin(), values.end(), [&](const RunValue& value) { return EqualsIgnoreCase(value.name, name); });
			};
			if (isFree(BaseValueName))
			{
				return BaseValueName;
			}
			// A drive root's label "D:\" -> "D"
			std::wstring clean;
			for (const wchar_t c : label)
			{
				if (c != L'\\' && c != L'/' && c != L':' && c != L'(' && c != L')')
				{
					clean.push_back(c);
				}
			}
			clean = Trim(clean);
			for (int number = clean.empty() ? 2 : 1;; ++number)
			{
				const std::wstring name = clean.empty() ? std::format(L"{} ({})", BaseValueName, number)
					: number == 1                     ? std::format(L"{} ({})", BaseValueName, clean)
													  : std::format(L"{} ({} {})", BaseValueName, clean, number);
				if (isFree(name))
				{
					return name;
				}
			}
		}
	}

	bool IsRegisteredAtLogon(const fs::path& exe, const wchar_t* key)
	{
		const std::vector<RunValue> values = ReadAppTrayValues(key);
		return std::any_of(values.begin(), values.end(), [&](const RunValue& value) { return StartsExe(value, exe); });
	}

	bool SetRegisteredAtLogon(bool enabled, const fs::path& exe, std::wstring_view label, std::wstring& error, const wchar_t* key)
	{
		const std::vector<RunValue> values = ReadAppTrayValues(key);
		LSTATUS status = ERROR_SUCCESS;
		if (enabled)
		{
			if (std::any_of(values.begin(), values.end(), [&](const RunValue& value) { return StartsExe(value, exe); }))
			{
				return true;
			}
			status = WriteValue(key, ChooseValueName(values, label), exe);
		}
		else
		{
			for (const RunValue& value : values)
			{
				if (StartsExe(value, exe))
				{
					const LSTATUS deleted = RegDeleteKeyValueW(HKEY_CURRENT_USER, key, value.name.c_str());
					if (deleted != ERROR_SUCCESS && deleted != ERROR_FILE_NOT_FOUND)
					{
						status = deleted;
					}
				}
			}
		}
		if (status != ERROR_SUCCESS)
		{
			error = std::format(L"cannot change the Run registry value (error {})", status);
			return false;
		}
		return true;
	}

	void RepairLogonRegistration(const fs::path& exe, const wchar_t* key)
	{
		const std::vector<RunValue> values = ReadAppTrayValues(key);
		if (std::any_of(values.begin(), values.end(), [&](const RunValue& value) { return StartsExe(value, exe); }))
		{
			return;
		}
		const std::wstring tail = GetPathTail(exe);
		std::vector<const RunValue*> mine;
		for (const RunValue& value : values)
		{
			const std::optional<fs::path> program = GetCommandProgram(value.command);
			if (program && StartsMissingExe(value) && EqualsIgnoreCase(GetPathTail(*program), tail))
			{
				mine.push_back(&value);
			}
		}
		// Two that both look like this copy: not clear which one was
		if (mine.size() == 1)
		{
			WriteValue(key, mine.front()->name, exe);
		}
	}
}
