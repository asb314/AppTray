#include "ExeInspector.h"

#include "NameTokens.h"
#include "StringUtil.h"

#include <windows.h>

#include <wil/resource.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <vector>

#pragma comment(lib, "version.lib")

namespace apptray
{
	namespace
	{
		constexpr DWORD HeaderBytes = 4096;

		template <typename T> bool ReadStruct(const std::vector<BYTE>& buffer, size_t offset, T& value)
		{
			if (offset + sizeof(T) > buffer.size())
			{
				return false;
			}
			std::memcpy(&value, buffer.data() + offset, sizeof(T));
			return true;
		}

		void ReadPeHeader(const std::filesystem::path& file, ExeInfo& info)
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
				return;
			}
			LARGE_INTEGER size{};
			if (GetFileSizeEx(handle.get(), &size))
			{
				info.size = static_cast<std::uint64_t>(size.QuadPart);
			}

			std::vector<BYTE> buffer(HeaderBytes);
			DWORD read = 0;
			if (!ReadFile(handle.get(), buffer.data(), HeaderBytes, &read, nullptr))
			{
				return;
			}
			buffer.resize(read);

			IMAGE_DOS_HEADER dos{};
			if (!ReadStruct(buffer, 0, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0)
			{
				return;
			}
			const size_t ntOffset = static_cast<size_t>(dos.e_lfanew);
			DWORD signature = 0;
			IMAGE_FILE_HEADER fileHeader{};
			WORD magic = 0;
			if (!ReadStruct(buffer, ntOffset, signature) || signature != IMAGE_NT_SIGNATURE
				|| !ReadStruct(buffer, ntOffset + sizeof(DWORD), fileHeader)
				|| !ReadStruct(buffer, ntOffset + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER), magic))
			{
				return;
			}

			const size_t optionalOffset = ntOffset + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER);
			WORD subsystem = 0;
			if (magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC)
			{
				IMAGE_OPTIONAL_HEADER64 optional{};
				if (!ReadStruct(buffer, optionalOffset, optional))
				{
					return;
				}
				subsystem = optional.Subsystem;
			}
			else if (magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC)
			{
				IMAGE_OPTIONAL_HEADER32 optional{};
				if (!ReadStruct(buffer, optionalOffset, optional))
				{
					return;
				}
				subsystem = optional.Subsystem;
			}
			else
			{
				return;
			}

			info.isPe = true;
			info.machine = fileHeader.Machine;
			info.isGui = subsystem == IMAGE_SUBSYSTEM_WINDOWS_GUI;
			info.isConsole = subsystem == IMAGE_SUBSYSTEM_WINDOWS_CUI;
		}

		std::wstring QueryVersionString(const std::vector<BYTE>& data, std::wstring_view block, std::wstring_view key)
		{
			const std::wstring query = std::format(L"\\StringFileInfo\\{}\\{}", block, key);
			wchar_t* value = nullptr;
			UINT length = 0;
			if (VerQueryValueW(data.data(), query.c_str(), reinterpret_cast<void**>(&value), &length)
				&& value != nullptr && length > 0)
			{
				return Trim(std::wstring(value, wcsnlen(value, length)));
			}
			return {};
		}

		void ReadVersionInfo(const std::filesystem::path& file, ExeInfo& info, std::wstring& comments)
		{
			DWORD ignored = 0;
			const DWORD size = GetFileVersionInfoSizeExW(FILE_VER_GET_NEUTRAL, file.c_str(), &ignored);
			if (size == 0)
			{
				return;
			}
			std::vector<BYTE> data(size);
			if (!GetFileVersionInfoExW(FILE_VER_GET_NEUTRAL, file.c_str(), 0, size, data.data()))
			{
				return;
			}

			std::vector<std::wstring> blocks;
			struct Translation
			{
				WORD language;
				WORD codePage;
			};
			Translation* translations = nullptr;
			UINT length = 0;
			if (VerQueryValueW(
					data.data(),
					L"\\VarFileInfo\\Translation",
					reinterpret_cast<void**>(&translations),
					&length)
				&& translations != nullptr)
			{
				for (UINT i = 0; i < length / sizeof(Translation); ++i)
				{
					blocks.push_back(
						std::format(L"{:04x}{:04x}", translations[i].language, translations[i].codePage));
				}
			}
			for (const wchar_t* fallback : {L"040904b0", L"040904e4", L"000004b0", L"0c0a04b0"})
			{
				blocks.emplace_back(fallback);
			}

			for (const std::wstring& block : blocks)
			{
				if (info.productName.empty())
				{
					info.productName = QueryVersionString(data, block, L"ProductName");
				}
				if (info.fileDescription.empty())
				{
					info.fileDescription = QueryVersionString(data, block, L"FileDescription");
				}
				if (comments.empty())
				{
					comments = QueryVersionString(data, block, L"Comments");
				}
			}
		}

		void ReadManifest(const std::filesystem::path& file, ExeInfo& info)
		{
			wil::unique_hmodule module(LoadLibraryExW(
				file.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE));
			if (!module)
			{
				return;
			}
			HRSRC resource = FindResourceW(module.get(), MAKEINTRESOURCEW(1), RT_MANIFEST);
			if (resource == nullptr)
			{
				return;
			}
			const DWORD size = SizeofResource(module.get(), resource);
			HGLOBAL loaded = LoadResource(module.get(), resource);
			const void* bytes = loaded != nullptr ? LockResource(loaded) : nullptr;
			if (bytes == nullptr || size == 0)
			{
				return;
			}
			std::string manifest(static_cast<const char*>(bytes), size);
			std::transform(
				manifest.begin(),
				manifest.end(),
				manifest.begin(),
				[](char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c); });
			info.requiresAdmin = manifest.find("requireadministrator") != std::string::npos
				|| manifest.find("highestavailable") != std::string::npos;
		}

		// "Firebird Setup", "7-Zip Installer", "This installation was built with Inno Setup".
		// Not "Inno Setup Compiler": "setup" counts only as the last word.
		bool DescribesInstaller(std::wstring_view text)
		{
			const std::vector<std::wstring> words = SplitNameWords(text, false);
			for (const std::wstring& word : words)
			{
				for (const std::wstring_view strong :
					 {L"installer", L"instalador", L"installation", L"uninstaller", L"redistributable"})
				{
					if (word == strong)
					{
						return true;
					}
				}
			}
			return !words.empty() && (words.back() == L"setup" || words.back() == L"install");
		}
	}

	ExeInfo InspectExe(const std::filesystem::path& file, InspectDetail detail)
	{
		ExeInfo info;
		ReadPeHeader(file, info);
		if (!info.isPe)
		{
			return info;
		}
		std::wstring comments;
		ReadVersionInfo(file, info, comments);
		if (detail == InspectDetail::Full)
		{
			ReadManifest(file, info);
		}
		info.looksLikeInstaller = DescribesInstaller(info.fileDescription) || DescribesInstaller(comments);
		return info;
	}

	bool ReadRequiresAdmin(const std::filesystem::path& file)
	{
		ExeInfo info;
		ReadManifest(file, info);
		return info.requiresAdmin;
	}
}
