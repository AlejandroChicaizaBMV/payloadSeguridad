#include <windows.h>
#include <ShlObj.h>
#include <iostream>
#include <filesystem>
#include <string>

#pragma comment(lib, "Advapi32.lib")

namespace fs = std::filesystem;

// Obtiene la ruta del ejecutable actual
std::wstring GetCurrentExecutablePath() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    return std::wstring(path);
}

// Obtiene %APPDATA%
std::wstring GetAppDataPath() {
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, path))) {
        return std::wstring(path);
    }
    return L"";
}

// Copia el ejecutable a una ubicación persistente
std::wstring InstallSelf() {
    std::wstring appData = GetAppDataPath();
    std::wstring targetDir = appData + L"\\HelloPersist";
    std::wstring targetPath = targetDir + L"\\hello.exe";

    if (!fs::exists(targetDir)) {
        fs::create_directories(targetDir);
    }

    std::wstring currentExe = GetCurrentExecutablePath();

    // Si aún no está instalado, copiarse
    if (currentExe != targetPath) {
        CopyFileW(currentExe.c_str(), targetPath.c_str(), FALSE);
    }

    return targetPath;
}

// Agrega persistencia al registro (HKCU Run)
bool AddPersistence(const std::wstring& exePath) {
    HKEY hKey;
    const wchar_t* regPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

    LONG result = RegOpenKeyExW(
        HKEY_CURRENT_USER,
        regPath,
        0,
        KEY_SET_VALUE,
        &hKey
    );

    if (result != ERROR_SUCCESS) {
        return false;
    }

    result = RegSetValueExW(
        hKey,
        L"HelloPersistProgram",
        0,
        REG_SZ,
        reinterpret_cast<const BYTE*>(exePath.c_str()),
        static_cast<DWORD>((exePath.size() + 1) * sizeof(wchar_t))
    );

    RegCloseKey(hKey);

    return (result == ERROR_SUCCESS);
}

int main() {
    std::wstring installedPath = InstallSelf();

    if (AddPersistence(installedPath)) {
        std::wcout << L"Persistencia configurada correctamente.\n";
    } else {
        std::wcout << L"No se pudo configurar la persistencia.\n";
    }

    std::cout << "Hello World" << std::endl;

    return 0;
}