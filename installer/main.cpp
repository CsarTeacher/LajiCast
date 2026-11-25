#include <windows.h>
#include <shlobj.h>
#include <tlhelp32.h>

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct PayloadFile {
	int resourceId;
	const wchar_t *relativePath;
};

constexpr std::array<PayloadFile, 8> kPayload = {{
	{101, L"lajicast.dll"},
	{102, L"data\\mediamtx.exe"},
	{103, L"data\\MEDIAMTX-LICENSE"},
	{104, L"data\\viewer.html"},
	{105, L"data\\branding\\lajicast.ico"},
	{106, L"data\\branding\\lajicast.png"},
	{107, L"data\\branding\\lajicast.svg"},
	{109, L"LICENSE"},
}};

std::wstring programDataPath()
{
	PWSTR rawPath = nullptr;
	if (FAILED(SHGetKnownFolderPath(FOLDERID_ProgramData, KF_FLAG_DEFAULT, nullptr, &rawPath)))
		return {};
	std::wstring path(rawPath);
	CoTaskMemFree(rawPath);
	return path;
}

bool obsIsRunning()
{
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot == INVALID_HANDLE_VALUE)
		return false;

	PROCESSENTRY32W entry{};
	entry.dwSize = sizeof(entry);
	bool found = false;
	if (Process32FirstW(snapshot, &entry)) {
		do {
			if (_wcsicmp(entry.szExeFile, L"obs64.exe") == 0 || _wcsicmp(entry.szExeFile, L"obs.exe") == 0) {
				found = true;
				break;
			}
		} while (Process32NextW(snapshot, &entry));
	}
	CloseHandle(snapshot);
	return found;
}

bool writeResource(int resourceId, const fs::path &destination)
{
	HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
	if (!resource)
		return false;
	HGLOBAL loaded = LoadResource(nullptr, resource);
	if (!loaded)
		return false;
	const DWORD size = SizeofResource(nullptr, resource);
	const void *data = LockResource(loaded);
	if (!data || size == 0)
		return false;

	std::error_code error;
	fs::create_directories(destination.parent_path(), error);
	if (error)
		return false;
	HANDLE file = CreateFileW(destination.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
		return false;

	const BYTE *cursor = static_cast<const BYTE *>(data);
	DWORD remaining = size;
	bool success = true;
	while (remaining > 0) {
		DWORD written = 0;
		const DWORD chunk = (remaining > 1024 * 1024) ? 1024 * 1024 : remaining;
		if (!WriteFile(file, cursor, chunk, &written, nullptr) || written == 0) {
			success = false;
			break;
		}
		cursor += written;
		remaining -= written;
	}
	CloseHandle(file);
	if (!success) {
		fs::remove(destination, error);
		return false;
	}
	return true;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
	if (obsIsRunning()) {
		MessageBoxW(nullptr, L"Cierra OBS Studio antes de instalar LajiCast y vuelve a ejecutar el instalador.",
				L"LajiCast", MB_OK | MB_ICONINFORMATION);
		return 1;
	}

	const std::wstring base = programDataPath();
	if (base.empty()) {
		MessageBoxW(nullptr, L"Windows no permitió localizar la carpeta ProgramData.", L"LajiCast",
				MB_OK | MB_ICONERROR);
		return 2;
	}
	const fs::path pluginDirectory = fs::path(base) / L"obs-studio" / L"plugins" / L"lajicast";
	std::error_code error;
	if (fs::exists(pluginDirectory, error)) {
		const int choice = MessageBoxW(nullptr,
				L"Ya existe una instalación de LajiCast. ¿Quieres actualizarla con esta versión?",
				L"Actualizar LajiCast", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
		if (choice != IDYES)
			return 0;
	}

	for (const auto &file : kPayload) {
		if (!writeResource(file.resourceId, pluginDirectory / file.relativePath)) {
			MessageBoxW(nullptr,
					L"No se pudo completar la instalación de LajiCast. Comprueba el espacio disponible y vuelve a intentarlo.",
					L"LajiCast", MB_OK | MB_ICONERROR);
			return 3;
		}
	}

	MessageBoxW(nullptr,
			L"LajiCast se ha instalado para OBS Studio. Abre OBS y busca el panel LajiCast en el menú Ver > Paneles.",
			L"Instalación completada", MB_OK | MB_ICONINFORMATION);
	return 0;
}
