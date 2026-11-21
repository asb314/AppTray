#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace apptray
{
	// "Start with Windows": a value "AppTray" under HKCU\...\CurrentVersion\Run with
	// "<exe>" --startup. Per user, no elevation, visible in Task Manager's Startup tab.

	// The command registered now, if any
	std::optional<std::wstring> GetLogonCommand();

	// True only when the value starts this exe (another AppTray copy may be the registered one)
	bool IsRegisteredAtLogon(const std::filesystem::path& exe);

	bool SetRegisteredAtLogon(bool enabled, const std::filesystem::path& exe, std::wstring& error);

	// The folder moved: the registered exe is gone, so register this one instead
	void RepairLogonRegistration(const std::filesystem::path& exe);
}
