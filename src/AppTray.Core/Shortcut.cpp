#include "Shortcut.h"

#include "ConsoleCommand.h"
#include "StringUtil.h"

#include <windows.h>

#include <shlobj.h>
#include <shobjidl.h>

#include <wil/com.h>
#include <wil/resource.h>

#include <format>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "uuid.lib")

namespace fs = std::filesystem;

namespace apptray
{
	std::optional<ShortcutPlace> ParseShortcutPlace(std::wstring_view text)
	{
		if (EqualsIgnoreCase(text, L"desktop"))
		{
			return ShortcutPlace::Desktop;
		}
		if (EqualsIgnoreCase(text, L"startmenu"))
		{
			return ShortcutPlace::StartMenu;
		}
		return std::nullopt;
	}

	std::wstring_view ToString(ShortcutPlace place)
	{
		return place == ShortcutPlace::StartMenu ? L"startmenu" : L"desktop";
	}

	std::wstring MakeShortcutFileName(std::wstring_view appName)
	{
		std::wstring name;
		for (const wchar_t c : appName)
		{
			if (c < L' ' || std::wstring_view(L"\\/:*?\"<>|").find(c) != std::wstring_view::npos)
			{
				continue;
			}
			if (c == L' ' && (name.empty() || name.back() == L' '))
			{
				continue; // a removed character must not leave a double space
			}
			name.push_back(c);
		}
		// Windows drops trailing dots and spaces from file names
		while (!name.empty() && (name.back() == L'.' || name.back() == L' '))
		{
			name.pop_back();
		}
		if (name.empty())
		{
			name = L"App";
		}
		// "Con.lnk" or "COM1.lnk" cannot be created: device names, whatever the extension
		const bool isDevice = EqualsIgnoreCase(name, L"CON") || EqualsIgnoreCase(name, L"PRN")
			|| EqualsIgnoreCase(name, L"AUX") || EqualsIgnoreCase(name, L"NUL")
			|| (name.size() == 4 && (StartsWithIgnoreCase(name, L"COM") || StartsWithIgnoreCase(name, L"LPT"))
				&& name[3] >= L'1' && name[3] <= L'9');
		return name + (isDevice ? L"_" : L"") + L".lnk";
	}

	namespace
	{
		struct ShortcutCommand
		{
			std::wstring target;
			std::wstring arguments;
		};

		// Target and arguments of an existing .lnk; an empty target if it cannot be read
		ShortcutCommand ReadShortcutCommand(const fs::path& file)
		{
			wil::com_ptr_nothrow<IShellLinkW> link;
			wil::com_ptr_nothrow<IPersistFile> persist;
			if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))
				|| FAILED(link->QueryInterface(IID_PPV_ARGS(&persist))) || FAILED(persist->Load(file.c_str(), STGM_READ)))
			{
				return {};
			}
			ShortcutCommand command;
			command.target.resize(32768);
			if (FAILED(link->GetPath(command.target.data(), static_cast<int>(command.target.size()), nullptr, SLGP_RAWPATH)))
			{
				return {};
			}
			command.target.resize(wcsnlen(command.target.c_str(), command.target.size()));
			command.arguments.resize(32768);
			if (FAILED(link->GetArguments(command.arguments.data(), static_cast<int>(command.arguments.size()))))
			{
				command.arguments.clear(); // the target is still known
			}
			command.arguments.resize(wcsnlen(command.arguments.c_str(), command.arguments.size()));
			return command;
		}

		bool IsShortcutTo(const ShortcutCommand& link, const fs::path& target)
		{
			if (link.target.empty())
			{
				return false;
			}
			const fs::path linkTarget = fs::path(link.target).lexically_normal();
			if (!IsPowerShellScript(target))
			{
				return EqualsIgnoreCase(linkTarget.wstring(), target.lexically_normal().wstring());
			}
			// The PowerShell found may change (pwsh installed later) and so may the arguments
			const std::wstring host = linkTarget.filename().wstring();
			return (EqualsIgnoreCase(host, L"pwsh.exe") || EqualsIgnoreCase(host, L"powershell.exe"))
				&& ContainsIgnoreCase(link.arguments, QuoteForPowerShell(target.lexically_normal().wstring()));
		}
	}

	fs::path ChooseShortcutFile(const fs::path& folder, std::wstring_view fileName, const fs::path& target)
	{
		std::wstring stem(fileName);
		if (EndsWithIgnoreCase(stem, L".lnk"))
		{
			stem.resize(stem.size() - 4);
		}
		for (int number = 1; number <= 99; ++number)
		{
			const fs::path candidate =
				folder / (number == 1 ? stem + L".lnk" : std::format(L"{} ({}).lnk", stem, number));
			std::error_code existsError;
			if (!fs::exists(candidate, existsError))
			{
				return candidate;
			}
			if (IsShortcutTo(ReadShortcutCommand(candidate), target))
			{
				return candidate; // ours (or the same thing anyway): refreshed
			}
		}
		return {};
	}

	fs::path GetShortcutFolder(ShortcutPlace place, std::wstring& error)
	{
		wil::unique_cotaskmem_string path;
		const KNOWNFOLDERID& id = place == ShortcutPlace::StartMenu ? FOLDERID_Programs : FOLDERID_Desktop;
		const HRESULT result = SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &path);
		if (FAILED(result))
		{
			error = std::format(L"cannot find the {} folder (0x{:08X})", ToString(place), static_cast<unsigned long>(result));
			return {};
		}
		fs::path folder = path.get();
		if (place == ShortcutPlace::StartMenu)
		{
			folder /= L"AppTray";
			std::error_code createError;
			fs::create_directories(folder, createError);
			if (createError)
			{
				error = std::format(L"cannot create {}", folder.wstring());
				return {};
			}
		}
		return folder;
	}

	bool WriteShortcut(const ShortcutSpec& spec, std::wstring& error)
	{
		const auto fail = [&](std::wstring_view step, HRESULT result)
		{
			error = std::format(L"{} (0x{:08X})", step, static_cast<unsigned long>(result));
			return false;
		};
		wil::com_ptr_nothrow<IShellLinkW> link;
		HRESULT result = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link));
		if (FAILED(result))
		{
			return fail(L"cannot create a shell link", result);
		}
		result = link->SetPath(spec.target.c_str());
		if (SUCCEEDED(result) && !spec.arguments.empty())
		{
			result = link->SetArguments(spec.arguments.c_str());
		}
		if (SUCCEEDED(result) && !spec.workingDirectory.empty())
		{
			result = link->SetWorkingDirectory(spec.workingDirectory.c_str());
		}
		if (SUCCEEDED(result) && !spec.description.empty())
		{
			const std::wstring description = spec.description.substr(0, INFOTIPSIZE - 1);
			result = link->SetDescription(description.c_str());
		}
		if (FAILED(result))
		{
			return fail(L"cannot fill the shell link", result);
		}
		wil::com_ptr_nothrow<IPersistFile> file;
		result = link->QueryInterface(IID_PPV_ARGS(&file));
		if (SUCCEEDED(result))
		{
			result = file->Save(spec.file.c_str(), TRUE);
		}
		if (FAILED(result))
		{
			return fail(L"cannot save " + spec.file.wstring(), result);
		}
		return true;
	}
}
