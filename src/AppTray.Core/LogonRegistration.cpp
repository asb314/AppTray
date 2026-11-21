#include "LogonRegistration.h"

#include "CommandLine.h"

#include <windows.h>

#include <shellapi.h>

#include <format>

namespace fs = std::filesystem;

namespace apptray
{
	namespace
	{
		constexpr wchar_t RunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
		constexpr wchar_t ValueName[] = L"AppTray";

		std::wstring MakeCommand(const fs::path& exe)
		{
			return QuoteArgument(exe.wstring()) + L" --startup";
		}

		// The program part of a registered command
		std::optional<fs::path> GetCommandProgram(const std::wstring& command)
		{
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
	}

	std::optional<std::wstring> GetLogonCommand()
	{
		wchar_t buffer[2048]{};
		DWORD size = sizeof(buffer);
		if (RegGetValueW(HKEY_CURRENT_USER, RunKey, ValueName, RRF_RT_REG_SZ, nullptr, buffer, &size) != ERROR_SUCCESS)
		{
			return std::nullopt;
		}
		return std::wstring(buffer);
	}

	bool IsRegisteredAtLogon(const fs::path& exe)
	{
		const std::optional<std::wstring> command = GetLogonCommand();
		const std::optional<fs::path> program = command ? GetCommandProgram(*command) : std::nullopt;
		if (!program)
		{
			return false;
		}
		const std::wstring left = program->lexically_normal().wstring();
		const std::wstring right = exe.lexically_normal().wstring();
		return CompareStringOrdinal(
				   left.c_str(), static_cast<int>(left.size()), right.c_str(), static_cast<int>(right.size()), TRUE)
			== CSTR_EQUAL;
	}

	bool SetRegisteredAtLogon(bool enabled, const fs::path& exe, std::wstring& error)
	{
		LSTATUS status = ERROR_SUCCESS;
		if (enabled)
		{
			const std::wstring command = MakeCommand(exe);
			status = RegSetKeyValueW(
				HKEY_CURRENT_USER, RunKey, ValueName, REG_SZ, command.c_str(),
				static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
		}
		else
		{
			status = RegDeleteKeyValueW(HKEY_CURRENT_USER, RunKey, ValueName);
			if (status == ERROR_FILE_NOT_FOUND)
			{
				status = ERROR_SUCCESS;
			}
		}
		if (status != ERROR_SUCCESS)
		{
			error = std::format(L"cannot change the Run registry value (error {})", status);
			return false;
		}
		return true;
	}

	void RepairLogonRegistration(const fs::path& exe)
	{
		const std::optional<std::wstring> command = GetLogonCommand();
		if (!command)
		{
			return;
		}
		const std::optional<fs::path> program = GetCommandProgram(*command);
		std::error_code error;
		if (program && !fs::exists(*program, error))
		{
			std::wstring ignored;
			SetRegisteredAtLogon(true, exe, ignored);
		}
	}
}
