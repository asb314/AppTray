#pragma once

// User-facing texts, in Spanish and English. The language follows Windows' display language
// (Spanish for any Spanish locale, English otherwise) unless user.json "language" says "es" or
// "en". Each text converts to const wchar_t* in the current language; texts with {} go through
// text::Format.

#include <windows.h>

#include <format>
#include <string>
#include <string_view>

namespace apptray::text
{
	enum class Language
	{
		Spanish,
		English,
	};

	inline Language currentLanguage = Language::Spanish;

	inline void SetLanguage(Language language)
	{
		currentLanguage = language;
	}

	inline bool IsSpanish()
	{
		return currentLanguage == Language::Spanish;
	}

	// user.json "language": "es", "en", or anything else (default "auto") for Windows' language
	inline Language ResolveLanguage(std::wstring_view setting)
	{
		if (setting == L"es")
		{
			return Language::Spanish;
		}
		if (setting == L"en")
		{
			return Language::English;
		}
		return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_SPANISH ? Language::Spanish : Language::English;
	}

	struct Text
	{
		const wchar_t* es;
		const wchar_t* en;

		const wchar_t* Get() const
		{
			return IsSpanish() ? es : en;
		}
		operator const wchar_t*() const
		{
			return Get();
		}
		operator std::wstring_view() const
		{
			return Get();
		}
	};

	// std::format with a text of the current language as the format string
	template <typename... Args> std::wstring Format(const Text& format, const Args&... args)
	{
		return std::vformat(std::wstring_view(format.Get()), std::make_wformat_args(args...));
	}

	inline constexpr Text AppName{L"AppTray", L"AppTray"};

	// Tray menu
	inline constexpr Text OpenLauncher{L"Abrir AppTray", L"Open AppTray"};
	inline constexpr Text NoFavorites{L"Sin favoritos (Ctrl+D en la lista)", L"No favorites (Ctrl+D in the list)"};
	inline constexpr Text Empty{L"(vacía)", L"(empty)"};
	inline constexpr Text Refresh{L"Refrescar", L"Refresh"};
	inline constexpr Text Refreshing{L"Refrescando…", L"Refreshing…"};
	inline constexpr Text EditSettings{L"Editar configuración", L"Edit settings"};
	inline constexpr Text StartWithWindows{L"Iniciar con Windows", L"Start with Windows"};
	inline constexpr Text CheckUpdate{L"Comprobar nueva versión", L"Check for a new version"};
	inline constexpr Text CheckingUpdate{L"Comprobando versión…", L"Checking for a new version…"};
	inline constexpr Text About{L"Acerca de AppTray", L"About AppTray"};
	inline constexpr Text AboutText{
		L"Lanzador de bandeja para las apps portables de una carpeta.\n\n"
		L"{}\n"
		L"Atajo de teclado: {}\n\n"
		L"Código, novedades y versiones: <a href=\"repo\">github.com/{}</a>\n"
		L"<a href=\"data\">Abrir la carpeta de datos</a> (user.json, catálogo y registro)\n\n"
		L"Licencia MIT · © 2026 asb314",
		L"Tray launcher for the portable apps of a folder.\n\n"
		L"{}\n"
		L"Hotkey: {}\n\n"
		L"Code, news and releases: <a href=\"repo\">github.com/{}</a>\n"
		L"<a href=\"data\">Open the data folder</a> (user.json, catalog and log)\n\n"
		L"MIT License · © 2026 asb314"};
	inline constexpr Text AboutApps{L"{} apps en {}", L"{} apps in {}"};
	inline constexpr Text AboutScanning{L"Escaneando {}…", L"Scanning {}…"};
	inline constexpr Text AboutNoHotkey{L"ninguno", L"none"};
	inline constexpr Text AboutHotkeyInUse{L"{} (lo usa otro programa)", L"{} (in use by another program)"};
	inline constexpr Text Close{L"Cerrar", L"Close"};
	inline constexpr Text NewVersionMenu{L"Nueva versión {} disponible…", L"New version {} available…"};
	inline constexpr Text Exit{L"Salir", L"Exit"};

