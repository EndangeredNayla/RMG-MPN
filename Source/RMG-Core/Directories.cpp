/*
 * Rosalie's Mupen GUI - https://github.com/Rosalie241/RMG
 *  Copyright (C) 2020 Rosalie Wanders <rosalie@mailbox.org>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3.
 *  You should have received a copy of the GNU General Public License
 *  along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#include "Directories.hpp"
#include "Error.hpp"
#include "Core.hpp"
#include "Config.hpp"
#include "m64p/Api.hpp"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <filesystem>
#ifdef _WIN32
#include <Windows.h>
#include <shlobj.h>
#endif // _WIN32

//
// Local Variables
//

static std::filesystem::path l_LibraryPathOverride;
static std::filesystem::path l_CorePathOverride;
static std::filesystem::path l_PluginPathOverride;
static std::filesystem::path l_SharedDataPathOverride;

//
// Local Functions
//

#ifdef PORTABLE_INSTALL
static std::filesystem::path get_exe_directory(void)
{
    static std::filesystem::path directory;
#ifdef _WIN32
    wchar_t buffer[MAX_PATH] = {0};
#endif // _WIN32

    if (!directory.empty())
    {
        return directory;
    }

#ifdef _WIN32
    if (GetModuleFileNameW(nullptr, buffer, MAX_PATH) == 0)
    {
        MessageBoxA(nullptr, "get_exe_directory: GetModuleFileNameW() Failed!", "Error", MB_OK | MB_ICONERROR);
        std::terminate();
    }
    directory = std::filesystem::path(buffer).parent_path();
#else // _WIN32
    try
    {
        directory =  std::filesystem::canonical("/proc/self/exe").parent_path();
    }
    catch (...)
    { // fail silently and fallback to current path
        std::cerr << "get_exe_directory: std::filesystem::canonical(\"/proc/self/exe\") threw an exception!" << std::endl;
        std::terminate();
    }
#endif // _WIN32
    return directory;
}
#endif // PORTABLE_INSTALL

#ifdef _WIN32
static std::filesystem::path get_appdata_directory(std::filesystem::path directory)
{
    static std::filesystem::path appdataDirectory;
    std::filesystem::path fullDirectory;

    if (appdataDirectory.empty())
    {
        wchar_t buffer[MAX_PATH];
        LPITEMIDLIST pidl;

        if (SHGetSpecialFolderLocation(nullptr, CSIDL_APPDATA, &pidl) != S_OK ||
            !SHGetPathFromIDListW(pidl, buffer))
        { // fallback to executable directory
            appdataDirectory = get_exe_directory();
        }
        else
        {
            appdataDirectory = std::filesystem::path(buffer);
        }
    }

    fullDirectory = appdataDirectory;
    fullDirectory += "/RMG/";
    fullDirectory += directory;

    return fullDirectory;
}
#else // _WIN32
static std::filesystem::path get_var_directory(std::string var, std::string append, std::string fallbackVar, std::string fallbackAppend)
{
    std::filesystem::path directory;

    const char* env = std::getenv(var.c_str());
    if (env == nullptr)
    {
        env = std::getenv(fallbackVar.c_str());
        if (env == nullptr)
        {
            std::cerr << "get_var_directory: fallbackVar: $" << fallbackVar << " cannot be nullptr!" << std::endl;
            std::terminate();
        }
        directory = env;
        directory += fallbackAppend;
    }
    else
    {
        directory = env;
        directory += append;
    }

    return directory;
}

static std::filesystem::path get_command_output(std::string command)
{
    std::string output;
    char buf[2048];
    FILE* pipe = nullptr;

    pipe = popen(command.c_str(), "r");
    if (pipe == nullptr)
    {
        return std::string();
    }

    while (fgets(buf, sizeof(buf), pipe) != nullptr)
    {
        output += buf;
    }

    pclose(pipe);

    // strip newline
    if (output.back() == '\n')
    {
        output.pop_back();
    }

    return output;
}
#endif // _WIN32

//
// Exported Functions
//

bool CoreCreateDirectories(void)
{
    std::string error;

    std::filesystem::path directories[] = 
    {
        CoreGetCoreDirectory(),
        CoreGetPluginDirectory(),
        CoreGetSharedDataDirectory(),
        CoreGetUserConfigDirectory(),
        CoreGetUserDataDirectory(),
        CoreGetUserCacheDirectory(),
        CoreGetSaveDirectory(),
        CoreGetSaveStateDirectory(),
        CoreGetScreenshotDirectory()
    };

    for (const std::filesystem::path& directory : directories)
    {
        try
        {
            std::filesystem::create_directories(directory);
        }
        catch (...)
        {
            error = "CoreCreateDirectories Failed: cannot create the '";
            error += directory.string();
            error += "' directory!";
            CoreSetError(error);
            return false;
        }
    }

    return true;
}

bool CoreGetPortableDirectoryMode(void)
{
#ifdef PORTABLE_INSTALL
    static bool portable_set = false;
    static bool is_portable  = false;

    if (portable_set)
    {
        return is_portable;
    }

    std::filesystem::path exeDirectory;
    std::filesystem::path configFile;
    std::filesystem::path portableFile;

    exeDirectory      = get_exe_directory();
    configFile        = exeDirectory;
    configFile        += "/Config/mupen64plus.cfg";
    portableFile      = exeDirectory;
    portableFile      += "/portable.txt";

    is_portable  = std::filesystem::is_regular_file(portableFile) ||
                    std::filesystem::is_regular_file(configFile);
    portable_set = true;
    return is_portable;
#else // Linux install
    return false;
#endif // PORTABLE_INSTALL
}

std::filesystem::path CoreGetLibraryDirectory(void)
{
    std::filesystem::path directory;
    directory = ".";
    return directory;
}

std::filesystem::path CoreGetCoreDirectory(void)
{
    std::filesystem::path directory;
    directory = "Core";
    return directory;
}

std::filesystem::path CoreGetPluginDirectory(void)
{
    std::filesystem::path directory;
    directory = "Plugin";
    return directory;
}

std::filesystem::path CoreGetUserConfigDirectory(void)
{
    std::filesystem::path directory;
    directory = "Config";
    return directory;
}

std::filesystem::path CoreGetDefaultUserDataDirectory(void)
{
    std::filesystem::path directory;
    directory = "Data";
    return directory;
}

std::filesystem::path CoreGetDefaultUserCacheDirectory(void)
{
    std::filesystem::path directory;
    directory = "Cache";
    return directory;
}

std::filesystem::path CoreGetDefaultSaveDirectory(void)
{
    std::filesystem::path directory;
    directory = "Save/Game";
    return directory;
}

std::filesystem::path CoreGetDefaultSaveStateDirectory(void)
{
    std::filesystem::path directory;
    directory = "Save/State";
    return directory;
}

std::filesystem::path CoreGetDefaultScreenshotDirectory(void)
{
    std::filesystem::path directory;
    directory = "Screenshots";
    return directory;
}

std::filesystem::path CoreGetUserDataDirectory(void)
{
    return std::filesystem::path(m64p::Config.GetUserDataPath());
}

std::filesystem::path CoreGetUserCacheDirectory(void)
{
    return std::filesystem::path(m64p::Config.GetUserCachePath());
}

std::filesystem::path CoreGetSharedDataDirectory(void)
{
    std::filesystem::path directory;
    directory = "Data";
    return directory;
}

std::filesystem::path CoreGetSaveDirectory(void)
{
    return CoreSettingsGetStringValue(SettingsID::Core_SaveSRAMPath);
}

std::filesystem::path CoreGetSaveStateDirectory(void)
{
    return CoreSettingsGetStringValue(SettingsID::Core_SaveStatePath);
}

std::filesystem::path CoreGetScreenshotDirectory(void)
{
    return CoreSettingsGetStringValue(SettingsID::Core_ScreenshotPath);
}