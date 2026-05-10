/*
 * wsudo — same-terminal elevated command execution for Winix
 *
 * Architecture: named pipe I/O bridge + ShellExecuteEx runas elevation
 *
 * Two modes in one binary:
 *   Client mode  (wsudo <cmd> [args])          — non-elevated, user-facing
 *   Broker mode  (wsudo --broker <pipe> ...)   — elevated, hidden, launched by client
 *
 * The client triggers UAC via ShellExecuteEx runas, launching itself as the
 * broker. A named pipe carries stdin/stdout/stderr between them so the
 * elevated command's I/O appears in the original terminal window.
 */

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef WINIX_VERSION
#  define WINIX_VERSION "0.0"
#endif

#define WSUDO_VERSION "1.0"

#define PIPE_BUF      4096
#define PIPE_TIMEOUT  5000   /* ms to wait for broker to create pipe */

/* ------------------------------------------------------------------ helpers */

static int is_elevated(void) {
    BOOL elevated = FALSE;
    HANDLE token;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        TOKEN_ELEVATION elev;
        DWORD sz = sizeof(elev);
        if (GetTokenInformation(token, TokenElevation, &elev, sz, &sz))
            elevated = elev.TokenIsElevated;
        CloseHandle(token);
    }
    return (int)elevated;
}

/* Build a single quoted command string from argv[start..argc-1].
   Caller must free the returned buffer. */
static char *join_args(int argc, char **argv, int start) {
    size_t total = 1;
    for (int i = start; i < argc; i++)
        total += strlen(argv[i]) + 3; /* quotes + space */
    char *buf = malloc(total);
    if (!buf) return NULL;
    buf[0] = '\0';
    for (int i = start; i < argc; i++) {
        if (i > start) strcat(buf, " ");
        /* quote args that contain spaces */
        if (strchr(argv[i], ' ')) {
            strcat(buf, "\"");
            strcat(buf, argv[i]);
            strcat(buf, "\"");
        } else {
            strcat(buf, argv[i]);
        }
    }
    return buf;
}

/* Generate a unique pipe name based on PID + tick count */
static void make_pipe_name(char *out, size_t sz) {
    snprintf(out, sz, "\\\\.\\pipe\\wsudo_%lu_%lu",
             (unsigned long)GetCurrentProcessId(),
             (unsigned long)GetTickCount());
}

/* ------------------------------------------------------------------ client */

/*
 * Forward I/O between the console and the named pipe until the pipe closes.
 * Returns the exit code sent by the broker as the last 4 bytes on the pipe.
 */
static DWORD client_io_loop(HANDLE pipe) {
    HANDLE con_in  = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE con_out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD  exit_code = 1;
    DWORD  avail = 0;
    DWORD  to_read = 0;
    DWORD  events = 0;
    DWORD  orig_mode = 0;
    DWORD  n = 0;
    BOOL   is_console_in;
    char   buf[PIPE_BUF];

    is_console_in = GetConsoleMode(con_in, &orig_mode);
    if (is_console_in)
        SetConsoleMode(con_in, orig_mode & ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT));

    for (;;) {
        /* Drain any output the broker has sent */
        avail = 0;
        while (PeekNamedPipe(pipe, NULL, 0, NULL, &avail, NULL) && avail > 0) {
            to_read = avail < PIPE_BUF ? avail : PIPE_BUF;
            if (!ReadFile(pipe, buf, to_read, &n, NULL) || n == 0)
                goto done;
            WriteFile(con_out, buf, n, &n, NULL);
            avail = 0;
        }

        /* Check if pipe is still alive */
        if (!PeekNamedPipe(pipe, NULL, 0, NULL, NULL, NULL))
            break;

        /* Forward any pending stdin to the broker */
        if (is_console_in) {
            events = 0;
            GetNumberOfConsoleInputEvents(con_in, &events);
            if (events > 0) {
                if (ReadFile(con_in, buf, PIPE_BUF, &n, NULL) && n > 0)
                    WriteFile(pipe, buf, n, &n, NULL);
            }
        }

        Sleep(10);
    }

done:
    /* Read exit code — broker sends 4-byte DWORD as final message */
    avail = 0;
    if (PeekNamedPipe(pipe, NULL, 0, NULL, &avail, NULL) && avail >= 4) {
        DWORD code = 0;
        if (ReadFile(pipe, &code, 4, &n, NULL) && n == 4)
            exit_code = code;
    }

    if (is_console_in)
        SetConsoleMode(con_in, orig_mode);

    return exit_code;
}

