#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <filesystem>
#include <fstream>
#include <windows.h>
#include <io.h>        // _get_osfhandle, _dup, _dup2, _close
#include <algorithm>
#include <fcntl.h>  // for _O_APPEND, _O_WRONLY

namespace fs = std::filesystem;

// ----------------- helpers -----------------
static inline void trim(std::string& s) {
    const char* ws = " \t\r\n";
    auto a = s.find_first_not_of(ws);
    auto b = s.find_last_not_of(ws);
    if (a == std::string::npos) { s.clear(); return; }
    s = s.substr(a, b - a + 1);
}

static std::vector<std::string> split_tokens(const std::string& line) {
    std::istringstream iss(line);
    std::vector<std::string> t;
    std::string tok;
    while (iss >> tok) t.push_back(tok);
    return t;
}

// Parse <, >, >> out of the command line.
// Rebuilds a "clean" command string (no redir tokens) and returns file paths.
static void parse_redirections(const std::string& line,
                               std::string& cleanCmd,
                               std::string& inPath,
                               std::string& outPath,
                               bool& append) {
    auto toks = split_tokens(line);
    std::vector<std::string> kept;
    append = false;
    inPath.clear(); outPath.clear();

    for (size_t i = 0; i < toks.size(); ++i) {
        const std::string& t = toks[i];
        if (t == "<") {
            if (i + 1 < toks.size()) inPath = toks[++i];
        } else if (t == ">>") {
            if (i + 1 < toks.size()) { outPath = toks[++i]; append = true; }
        } else if (t == ">") {
            if (i + 1 < toks.size()) { outPath = toks[++i]; append = false; }
        } else {
            kept.push_back(t);
        }
    }
    // rebuild
    std::ostringstream oss;
    for (size_t i = 0; i < kept.size(); ++i) {
        if (i) oss << ' ';
        oss << kept[i];
    }
    cleanCmd = oss.str();
}

// Ensure a FILE*’s underlying HANDLE is inheritable for CreateProcess.
static void make_inheritable(FILE* f) {
    if (!f) return;
    HANDLE h = (HANDLE)_get_osfhandle(_fileno(f));
    SetHandleInformation(h, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
}

// Spawn an external program with optional redirected stdin/stdout.
static int spawn_external(const std::string& exePath,
                          const std::string& args,
                          FILE* inFile,
                          FILE* outFile) {
    // child std handles (default to parent console)
    HANDLE hIn  = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);

    if (inFile)  { make_inheritable(inFile);  hIn  = (HANDLE)_get_osfhandle(_fileno(inFile)); }
    if (outFile) { make_inheritable(outFile); hOut = (HANDLE)_get_osfhandle(_fileno(outFile)); }

    STARTUPINFOA si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput  = hIn;
    si.hStdOutput = hOut;
    si.hStdError  = hErr;

    // Build: "C:\...\prog.exe" + (optional) " " + args
    std::string cmdline;
    cmdline.reserve(exePath.size() + args.size() + 4);
    cmdline.push_back('"'); cmdline += exePath; cmdline.push_back('"');
    if (!args.empty()) { cmdline.push_back(' '); cmdline += args; }

    // CreateProcess requires a modifiable buffer
    std::vector<char> buf(cmdline.begin(), cmdline.end());
    buf.push_back('\0');

    BOOL ok = CreateProcessA(
        /*app*/      NULL,
        /*cmd*/      buf.data(),
        /*procAttr*/ NULL,
        /*thrAttr*/  NULL,
        /*inherit*/  TRUE,                // inherit handles
        /*flags*/    0,
        /*env*/      NULL,
        /*cwd*/      NULL,
        /*si*/       &si,
        /*pi*/       &pi
    );

    if (!ok) return -1;

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (int)exitCode;
}

// -------------------------------------------

