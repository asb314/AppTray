#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string>

namespace apptray
{
	using Json = nlohmann::ordered_json;

	// Reads a UTF-8 JSON file (BOM and // comments allowed). nullopt with a message on failure.
	std::optional<Json> ReadJsonFile(const std::filesystem::path& file, std::wstring& error);

	// Writes tab-indented JSON through a temporary file and an atomic rename, so a crash never
	// leaves a half-written file behind.
	bool WriteJsonFile(const std::filesystem::path& file, const Json& json, std::wstring& error);

	// Helpers to move wide strings in and out of JSON (stored as UTF-8)
	std::wstring GetString(const Json& object, const char* key, std::wstring_view fallback = {});
	std::optional<std::wstring> GetOptionalString(const Json& object, const char* key);
	bool GetBool(const Json& object, const char* key, bool fallback = false);
	int GetInt(const Json& object, const char* key, int fallback);
	Json ToJson(std::wstring_view text);
}
