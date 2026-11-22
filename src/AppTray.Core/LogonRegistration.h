#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace apptray
{
	// "Start with Windows": a value under HKCU\...\CurrentVersion\Run with "<exe>" --startup. Per
	// user, no elevation, visible in Task Manager's Startup tab. The first AppTray copy gets the
	// value "AppTray"; another copy (another tree, such as games) gets "AppTray (<its folder>)", so
	// each copy starts with Windows on its own.

	// Where the values live; tests use their own key under HKEY_CURRENT_USER
	inline constexpr wchar_t LogonRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

	// True when one of AppTray's values starts this exe
	bool IsRegisteredAtLogon(const std::filesystem::path& exe, const wchar_t* key = LogonRunKey);

	// label names another copy's value: the folder of its tree ("Juegos"). Turning it off removes
	// every AppTray value that starts this exe; the other copies' values stay.
	bool SetRegisteredAtLogon(
		bool enabled, const std::filesystem::path& exe, std::wstring_view label, std::wstring& error,
		const wchar_t* key = LogonRunKey);

	// The folder moved (or its drive letter changed): when no value starts this exe and exactly one
	// AppTray value starts a missing exe with the same last folders ("Juegos\AppTray\AppTray.exe"),
	// that value was this copy's and now starts this exe. Another copy's value (a USB drive
	// unplugged) never matches.
	void RepairLogonRegistration(const std::filesystem::path& exe, const wchar_t* key = LogonRunKey);
}
