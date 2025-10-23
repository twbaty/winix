// src/shell/main.cpp
#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <filesystem>
#include <fstream>
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <algorithm>

namespace fs = std::filesystem;

// ---------------- helpers ----------------
static inline void trim(std::string &s) {
    const char *ws = " \t\r\n";
    auto a = s.find_first_not_of(ws);
    if (a == std::string::npos) { s.clear(); return; }
    auto b = s.find_last_not_of(ws);
    s = s.substr(a, b - a + 1);
}

static std::vector<std::string> split_tokens(const std::string &line) {
    std::istringstream iss(line);
    std::vector<std::string> t;
    std::string tok;
    while (iss >> tok) t.push_back(tok);
    return t;
}

static std::vector<std::string> split_pipeline(const std::string &line) {
    std::vector<std::string> segs;
    std::string cur;
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '|') {
            trim(cur);
            if (!cur.empty()) segs.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(line[i]);
        }
    }
    trim(cur);
    if (!cur.empty()) segs.push_back(cur);
    return segs;
}

// look for an executable in the search paths; returns empty string if not found
static std::string find_executable(const std::string &cmd, const std::vector<std::string> &searchPaths) {
    if (fs::path(cmd).has_extension() && fs::exists(cmd)) return cmd;
    for (const auto &p : searchPaths) {
        fs::path candidate = fs::path(p) / (cmd + ".exe");
        if (fs::exists(candidate)) return candidate.string();
    }
    // fallback: maybe cmd is on PATH; return cmd so CreateProcess can try it
    return cmd;
}

