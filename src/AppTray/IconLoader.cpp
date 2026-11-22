#include "IconLoader.h"

#include "ConsoleCommand.h"
#include "StringUtil.h"

#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wincodec.h>

#include <wil/com.h>
#include <wil/resource.h>

#include <cstring>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

namespace fs = std::filesystem;

namespace apptray
{
	namespace
	{
		// The icon resource of a file (exe, dll), without the shell's icon handlers
		HICON ExtractFileIcon(const fs::path& file, int index, int size)
		{
			HICON icon = nullptr;
			if (SHDefExtractIconW(file.c_str(), index, 0, &icon, nullptr, MAKELONG(size, 0)) != S_OK)
			{
				return nullptr;
			}
			return icon;
		}

		HICON GetStockIcon(SHSTOCKICONID id, int size)
		{
			SHSTOCKICONINFO stock{};
			stock.cbSize = sizeof(stock);
			const UINT flags = SHGSI_ICON | (size <= 16 ? SHGSI_SMALLICON : SHGSI_LARGEICON);
			return SUCCEEDED(SHGetStockIconInfo(id, flags, &stock)) ? stock.hIcon : nullptr;
		}

		fs::path GetSystemFile(const wchar_t* name)
		{
			wchar_t directory[MAX_PATH]{};
			GetSystemDirectoryW(directory, MAX_PATH);
			return fs::path(directory) / name;
		}

		std::wstring ExpandEnvironment(const std::wstring& text)
		{
			wchar_t expanded[MAX_PATH * 2]{};
			const DWORD length = ExpandEnvironmentStringsW(text.c_str(), expanded, static_cast<DWORD>(std::size(expanded)));
			return length > 0 && length <= std::size(expanded) ? std::wstring(expanded) : text;
		}

		// What a shortcut points to, read straight from the .lnk file (MS-SHLLINK). The shell's
		// IShellLink would do it, but it loads the same heavy machinery as SHGetFileInfo.
		struct ShortcutInfo
		{
			std::wstring iconPath;
			int iconIndex = 0;
			std::wstring target;
		};

