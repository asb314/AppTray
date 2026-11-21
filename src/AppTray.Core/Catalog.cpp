#include "Catalog.h"

#include "JsonFile.h"
#include "StringUtil.h"

namespace apptray
{
	namespace
	{
		constexpr int CatalogVersion = 1;

		Json SerializeEntry(const AppEntry& entry)
		{
			Json json = Json::object();
			json["id"] = ToJson(entry.id);
			json["kind"] = ToJson(ToString(entry.kind));
			json["name"] = ToJson(entry.name);
			if (!entry.description.empty())
			{
				json["description"] = ToJson(entry.description);
			}
			if (!entry.version.empty())
			{
				json["version"] = ToJson(entry.version);
			}
			json["category"] = ToJson(entry.category);
			json["target"] = ToJson(entry.target);
			if (!entry.alternatives.empty())
			{
				Json alternatives = Json::array();
				for (const std::wstring& alternative : entry.alternatives)
				{
					alternatives.push_back(ToJson(alternative));
				}
				json["alternatives"] = std::move(alternatives);
			}
			if (!entry.readme.empty())
			{
				json["readme"] = ToJson(entry.readme);
			}
			json["confidence"] = entry.confidence;
			if (entry.requiresAdmin)
			{
				json["requiresAdmin"] = true;
			}
			if (entry.console)
			{
				json["console"] = true;
			}
			if (entry.alternate)
			{
				json["alternate"] = true;
			}
			if (entry.missing)
			{
				json["missing"] = true;
			}
			json["firstSeen"] = ToJson(entry.firstSeen);
			if (!entry.renamedFrom.empty())
			{
				json["renamedFrom"] = ToJson(entry.renamedFrom);
			}
			return json;
		}

		std::optional<AppEntry> ParseEntry(const Json& json)
		{
			if (!json.is_object())
			{
				return std::nullopt;
			}
			AppEntry entry;
			entry.id = GetString(json, "id");
			if (entry.id.empty())
			{
				return std::nullopt;
			}
			entry.kind = ParseAppKind(GetString(json, "kind")).value_or(AppKind::Folder);
			entry.name = GetString(json, "name", entry.id);
			entry.description = GetString(json, "description");
			entry.version = GetString(json, "version");
			entry.category = GetString(json, "category");
			entry.target = GetString(json, "target");
			if (const auto found = json.find("alternatives"); found != json.end() && found->is_array())
			{
				for (const Json& item : *found)
				{
					if (item.is_string())
					{
						entry.alternatives.push_back(FromUtf8(item.get_ref<const std::string&>()));
					}
				}
			}
			entry.readme = GetString(json, "readme");
			entry.confidence = GetInt(json, "confidence", 0);
			entry.requiresAdmin = GetBool(json, "requiresAdmin");
			entry.console = GetBool(json, "console");
			entry.alternate = GetBool(json, "alternate");
			entry.missing = GetBool(json, "missing");
			entry.firstSeen = GetString(json, "firstSeen");
			entry.renamedFrom = GetString(json, "renamedFrom");
			return entry;
		}
	}

	std::optional<Catalog> LoadCatalog(const std::filesystem::path& file, std::wstring& error)
	{
		const std::optional<Json> json = ReadJsonFile(file, error);
		if (!json)
		{
			return std::nullopt;
		}
		if (!json->is_object())
		{
			error = file.wstring() + L" is not a JSON object";
			return std::nullopt;
		}
		Catalog catalog;
		catalog.root = GetString(*json, "root");
		catalog.scannedAt = GetString(*json, "scannedAt");
		catalog.baselineAt = GetString(*json, "baselineAt", catalog.scannedAt);
		if (const auto apps = json->find("apps"); apps != json->end() && apps->is_array())
		{
			for (const Json& item : *apps)
			{
				if (std::optional<AppEntry> entry = ParseEntry(item))
				{
					catalog.apps.push_back(std::move(*entry));
				}
			}
		}
		return catalog;
	}

	bool SaveCatalog(const std::filesystem::path& file, const Catalog& catalog, std::wstring& error)
	{
		Json json = Json::object();
		json["version"] = CatalogVersion;
		json["root"] = ToJson(catalog.root);
		json["scannedAt"] = ToJson(catalog.scannedAt);
		json["baselineAt"] = ToJson(catalog.baselineAt);
		Json apps = Json::array();
		for (const AppEntry& entry : catalog.apps)
		{
			apps.push_back(SerializeEntry(entry));
		}
		json["apps"] = std::move(apps);
		return WriteJsonFile(file, json, error);
	}
}
