#include "UpdateCheck.h"

#include "JsonFile.h"
#include "StringUtil.h"
#include "Version.h"

#include <windows.h>

#include <winhttp.h>

#include <wil/resource.h>

#include <algorithm>
#include <format>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace apptray
{
	namespace
	{
		using unique_hinternet = wil::unique_any<HINTERNET, decltype(&::WinHttpCloseHandle), ::WinHttpCloseHandle>;

		std::vector<unsigned long> SplitVersion(std::wstring_view text)
		{
			if (!text.empty() && (text.front() == L'v' || text.front() == L'V'))
			{
				text.remove_prefix(1);
			}
			std::vector<unsigned long> parts;
			unsigned long current = 0;
			bool inNumber = false;
			for (const wchar_t c : text)
			{
				if (c >= L'0' && c <= L'9')
				{
					current = current * 10 + static_cast<unsigned long>(c - L'0');
					inNumber = true;
				}
				else if (c == L'.')
				{
					parts.push_back(current);
					current = 0;
					inNumber = false;
				}
				else
				{
					break; // "0.3.1-beta": the suffix is ignored
				}
			}
			if (inNumber || !parts.empty())
			{
				parts.push_back(current);
			}
			return parts;
		}
	}

	int CompareVersions(std::wstring_view a, std::wstring_view b)
	{
		const std::vector<unsigned long> left = SplitVersion(a);
		const std::vector<unsigned long> right = SplitVersion(b);
		for (size_t i = 0; i < std::max(left.size(), right.size()); ++i)
		{
			const unsigned long x = i < left.size() ? left[i] : 0;
			const unsigned long y = i < right.size() ? right[i] : 0;
			if (x != y)
			{
				return x < y ? -1 : 1;
			}
		}
		return 0;
	}

	std::optional<ReleaseInfo> ParseLatestRelease(std::string_view json)
	{
		const Json document = Json::parse(json, nullptr, false);
		if (document.is_discarded() || !document.is_object())
		{
			return std::nullopt;
		}
		std::wstring tag = GetString(document, "tag_name");
		if (tag.empty())
		{
			return std::nullopt;
		}
		if (tag.front() == L'v' || tag.front() == L'V')
		{
			tag.erase(0, 1);
		}
		return ReleaseInfo{tag, GetString(document, "html_url")};
	}

	std::optional<ReleaseInfo> FetchLatestRelease(std::wstring_view repository, std::wstring& error)
	{
		unique_hinternet session(WinHttpOpen(
			L"AppTray/" APPTRAY_VERSION_WIDE, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
			WINHTTP_NO_PROXY_BYPASS, 0));
		if (!session)
		{
			error = std::format(L"WinHttpOpen failed ({})", GetLastError());
			return std::nullopt;
		}
		WinHttpSetTimeouts(session.get(), 15000, 15000, 15000, 15000);
		unique_hinternet connection(WinHttpConnect(session.get(), L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0));
		const std::wstring path = std::format(L"/repos/{}/releases/latest", repository);
		unique_hinternet request(
			connection ? WinHttpOpenRequest(
							 connection.get(), L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
							 WINHTTP_FLAG_SECURE)
					   : nullptr);
		const wchar_t* headers = L"Accept: application/vnd.github+json\r\n";
		if (!request
			|| !WinHttpSendRequest(request.get(), headers, static_cast<DWORD>(-1L), WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
			|| !WinHttpReceiveResponse(request.get(), nullptr))
		{
			error = std::format(L"cannot reach api.github.com ({})", GetLastError());
			return std::nullopt;
		}

		DWORD status = 0;
		DWORD size = sizeof(status);
		WinHttpQueryHeaders(
			request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status,
			&size, WINHTTP_NO_HEADER_INDEX);
		if (status == 404)
		{
			return std::nullopt; // no release published yet
		}
		if (status != 200)
		{
			error = std::format(L"GitHub answered HTTP {}", status);
			return std::nullopt;
		}

		std::string body;
		for (;;)
		{
			DWORD available = 0;
			if (!WinHttpQueryDataAvailable(request.get(), &available) || available == 0 || body.size() > 1024 * 1024)
			{
				break;
			}
			const size_t offset = body.size();
			body.resize(offset + available);
			DWORD read = 0;
			if (!WinHttpReadData(request.get(), body.data() + offset, available, &read))
			{
				break;
			}
			body.resize(offset + read);
		}
		std::optional<ReleaseInfo> release = ParseLatestRelease(body);
		if (!release)
		{
			error = L"unexpected answer from GitHub";
		}
		return release;
	}

	std::optional<UpdateState> LoadUpdateState(const std::filesystem::path& file)
	{
		std::wstring error;
		const std::optional<Json> json = ReadJsonFile(file, error);
		if (!json || !json->is_object())
		{
			return std::nullopt;
		}
		return UpdateState{
			GetString(*json, "checkedAt"), GetString(*json, "latestVersion"), GetString(*json, "url"),
			GetString(*json, "notifiedVersion")};
	}

	bool SaveUpdateState(const std::filesystem::path& file, const UpdateState& state, std::wstring& error)
	{
		Json json = Json::object();
		json["checkedAt"] = ToJson(state.checkedAt);
		json["latestVersion"] = ToJson(state.latestVersion);
		json["url"] = ToJson(state.url);
		json["notifiedVersion"] = ToJson(state.notifiedVersion);
		return WriteJsonFile(file, json, error);
	}
}