	// Launcher window
	inline constexpr Text SearchCue{L"Buscar apps", L"Search apps"};
	inline constexpr Text FolderSearchCue{L"Buscar carpeta", L"Search folders"};
	inline constexpr Text NoFolderMatches{L"Ninguna carpeta coincide", L"No folder matches"};
	inline constexpr Text Favorites{L"Favoritos", L"Favorites"};
	inline constexpr Text Recent{L"Recientes", L"Recent"};
	inline constexpr Text All{L"Todas", L"All"};
	inline constexpr Text New{L"Nuevas", L"New"};
	inline constexpr Text InstallersAndCopies{L"Instaladores y copias", L"Installers and copies"};
	inline constexpr Text Hidden{L"Ocultas", L"Hidden"};
	inline constexpr Text RootCategory{L"(raíz)", L"(root)"};
	inline constexpr Text OpensFolder{L"abre la carpeta", L"opens the folder"};
	inline constexpr Text NotFound{L"no encontrada", L"not found"};
	inline constexpr Text NewTag{L"nueva", L"new"};
	inline constexpr Text NoMatches{L"Nada coincide con la búsqueda", L"Nothing matches the search"};
	inline constexpr Text Keys{
		L"Intro abre · Ctrl+Intro admin · Ctrl+D favorito · F5 refrescar · F10 menú",
		L"Enter opens · Ctrl+Enter as admin · Ctrl+D favorite · F5 refresh · F10 menu"};

	// App context menu
	inline constexpr Text Open{L"Abrir", L"Open"};
	inline constexpr Text OpenAsAdmin{L"Abrir como administrador", L"Open as administrator"};
	inline constexpr Text OpenFolder{L"Abrir carpeta", L"Open folder"};
	inline constexpr Text OpenHelp{L"Ayuda", L"Help"};
	inline constexpr Text CreateShortcut{L"Crear acceso directo", L"Create shortcut"};
	inline constexpr Text ShortcutOnDesktop{L"En el escritorio", L"On the desktop"};
	inline constexpr Text ShortcutInStartMenu{L"En el menú Inicio", L"In the Start menu"};
	inline constexpr Text AddFavorite{L"Añadir a favoritos\tCtrl+D", L"Add to favorites\tCtrl+D"};
	inline constexpr Text RemoveFavorite{L"Quitar de favoritos\tCtrl+D", L"Remove from favorites\tCtrl+D"};
	inline constexpr Text UseOtherExecutable{L"Usar otro ejecutable", L"Use another executable"};
	inline constexpr Text ResetExecutable{L"Volver al que eligió AppTray", L"Back to AppTray's choice"};
	inline constexpr Text ExpandSuite{L"Mostrar sus programas por separado", L"Show its programs separately"};
	inline constexpr Text CollapseSuite{L"Agrupar en una sola entrada", L"Group them into one entry"};
	inline constexpr Text AtLogon{L"Al iniciar sesión", L"At logon"};
	inline constexpr Text AutostartNo{L"No abrir", L"Don't open"};
	inline constexpr Text AutostartAlways{L"Abrir siempre", L"Always open"};
	inline constexpr Text AutostartAsk{L"Preguntar (casilla al iniciar)", L"Ask (checkbox at logon)"};
	inline constexpr Text RunWithoutUac{L"Abrir sin UAC (tarea elevada)", L"Open without UAC (elevated task)"};
	inline constexpr Text Hide{L"Ocultar", L"Hide"};
	inline constexpr Text Unhide{L"Volver a mostrar", L"Show again"};

