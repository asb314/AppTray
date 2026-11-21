#include "JsonFile.h"

#include "StringUtil.h"

#include <windows.h>

#include <wil/resource.h>

#include <format>

namespace fs = std::filesystem;

namespace apptray
{
	namespace
	{
		std::wstring FormatLastError(std::wstring_view action, const fs::path& file)
		{
			return std::format(L"{} {} (error {})", action, file.wstring(), GetLastError());
		}
	}

	std::optional<Json> ReadJsonFile(const fs::path& file, std::wstring& error)
	{
		wil::unique_hfile handle(CreateFileW(
			file.c_str(),
			GENERIC_READ,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr,
			OPEN_EXISTING,
			FILE_FLAG_SEQUENTIAL_SCAN,
			nullptr));
		if (!handle)
		{
			error = FormatLastError(L"Cannot open", file);
			return std::nullopt;
		}
		LARGE_INTEGER size{};
		if (!GetFileSizeEx(handle.get(), &size) || size.QuadPart > 64LL * 1024 * 1024)
		{
			error = std::format(L"Cannot read {} (too big or unreadable)", file.wstring());
			return std::nullopt;
		}
		std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
		DWORD read = 0;
		if (!bytes.empty()
			&& !ReadFile(handle.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr))
		{
			error = FormatLastError(L"Cannot read", file);
			return std::nullopt;
		}
		bytes.resize(read);
		if (bytes.starts_with("\xEF\xBB\xBF"))
		{
			bytes.erase(0, 3);
		}

		Json json = Json::parse(bytes, nullptr, false, true);
		if (json.is_discarded())
		{
			error = std::format(L"{} is not valid JSON", file.wstring());
			return std::nullopt;
		}
		return json;
	}

	bool WriteJsonFile(const fs::path& file, const Json& json, std::wstring& error)
	{
		std::error_code directoryError;
		fs::create_directories(file.parent_path(), directoryError);

		const std::string text = json.dump(1, '\t') + "\n";
		const fs::path temporary = file.wstring() + L".tmp";
		{
			wil::unique_hfile handle(CreateFileW(
				temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
			if (!handle)
			{
				error = FormatLastError(L"Cannot create", temporary);
				return false;
			}
			DWORD written = 0;
			if (!WriteFile(handle.get(), text.data(), static_cast<DWORD>(text.size()), &written, nullptr)
				|| written != text.size() || !FlushFileBuffers(handle.get()))
			{
				error = FormatLastError(L"Cannot write", temporary);
				return false;
			}
		}
		if (!MoveFileExW(
				temporary.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		{
			error = FormatLastError(L"Cannot replace", file);
			DeleteFileW(temporary.c_str());
			return false;
		}
		return true;
	}

	std::wstring GetString(const Json& object, const char* key, std::wstring_view fallback)
	{
		const auto found = object.find(key);
		if (found == object.end() || !found->is_string())
		{
			return std::wstring(fallback);
		}
		return FromUtf8(found->get_ref<const std::string&>());
	}

	std::optional<std::wstring> GetOptionalString(const Json& object, const char* key)
	{
		const auto found = object.find(key);
		if (found == object.end() || !found->is_string())
		{
			return std::nullopt;
		}
		return FromUtf8(found->get_ref<const std::string&>());
	}

	bool GetBool(const Json& object, const char* key, bool fallback)
	{
		const auto found = object.find(key);
		return found != object.end() && found->is_boolean() ? found->get<bool>() : fallback;
	}

	int GetInt(const Json& object, const char* key, int fallback)
	{
		const auto found = object.find(key);
		return found != object.end() && found->is_number_integer() ? found->get<int>() : fallback;
	}

	Json ToJson(std::wstring_view text)
	{
		return Json(ToUtf8(text));
	}
}
