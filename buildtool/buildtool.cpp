#include <windows.h>
#include <ppl.h>
#include <vector>
#include <string>

#pragma comment(lib, "user32.lib")

//struct CompilerFlags {
//    static constexpr const char* debug = "-g -gcodeview -O0 -Wall -Wextra -std=c++17 --target=x86_64-pc-windows-msvc -fms-compatibility";
//    static constexpr const char* release = "-O3 -DNDEBUG -Wall -Wextra -std=c++17 -flto";
//};

struct CompilerFlags
{
    const char* compile_exe;
    const char* link_exe;
    const char* compile_debug;
    const char* link_debug;
};

CompilerFlags clang_cl
{
    "clang-cl ",
    "lld-link ",
    "/Zi /Od /MT /W4 /std:c++17 ",
    "/debug /pdb:build/main.pdb ",
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
    
    FileEnumerator::GetSourceFiles(config.SourceFiles);
    
    if (config.SourceFiles.empty()) {
        puts("No Source Files Found. Exiting.");
        return 1;
    }

    config.ObjectFiles.resize(config.SourceFiles.size());
    std::vector<bool> compileResults(config.SourceFiles.size());

    printf("Compiling %d files...\n", (int)config.ObjectFiles.size());
    Concurrency::parallel_for(size_t(0), config.SourceFiles.size(), [&](size_t i) {
        compileResults[i] = Compiler::CompileFile(config.SourceFiles[i], config.CompilerFlags, config.ObjectFiles[i]);
    });

    bool allCompiled = true;
    for (size_t i = 0; i < compileResults.size(); i++) {
        if (!compileResults[i]) {
            allCompiled = false;
            break;
        }
    }

    if (!allCompiled) {
        return 1;
    }

    puts("Linking...");
    if (!Linker::LinkFiles(config.ObjectFiles, config.CompilerFlags)) {
        return 1;
    }
    puts("Done");

    return 0;
}