static int run_client(int argc, char **argv) {
    /* Already elevated — just exec the command directly */
    if (is_elevated()) {
        char *cmd = join_args(argc, argv, 1);
        if (!cmd) { fprintf(stderr, "wsudo: out of memory\n"); return 1; }
        int rc = system(cmd);
        free(cmd);
        return rc;
    }

    if (argc < 2) {
        fprintf(stderr, "wsudo: no command specified\n");
        return 1;
    }

    /* Build unique pipe name */
    char pipe_name[256];
    make_pipe_name(pipe_name, sizeof(pipe_name));

    /* Build args for broker: wsudo --broker <pipename> <cmd> [args] */
    char broker_args[4096];
    char *user_cmd = join_args(argc, argv, 1);
    if (!user_cmd) { fprintf(stderr, "wsudo: out of memory\n"); return 1; }
    snprintf(broker_args, sizeof(broker_args),
             "--broker \"%s\" %s", pipe_name, user_cmd);
    free(user_cmd);

    /* Get path to this executable */
    char self_path[MAX_PATH];
    GetModuleFileNameA(NULL, self_path, MAX_PATH);

    /* Launch elevated broker — triggers UAC */
    SHELLEXECUTEINFOA sei = {0};
    sei.cbSize       = sizeof(sei);
    sei.fMask        = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NO_CONSOLE;
    sei.lpVerb       = "runas";
    sei.lpFile       = self_path;
    sei.lpParameters = broker_args;
    sei.nShow        = SW_HIDE;

    if (!ShellExecuteExA(&sei)) {
        DWORD err = GetLastError();
        if (err == ERROR_CANCELLED)
            fprintf(stderr, "wsudo: elevation cancelled\n");
        else
            fprintf(stderr, "wsudo: failed to elevate (error %lu)\n", err);
        return 1;
    }

    /* Wait for broker to create the pipe (up to PIPE_TIMEOUT ms) */
    HANDLE pipe = INVALID_HANDLE_VALUE;
    DWORD deadline = GetTickCount() + PIPE_TIMEOUT;
    while (GetTickCount() < deadline) {
        pipe = CreateFileA(pipe_name, GENERIC_READ | GENERIC_WRITE,
                           0, NULL, OPEN_EXISTING, 0, NULL);
        if (pipe != INVALID_HANDLE_VALUE) break;
        Sleep(50);
    }

    if (pipe == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "wsudo: timed out waiting for elevated process\n");
        if (sei.hProcess) CloseHandle(sei.hProcess);
        return 1;
    }

    DWORD exit_code = client_io_loop(pipe);

    CloseHandle(pipe);
    if (sei.hProcess) {
        WaitForSingleObject(sei.hProcess, 3000);
        CloseHandle(sei.hProcess);
    }

    return (int)exit_code;
}

/* ------------------------------------------------------------------ broker */

/*
 * Broker runs elevated and hidden. It:
 *   1. Creates the named pipe
 *   2. Waits for the client to connect
 *   3. Spawns the target command with redirected stdio
 *   4. Bridges I/O between the process and the pipe
 *   5. Sends the exit code as a final 4-byte DWORD
 */
