#include "FolderScanner.h"

#include "ExeRanker.h"
#include "NameTokens.h"
#include "PathResolver.h"
#include "StringUtil.h"

#include <windows.h>

#include <wil/resource.h>

#include <algorithm>
#include <atomic>
#include <exception>
#include <format>
#include <thread>
#include <unordered_map>

namespace fs = std::filesystem;

namespace apptray
{
	namespace
	{
		// Folders never entered anywhere: version control, caches, deliberate backups
		constexpr std::wstring_view AlwaysSkippedNames[] = {
			L".git", L".svn", L".vs", L"node_modules", L"__pycache__", L"site-packages",
			L"_originales", L"_anterior",
		};

		// Folders not searched for executables inside an app folder (resources, not programs).
		// Outside app folders they are walked normally: "C:\Applications\Docs" is a category.
		constexpr std::wstring_view AppResourceNames[] = {
			L"locale", L"locales", L"lang", L"langs", L"language", L"languages", L"translations",
			L"i18n", L"share", L"doc", L"docs", L"help", L"manual", L"platforms", L"imageformats",
			L"iconengines", L"styles",
		};

		// Folder name tokens that mark cracks and key generators: left out of the catalog
		constexpr std::wstring_view ExcludedTokens[] = {
			L"crack", L"cracked", L"keygen", L"keymaker", L"patch", L"activator",
		};

		// Layout folders of a single app: an app may keep all its programs there
		// (WinDirStat\x64, ffmpeg\bin), so a folder with only these below is one app
		constexpr std::wstring_view LayoutNames[] = {
			L"bin", L"bin32", L"bin64", L"x64", L"x86", L"x86_64", L"amd64", L"arm64", L"win32",
			L"win64", L"32-bit", L"64-bit", L"32bit", L"64bit", L"app", L"apps", L"program",
			L"programs", L"lib", L"lib64", L"libexec",
		};

		// Folders that belong to an app when it has programs of its own (SwitchOff\scripts,
		// an embedded Python), but can also stand alone ("Media\Tools", "Dev\Python312")
		constexpr std::wstring_view HelperNames[] = {
			L"tools", L"scripts", L"plugins", L"plugin", L"updater", L"update", L"uninstall",
			L"redist", L"sdk", L"runtime", L"jre", L"java",
		};

		constexpr int CandidateDepth = 2;       // levels below an app folder searched for executables
		constexpr int LayoutCandidateDepth = 4; // ... when the app was found through layout folders
		constexpr size_t CandidateLimit = 400;  // per app folder
		constexpr size_t SuiteThreshold = 15;   // top-level executables without a matching name
		constexpr size_t AlternativeLimit = 40;
		constexpr int MatchThreshold = 40;

		// Below the root, a PowerShell script counts only when named like its folder
		// (WinUtil\winutil.ps1). Others are helpers (build, install, activate): counted, their
		// folder (an app's scripts\, a DLL folder with build.ps1) would turn into an app of its
		// own and split the app around it. They stay alternatives of the app they belong to.
		bool IsHelperScript(std::wstring_view folderName, std::wstring_view fileName)
		{
			return GetLaunchType(fileName) == LaunchType::PowerShellScript
				&& ScoreNameMatch(folderName, fileName) < MatchThreshold;
		}
		constexpr int InspectLimit = 4;   // executables opened per app folder, best ranked first
		constexpr int InspectMargin = 30; // ... and only those within this many points of the best
		constexpr size_t ParallelFolders = 8; // top-level folders scanned at the same time

		template <size_t N>
		bool IsOneOfIgnoreCase(std::wstring_view text, const std::wstring_view (&words)[N])
		{
			return std::any_of(
				std::begin(words), std::end(words), [&](std::wstring_view w) { return EqualsIgnoreCase(text, w); });
		}

		struct FileItem
		{
			std::wstring name;
			std::uint64_t size = 0;
		};

		struct DirectoryListing
		{
			bool found = false;
			std::vector<FileItem> files;
			std::vector<std::wstring> directories;
			bool hasDll = false;
		};