// Make a FILE* handle inheritable for child processes
static void make_inheritable(FILE *f) {
    if (!f) return;
    intptr_t osfh = _get_osfhandle(_fileno(f));
    if (osfh == -1) return;
    HANDLE h = (HANDLE)osfh;
    SetHandleInformation(h, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
}

// open output HANDLE/FILE for append or truncate using CreateFile for append correctness
static FILE *open_output_file(const std::string &path, bool append) {
    if (append) {
        HANDLE h = CreateFileA(
            path.c_str(),
            FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );
        if (h == INVALID_HANDLE_VALUE) return nullptr;
        // position to end
        SetFilePointer(h, 0, nullptr, FILE_END);
        int fd = _open_osfhandle((intptr_t)h, _O_APPEND | _O_WRONLY);
        if (fd == -1) { CloseHandle(h); return nullptr; }
        FILE *f = _fdopen(fd, "ab");
        if (!f) { _close(fd); return nullptr; }
        return f;
    } else {
        // truncate/overwrite
        FILE *f = fopen(path.c_str(), "wb");
        return f;
    }
}

static FILE *open_input_file(const std::string &path) {
    return fopen(path.c_str(), "rb");
}

// Spawn a pipeline of external commands (segments). Each segment has its own
// redirection tokens parsed inside the segment string.
// Returns exit code of last process (or -1 on spawn error).
static int winix_exec_pipeline(const std::vector<std::string> &segments,
                               const std::vector<std::string> &searchPaths) {
    size_t n = segments.size();
    if (n == 0) return 0;

    // Data structures for handles and PROCESS_INFORMATION
    std::vector<HANDLE> childProcessHandles;
    std::vector<PROCESS_INFORMATION> procs;

    // we'll create pipes between processes: for i'th process, its stdout goes to pipeWrite (if not last),
    // and its stdin comes from previous pipeRead (if not first).
    HANDLE prevRead = NULL;

    for (size_t i = 0; i < n; ++i) {
        // Parse segment: tokens, and redirections (<, >, >>)
        std::string seg = segments[i];
        auto toks = split_tokens(seg);
        std::vector<std::string> keep;
        std::string inPath, outPath;
        bool append = false;
        for (size_t t = 0; t < toks.size(); ++t) {
            if (toks[t] == "<") {
                if (t + 1 < toks.size()) { inPath = toks[++t]; }
            } else if (toks[t] == ">>") {
                if (t + 1 < toks.size()) { outPath = toks[++t]; append = true; }
            } else if (toks[t] == ">") {
                if (t + 1 < toks.size()) { outPath = toks[++t]; append = false; }
            } else {
                keep.push_back(toks[t]);
            }
        }
        if (keep.empty()) { // nothing to run
            // clean up any handles created
            if (prevRead) { CloseHandle(prevRead); prevRead = NULL; }
            for (auto &pi : procs) { if (pi.hProcess) CloseHandle(pi.hProcess); if (pi.hThread) CloseHandle(pi.hThread); }
            return -1;
        }

        // build command and args
        std::string cmd = keep[0];
        std::string args;
        for (size_t k = 1; k < keep.size(); ++k) {
            if (!args.empty()) args.push_back(' ');
            args += keep[k];
        }

        // If this is the only segment and it's a built-in, return control to caller (they can handle builtin).
        // But here we're in external pipeline executor; handle only external binaries.
        std::string exePath = find_executable(cmd, searchPaths);

        // Prepare stdin handle for this process
        HANDLE hChildStd_IN = GetStdHandle(STD_INPUT_HANDLE);
        FILE *inFile = nullptr;
        if (!inPath.empty()) {
            inFile = open_input_file(inPath);
            if (!inFile) {
                std::cerr << "Error: cannot open input '" << inPath << "'\n";
                // cleanup previous pipe
                if (prevRead) { CloseHandle(prevRead); prevRead = NULL; }
                for (auto &pi : procs) { if (pi.hProcess) CloseHandle(pi.hProcess); if (pi.hThread) CloseHandle(pi.hThread); }
                return -1;
            }
            make_inheritable(inFile);
            hChildStd_IN = (HANDLE)_get_osfhandle(_fileno(inFile));
        } else if (prevRead != NULL) {
            // take read end of previous pipe
            hChildStd_IN = prevRead;
        }

        // Prepare stdout handle for this process
        HANDLE hChildStd_OUT = GetStdHandle(STD_OUTPUT_HANDLE);
        FILE *outFile = nullptr;
        HANDLE nextPipeRead = NULL;
        HANDLE nextPipeWrite = NULL;

        if (!outPath.empty()) {
            outFile = open_output_file(outPath, append);
            if (!outFile) {
                std::cerr << "Error: cannot open output '" << outPath << "'\n";
                if (inFile) fclose(inFile);
                if (prevRead) { CloseHandle(prevRead); prevRead = NULL; }
                for (auto &pi : procs) { if (pi.hProcess) CloseHandle(pi.hProcess); if (pi.hThread) CloseHandle(pi.hThread); }
                return -1;
            }
            make_inheritable(outFile);
            hChildStd_OUT = (HANDLE)_get_osfhandle(_fileno(outFile));
        } else if (i + 1 < n) {
            // create a pipe for this process's stdout -> next process's stdin
            SECURITY_ATTRIBUTES sa{};
            sa.nLength = sizeof(sa);
            sa.bInheritHandle = TRUE;
            sa.lpSecurityDescriptor = nullptr;
            if (!CreatePipe(&nextPipeRead, &nextPipeWrite, &sa, 0)) {
                std::cerr << "Error: CreatePipe failed\n";
                if (inFile) fclose(inFile);
                if (prevRead) { CloseHandle(prevRead); prevRead = NULL; }
                for (auto &pi : procs) { if (pi.hProcess) CloseHandle(pi.hProcess); if (pi.hThread) CloseHandle(pi.hThread); }
                return -1;
            }
            // Ensure read handle is not inheritable by the child that writes to it? No: read end should be inherited by next child.
            hChildStd_OUT = nextPipeWrite;
        }

        // Setup STARTUPINFO
        STARTUPINFOA si{};
        PROCESS_INFORMATION pi{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = hChildStd_IN;
        si.hStdOutput = hChildStd_OUT;
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

        // Build command line: quote exePath in case of spaces
        std::string cmdline;
        cmdline.push_back('"');
        cmdline += exePath;
        cmdline.push_back('"');
        if (!args.empty()) { cmdline.push_back(' '); cmdline += args; }

        std::vector<char> cmdbuf(cmdline.begin(), cmdline.end());
        cmdbuf.push_back('\0');

        BOOL ok = CreateProcessA(
            NULL,
            cmdbuf.data(),
            NULL,
            NULL,
            TRUE,   // inherit handles
            0,
            NULL,
            NULL,
            &si,
            &pi
        );

        // After CreateProcess:
        // Close handles we no longer need in parent:
        // - if we created nextPipeWrite, parent should close it (child has it inherited)
        if (!ok) {
            std::cerr << "Error: CreateProcess failed for: " << cmdline << "\n";
            if (inFile) fclose(inFile);
            if (outFile) fclose(outFile);
            if (nextPipeWrite) { CloseHandle(nextPipeWrite); nextPipeWrite = NULL; }
            if (nextPipeRead)  { CloseHandle(nextPipeRead);  nextPipeRead = NULL; }
            if (prevRead) { CloseHandle(prevRead); prevRead = NULL; }
            for (auto &pinfo : procs) { if (pinfo.hProcess) CloseHandle(pinfo.hProcess); if (pinfo.hThread) CloseHandle(pinfo.hThread); }
            return -1;
        }

        // Save PROCESS_INFORMATION to wait later
        procs.push_back(pi);

        // Parent should close handles it doesn't need:
        if (inFile) {
            // parent can close inFile after child started; child inherited it
            fclose(inFile);
            inFile = nullptr;
        }
        if (outFile) {
            // parent can close outFile after child started; child inherited it
            fclose(outFile);
            outFile = nullptr;
        }

        if (prevRead) {
            // parent can close prevRead; it's used by the child that we started
            CloseHandle(prevRead);
        }
        // The write end of pipe is owned by child; parent should close its copy if created
        if (nextPipeWrite) {
            // parent must close its local copy of write end (we created it); otherwise child won't get EOF
            CloseHandle(nextPipeWrite);
        }

        // next process should use nextPipeRead as its stdin
        prevRead = nextPipeRead;
    } // end for each segment

    // Wait for all children to finish and collect last exit code
    int lastExit = 0;
    for (auto &pi : procs) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD exitCode = 0;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        lastExit = static_cast<int>(exitCode);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }

    return lastExit;
}

// -------------------------------------------
// Main shell (keeps built-ins and interactive prompt)
int main() {
    std::cout << "Winix Shell (CreateProcess engine)\n";
    std::vector<std::string> searchPaths = { ".", "build", "bin" };

    // load history (simple)
    std::vector<std::string> history;
    {
        std::ifstream hf("winix_history.txt");
        std::string line;
        while (std::getline(hf, line)) if (!line.empty()) history.push_back(line);
    }

    for (std::string line;;) {
        std::cout << "\033[1;32m[Winix]\033[0m " << fs::current_path().string() << " > ";
        if (!std::getline(std::cin, line)) break;
        trim(line);
        if (line.empty()) continue;
        history.push_back(line);

        // detect pipeline segments (pipe syntax has highest precedence)
        auto segments = split_pipeline(line);
        if (segments.empty()) continue;

        // Special-case single segment built-ins with redirection handled in-process:
        if (segments.size() == 1) {
            // parse single segment's tokens and redirections
            auto toks = split_tokens(segments[0]);
            std::vector<std::string> keep;
            std::string inPath, outPath;
            bool append = false;
            for (size_t t = 0; t < toks.size(); ++t) {
                if (toks[t] == "<") { if (t+1 < toks.size()) inPath = toks[++t]; }
                else if (toks[t] == ">>") { if (t+1 < toks.size()) outPath = toks[++t], append = true; }
                else if (toks[t] == ">") { if (t+1 < toks.size()) outPath = toks[++t], append = false; }
                else keep.push_back(toks[t]);
            }
            if (!keep.empty()) {
                std::string cmd = keep[0];
                std::string args;
                for (size_t i = 1; i < keep.size(); ++i) { if (!args.empty()) args.push_back(' '); args += keep[i]; }

                // built-ins: cd, pwd, echo, help, exit
                if (cmd == "exit" || cmd == "quit") {
                    std::cout << "Goodbye.\n";
                    break;
                } else if (cmd == "cd") {
                    std::string path = args;
                    trim(path);
                    if (path.empty()) std::cout << "Usage: cd <dir>\n";
                    else {
                        try { fs::current_path(path); } catch (std::exception &e) { std::cerr << "cd: " << e.what() << "\n"; }
                    }
                    continue;
                } else if (cmd == "pwd" || cmd == "echo" || cmd == "help") {
                    // Prepare optional redirection FILE* and dup/restore
                    FILE *inFile = nullptr, *outFile = nullptr;
                    int oldStdin = -1, oldStdout = -1;
                    if (!inPath.empty()) {
                        inFile = open_input_file(inPath);
                        if (!inFile) { std::cerr << "Error: cannot open input '" << inPath << "'\n"; continue; }
                        oldStdin = _dup(_fileno(stdin));
                        _dup2(_fileno(inFile), _fileno(stdin));
                    }
                    if (!outPath.empty()) {
                        outFile = open_output_file(outPath, append);
                        if (!outFile) { std::cerr << "Error: cannot open output '" << outPath << "'\n"; if (inFile) { _dup2(oldStdin, _fileno(stdin)); _close(oldStdin); fclose(inFile);} continue; }
                        oldStdout = _dup(_fileno(stdout));
                        _dup2(_fileno(outFile), _fileno(stdout));
                    }

                    // run built-in
                    if (cmd == "pwd") {
                        std::cout << fs::current_path().string() << "\n";
                    } else if (cmd == "echo") {
                        std::cout << args << "\n";
                    } else if (cmd == "help") {
                        std::cout << "Built-in commands:\n  cd <dir>\n  pwd\n  echo <text>\n  clear\n  help\n  exit\n";
                    }

                    // restore fds and cleanup
                    if (outFile) { fflush(stdout); _dup2(oldStdout, _fileno(stdout)); _close(oldStdout); fclose(outFile); }
                    if (inFile)  { _dup2(oldStdin, _fileno(stdin));  _close(oldStdin);  fclose(inFile); }
                    continue;
                }
            }
        }

        // Otherwise, handle pipeline via CreateProcess
        int rc = winix_exec_pipeline(segments, searchPaths);
        if (rc != 0) {
            // Non-fatal: print exit code for visibility
            // std::cerr << "pipeline exit code: " << rc << "\n";
        }
    }

    // save history
    {
        std::ofstream hf("winix_history.txt", std::ios::trunc);
        for (auto &h : history) hf << h << "\n";
    }

    return 0;
}
