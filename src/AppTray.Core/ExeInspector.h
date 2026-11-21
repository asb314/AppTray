#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace apptray
{
	// What AppTray needs to know about an executable, read without running or loading its code
	struct ExeInfo
	{
		bool isPe = false;
		bool isGui = false;
		bool isConsole = false;
		std::uint16_t machine = 0;  // IMAGE_FILE_MACHINE_AMD64, _I386, _ARM64...
		bool requiresAdmin = false; // manifest asks for requireAdministrator or highestAvailable
		bool looksLikeInstaller = false;
		std::uint64_t size = 0;
		std::wstring productName;
		std::wstring fileDescription;
	};

	enum class InspectDetail
	{
		Header, // PE header and version resource
		Full,   // plus the manifest (requiresAdmin): maps the file, so only for chosen targets
	};

	// Never throws: a file that is not a valid PE just returns isPe = false (with its size).
	// Every file opened costs a real-time antivirus scan the first time, so callers inspect as
	// few files as they can.
	ExeInfo InspectExe(const std::filesystem::path& file, InspectDetail detail = InspectDetail::Full);

	// Only the manifest part of InspectExe
	bool ReadRequiresAdmin(const std::filesystem::path& file);
}
