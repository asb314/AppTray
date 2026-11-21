#pragma once

// User-facing texts (Spanish). Kept in one place so they can be translated later.

namespace apptray::text
{
	inline constexpr wchar_t AppName[] = L"AppTray";

	// Tray menu
	inline constexpr wchar_t OpenLauncher[] = L"Abrir AppTray";
	inline constexpr wchar_t NoFavorites[] = L"Sin favoritos (Ctrl+D en la lista)";
	inline constexpr wchar_t Empty[] = L"(vacía)";
	inline constexpr wchar_t Refresh[] = L"Refrescar";
	inline constexpr wchar_t Refreshing[] = L"Refrescando…";
	inline constexpr wchar_t EditSettings[] = L"Editar configuración";
	inline constexpr wchar_t Exit[] = L"Salir";

	// Launcher window
	inline constexpr wchar_t SearchCue[] = L"Buscar apps";
	inline constexpr wchar_t FolderSearchCue[] = L"Buscar carpeta";
	inline constexpr wchar_t NoFolderMatches[] = L"Ninguna carpeta coincide";
	inline constexpr wchar_t Favorites[] = L"Favoritos";
	inline constexpr wchar_t All[] = L"Todas";
	inline constexpr wchar_t New[] = L"Nuevas";
	inline constexpr wchar_t InstallersAndCopies[] = L"Instaladores y copias";
	inline constexpr wchar_t Hidden[] = L"Ocultas";
	inline constexpr wchar_t RootCategory[] = L"(raíz)";
	inline constexpr wchar_t OpensFolder[] = L"abre la carpeta";
	inline constexpr wchar_t NotFound[] = L"no encontrada";
	inline constexpr wchar_t NewTag[] = L"nueva";
	inline constexpr wchar_t NoMatches[] = L"Nada coincide con la búsqueda";
	inline constexpr wchar_t Keys[] =
		L"Intro abre · Ctrl+Intro como admin · Ctrl+D favorito · Ctrl+Mayús+F carpetas · F5 refrescar";

	// App context menu
	inline constexpr wchar_t Open[] = L"Abrir";
	inline constexpr wchar_t OpenAsAdmin[] = L"Abrir como administrador";
	inline constexpr wchar_t OpenFolder[] = L"Abrir carpeta";
	inline constexpr wchar_t OpenHelp[] = L"Ayuda";
	inline constexpr wchar_t AddFavorite[] = L"Añadir a favoritos\tCtrl+D";
	inline constexpr wchar_t RemoveFavorite[] = L"Quitar de favoritos\tCtrl+D";
	inline constexpr wchar_t UseOtherExecutable[] = L"Usar otro ejecutable";
	inline constexpr wchar_t ResetExecutable[] = L"Volver al que eligió AppTray";
	inline constexpr wchar_t ExpandSuite[] = L"Mostrar sus programas por separado";
	inline constexpr wchar_t CollapseSuite[] = L"Agrupar en una sola entrada";
	inline constexpr wchar_t Hide[] = L"Ocultar";
	inline constexpr wchar_t Unhide[] = L"Volver a mostrar";

	// Messages
	inline constexpr wchar_t RootMissingTitle[] = L"No se puede leer la carpeta de apps";
	inline constexpr wchar_t SettingsBrokenTitle[] = L"user.json tiene un error";
	inline constexpr wchar_t SettingsFixFirst[] =
		L"corrígelo (Editar configuración) antes de cambiar favoritos u opciones; no se ha guardado nada";
	inline constexpr wchar_t NewAppsTitle[] = L"Apps nuevas";
	inline constexpr wchar_t LaunchFailed[] = L"No se pudo abrir";
	inline constexpr wchar_t CatalogSaveFailed[] = L"No se pudo guardar el catálogo";
	inline constexpr wchar_t RefreshFailed[] = L"No se pudo refrescar";
	inline constexpr wchar_t SettingsFixBeforeRefresh[] = L"corrígelo (Editar configuración) antes de refrescar";
}
