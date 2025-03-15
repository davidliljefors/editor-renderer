#include <windows.h>
#include <ppl.h>
#include <vector>
#include <string>
#include <unordered_map>
#include <fstream>

#pragma comment(lib, "user32.lib")

struct CompilerFlags {
    const char* compile_exe;
    const char* link_exe;
    const char* compile_debug;
    const char* link_debug;
};

CompilerFlags clang_cl {
    "clang-cl ",
    "lld-link ",
    "/Zi /Od /MTd /W4 /std:c++17",
    "/debug /pdb:build/main.pdb",
};

struct BuildConfig {
    CompilerFlags CompilerFlags;
    std::vector<std::wstring> SourceFiles;
    std::vector<std::wstring> ObjectFiles;
    static constexpr const wchar_t* BuildDir = L"build";
    
    static void EnsureBuildDir() {
        CreateDirectoryW(BuildDir, NULL);
    }
};

struct FileEnumerator {
    static void GetSourceFiles(std::vector<std::wstring>& files) {
        WIN32_FIND_DATAW findData;
        HANDLE hFind = FindFirstFileW(L".\\*.cpp", &findData);
        
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                files.push_back(findData.cFileName);
            } while (FindNextFileW(hFind, &findData));
            FindClose(hFind);
        }
    }
};

struct TimestampCache {
    static constexpr const wchar_t* CacheFile = L"build/timestamps.cache";
    std::unordered_map<std::wstring, FILETIME> timestamps;

    // Load the cache from disk
    void Load() {
        std::wifstream file(CacheFile);
        if (file.is_open()) {
            std::wstring sourceFile;
            ULARGE_INTEGER timestamp;
            while (file >> sourceFile >> timestamp.QuadPart) {
                FILETIME ft;
                ft.dwLowDateTime = timestamp.LowPart;
                ft.dwHighDateTime = timestamp.HighPart;
                timestamps[sourceFile] = ft;
            }
            file.close();
        }
    }

    // Save the cache to disk
    void Save() const {
        std::wofstream file(CacheFile);
        if (file.is_open()) {
            for (const auto& [sourceFile, timestamp] : timestamps) {
                ULARGE_INTEGER uli;
                uli.LowPart = timestamp.dwLowDateTime;
                uli.HighPart = timestamp.dwHighDateTime;
                file << sourceFile << L" " << uli.QuadPart << L"\n";
            }
            file.close();
        }
    }

    // Get the last write time of a file
    static FILETIME GetFileLastWriteTime(const std::wstring& filePath) {
        FILETIME ft = {0};
        HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            GetFileTime(hFile, NULL, NULL, &ft);
            CloseHandle(hFile);
        }
        return ft;
    }

    // Check if a source file has changed since the last build
    bool HasFileChanged(const std::wstring& sourceFile) const {
        FILETIME currentTime = GetFileLastWriteTime(sourceFile);
        auto it = timestamps.find(sourceFile);
        if (it == timestamps.end()) {
            return true; // Not in cache, needs compilation
        }

        // Compare FILETIME structures
        ULARGE_INTEGER current, cached;
        current.LowPart = currentTime.dwLowDateTime;
        current.HighPart = currentTime.dwHighDateTime;
        cached.LowPart = it->second.dwLowDateTime;
        cached.HighPart = it->second.dwHighDateTime;

        return current.QuadPart != cached.QuadPart;
    }

    // Update the timestamp for a source file after compilation
    void UpdateTimestamp(const std::wstring& sourceFile) {
        timestamps[sourceFile] = GetFileLastWriteTime(sourceFile);
    }

    // Check if the corresponding object file exists
    static bool ObjectFileExists(const std::wstring& sourceFile) {
        std::wstring objName = std::wstring(BuildConfig::BuildDir) + L"\\" + sourceFile.substr(0, sourceFile.length() - 4) + L".o";
        DWORD attrib = GetFileAttributesW(objName.c_str());
        return attrib != INVALID_FILE_ATTRIBUTES && !(attrib & FILE_ATTRIBUTE_DIRECTORY);
    }
};

