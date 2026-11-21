#include "UserSettings.h"

#include "PathResolver.h"
#include "StringUtil.h"

#include <algorithm>

namespace fs = std::filesystem;

namespace apptray
{
	namespace
	{
		constexpr int SettingsVersion = 1;

		std::wstring_view ToString(AutostartMode mode)
		{
			switch (mode)
			{
			case AutostartMode::Always:
				return L"always";
			case AutostartMode::Ask:
				return L"ask";
			case AutostartMode::No:
				break;
			}
			return L"no";
		}

		AutostartMode ParseAutostart(std::wstring_view text)
		{
			if (text == L"always")
			{
				return AutostartMode::Always;
			}
			if (text == L"ask")
			{
				return AutostartMode::Ask;
			}
			return AutostartMode::No;
		}

		std::vector<std::wstring> ReadStringArray(const Json& object, const char* key, std::vector<std::wstring> fallback)
		{
			const auto found = object.find(key);
			if (found == object.end() || !found->is_array())
			{
				return fallback;
			}
			std::vector<std::wstring> values;
			for (const Json& item : *found)
			{
				if (item.is_string())
				{
					values.push_back(FromUtf8(item.get_ref<const std::string&>()));
				}
			}
			return values;
		}

		Json SerializeStringArray(const std::vector<std::wstring>& values)
		{
			Json array = Json::array();
			for (const std::wstring& value : values)
			{
				array.push_back(ToJson(value));
			}
			return array;
		}

		AppOverride ParseOverride(const Json& json)
		{
			AppOverride item;
			item.hidden = GetBool(json, "hidden");
			item.name = GetOptionalString(json, "name");
			item.category = GetOptionalString(json, "category");
			item.target = GetOptionalString(json, "target");
			item.args = GetOptionalString(json, "args");
			item.expand = GetBool(json, "expand");
			item.autostart = ParseAutostart(GetString(json, "autostart"));
			item.askDefault = GetBool(json, "askDefault");
			item.elevatedTask = GetBool(json, "elevatedTask");
			return item;
		}

		// Writes known fields over the stored object, so unknown keys stay
		Json SerializeOverride(const AppOverride& item, Json json)
		{
			const auto setBool = [&](const char* key, bool value)
			{
				if (value)
				{
					json[key] = true;
				}
				else
				{
					json.erase(key);
				}
			};
			const auto setString = [&](const char* key, const std::optional<std::wstring>& value)
			{
				if (value)
				{
					json[key] = ToJson(*value);
				}
				else
				{
					json.erase(key);
				}
			};
			setBool("hidden", item.hidden);
			setString("name", item.name);
			setString("category", item.category);
			setString("target", item.target);
			setString("args", item.args);
			setBool("expand", item.expand);
			if (item.autostart != AutostartMode::No)
			{
				json["autostart"] = ToJson(ToString(item.autostart));
			}
			else
			{
				json.erase("autostart");
			}
			setBool("askDefault", item.askDefault);
			setBool("elevatedTask", item.elevatedTask);
			return json;
		}
	}

	bool AppOverride::IsEmpty() const
	{
		return !hidden && !name && !category && !target && !args && !expand
			&& autostart == AutostartMode::No && !askDefault && !elevatedTask;
	}

	bool UserSettings::IsFavorite(const std::wstring& id) const
	{
		return std::any_of(favorites.begin(), favorites.end(), [&](const std::wstring& f) { return IsSameRelative(f, id); });
	}

	bool UserSettings::HasUserData(const std::wstring& id) const
	{
		if (IsFavorite(id))
		{
			return true;
		}
		const AppOverride* item = FindOverride(id);
		return item != nullptr && !item->IsEmpty();
	}

	std::vector<std::wstring> UserSettings::ExpandedIds() const
	{
		std::vector<std::wstring> ids;
		for (const auto& [id, item] : apps)
		{
			if (item.expand)
			{
				ids.push_back(NormalizeRelative(id));
			}
		}
		return ids;
	}

	const AppOverride* UserSettings::FindOverride(const std::wstring& id) const
	{
		for (const auto& [key, item] : apps)
		{
			if (IsSameRelative(key, id))
			{
				return &item;
			}
		}
		return nullptr;
	}

	bool UserSettings::IsExpanded(const std::wstring& id) const
	{
		const AppOverride* item = FindOverride(id);
		return item != nullptr && item->expand;
	}

	namespace
	{
		// Changes the override of an id, reusing a key the user wrote with other casing
		template <typename Change> void ChangeOverride(UserSettings& settings, const std::wstring& id, Change change)
		{
			std::wstring key = id;
			for (const auto& [existing, item] : settings.apps)
			{
				if (IsSameRelative(existing, id))
				{
					key = existing;
					break;
				}
			}
			AppOverride& item = settings.apps[key];
			change(item);
			if (item.IsEmpty())
			{
				// Keys the user added by hand to this entry survive: SaveUserSettings keeps them
				settings.apps.erase(key);
			}
		}
	}

	void ToggleFavorite(UserSettings& settings, const std::wstring& id)
	{
		const auto found = std::find_if(
			settings.favorites.begin(), settings.favorites.end(), [&](const std::wstring& f) { return IsSameRelative(f, id); });
		if (found == settings.favorites.end())
		{
			settings.favorites.push_back(id);
		}
		else
		{
			settings.favorites.erase(found);
		}
	}