		bool IsLessIgnoreCase(const std::wstring& a, const std::wstring& b)
		{
			return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_LESS_THAN;
		}

		DirectoryListing ListDirectory(const fs::path& directory)
		{
			DirectoryListing listing;
			WIN32_FIND_DATAW data{};
			wil::unique_hfind find(FindFirstFileExW(
				(directory / L"*").c_str(),
				FindExInfoBasic,
				&data,
				FindExSearchNameMatch,
				nullptr,
				FIND_FIRST_EX_LARGE_FETCH));
			if (!find)
			{
				return listing;
			}
			listing.found = true;
			do
			{
				const std::wstring_view name = data.cFileName;
				if (name == L"." || name == L"..")
				{
					continue;
				}
				if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
				{
					constexpr DWORD ignored =
						FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM;
					if ((data.dwFileAttributes & ignored) == 0)
					{
						listing.directories.emplace_back(name);
					}
					continue;
				}
				const std::uint64_t size =
					(static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
				listing.files.push_back({std::wstring(name), size});
				if (EndsWithIgnoreCase(name, L".dll"))
				{
					listing.hasDll = true;
				}
			} while (FindNextFileW(find.get(), &data));

			std::sort(listing.directories.begin(), listing.directories.end(), IsLessIgnoreCase);
			std::sort(
				listing.files.begin(),
				listing.files.end(),
				[](const FileItem& a, const FileItem& b) { return IsLessIgnoreCase(a.name, b.name); });
			return listing;
		}

		bool IsLayoutName(std::wstring_view name)
		{
			return IsOneOfIgnoreCase(name, LayoutNames);
		}

		bool IsHelperName(std::wstring_view name)
		{
			return IsOneOfIgnoreCase(name, HelperNames) || StartsWithIgnoreCase(name, L"python");
		}

		bool HasExcludedToken(std::wstring_view name)
		{
			for (const std::wstring& token : TokenizeName(name, false))
			{
				if (IsOneOfIgnoreCase(token, ExcludedTokens))
				{
					return true;
				}
			}
			return false;
		}

		bool IsPortableAppsFolder(const fs::path& directory)
		{
			std::error_code error;
			return fs::is_regular_file(directory / L"App" / L"AppInfo" / L"appinfo.ini", error);
		}

		// "dbeaver_", "HandBrake_1", "Foo - copia": deliberate backup copies.
		// "CrystalDiskInfo9_9_3" is a version, not a copy (a digit before the '_').
		bool IsBackupName(std::wstring_view name)
		{
			if (name.empty())
			{
				return false;
			}
			if (name.back() == L'_')
			{
				return true;
			}
			const size_t underscore = name.rfind(L'_');
			if (underscore != std::wstring_view::npos && underscore > 0)
			{
				const std::wstring_view suffix = name.substr(underscore + 1);
				const auto isDigit = [](wchar_t c) { return c >= L'0' && c <= L'9'; };
				const bool shortNumber =
					!suffix.empty() && suffix.size() <= 2 && std::all_of(suffix.begin(), suffix.end(), isDigit);
				if (shortNumber && !isDigit(name[underscore - 1]))
				{
					return true;
				}
			}
			const std::wstring lower = ToLower(name);
			return lower.find(L" - copia") != std::wstring::npos || lower.find(L" - copy") != std::wstring::npos;
		}

		std::wstring MakeDisplayName(std::wstring_view text)
		{
			std::wstring result;
			for (const wchar_t c : text)
			{
				const wchar_t mapped = c == L'_' ? L' ' : c;
				if (mapped == L' ' && (result.empty() || result.back() == L' '))
				{
					continue;
				}
				result.push_back(mapped);
			}
			return Trim(result);
		}

		std::wstring Describe(const ExeInfo& info)
		{
			if (info.productName.empty())
			{
				return info.fileDescription;
			}
			if (info.fileDescription.empty() || EqualsIgnoreCase(info.productName, info.fileDescription)
				|| ContainsIgnoreCase(info.fileDescription, info.productName))
			{
				return info.fileDescription.empty() ? info.productName : info.fileDescription;
			}
			return std::format(L"{} — {}", info.productName, info.fileDescription);
		}

		// Help next to the app: Spanish LEEME first (usually written by hand), then README, then CHM
		std::wstring FindReadmeName(const DirectoryListing& listing)
		{
			for (const std::wstring_view wanted : {L"leeme.txt", L"léeme.txt", L"readme.txt", L"readme.md"})
			{
				for (const FileItem& file : listing.files)
				{
					if (EqualsIgnoreCase(file.name, wanted))
					{
						return file.name;
					}
				}
			}
			for (const FileItem& file : listing.files)
			{
				if (EndsWithIgnoreCase(file.name, L".chm"))
				{
					return file.name;
				}
			}
			return {};
		}


		std::wstring ReadIniString(const fs::path& file, const wchar_t* section, const wchar_t* key)
		{
			wchar_t buffer[512]{};
			GetPrivateProfileStringW(section, key, L"", buffer, static_cast<DWORD>(std::size(buffer)), file.c_str());
			return Trim(buffer);
		}

		void AppendEntries(std::vector<AppEntry>& to, std::vector<AppEntry>&& from)
		{
			to.insert(to.end(), std::make_move_iterator(from.begin()), std::make_move_iterator(from.end()));
		}

		class Scanner
		{
		public:
			explicit Scanner(const ScanOptions& options)
				: options_(options), resolver_(options.root)
			{
				for (const std::wstring& path : options.ignorePaths)
				{
					ignore_.push_back(NormalizeRelative(path));
				}
				for (const std::wstring& id : options.expandIds)
				{
					expand_.push_back(NormalizeRelative(id));
				}
			}

			ScanResult Run()
			{
				const ULONGLONG start = GetTickCount64();
				std::error_code error;
				result_.rootFound = fs::is_directory(resolver_.Root(), error) && ListDirectory(resolver_.Root()).found;
				if (result_.rootFound)
				{
					result_.apps = EvaluateRoot();
				}
				result_.cancelled = IsStopRequested();
				result_.elapsedMilliseconds = GetTickCount64() - start;
				return std::move(result_);
			}

		private:
			// The root is always a category. Its folders are independent subtrees, scanned in
			// parallel: most of a cold scan is spent waiting for the antivirus to clear each
			// executable AppTray opens, and those waits overlap well. Results are merged in folder
			// order, so the outcome is the same as a sequential scan.
			std::vector<AppEntry> EvaluateRoot()
			{
				const fs::path& root = resolver_.Root();
				++result_.foldersVisited;
				if (options_.progress)
				{
					options_.progress(L"folder " + root.wstring());
				}
				const DirectoryListing listing = ListDirectory(root);
				// Same order as Evaluate: loose programs, loose installers, then the subfolders
				std::vector<AppEntry> entries;
				for (const bool installers : {false, true})
				{
					for (const FileItem& file : listing.files)
					{
						if (IsLaunchable(file.name) && IsInstallerName(file.name) == installers)
						{
							entries.push_back(MakeFileEntry(root, file, installers));
						}
					}
				}
				if (options_.maxDepth < 1)
				{
					return entries;
				}

				std::vector<fs::path> folders;
				for (const std::wstring& sub : listing.directories)
				{
					if (!IsSkipped(root / sub, sub))
					{
						folders.push_back(root / sub);
					}
				}
				std::vector<std::vector<AppEntry>> appsByFolder(folders.size());
				std::vector<ScanResult> partials(folders.size());
				std::vector<std::exception_ptr> failures(folders.size());
				std::atomic<size_t> next = 0;
				const size_t workerCount = std::min(ParallelFolders, folders.size());
				{
					std::vector<std::jthread> workers;
					for (size_t w = 0; w < workerCount; ++w)
					{
						workers.emplace_back(
							[&]
							{
								for (size_t i = next++; i < folders.size(); i = next++)
								{
									try
									{
										Scanner child(options_);
										appsByFolder[i] = child.Evaluate(folders[i], 1);
										partials[i] = std::move(child.result_);
									}
									catch (...)
									{
										failures[i] = std::current_exception();
									}
								}
							});
					}
				} // joined here

				// Grouped like Evaluate does: other apps, then layout folders, then helper folders
				std::vector<AppEntry> childApps;
				std::vector<AppEntry> layoutApps;
				std::vector<AppEntry> helperApps;
				for (size_t i = 0; i < folders.size(); ++i)
				{
					if (failures[i])
					{
						std::rethrow_exception(failures[i]);
					}
					const std::wstring name = folders[i].filename().wstring();
					const bool isSingleApp = appsByFolder[i].size() == 1
						&& EqualsIgnoreCase(appsByFolder[i].front().id, MakeRelative(folders[i]));
					if (isSingleApp && IsLayoutName(name))
					{
						AppendEntries(layoutApps, std::move(appsByFolder[i]));
					}
					else if (isSingleApp && IsHelperName(name))
					{
						AppendEntries(helperApps, std::move(appsByFolder[i]));
					}
					else
					{
						AppendEntries(childApps, std::move(appsByFolder[i]));
					}
					result_.foldersVisited += partials[i].foldersVisited;
					result_.executablesInspected += partials[i].executablesInspected;
					result_.skippedFolders.insert(
						result_.skippedFolders.end(), partials[i].skippedFolders.begin(), partials[i].skippedFolders.end());
				}
				AppendEntries(entries, std::move(childApps));
				AppendEntries(entries, std::move(layoutApps));
				AppendEntries(entries, std::move(helperApps));
				return entries;
			}

			bool IsStopRequested() const
			{
				return options_.stop.stop_requested();
			}

			std::wstring MakeRelative(const fs::path& path) const
			{
				return resolver_.ToRelative(path).value_or(path.wstring());
			}

			static bool IsListedIgnoreCase(const std::vector<std::wstring>& list, std::wstring_view item)
			{
				return std::any_of(
					list.begin(), list.end(), [&](const std::wstring& value) { return EqualsIgnoreCase(value, item); });
			}

			// user.json "ignore" paths and AppTray's own folder: never scanned, at any level
			bool IsIgnoredPath(const fs::path& directory) const
			{
				std::error_code error;
				const bool isSelf =
					!options_.selfDirectory.empty() && fs::equivalent(directory, options_.selfDirectory, error);
				return isSelf || IsListedIgnoreCase(ignore_, MakeRelative(directory));
			}

			// Folders left out while walking categories. Rule-based ones go to the report.
			bool IsSkipped(const fs::path& directory, const std::wstring& name)
			{
				if (IsOneOfIgnoreCase(name, AlwaysSkippedNames) || IsListedIgnoreCase(options_.skipNames, name))
				{
					return true;
				}
				if (IsIgnoredPath(directory) || HasExcludedToken(name))
				{
					result_.skippedFolders.push_back(MakeRelative(directory));
					return true;
				}
				return false;
			}

			// Folders left out while collecting the executables of one app
			bool IsSkippedInsideApp(const fs::path& directory, const std::wstring& name) const
			{
				return IsOneOfIgnoreCase(name, AlwaysSkippedNames) || IsOneOfIgnoreCase(name, AppResourceNames)
					|| IsListedIgnoreCase(options_.skipNames, name) || HasExcludedToken(name)
					|| IsIgnoredPath(directory);
			}

			// PE header and version resource, cached. Only called for the few files that matter.
			const ExeInfo& Inspect(const fs::path& file)
			{
				const std::wstring key = ToLower(file.wstring());
				auto found = inspected_.find(key);
				if (found == inspected_.end())
				{
					++result_.executablesInspected;
					if (options_.progress)
					{
						options_.progress(L"inspect " + file.wstring());
					}
					found = inspected_.emplace(key, InspectExe(file, InspectDetail::Header)).first;
				}
				return found->second;
			}

			void Inspect(Candidate& candidate)
			{
				if (GetLaunchType(candidate.fileName) == LaunchType::Exe && !candidate.info.isPe)
				{
					const std::uint64_t size = candidate.info.size;
					candidate.info = Inspect(candidate.path);
					candidate.info.size = size;
				}
			}

			// Manifest check, only for the executable an entry will launch
			bool IsElevationRequired(const fs::path& file)
			{
				if (GetLaunchType(file.filename().wstring()) != LaunchType::Exe)
				{
					return false;
				}
				const std::wstring key = ToLower(file.wstring());
				auto found = elevation_.find(key);
				if (found == elevation_.end())
				{
					found = elevation_.emplace(key, ReadRequiresAdmin(file)).first;
				}
				return found->second;
			}

			static Candidate MakeCandidate(
				const fs::path& directory, const FileItem& file, const std::wstring& relativeDir, int depth)
			{
				Candidate candidate;
				candidate.path = directory / file.name;
				candidate.fileName = file.name;
				candidate.relativeDir = relativeDir;
				candidate.depth = depth;
				candidate.info.size = file.size;
				return candidate;
			}

			void CollectCandidates(
				const fs::path& directory,
				const std::wstring& relativeDir,
				int depth,
				int maxDepth,
				std::vector<Candidate>& programs,
				std::vector<Candidate>& installers)
			{
				if (IsStopRequested())
				{
					return;
				}
				const DirectoryListing listing = ListDirectory(directory);
				for (const FileItem& file : listing.files)
				{
					if (programs.size() + installers.size() >= CandidateLimit)
					{
						return;
					}
					if (IsLaunchable(file.name))
					{
						(IsInstallerName(file.name) ? installers : programs)
							.push_back(MakeCandidate(directory, file, relativeDir, depth));
					}
				}
				if (depth >= maxDepth)
				{
					return;
				}
				for (const std::wstring& sub : listing.directories)
				{
					const fs::path subDirectory = directory / sub;
					if (IsSkippedInsideApp(subDirectory, sub))
					{
						continue;
					}
					const std::wstring subRelative = relativeDir.empty() ? sub : relativeDir + L"/" + sub;
					CollectCandidates(subDirectory, subRelative, depth + 1, maxDepth, programs, installers);
				}
			}

			// Ranks by name first (free), then inspects only the few candidates close to the top
			// and ranks again with what their headers say. Inspected installers move out and do
			// not use up the limit (a folder of setups).
			void RankWithInspection(
				std::wstring_view folderName, std::vector<Candidate>& programs, std::vector<Candidate>& installers)
			{
				RankCandidates(folderName, programs);
				if (programs.empty())
				{
					return;
				}
				const int best = programs.front().score;
				int inspected = 0;
				for (Candidate& candidate : programs)
				{
					if (inspected >= InspectLimit || candidate.score < best - InspectMargin)
					{
						break;
					}
					Inspect(candidate);
					if (!candidate.info.looksLikeInstaller)
					{
						++inspected;
					}
				}
				for (auto it = programs.begin(); it != programs.end();)
				{
					if (it->info.looksLikeInstaller)
					{
						installers.push_back(std::move(*it));
						it = programs.erase(it);
					}
					else
					{
						++it;
					}
				}
				RankCandidates(folderName, programs);
			}

			std::wstring MakeRelativeOf(const Candidate& candidate) const
			{
				return MakeRelative(candidate.path);
			}

			AppEntry MakeFileEntry(const fs::path& directory, const FileItem& file, bool installerByName)
			{
				const fs::path path = directory / file.name;
				AppEntry entry;
				entry.id = MakeRelative(path);
				entry.target = entry.id;
				entry.category = MakeRelative(directory);
				entry.name = MakeDisplayName(StripLaunchExtension(file.name));
				entry.confidence = 100;
				bool installer = installerByName;
				if (!installer && GetLaunchType(file.name) == LaunchType::Exe)
				{
					const ExeInfo& info = Inspect(path);
					entry.description = Describe(info);
					entry.console = info.isConsole;
					installer = info.looksLikeInstaller;
					entry.requiresAdmin = !installer && IsElevationRequired(path);
					entry.version = info.version;
				}
				if (entry.version.empty())
				{
					entry.version = FindVersionInName(StripLaunchExtension(file.name));
				}
				entry.kind = installer ? AppKind::Installer : AppKind::File;
				return entry;
			}

			AppEntry MakeInstallerFolderEntry(const fs::path& directory, std::vector<Candidate> installers)
			{
				const std::wstring name = directory.filename().wstring();
				RankCandidates(name, installers);
				AppEntry entry;
				entry.id = MakeRelative(directory);
				entry.kind = AppKind::Installer;
				entry.name = MakeDisplayName(name);
				entry.category = GetParentOfRelative(entry.id);
				entry.target = MakeRelativeOf(installers.front());
				entry.description = Describe(installers.front().info);
				entry.version = installers.front().info.version;
				if (entry.version.empty())
				{
					entry.version = FindVersionInName(name);
				}
				entry.confidence = installers.front().match;
				entry.alternate = IsBackupName(name);
				for (size_t i = 1; i < installers.size() && entry.alternatives.size() < AlternativeLimit; ++i)
				{
					entry.alternatives.push_back(MakeRelativeOf(installers[i]));
				}
				return entry;
			}

			// The executables of an expanded suite become apps of their own; "64" variants win
			std::vector<AppEntry> MakeExpandedEntries(const fs::path& directory, const std::vector<Candidate>& programs)
			{
				std::vector<const Candidate*> topLevel;
				for (const Candidate& candidate : programs)
				{
					if (candidate.depth == 0)
					{
						topLevel.push_back(&candidate);
					}
				}
				const auto getStem = [](const Candidate* c) { return ToLower(StripLaunchExtension(c->fileName)); };
				std::vector<AppEntry> entries;
				for (const Candidate* candidate : topLevel)
				{
					const std::wstring stem = getStem(candidate);
					const bool hasWiderVariant = std::any_of(
						topLevel.begin(),
						topLevel.end(),
						[&](const Candidate* other) { return other != candidate && getStem(other) == stem + L"64"; });
					const bool isArmVariant = stem.ends_with(L"a64")
						&& std::any_of(
							topLevel.begin(),
							topLevel.end(),
							[&](const Candidate* other) { return getStem(other) == stem.substr(0, stem.size() - 3); });
					if (hasWiderVariant || isArmVariant)
					{
						continue;
					}
					entries.push_back(MakeFileEntry(directory, {candidate->fileName, candidate->info.size}, false));
				}
				return entries;
			}

			// One app for this folder (or its expanded programs). Empty if no executable was found
			// within maxDepth levels.
			std::vector<AppEntry> MakeAppFolderEntries(const fs::path& directory, int maxDepth)
			{
				const std::wstring name = directory.filename().wstring();
				const std::wstring id = MakeRelative(directory);
				std::vector<Candidate> programs;
				std::vector<Candidate> installers;
				CollectCandidates(directory, L"", 0, maxDepth, programs, installers);

				if (IsListedIgnoreCase(expand_, id))
				{
					return MakeExpandedEntries(directory, programs);
				}
				if (programs.empty())
				{
					if (installers.empty())
					{
						return {};
					}
					return {MakeInstallerFolderEntry(directory, std::move(installers))};
				}

				const DirectoryListing listing = ListDirectory(directory);
				AppEntry entry;
				entry.id = id;
				entry.name = MakeDisplayName(name);
				entry.category = GetParentOfRelative(id);
				const std::wstring readme = FindReadmeName(listing);
				if (!readme.empty())
				{
					entry.readme = id + L"/" + readme;
				}
				entry.alternate = IsBackupName(name);

				// A suite is decided on names alone: no need to open its many programs. A folder
				// with its own DLLs is one program with helpers (MiniTool ShadowMaker), not a suite.
				RankCandidates(name, programs);
				const size_t topLevel = static_cast<size_t>(std::count_if(
					programs.begin(), programs.end(), [](const Candidate& c) { return c.depth == 0; }));
				if (!listing.hasDll && topLevel > SuiteThreshold && programs.front().match < MatchThreshold)
				{
					entry.kind = AppKind::Suite;
					entry.confidence = programs.front().match;
					entry.description = std::format(L"{} programs", topLevel);
					entry.version = FindVersionInName(name);
					for (const Candidate& candidate : programs)
					{
						if (candidate.depth == 0 && entry.alternatives.size() < AlternativeLimit)
						{
							entry.alternatives.push_back(MakeRelativeOf(candidate));
						}
					}
					return {entry};
				}

				RankWithInspection(name, programs, installers);
				if (programs.empty())
				{
					return {MakeInstallerFolderEntry(directory, std::move(installers))};
				}
				const Candidate& best = programs.front();
				entry.kind = AppKind::Folder;
				entry.confidence = best.match;
				entry.target = MakeRelativeOf(best);
				entry.description = Describe(best.info);
				// Scripts have no version resource: the folder name often carries one
				entry.version = best.info.version.empty() ? FindVersionInName(name) : best.info.version;
				entry.requiresAdmin = IsElevationRequired(best.path);
				entry.console = best.info.isConsole;
				for (size_t i = 1; i < programs.size() && entry.alternatives.size() < AlternativeLimit; ++i)
				{
					entry.alternatives.push_back(MakeRelativeOf(programs[i]));
				}
				return {entry};
			}

			// appinfo.ini "Start=" is trusted only when it points inside the app folder
			std::optional<fs::path> ResolvePortableAppsStart(const fs::path& directory, const std::wstring& start) const
			{
				if (start.empty())
				{
					return std::nullopt;
				}
				const fs::path candidate = (directory / start).lexically_normal();
				const std::optional<std::wstring> relative = resolver_.ToRelative(candidate);
				const std::wstring prefix = MakeRelative(directory) + L"/";
				std::error_code error;
				if (!relative || !StartsWithIgnoreCase(*relative, prefix) || !fs::is_regular_file(candidate, error))
				{
					return std::nullopt;
				}
				return candidate;
			}

			AppEntry MakePortableAppsEntry(const fs::path& directory)
			{
				const fs::path ini = directory / L"App" / L"AppInfo" / L"appinfo.ini";
				const std::wstring name = directory.filename().wstring();
				AppEntry entry;
				entry.id = MakeRelative(directory);
				entry.kind = AppKind::PortableApps;
				entry.category = GetParentOfRelative(entry.id);
				entry.alternate = IsBackupName(name);

				const std::wstring displayName = ReadIniString(ini, L"Details", L"Name");
				entry.name = displayName.empty() ? MakeDisplayName(name) : displayName;
				const DirectoryListing listing = ListDirectory(directory);
				fs::path target =
					ResolvePortableAppsStart(directory, ReadIniString(ini, L"Control", L"Start")).value_or(fs::path{});
				if (target.empty())
				{
					std::vector<Candidate> programs;
					for (const FileItem& file : listing.files)
					{
						if (GetLaunchType(file.name) == LaunchType::Exe && !IsInstallerName(file.name))
						{
							programs.push_back(MakeCandidate(directory, file, L"", 0));
						}
					}
					if (!programs.empty())
					{
						RankCandidates(name, programs);
						target = programs.front().path;
					}
				}

				entry.description = ReadIniString(ini, L"Details", L"Description");
				entry.version = NormalizeVersion(ReadIniString(ini, L"Version", L"DisplayVersion"));
				entry.confidence = 100;
				if (!target.empty())
				{
					entry.target = MakeRelative(target);
					entry.requiresAdmin = IsElevationRequired(target);
					if (entry.description.empty() || entry.version.empty())
					{
						const ExeInfo& info = Inspect(target);
						entry.description = entry.description.empty() ? Describe(info) : entry.description;
						entry.version = entry.version.empty() ? info.version : entry.version;
					}
				}
				if (entry.version.empty())
				{
					entry.version = FindVersionInName(name);
				}
				const std::wstring readme = FindReadmeName(listing);
				if (!readme.empty())
				{
					entry.readme = entry.id + L"/" + readme;
				}
				return entry;
			}

			std::vector<AppEntry> Evaluate(const fs::path& directory, int depth)
			{
				if (IsStopRequested())
				{
					return {};
				}
				++result_.foldersVisited;
				if (options_.progress)
				{
					options_.progress(L"folder " + directory.wstring());
				}
				const std::wstring name = directory.filename().wstring();
				if (depth > 0 && IsPortableAppsFolder(directory))
				{
					return {MakePortableAppsEntry(directory)};
				}

				const DirectoryListing listing = ListDirectory(directory);
				std::vector<FileItem> programs;
				std::vector<FileItem> installers;
				for (const FileItem& file : listing.files)
				{
					if (IsLaunchable(file.name) && !IsHelperScript(name, file.name))
					{
						(IsInstallerName(file.name) ? installers : programs).push_back(file);
					}
				}
				const bool nameMatches = std::any_of(
					programs.begin(),
					programs.end(),
					[&](const FileItem& f) { return ScoreNameMatch(name, f.name) >= MatchThreshold; });

				// An app folder: its programs match its name, or it carries its own DLLs
				if (depth > 0 && !programs.empty() && (nameMatches || listing.hasDll))
				{
					std::vector<AppEntry> entries = MakeAppFolderEntries(directory, CandidateDepth);
					if (!entries.empty())
					{
						return entries;
					}
				}

				// Subfolders that are themselves one app go by name: layout (bin, x64), helper
				// (tools, scripts, python) or other. A subfolder that held several apps, or apps
				// further down, acted as a category ("Media\Tools") and always counts as other.
				std::vector<AppEntry> childApps;
				std::vector<AppEntry> layoutApps;
				std::vector<AppEntry> helperApps;
				if (depth < options_.maxDepth)
				{
					for (const std::wstring& sub : listing.directories)
					{
						const fs::path subDirectory = directory / sub;
						if (IsSkipped(subDirectory, sub))
						{
							continue;
						}
						std::vector<AppEntry> apps = Evaluate(subDirectory, depth + 1);
						const std::wstring subId = MakeRelative(subDirectory);
						const bool isSingleApp = apps.size() == 1 && EqualsIgnoreCase(apps.front().id, subId);
						if (isSingleApp && IsLayoutName(sub))
						{
							AppendEntries(layoutApps, std::move(apps));
						}
						else if (isSingleApp && IsHelperName(sub))
						{
							AppendEntries(helperApps, std::move(apps));
						}
						else
						{
							AppendEntries(childApps, std::move(apps));
						}
					}
				}

				if (depth > 0 && childApps.empty())
				{
					// Programs here and only layout or helper folders below (SwitchOff + scripts\)
					if (!programs.empty())
					{
						std::vector<AppEntry> entries = MakeAppFolderEntries(directory, CandidateDepth);
						if (!entries.empty())
						{
							return entries;
						}
					}
					// Programs only in layout folders (WinDirStat\x64, ffmpeg\bin)
					else if (!layoutApps.empty() && helperApps.empty())
					{
						std::vector<AppEntry> entries = MakeAppFolderEntries(directory, LayoutCandidateDepth);
						if (!entries.empty())
						{
							return entries;
						}
					}
					else if (layoutApps.empty() && helperApps.empty() && !installers.empty())
					{
						std::vector<Candidate> candidates;
						for (const FileItem& file : installers)
						{
							candidates.push_back(MakeCandidate(directory, file, L"", 0));
						}
						return {MakeInstallerFolderEntry(directory, std::move(candidates))};
					}
				}

				// A category folder: loose files are single-file apps, subfolders were evaluated
				std::vector<AppEntry> entries;
				for (const FileItem& file : programs)
				{
					entries.push_back(MakeFileEntry(directory, file, false));
				}
				for (const FileItem& file : installers)
				{
					entries.push_back(MakeFileEntry(directory, file, true));
				}
				AppendEntries(entries, std::move(childApps));
				AppendEntries(entries, std::move(layoutApps));
				AppendEntries(entries, std::move(helperApps));
				return entries;
			}

			const ScanOptions& options_;
			PathResolver resolver_;
			std::vector<std::wstring> ignore_;
			std::vector<std::wstring> expand_;
			std::unordered_map<std::wstring, ExeInfo> inspected_;
			std::unordered_map<std::wstring, bool> elevation_;
			ScanResult result_;
		};
	}

	ScanResult ScanFolders(const ScanOptions& options)
	{
		Scanner scanner(options);
		return scanner.Run();
	}
}