	// Messages
	inline constexpr Text RootMissingTitle{L"No se puede leer la carpeta de apps", L"Cannot read the apps folder"};
	inline constexpr Text SettingsBrokenTitle{L"user.json tiene un error", L"user.json has an error"};
	inline constexpr Text SettingsFixFirst{
		L"corrígelo (Editar configuración) antes de cambiar favoritos u opciones; no se ha guardado nada",
		L"fix it (Edit settings) before changing favorites or options; nothing was saved"};
	inline constexpr Text SettingsFixBeforeRefresh{
		L"corrígelo (Editar configuración) antes de refrescar", L"fix it (Edit settings) before refreshing"};
	inline constexpr Text NewAppsTitle{L"Apps nuevas", L"New apps"};
	inline constexpr Text NewAppsText{L"{} apps nuevas desde el último refresco", L"{} new apps since the last refresh"};
	inline constexpr Text LaunchFailed{L"No se pudo abrir", L"Could not open"};
	inline constexpr Text RefreshFailed{L"No se pudo refrescar", L"Could not refresh"};
	inline constexpr Text RefreshExitCode{L"código {} (detalles en AppTray.log)", L"code {} (details in AppTray.log)"};
	inline constexpr Text SeeLog{L"detalles en AppTray.log", L"details in AppTray.log"};
	inline constexpr Text TaskFailed{L"No se pudo cambiar la tarea elevada", L"Could not change the elevated task"};
	inline constexpr Text NeedsElevatedTask{
		L"{} pide administrador: actívale «Abrir sin UAC» para abrirlo al iniciar sesión",
		L"{} needs administrator: turn on \"Open without UAC\" to open it at logon"};
	inline constexpr Text ElevatedTaskStale{
		L"la tarea elevada sigue abriendo el ejecutable anterior: desactiva y activa «Abrir sin UAC»",
		L"the elevated task still opens the previous executable: turn \"Open without UAC\" off and on"};
	inline constexpr Text NewVersionTitle{L"Nueva versión de AppTray", L"New version of AppTray"};
	inline constexpr Text NewVersionText{
		L"La {} está disponible (tienes la {}). Menú del icono → Nueva versión.",
		L"Version {} is available (you have {}). Tray menu → New version."};
	inline constexpr Text UpdateCheckTitle{L"Versión de AppTray", L"AppTray version"};
	inline constexpr Text UpToDate{L"Tienes la última versión ({})", L"You have the latest version ({})"};
	inline constexpr Text UpdateCheckFailed{
		L"No se pudo consultar GitHub (detalles en AppTray.log)", L"Could not reach GitHub (details in AppTray.log)"};
	inline constexpr Text UpdateNotSaved{
		L"Hay una versión nueva en GitHub, pero no se pudo guardar update.json (detalles en AppTray.log)",
		L"There is a new version on GitHub, but update.json could not be saved (details in AppTray.log)"};
	inline constexpr Text LogonTitle{L"Inicio de sesión", L"Logon"};
	inline constexpr Text ShortcutCreatedTitle{L"Acceso directo creado", L"Shortcut created"};
	inline constexpr Text ShortcutCreatedDesktop{
		L"Acceso directo a {} creado en el escritorio", L"Shortcut to {} created on the desktop"};
	inline constexpr Text ShortcutCreatedStartMenu{
		L"Acceso directo a {} creado en el menú Inicio (carpeta AppTray)",
		L"Shortcut to {} created in the Start menu (AppTray folder)"};
	inline constexpr Text ShortcutFailed{L"No se pudo crear el acceso directo", L"Could not create the shortcut"};
	inline constexpr Text ShortcutBusy{
		L"Ya se está creando otro acceso directo: prueba de nuevo en un momento",
		L"Another shortcut is being created: try again in a moment"};
	inline constexpr Text HotkeyTitle{L"Atajo de teclado", L"Hotkey"};
	inline constexpr Text HotkeyInvalid{
		L"«{}» no es un atajo válido (p. ej. Ctrl+Alt+Space): revisa «hotkey» en user.json",
		L"\"{}\" is not a valid hotkey (e.g. Ctrl+Alt+Space): check \"hotkey\" in user.json"};
	inline constexpr Text HotkeyTaken{
		L"{} ya lo usa otro programa: cambia «hotkey» en user.json",
		L"{} is used by another program: change \"hotkey\" in user.json"};
	inline constexpr Text RenamedAppsTitle{L"Apps actualizadas", L"Apps updated"};
	inline constexpr Text RenamedAppsText{
		L"Tus ajustes pasan a la carpeta nueva de: {}", L"Your settings move to the new folder of: {}"};
	inline constexpr Text RenamedAppsReenable{
		L". Vuelve a activar «Abrir sin UAC» en: {}", L". Turn \"Open without UAC\" on again for: {}"};

	// Logon dialog
	inline constexpr Text StartupPromptTitle{L"Inicio de sesión", L"Logon"};
	inline constexpr Text StartupPromptLabel{L"Abrir también:", L"Also open:"};
	inline constexpr Text CountdownPrefix{L"Se continúa en", L"Continuing in"};
	inline constexpr Text CountdownStopped{L"Pulsa Abrir para continuar", L"Press Open to continue"};
	inline constexpr Text OpenButton{L"Abrir", L"Open"};
	inline constexpr Text NoneButton{L"Ninguna", L"None"};
}

// A text as a std::format argument, in the current language
template <> struct std::formatter<apptray::text::Text, wchar_t> : std::formatter<std::wstring_view, wchar_t>
{
	template <typename Context> auto format(const apptray::text::Text& text, Context& context) const
	{
		return std::formatter<std::wstring_view, wchar_t>::format(std::wstring_view(text.Get()), context);
	}
};