struct Compiler {
    static bool CompileFile(const std::wstring& sourceFile, CompilerFlags flags, std::wstring& objFile) {
        std::wstring objName = std::wstring(BuildConfig::BuildDir) + L"\\" + sourceFile.substr(0, sourceFile.length() - 4) + L".o";
        std::string command = flags.compile_exe;
        command += flags.compile_debug;
        command += " -c ";
        command += " -o ";
        
        char objFileStr[MAX_PATH];
        WideCharToMultiByte(CP_UTF8, 0, objName.c_str(), -1, objFileStr, MAX_PATH, NULL, NULL);
        command += objFileStr;
        command += " ";
        
        char sourceFileStr[MAX_PATH];
        WideCharToMultiByte(CP_UTF8, 0, sourceFile.c_str(), -1, sourceFileStr, MAX_PATH, NULL, NULL);
        command += sourceFileStr;

        STARTUPINFOA si = {sizeof(si)};
        PROCESS_INFORMATION pi;
        
        if (CreateProcessA(NULL, const_cast<LPSTR>(command.c_str()), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, INFINITE);
            DWORD exitCode;
            GetExitCodeProcess(pi.hProcess, &exitCode);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            
            if (exitCode == 0) {
                objFile = objName;
                return true;
            }
        }
        return false;
    }
};

struct Linker {
    static bool LinkFiles(const std::vector<std::wstring>& objFiles, CompilerFlags flags) {
        std::string command = flags.link_exe;
        
        for (const auto& obj : objFiles) {
            char objFileStr[MAX_PATH];
            WideCharToMultiByte(CP_UTF8, 0, obj.c_str(), -1, objFileStr, MAX_PATH, NULL, NULL);
            command += objFileStr;
            command += " ";
        }
        command += "/out:build/output.exe ";
        command += flags.link_debug;

        STARTUPINFOA si = {sizeof(si)};
        PROCESS_INFORMATION pi;
        
        if (CreateProcessA(NULL, const_cast<LPSTR>(command.c_str()), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, INFINITE);
            DWORD exitCode;
            GetExitCodeProcess(pi.hProcess, &exitCode);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return exitCode == 0;
        }
        return false;
    }
};

int main(int argc, char* argv[]) {
    BuildConfig::EnsureBuildDir();
    
    BuildConfig config;
    config.CompilerFlags = clang_cl;
    
    // Load the timestamp cache
    TimestampCache cache;
    cache.Load();

    // Get all source files
    FileEnumerator::GetSourceFiles(config.SourceFiles);
    
    if (config.SourceFiles.empty()) {
        puts("No Source Files Found. Exiting.");
        return 1;
    }

    // Filter source files: keep only those that need compilation
    std::vector<std::wstring> filesToCompile;
    std::vector<std::wstring> objFilesForLinking;
    for (const auto& sourceFile : config.SourceFiles) {
        // Check if the source file has changed or if the object file doesn't exist
        if (cache.HasFileChanged(sourceFile) || !TimestampCache::ObjectFileExists(sourceFile)) {
            filesToCompile.push_back(sourceFile);
        }
        // Always add the corresponding object file to the linking list
        std::wstring objName = std::wstring(BuildConfig::BuildDir) + L"\\" + sourceFile.substr(0, sourceFile.length() - 4) + L".o";
        objFilesForLinking.push_back(objName);
    }

    // Compile only the files that need it
    if (!filesToCompile.empty()) {
        printf("Compiling %d out of %d files...\n", (int)filesToCompile.size(), (int)config.SourceFiles.size());
        std::vector<bool> compileResults(filesToCompile.size());
        std::vector<std::wstring> compiledObjFiles(filesToCompile.size());

        Concurrency::parallel_for(size_t(0), filesToCompile.size(), [&](size_t i) {
            compileResults[i] = Compiler::CompileFile(filesToCompile[i], config.CompilerFlags, compiledObjFiles[i]);
            if (compileResults[i]) {
                cache.UpdateTimestamp(filesToCompile[i]); // Update timestamp on successful compile
            }
        });

        bool allCompiled = true;
        for (size_t i = 0; i < compileResults.size(); i++) {
            if (!compileResults[i]) {
                allCompiled = false;
                printf("Failed to compile: %ls\n", filesToCompile[i].c_str());
                break;
            }
        }

        if (!allCompiled) {
            return 1;
        }
    } else {
        puts("No files need compilation.");
    }

    // Save the updated timestamp cache
    cache.Save();

    // Link all object files (including unchanged ones)
    puts("Linking...");
    if (!Linker::LinkFiles(objFilesForLinking, config.CompilerFlags)) {
        return 1;
    }
    puts("Done");

    return 0;
}