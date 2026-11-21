#pragma once

#include <filesystem>
#include <string_view>

namespace apptray
{
	// Small UTF-8 log file; starts over when it passes 100 KB. Thread-safe. Does nothing until
	// InitializeLog is called.
	void InitializeLog(const std::filesystem::path& file);
	void WriteLog(std::wstring_view message);
}