	void SetHidden(UserSettings& settings, const std::wstring& id, bool hidden)
	{
		ChangeOverride(settings, id, [&](AppOverride& item) { item.hidden = hidden; });
	}

	void SetTargetOverride(UserSettings& settings, const std::wstring& id, std::optional<std::wstring> target)
	{
		ChangeOverride(settings, id, [&](AppOverride& item) { item.target = std::move(target); });
	}

	void SetExpanded(UserSettings& settings, const std::wstring& id, bool expanded)
	{
		ChangeOverride(settings, id, [&](AppOverride& item) { item.expand = expanded; });
	}

	void SetAutostart(UserSettings& settings, const std::wstring& id, AutostartMode mode)
	{
		ChangeOverride(settings, id, [&](AppOverride& item) { item.autostart = mode; });
	}

	void SetAskDefault(UserSettings& settings, const std::wstring& id, bool checked)
	{
		ChangeOverride(settings, id, [&](AppOverride& item) { item.askDefault = checked; });
	}

	void SetElevatedTask(UserSettings& settings, const std::wstring& id, bool enabled)
	{
		ChangeOverride(settings, id, [&](AppOverride& item) { item.elevatedTask = enabled; });
	}

	UserSettings LoadUserSettings(const fs::path& file, std::wstring& error)
	{
		UserSettings settings;
		std::error_code exists;
		if (!fs::exists(file, exists))
		{
			return settings;
		}
		std::optional<Json> json = ReadJsonFile(file, error);
		if (!json || !json->is_object())
		{
			if (json)
			{
				error = file.wstring() + L" is not a JSON object";
			}
			return settings;
		}

		const Json& document = *json;
		settings.root = GetOptionalString(document, "root");
		settings.startupDelaySeconds = GetInt(document, "startupDelaySeconds", settings.startupDelaySeconds);
		settings.startupPromptSeconds = GetInt(document, "startupPromptSeconds", settings.startupPromptSeconds);
		settings.categoryDepth = std::clamp(GetInt(document, "categoryDepth", settings.categoryDepth), 0, 20);
		settings.checkForUpdates = GetBool(document, "checkForUpdates", settings.checkForUpdates);
		if (const auto scan = document.find("scan"); scan != document.end() && scan->is_object())
		{
			settings.scan.maxDepth = std::clamp(GetInt(*scan, "maxDepth", settings.scan.maxDepth), 1, 20);
			settings.scan.ignore = ReadStringArray(*scan, "ignore", settings.scan.ignore);
			settings.scan.skipNames = ReadStringArray(*scan, "skipNames", settings.scan.skipNames);
		}
		settings.favorites = ReadStringArray(document, "favorites", {});
		if (const auto apps = document.find("apps"); apps != document.end() && apps->is_object())
		{
			for (const auto& [key, value] : apps->items())
			{
				if (value.is_object())
				{
					settings.apps[FromUtf8(key)] = ParseOverride(value);
				}
			}
		}
		settings.document = std::move(*json);
		return settings;
	}

	bool SaveUserSettings(const fs::path& file, const UserSettings& settings, std::wstring& error)
	{
		Json json = settings.document.is_object() ? settings.document : Json::object();
		json["version"] = SettingsVersion;
		if (settings.root)
		{
			json["root"] = ToJson(*settings.root);
		}
		else
		{
			json.erase("root");
		}
		json.erase("startWithWindows"); // replaced by the Run registry value (tray menu)
		json["startupDelaySeconds"] = settings.startupDelaySeconds;
		json["startupPromptSeconds"] = settings.startupPromptSeconds;
		json["categoryDepth"] = settings.categoryDepth;
		json["checkForUpdates"] = settings.checkForUpdates;

		Json scan = json.contains("scan") && json["scan"].is_object() ? json["scan"] : Json::object();
		scan["maxDepth"] = settings.scan.maxDepth;
		scan["ignore"] = SerializeStringArray(settings.scan.ignore);
		scan["skipNames"] = SerializeStringArray(settings.scan.skipNames);
		json["scan"] = std::move(scan);

		json["favorites"] = SerializeStringArray(settings.favorites);

		const Json storedApps = json.contains("apps") && json["apps"].is_object() ? json["apps"] : Json::object();
		Json apps = Json::object();
		for (const auto& [id, item] : settings.apps)
		{
			const std::string key = ToUtf8(id);
			Json stored = storedApps.contains(key) && storedApps[key].is_object() ? storedApps[key] : Json::object();
			Json value = SerializeOverride(item, std::move(stored));
			if (!value.empty())
			{
				apps[key] = std::move(value);
			}
		}
		// Entries whose known fields were all cleared still carry the keys the user added
		for (const auto& [key, stored] : storedApps.items())
		{
			if (apps.contains(key) || !stored.is_object())
			{
				continue;
			}
			Json rest = SerializeOverride(AppOverride{}, stored);
			if (!rest.empty())
			{
				apps[key] = std::move(rest);
			}
		}
		json["apps"] = std::move(apps);
		return WriteJsonFile(file, json, error);
	}
}