static int run_broker(const char *pipe_name, int argc, char **argv, int cmd_start) {
    /* Create the named pipe */
    HANDLE pipe = CreateNamedPipeA(
        pipe_name,
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        1, PIPE_BUF, PIPE_BUF, 0, NULL);

    if (pipe == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "wsudo: failed to create pipe (error %lu)\n", GetLastError());
        return 1;
    }

    /* Wait for client to connect */
    if (!ConnectNamedPipe(pipe, NULL)) {
        DWORD err = GetLastError();
        if (err != ERROR_PIPE_CONNECTED) {
            fprintf(stderr, "wsudo: pipe connect failed (error %lu)\n", err);
            CloseHandle(pipe);
            return 1;
        }
    }

    /* Build command line for CreateProcess */
    char *cmd_line = join_args(argc, argv, cmd_start);
    if (!cmd_line) {
        CloseHandle(pipe);
        return 1;
    }

    /* Set up stdio redirection: child stdin reads from pipe, stdout/stderr write to pipe */
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};

    HANDLE child_stdin_r,  child_stdin_w;
    HANDLE child_stdout_r, child_stdout_w;

    if (!CreatePipe(&child_stdin_r, &child_stdin_w, &sa, 0) ||
        !CreatePipe(&child_stdout_r, &child_stdout_w, &sa, 0)) {
        fprintf(stderr, "wsudo: failed to create I/O pipes\n");
        free(cmd_line);
        CloseHandle(pipe);
        return 1;
    }

    /* Don't inherit the write end of stdin pipe or read end of stdout pipe */
    SetHandleInformation(child_stdin_w,  HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(child_stdout_r, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = {0};
    si.cb          = sizeof(si);
    si.dwFlags     = STARTF_USESTDHANDLES;
    si.hStdInput   = child_stdin_r;
    si.hStdOutput  = child_stdout_w;
    si.hStdError   = child_stdout_w; /* merge stderr into stdout pipe */

    PROCESS_INFORMATION pi = {0};
    if (!CreateProcessA(NULL, cmd_line, NULL, NULL, TRUE,
                        CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        fprintf(stderr, "wsudo: failed to launch '%s' (error %lu)\n",
                cmd_line, GetLastError());
        free(cmd_line);
        CloseHandle(child_stdin_r);  CloseHandle(child_stdin_w);
        CloseHandle(child_stdout_r); CloseHandle(child_stdout_w);
        CloseHandle(pipe);
        return 1;
    }

    free(cmd_line);

    /* Close handles the child now owns */
    CloseHandle(child_stdin_r);
    CloseHandle(child_stdout_w);

    /* I/O bridge loop */
    char buf[PIPE_BUF];
    DWORD n;

    for (;;) {
        /* Forward child stdout → pipe */
        DWORD avail = 0;
        while (PeekNamedPipe(child_stdout_r, NULL, 0, NULL, &avail, NULL) && avail > 0) {
            DWORD to_read = avail < PIPE_BUF ? avail : PIPE_BUF;
            if (!ReadFile(child_stdout_r, buf, to_read, &n, NULL) || n == 0)
                goto child_done;
            WriteFile(pipe, buf, n, &n, NULL);
        }

        /* Check if child is still running */
        DWORD status;
        GetExitCodeProcess(pi.hProcess, &status);
        if (status != STILL_ACTIVE) break;

        /* Forward pipe (client stdin) → child stdin */
        avail = 0;
        if (PeekNamedPipe(pipe, NULL, 0, NULL, &avail, NULL) && avail > 0) {
            DWORD to_read = avail < PIPE_BUF ? avail : PIPE_BUF;
            if (ReadFile(pipe, buf, to_read, &n, NULL) && n > 0)
                WriteFile(child_stdin_w, buf, n, &n, NULL);
        }

        Sleep(10);
    }

child_done:
    /* Drain any remaining child output */
    while (ReadFile(child_stdout_r, buf, PIPE_BUF, &n, NULL) && n > 0)
        WriteFile(pipe, buf, n, &n, NULL);

    /* Send exit code as final 4-byte DWORD */
    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    WriteFile(pipe, &exit_code, 4, &n, NULL);
    FlushFileBuffers(pipe);

    CloseHandle(child_stdout_r);
    CloseHandle(child_stdin_w);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(pipe);

    return (int)exit_code;
}

/* ------------------------------------------------------------------ main */

static void usage(void) {
    puts("Usage: wsudo <command> [args...]");
    puts("       wsudo --status");
    puts("       wsudo --version");
    puts("");
    puts("Run a command with elevated (Administrator) privileges.");
    puts("A UAC prompt will appear if the current session is not elevated.");
    puts("");
    puts("Options:");
    puts("  --status     Print elevation status of current session and exit");
    puts("  --version    Print version and exit");
    puts("  --help       Print this help and exit");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage();
        return 1;
    }

    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        usage();
        return 0;
    }

    if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0) {
        printf("wsudo %s (Winix %s)\n", WSUDO_VERSION, WINIX_VERSION);
        return 0;
    }

    if (strcmp(argv[1], "--status") == 0) {
        if (is_elevated()) {
            puts("elevated");
            return 0;
        } else {
            puts("not elevated");
            return 1;
        }
    }

    /* Broker mode — launched by client via ShellExecuteEx runas */
    if (strcmp(argv[1], "--broker") == 0) {
        if (argc < 4) {
            fprintf(stderr, "wsudo: broker requires pipe name and command\n");
            return 1;
        }
        return run_broker(argv[2], argc, argv, 3);
    }

    /* Client mode */
    return run_client(argc, argv);
}