int main() {
    std::cout << "Winix Shell v0.5\n";

    std::vector<std::string> searchPaths = { ".", "build", "bin" };

    // load history
    std::vector<std::string> history;
    {
        std::ifstream f("winix_history.txt");
        std::string line;
        while (std::getline(f, line)) if (!line.empty()) history.push_back(line);
    }

    for (std::string line; ; ) {
        std::cout << "\033[1;32m[Winix]\033[0m " << fs::current_path().string() << " > ";
        if (!std::getline(std::cin, line)) break;
        trim(line);
        if (line.empty()) continue;
        history.push_back(line);

        // parse redirections
        std::string clean, inPath, outPath;
        bool append = false;
        parse_redirections(line, clean, inPath, outPath, append);
        if (clean.empty()) continue;

        // split command + args
        std::istringstream iss(clean);
        std::string cmd;
        iss >> cmd;
        std::string args;
        std::getline(iss, args);
        if (!args.empty() && args[0] == ' ') args.erase(0, 1);

// open redirection files if any
FILE* inFile  = nullptr;
FILE* outFile = nullptr;
HANDLE hOut   = nullptr;

if (!inPath.empty()) {
    inFile = fopen(inPath.c_str(), "rb");
    if (!inFile) {
        std::cerr << "Error: cannot open input '" << inPath << "'\n";
        continue;
    }
}

if (!outPath.empty()) {
    if (append) {
        hOut = CreateFileA(
            outPath.c_str(),
            FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );
        if (hOut == INVALID_HANDLE_VALUE) {
            std::cerr << "Error: cannot open output '" << outPath << "'\n";
            if (inFile) fclose(inFile);
            continue;
        }
        SetFilePointer(hOut, 0, nullptr, FILE_END);
        int fd = _open_osfhandle((intptr_t)hOut, _O_APPEND | _O_WRONLY);
        outFile = _fdopen(fd, "ab");
    } else {
        outFile = fopen(outPath.c_str(), "wb");
        if (!outFile) {
            std::cerr << "Error: cannot open output '" << outPath << "'\n";
            if (inFile) fclose(inFile);
            continue;
        }
    }
}


        // built-ins
        if (cmd == "exit" || cmd == "quit") {
            std::cout << "Goodbye.\n";
            if (inFile) fclose(inFile);
            if (outFile) fclose(outFile);
            break;
        }
        else if (cmd == "pwd") {
            // handle redirection for built-ins by temporarily swapping fds
            int old_in = -1, old_out = -1;
            if (inFile)  { old_in  = _dup(_fileno(stdin));  _dup2(_fileno(inFile),  _fileno(stdin)); }
            if (outFile) { old_out = _dup(_fileno(stdout)); _dup2(_fileno(outFile), _fileno(stdout)); }

            std::cout << fs::current_path().string() << "\n";

            if (outFile) { fflush(stdout); _dup2(old_out, _fileno(stdout)); _close(old_out); }
            if (inFile)  { _dup2(old_in,  _fileno(stdin));  _close(old_in);  }
            if (inFile) fclose(inFile);
            if (outFile) fclose(outFile);
            continue;
        }
        else if (cmd == "echo") {
            int old_out = -1, old_in = -1;
            if (inFile)  { old_in  = _dup(_fileno(stdin));  _dup2(_fileno(inFile),  _fileno(stdin)); }
            if (outFile) { old_out = _dup(_fileno(stdout)); _dup2(_fileno(outFile), _fileno(stdout)); }

            std::cout << args << "\n";

            if (outFile) { fflush(stdout); _dup2(old_out, _fileno(stdout)); _close(old_out); }
            if (inFile)  { _dup2(old_in,  _fileno(stdin));  _close(old_in);  }
            if (inFile) fclose(inFile);
            if (outFile) fclose(outFile);
            continue;
        }
        else if (cmd == "cd") {
            std::string path = args;
            trim(path);
            if (path.empty()) {
                std::cout << "Usage: cd <dir>\n";
            } else {
                try { fs::current_path(path); }
                catch (std::exception& e) { std::cerr << "cd: " << e.what() << "\n"; }
            }
            if (inFile) fclose(inFile);
            if (outFile) fclose(outFile);
            continue;
        }
        else if (cmd == "clear") {
            system("cls");
            if (inFile) fclose(inFile);
            if (outFile) fclose(outFile);
            continue;
        }
        else if (cmd == "help") {
            int old_out = -1;
            if (outFile) { old_out = _dup(_fileno(stdout)); _dup2(_fileno(outFile), _fileno(stdout)); }
            std::cout << "Built-in commands:\n"
                      << "  cd <dir>\n  pwd\n  echo <text>\n  clear\n  help\n  exit\n";
            if (outFile) { fflush(stdout); _dup2(old_out, _fileno(stdout)); _close(old_out); }
            if (inFile) fclose(inFile);
            if (outFile) fclose(outFile);
            continue;
        }

        // external executable search
        bool launched = false;
        for (const auto& base : searchPaths) {
            fs::path full = fs::path(base) / (cmd + ".exe");
            if (fs::exists(full)) {
                int rc = spawn_external(full.string(), args, inFile, outFile);
                if (rc != 0)
                    std::cerr << "Error executing: " << full.string() << "\n";
                launched = true;
                break;
            }
        }
        if (!launched) {
            std::cerr << cmd << ": command not found\n";
        }

        if (inFile) fclose(inFile);
        if (outFile) fclose(outFile);
    }

    // save history
    {
        std::ofstream f("winix_history.txt", std::ios::trunc);
        for (auto& h : history) f << h << "\n";
    }
    return 0;
}
