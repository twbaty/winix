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

#include "winix_version.h"

#define WSUDO_VERSION "0.1"

#define PIPE_BUF      4096
#define PIPE_TIMEOUT  30000  /* ms to wait for broker — must cover UAC response time */

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

static DWORD client_io_loop(HANDLE pipe, HANDLE broker_proc) {
    HANDLE con_in  = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE con_out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD  avail = 0, to_read = 0, n = 0, written = 0, exit_code = 1;
    char   buf[PIPE_BUF];
    DWORD  orig_mode = 0;
    BOOL   is_console = GetConsoleMode(con_in, &orig_mode);

    if (is_console) {
        SetConsoleMode(con_in, ENABLE_PROCESSED_INPUT);
        FlushConsoleInputBuffer(con_in);
    }

    /* Run until the broker calls DisconnectNamedPipe (or crashes), which
       causes PeekNamedPipe to return FALSE.  We do NOT break on broker
       process exit here — the broker keeps the pipe open until it has
       flushed all output, so the process exit signal is meaningless for
       flow control. */
    for (;;) {
        BOOL did_work = FALSE;

        /* Drain all available pipe output */
        avail = 0;
        if (!PeekNamedPipe(pipe, NULL, 0, NULL, &avail, NULL)) {
            /* ERROR_PIPE_NOT_CONNECTED (233): client connected before the
               broker called ConnectNamedPipe — race condition.  Retry while
               broker is still alive; once broker calls ConnectNamedPipe the
               pipe transitions to connected state and PeekNamedPipe works. */
            if (GetLastError() == ERROR_PIPE_NOT_CONNECTED &&
                WaitForSingleObject(broker_proc, 0) != WAIT_OBJECT_0) {
                Sleep(10);
                continue;
            }
            break; /* true disconnect or broker exited */
        }
        while (avail > 0) {
            to_read = avail < PIPE_BUF ? avail : PIPE_BUF;
            if (!ReadFile(pipe, buf, to_read, &n, NULL) || n == 0) goto done;
            WriteFile(con_out, buf, n, &written, NULL);
            did_work = TRUE;
            avail = 0;
            if (!PeekNamedPipe(pipe, NULL, 0, NULL, &avail, NULL)) goto done;
        }

        /* Forward keystrokes to the elevated process.
           No local echo for printable chars: cmd.exe ECHO ON echoes the full
           prompt+command line after receiving Enter, so local char echo would
           garble the display.  On Enter we write \r (CR only) to rewind the
           cursor to column 0; cmd.exe's echo "C:\...\demo>cmd\r\n" then
           overwrites the prompt line cleanly. */
        if (is_console) {
            DWORD nevents = 0;
            GetNumberOfConsoleInputEvents(con_in, &nevents);
            if (nevents) did_work = TRUE;
            for (; nevents > 0; nevents--) {
                INPUT_RECORD ir;
                if (!ReadConsoleInputA(con_in, &ir, 1, &n) || n == 0) break;
                if (ir.EventType != KEY_EVENT || !ir.Event.KeyEvent.bKeyDown) continue;
                WORD vk = ir.Event.KeyEvent.wVirtualKeyCode;
                char c  = ir.Event.KeyEvent.uChar.AsciiChar;
                if (vk == VK_RETURN) {
                    WriteFile(pipe,    "\r\n", 2, &written, NULL);
                    WriteFile(con_out, "\r",   1, &written, NULL);
                } else if (vk == VK_BACK) {
                    WriteFile(pipe, "\x08", 1, &written, NULL);
                } else if (c >= 0x20 && (unsigned char)c < 0x7F) {
                    WriteFile(pipe, &c, 1, &written, NULL);
                }
            }
        }

        if (!did_work) Sleep(5);
    }

done:
    if (is_console) SetConsoleMode(con_in, orig_mode);
    /* Broker may still be closing handles — wait briefly for clean exit */
    WaitForSingleObject(broker_proc, 3000);
    GetExitCodeProcess(broker_proc, &exit_code);
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
    sei.fMask        = SEE_MASK_NOCLOSEPROCESS;
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

    /* Poll until broker creates the pipe, or broker process exits (crash/cancel).
       No fixed timeout — ShellExecuteEx returns before UAC is answered, so the
       user can take as long as they need to respond to the prompt. */
    HANDLE pipe = INVALID_HANDLE_VALUE;
    for (;;) {
        pipe = CreateFileA(pipe_name, GENERIC_READ | GENERIC_WRITE,
                           0, NULL, OPEN_EXISTING, 0, NULL);
        if (pipe != INVALID_HANDLE_VALUE) break;

        /* Broker process exited without creating the pipe — it crashed */
        if (sei.hProcess &&
            WaitForSingleObject(sei.hProcess, 0) == WAIT_OBJECT_0) {
            fprintf(stderr, "wsudo: elevated process failed to start\n");
            CloseHandle(sei.hProcess);
            return 1;
        }

        Sleep(50);
    }

    DWORD exit_code = client_io_loop(pipe, sei.hProcess);

    CloseHandle(pipe);
    CloseHandle(sei.hProcess);

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
    /* Detach from the shared console — broker communicates through the named
       pipe only. Keeping the console handle causes state corruption on exit. */
    FreeConsole();

    /* Create the named pipe */
    /* NULL DACL lets the non-elevated client connect across the elevation boundary */
    SECURITY_DESCRIPTOR sd;
    SECURITY_ATTRIBUTES pipe_sa;
    InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
    SetSecurityDescriptorDacl(&sd, TRUE, NULL, FALSE);
    pipe_sa.nLength              = sizeof(pipe_sa);
    pipe_sa.lpSecurityDescriptor = &sd;
    pipe_sa.bInheritHandle       = FALSE;

    HANDLE pipe = CreateNamedPipeA(
        pipe_name,
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        1, PIPE_BUF, PIPE_BUF, PIPE_TIMEOUT, &pipe_sa);

    if (pipe == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "wsudo: failed to create pipe (error %lu)\n", GetLastError());
        return 1;
    }

    /* Wait for client to connect — blocking, but client is already polling so
       this returns almost immediately after the client calls CreateFileA */
    if (!ConnectNamedPipe(pipe, NULL)) {
        DWORD err = GetLastError();
        if (err != ERROR_PIPE_CONNECTED) {
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
        DWORD cperr = GetLastError();
        fprintf(stderr, "wsudo: failed to launch '%s' (error %lu)\n",
                cmd_line, cperr);
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

    /* Single-threaded I/O bridge — avoids the deadlock caused by concurrent
       synchronous ReadFile + WriteFile on the same named pipe server handle.
       Windows serializes sync I/O on a single handle: if one thread blocks in
       ReadFile(pipe) waiting for client input, a second thread's WriteFile(pipe)
       can never proceed. Poll both directions with PeekNamedPipe instead. */
    {
        char iobuf[PIPE_BUF];
        DWORD n, written, avail;
        for (;;) {
            BOOL did_work = FALSE;

            /* Forward any available child stdout → named pipe */
            avail = 0;
            PeekNamedPipe(child_stdout_r, NULL, 0, NULL, &avail, NULL);
            while (avail > 0) {
                DWORD to_read = avail < (DWORD)PIPE_BUF ? avail : (DWORD)PIPE_BUF;
                if (!ReadFile(child_stdout_r, iobuf, to_read, &n, NULL) || n == 0)
                    goto io_done;
                WriteFile(pipe, iobuf, n, &written, NULL);
                did_work = TRUE;
                avail = 0;
                PeekNamedPipe(child_stdout_r, NULL, 0, NULL, &avail, NULL);
            }

            /* Forward any available client stdin → child stdin */
            avail = 0;
            PeekNamedPipe(pipe, NULL, 0, NULL, &avail, NULL);
            while (avail > 0) {
                DWORD to_read = avail < (DWORD)PIPE_BUF ? avail : (DWORD)PIPE_BUF;
                if (!ReadFile(pipe, iobuf, to_read, &n, NULL) || n == 0)
                    goto io_done;
                WriteFile(child_stdin_w, iobuf, n, &written, NULL);
                did_work = TRUE;
                avail = 0;
                PeekNamedPipe(pipe, NULL, 0, NULL, &avail, NULL);
            }

            /* Once child exits, drain any remaining output then stop */
            if (WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0) {
                avail = 0;
                PeekNamedPipe(child_stdout_r, NULL, 0, NULL, &avail, NULL);
                while (avail > 0) {
                    DWORD to_read = avail < (DWORD)PIPE_BUF ? avail : (DWORD)PIPE_BUF;
                    if (!ReadFile(child_stdout_r, iobuf, to_read, &n, NULL) || n == 0) break;
                    WriteFile(pipe, iobuf, n, &written, NULL);
                    avail = 0;
                    PeekNamedPipe(child_stdout_r, NULL, 0, NULL, &avail, NULL);
                }
                break;
            }

            if (!did_work) Sleep(5);
        }
        io_done:;
    }

    /* FlushFileBuffers blocks until the client has read all buffered data.
       DisconnectNamedPipe is the explicit "done" signal that causes the
       client's PeekNamedPipe to return FALSE and exit its poll loop. */
    FlushFileBuffers(pipe);
    DisconnectNamedPipe(pipe);

    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);

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