		std::optional<ShortcutInfo> ReadShortcut(const fs::path& shortcut)
		{
			std::vector<BYTE> data;
			{
				wil::unique_hfile file(CreateFileW(
					shortcut.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
				LARGE_INTEGER size{};
				if (!file || !GetFileSizeEx(file.get(), &size) || size.QuadPart < 0x4C || size.QuadPart > 1024 * 1024)
				{
					return std::nullopt;
				}
				data.resize(static_cast<size_t>(size.QuadPart));
				DWORD read = 0;
				if (!ReadFile(file.get(), data.data(), static_cast<DWORD>(data.size()), &read, nullptr) || read != data.size())
				{
					return std::nullopt;
				}
			}
			const auto readU16 = [&](size_t offset) -> std::optional<std::uint16_t>
			{
				if (offset + 2 > data.size())
				{
					return std::nullopt;
				}
				return static_cast<std::uint16_t>(data[offset] | (data[offset + 1] << 8));
			};
			const auto readU32 = [&](size_t offset) -> std::optional<std::uint32_t>
			{
				if (offset + 4 > data.size())
				{
					return std::nullopt;
				}
				std::uint32_t value = 0;
				std::memcpy(&value, data.data() + offset, 4);
				return value;
			};
			const auto readAnsiZ = [&](size_t offset) -> std::wstring
			{
				size_t end = offset;
				while (end < data.size() && data[end] != 0)
				{
					++end;
				}
				if (end <= offset || end >= data.size())
				{
					return {};
				}
				const int length = MultiByteToWideChar(
					CP_ACP, 0, reinterpret_cast<const char*>(data.data() + offset), static_cast<int>(end - offset), nullptr, 0);
				std::wstring text(static_cast<size_t>(length), L'\0');
				MultiByteToWideChar(
					CP_ACP, 0, reinterpret_cast<const char*>(data.data() + offset), static_cast<int>(end - offset),
					text.data(), length);
				return text;
			};
			const auto readUnicodeZ = [&](size_t offset) -> std::wstring
			{
				std::wstring text;
				for (size_t i = offset; i + 1 < data.size(); i += 2)
				{
					const wchar_t c = static_cast<wchar_t>(data[i] | (data[i + 1] << 8));
					if (c == 0)
					{
						return text;
					}
					text.push_back(c);
				}
				return {};
			};

			constexpr std::uint32_t HasTargetIdList = 0x1;
			constexpr std::uint32_t HasLinkInfo = 0x2;
			constexpr std::uint32_t HasName = 0x4;
			constexpr std::uint32_t HasRelativePath = 0x8;
			constexpr std::uint32_t HasWorkingDir = 0x10;
			constexpr std::uint32_t HasArguments = 0x20;
			constexpr std::uint32_t HasIconLocation = 0x40;
			constexpr std::uint32_t IsUnicode = 0x80;

			if (readU32(0).value_or(0) != 0x4C)
			{
				return std::nullopt;
			}
			const std::uint32_t flags = readU32(0x14).value_or(0);
			ShortcutInfo info;
			info.iconIndex = static_cast<int>(readU32(0x38).value_or(0));
			size_t offset = 0x4C;
			if ((flags & HasTargetIdList) != 0)
			{
				const std::optional<std::uint16_t> idListSize = readU16(offset);
				if (!idListSize)
				{
					return std::nullopt;
				}
				offset += 2 + *idListSize;
			}
			if ((flags & HasLinkInfo) != 0)
			{
				const size_t start = offset;
				const std::uint32_t linkInfoSize = readU32(start).value_or(0);
				const std::uint32_t headerSize = readU32(start + 4).value_or(0);
				const std::uint32_t linkInfoFlags = readU32(start + 8).value_or(0);
				if ((linkInfoFlags & 0x1) != 0) // volume id and local base path
				{
					const std::uint32_t unicodeOffset = headerSize >= 0x24 ? readU32(start + 0x1C).value_or(0) : 0;
					info.target = unicodeOffset != 0 ? readUnicodeZ(start + unicodeOffset)
													 : readAnsiZ(start + readU32(start + 0x10).value_or(0));
				}
				offset = start + linkInfoSize;
			}
			const auto readStringData = [&]() -> std::wstring
			{
				const std::optional<std::uint16_t> count = readU16(offset);
				if (!count)
				{
					return {};
				}
				offset += 2;
				std::wstring text;
				if ((flags & IsUnicode) != 0)
				{
					for (size_t i = 0; i < *count && offset + 1 < data.size(); ++i, offset += 2)
					{
						text.push_back(static_cast<wchar_t>(data[offset] | (data[offset + 1] << 8)));
					}
				}
				else
				{
					for (size_t i = 0; i < *count && offset < data.size(); ++i, ++offset)
					{
						text.push_back(static_cast<wchar_t>(data[offset]));
					}
				}
				return text;
			};
			std::wstring relativePath;
			if ((flags & HasName) != 0)
			{
				readStringData();
			}
			if ((flags & HasRelativePath) != 0)
			{
				relativePath = readStringData();
			}
			if ((flags & HasWorkingDir) != 0)
			{
				readStringData();
			}
			if ((flags & HasArguments) != 0)
			{
				readStringData();
			}
			if ((flags & HasIconLocation) != 0)
			{
				info.iconPath = readStringData();
			}
			if (info.target.empty() && !relativePath.empty())
			{
				info.target = (shortcut.parent_path() / relativePath).lexically_normal().wstring();
			}
			return info;
		}

		// A shortcut shows its own icon location, or its target's icon
		HICON ExtractShortcutIcon(const fs::path& shortcut, int size)
		{
			const std::optional<ShortcutInfo> info = ReadShortcut(shortcut);
			if (!info)
			{
				return nullptr;
			}
			if (!info->iconPath.empty())
			{
				if (HICON icon = ExtractFileIcon(ExpandEnvironment(info->iconPath), info->iconIndex, size))
				{
					return icon;
				}
			}
			if (!info->target.empty())
			{
				return ExtractFileIcon(ExpandEnvironment(info->target), 0, size);
			}
			return nullptr;
		}
	}

	// SHGetFileInfo is avoided on purpose: it loads the whole shell namespace and leaves
	// several threads and ~1.5 MB behind in a process that should stay small.
	HICON ExtractAppIcon(const fs::path& path, int size)
	{
		const std::wstring text = path.wstring();
		HICON icon = nullptr;
		std::error_code error;
		if (fs::is_directory(path, error))
		{
			icon = GetStockIcon(SIID_FOLDER, size);
		}
		else if (EndsWithIgnoreCase(text, L".exe"))
		{
			icon = ExtractFileIcon(path, 0, size);
		}
		else if (EndsWithIgnoreCase(text, L".bat") || EndsWithIgnoreCase(text, L".cmd"))
		{
			icon = ExtractFileIcon(GetSystemFile(L"cmd.exe"), 0, size);
		}
		else if (IsPowerShellScript(path))
		{
			// The console it runs in. A Store pwsh is an alias with no icon to read.
			icon = ExtractFileIcon(FindShellProgram(ConsoleShell::PowerShell), 0, size);
			if (icon == nullptr)
			{
				icon = ExtractFileIcon(GetSystemFile(L"WindowsPowerShell\\v1.0\\powershell.exe"), 0, size);
			}
		}
		else if (EndsWithIgnoreCase(text, L".lnk"))
		{
			icon = ExtractShortcutIcon(path, size);
		}
		return icon != nullptr ? icon : GetStockIcon(SIID_APPLICATION, size);
	}

	HBITMAP CreateMenuBitmap(HICON icon, int size)
	{
		wil::com_ptr_nothrow<IWICImagingFactory> factory;
		if (icon == nullptr
			|| FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
		{
			return nullptr;
		}
		wil::com_ptr_nothrow<IWICBitmap> bitmap;
		wil::com_ptr_nothrow<IWICBitmapScaler> scaler;
		wil::com_ptr_nothrow<IWICFormatConverter> converter;
		const UINT side = static_cast<UINT>(size);
		if (FAILED(factory->CreateBitmapFromHICON(icon, &bitmap)) || FAILED(factory->CreateBitmapScaler(&scaler))
			|| FAILED(scaler->Initialize(bitmap.get(), side, side, WICBitmapInterpolationModeFant))
			|| FAILED(factory->CreateFormatConverter(&converter))
			|| FAILED(converter->Initialize(
				scaler.get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
		{
			return nullptr;
		}

		BITMAPINFO info{};
		info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		info.bmiHeader.biWidth = size;
		info.bmiHeader.biHeight = -size; // top-down
		info.bmiHeader.biPlanes = 1;
		info.bmiHeader.biBitCount = 32;
		info.bmiHeader.biCompression = BI_RGB;
		void* bits = nullptr;
		HBITMAP dib = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
		if (dib == nullptr)
		{
			return nullptr;
		}
		const UINT stride = side * 4;
		if (FAILED(converter->CopyPixels(nullptr, stride, stride * side, static_cast<BYTE*>(bits))))
		{
			DeleteObject(dib);
			return nullptr;
		}
		return dib;
	}

	IconWorker::IconWorker(HWND notify, UINT message)
		: state_(std::make_shared<State>())
	{
		state_->notify = notify;
		state_->message = message;
		std::thread([state = state_] { Run(state); }).detach();
	}

	IconWorker::~IconWorker()
	{
		{
			std::scoped_lock lock(state_->mutex);
			state_->stopped = true;
			state_->jobs.clear();
		}
		state_->wake.notify_all();
	}

	void IconWorker::Request(std::uint64_t generation, std::size_t key, fs::path path, int size)
	{
		{
			std::scoped_lock lock(state_->mutex);
			state_->jobs.push_back({generation, key, std::move(path), size});
		}
		state_->wake.notify_one();
	}

	void IconWorker::Clear()
	{
		std::scoped_lock lock(state_->mutex);
		state_->jobs.clear();
	}

	void IconWorker::Run(std::shared_ptr<State> state)
	{
		// Shell icon lookups want COM on the calling thread
		const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
		for (;;)
		{
			Job job;
			{
				std::unique_lock lock(state->mutex);
				state->wake.wait(lock, [&] { return state->stopped || !state->jobs.empty(); });
				if (state->stopped)
				{
					break;
				}
				job = std::move(state->jobs.front());
				state->jobs.pop_front();
			}
			auto result = std::make_unique<IconResult>();
			result->generation = job.generation;
			result->key = job.key;
			result->icon = ExtractAppIcon(job.path, job.size);
			bool posted = false;
			{
				// Checked under the lock: once the worker is destroyed, nothing reaches the window
				std::scoped_lock lock(state->mutex);
				posted = !state->stopped
					&& PostMessageW(state->notify, state->message, 0, reinterpret_cast<LPARAM>(result.get()));
			}
			if (posted)
			{
				result.release();
			}
			else if (result->icon != nullptr)
			{
				DestroyIcon(result->icon);
			}
		}
		if (SUCCEEDED(com))
		{
			CoUninitialize();
		}
	}
}
