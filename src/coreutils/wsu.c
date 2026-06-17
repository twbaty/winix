/*
 * wsu — interactive elevated Winix shell (ConPTY bridge)
 *
 * OVERVIEW
 * --------
 * `wsu` launches an interactive elevated winix shell inside the CURRENT
 * terminal window — no new window pops up, colours and line editing work,
 * Ctrl+C, arrow keys, tab completion, everything.  Type `exit` to drop back
 * to the non-elevated shell.
 *
 * Compare with `wsudo <cmd>`: wsudo runs a single elevated command and exits.
 * wsu is the full interactive "su" equivalent for Windows.
 *
 *
 * WHY ConPTY, NOT PLAIN PIPE REDIRECTION
 * ----------------------------------------
 * The naive approach — CreateProcess with STARTF_USESTDHANDLES pointing to
 * anonymous pipes, then copy bytes — breaks for interactive programs because:
 *
 *   1. winix.exe calls GetConsoleScreenBufferInfo, SetConsoleCursorPosition,
 *      SetConsoleMode, etc.  Without a real console those calls fail silently
 *      or crash; the shell has no idea its terminal size.
 *
 *   2. Ctrl+C and Ctrl+Break are console signals, not byte sequences on stdin.
 *      With a plain pipe, the elevated process never receives them.
 *
 *   3. The elevated process can't write to the non-elevated console's CONOUT$
 *      (integrity level boundary — Windows blocks the cross-elevation handle).
 *
 * CreatePseudoConsole (ConPTY, Windows 10 Build 17763+) fixes all three:
 *   - The shell gets a real console object; all Console API calls work.
 *   - ConPTY translates Console API calls into VT sequences on its output pipe.
 *   - We read those VT sequences and write them to the user's visible terminal.
 *   - Keyboard input is translated from Windows INPUT_RECORDs to VT on our
 *     side and written to the PTY's input pipe.
 *
 *
 * PROCESS ROLES
 * -------------
 * Two roles, one binary:
 *
 *   CLIENT (non-elevated, visible terminal)
 *     - Detects console size, generates a unique named-pipe name.
 *     - Relaunches wsu.exe elevated (ShellExecuteEx "runas") passing
 *       "--broker <pipe_name>" on the command line.
 *     - Connects to the named pipe once the broker is ready.
 *     - Sends a COORD (console width/height) to the broker.
 *     - Switches the local console to raw mode (no echo, no line buffering).
 *     - Polls: pipe→stdout (VT output from shell) and keyboard→pipe (input).
 *
 *   BROKER (elevated, hidden — SW_HIDE)
 *     - FreeConsole() immediately; it doesn't need a visible window.
 *     - Creates the named pipe with a NULL DACL so the non-elevated client
 *       can connect despite the integrity level difference.
 *     - Waits for the client to connect, reads the COORD handshake.
 *     - Creates two anonymous pipe pairs (pty_in, pty_out) for the PTY.
 *     - Calls CreatePseudoConsole with the received size and the pipe ends.
 *     - Spawns winix.exe using STARTUPINFOEXA + PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
 *       so winix inherits the ConPTY as its console.
 *     - Runs the same single-threaded PeekNamedPipe I/O loop as wsudo to
 *       bridge PTY output→named pipe and named pipe→PTY input.
 *
 *
 * NAMED PIPE NAMING
 * -----------------
 * Format: \\.\pipe\wsu_<PID>_<tick>
 * PID + tick makes it unique per invocation even if wsu is run concurrently.
 * The broker receives the full pipe name as a command-line argument, so the
 * client and broker don't need to agree on any other out-of-band channel.
 *
 *
 * SINGLE-THREADED POLL LOOP (the wsudo lesson)
 * ---------------------------------------------
 * wsudo originally used two threads — one copying stdout→pipe, one copying
 * pipe→stdin — sharing the same named-pipe server handle.  Windows serialises
 * synchronous I/O per handle: the stdin thread's blocking ReadFile held the
 * handle, permanently starving the stdout thread's WriteFile.  Result: zero
 * output (WAIT_TIMEOUT after 2 s).
 *
 * The fix (used identically here): a single loop that checks BOTH directions
 * with PeekNamedPipe before reading, so neither direction ever blocks while
 * the other has data.  5 ms sleep only when nothing moved, keeping CPU idle
 * during quiet periods.
 *
 *
 * PIPE SECURITY (NULL DACL)
 * -------------------------
 * Named pipes created by an elevated process normally can't be opened by a
 * medium-integrity (non-elevated) client — access denied.  We create an
 * explicit SECURITY_DESCRIPTOR with a NULL DACL (everyone can access) so the
 * non-elevated client can connect.  This is intentional: the pipe is local-
 * machine only and carries terminal I/O, not secrets.
 *
 *
 * HANDLE OWNERSHIP AFTER CreatePseudoConsole
 * -------------------------------------------
 * We create two pipe pairs:
 *   pty_in_r  / pty_in_w   — PTY reads input from pty_in_r
 *   pty_out_r / pty_out_w  — PTY writes output to pty_out_w
 *
 * After CreatePseudoConsole(size, pty_in_r, pty_out_w, 0, &hPC):
 *   - The ConPTY owns pty_in_r and pty_out_w internally.
 *   - We must close our copies of those two handles immediately; keeping them
 *     open causes EOF never to signal when the PTY is torn down.
 *   - We retain pty_in_w (write input to PTY) and pty_out_r (read output from
 *     PTY) — these are our end of the bridge.
 *
 *
 * CONSOLE SIZE HANDSHAKE
 * ----------------------
 * The broker must know the terminal dimensions before calling
 * CreatePseudoConsole, otherwise winix starts with 0×0 and all cursor
 * positioning is wrong.  We send a raw COORD struct as the first 4 bytes over
 * the named pipe (sizeof(COORD) == 4: two 16-bit ints, X then Y).
 * Both sides are native so endianness is not a concern.
 *
 * Future: SIGWINCH-equivalent (console resize) could re-send a COORD prefixed
 * with a magic byte, then call ResizePseudoConsole.  Not yet implemented.
 *
 *
 * ALREADY-ELEVATED FAST PATH
 * --------------------------
 * If wsu is invoked from an already-elevated shell, the ConPTY bridge is
 * unnecessary — we can spawn winix.exe directly with handle inheritance so it
 * shares the existing console.  This is the same as just running winix, but
 * kept here so `wsu` always means "get me an elevated shell" regardless of
 * context.
 *
 *
 * MINIMUM WINDOWS VERSION
 * -----------------------
 * CreatePseudoConsole requires Windows 10 Build 17763 (October 2018 Update,
 * version 1809).  We use GetProcAddress at runtime so the binary can start on
 * older systems and print a helpful error rather than crashing at load time
 * with a missing-export error.
 *
 *
 * CMakeLists entry (not yet added — wsu is shelved pending integration test):
 *   add_executable(wsu src/coreutils/wsu.c)
 *   target_link_libraries(wsu shell32)
 *   # add wsu to the install(TARGETS ...) block alongside wsudo
 *
 *
 * KNOWN LIMITATIONS / FUTURE WORK
 * --------------------------------
 *   - Console resize not forwarded (ResizePseudoConsole not called on resize).
 *   - Mouse events not translated to VT (most shells ignore them anyway).
 *   - UTF-8/wide characters: key_to_vt uses AsciiChar; IME/CJK input untested.
 *   - No timeout if UAC prompt is left open indefinitely.
 *   - Single named-pipe instance (PIPE_TIMEOUT 30 s); concurrent wsu invocations
 *     each get their own unique pipe name so they don't collide.
 */

#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "winix_version.h"

#define WSU_VERSION  "0.1"

/* I/O loop buffer.  Larger buffers reduce syscall overhead for bulk output
 * (e.g. `ls` on a big directory) but add latency for keystroke echo.
 * 4 KiB is a good middle ground. */
#define IO_BUF       4096

/* ConnectNamedPipe timeout in ms — how long the broker waits for the client
 * to connect before giving up.  30 s is generous; UAC can be slow. */
#define PIPE_TIMEOUT 30000


/* ================================================================
 * ConPTY API — dynamic loading
 * ================================================================
 *
 * We GetProcAddress these from kernel32 rather than linking directly.
 * Reason: if wsu.exe is ever run on Windows 10 < 17763 or Windows 8/7,
 * the import would cause a "procedure not found" error at load time before
 * main() is even called.  With GetProcAddress we load cleanly and print a
 * human-readable error instead.
 *
 * HPCON is an opaque handle type.  The actual definition is internal to
 * conhost; we only ever pass it to the API functions, so VOID* is fine.
 */
typedef VOID *HPCON;
typedef HRESULT (WINAPI *PFN_CreatePseudoConsole)(COORD, HANDLE, HANDLE, DWORD, HPCON *);
typedef void    (WINAPI *PFN_ClosePseudoConsole)(HPCON);

static PFN_CreatePseudoConsole g_CreatePseudoConsole;
static PFN_ClosePseudoConsole  g_ClosePseudoConsole;

/*
 * PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE (0x00020016)
 *
 * This attribute tells CreateProcess to attach the new process to a ConPTY
 * instead of inheriting the calling process's console.  Older MinGW headers
 * may not define it, so we provide the raw value from the Windows SDK.
 * The value is stable since its introduction in Windows 10 1809.
 */
#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016UL
#endif

/*
 * conpty_load() — resolve ConPTY symbols at runtime.
 * Returns 1 if the current OS supports ConPTY, 0 otherwise.
 * Must be called before run_broker() or the already-elevated spawn path.
 */
static int conpty_load(void) {
    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    if (!k32) return 0;
    g_CreatePseudoConsole = (PFN_CreatePseudoConsole)
        GetProcAddress(k32, "CreatePseudoConsole");
    g_ClosePseudoConsole  = (PFN_ClosePseudoConsole)
        GetProcAddress(k32, "ClosePseudoConsole");
    return g_CreatePseudoConsole != NULL;
}


/* ================================================================
 * Helpers
 * ================================================================ */

/*
 * make_pipe_name() — generate a unique local named-pipe path.
 *
 * PID ensures no collision between concurrent wsu instances.
 * GetTickCount adds further uniqueness in case two processes share a PID
 * across a very fast create/destroy cycle (unlikely but free to guard).
 */
static void make_pipe_name(char *out, size_t sz) {
    snprintf(out, sz, "\\\\.\\pipe\\wsu_%lu_%lu",
             (unsigned long)GetCurrentProcessId(),
             (unsigned long)GetTickCount());
}

/*
 * find_winix() — locate winix.exe relative to wsu.exe.
 *
 * Install layout:
 *   C:\Winix\winix.exe       ← shell
 *   C:\Winix\bin\wsu.exe     ← this binary
 *
 * Algorithm: strip the filename, strip the "bin" directory, append winix.exe.
 * If that path doesn't exist (e.g. running from a build dir), fall back to
 * "winix.exe" and let CreateProcess search PATH.
 */
static void find_winix(char *out, size_t sz) {
    GetModuleFileNameA(NULL, out, (DWORD)sz);
    char *s = strrchr(out, '\\');
    if (s) { *s = '\0'; s = strrchr(out, '\\'); }
    if (s) strcpy(s + 1, "winix.exe");
    else   strncpy(out, "winix.exe", sz);
    if (GetFileAttributesA(out) == INVALID_FILE_ATTRIBUTES)
        strncpy(out, "winix.exe", sz);
}

/*
 * is_elevated() — returns 1 if the current token has elevated privileges.
 *
 * Uses TokenElevation from the process token.  On standard UAC systems this
 * reliably distinguishes HIGH integrity (admin) from MEDIUM (normal user).
 */
static int is_elevated(void) {
    BOOL e = FALSE;
    HANDLE tok;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) {
        TOKEN_ELEVATION te;
        DWORD sz = sizeof(te);
        if (GetTokenInformation(tok, TokenElevation, &te, sz, &sz))
            e = te.TokenIsElevated;
        CloseHandle(tok);
    }
    return (int)e;
}

/*
 * key_to_vt() — convert a Windows KEY_EVENT_RECORD to VT escape bytes.
 *
 * WHY: ConPTY's input pipe speaks VT, not Windows console INPUT_RECORDs.
 * ReadConsoleInputA gives us INPUT_RECORDs.  We translate printable characters
 * directly (uChar.AsciiChar) and map special keys to their ECMA-48 / xterm
 * VT sequences.
 *
 * Only key-DOWN events should be passed here; key-up events are silently
 * discarded by the caller.  Control keys (Ctrl+C, Ctrl+Z, etc.) come through
 * as AsciiChar values 0x03, 0x1A, etc. — the ConPTY interprets them correctly.
 *
 * Returns the number of bytes written to `out` (0 = no translation available,
 * caller should skip this event).  `out` must be at least 8 bytes.
 *
 * Sequences follow xterm/VT100 conventions, which is what most terminal-aware
 * programs (readline, winix's line editor) expect.  F6 is \x1B[17~ (not 16~)
 * because the numbering skips 16 per the xterm spec.
 */
static int key_to_vt(const KEY_EVENT_RECORD *ke, char *out) {
    char c = ke->uChar.AsciiChar;
    if (c) { out[0] = c; return 1; }

    switch (ke->wVirtualKeyCode) {
        case VK_UP:     memcpy(out, "\x1B[A",   3); return 3;
        case VK_DOWN:   memcpy(out, "\x1B[B",   3); return 3;
        case VK_RIGHT:  memcpy(out, "\x1B[C",   3); return 3;
        case VK_LEFT:   memcpy(out, "\x1B[D",   3); return 3;
        case VK_HOME:   memcpy(out, "\x1B[H",   3); return 3;
        case VK_END:    memcpy(out, "\x1B[F",   3); return 3;
        case VK_INSERT: memcpy(out, "\x1B[2~",  4); return 4;
        case VK_DELETE: memcpy(out, "\x1B[3~",  4); return 4;
        case VK_PRIOR:  memcpy(out, "\x1B[5~",  4); return 4;  /* Page Up   */
        case VK_NEXT:   memcpy(out, "\x1B[6~",  4); return 4;  /* Page Down */
        case VK_F1:     memcpy(out, "\x1BOP",   3); return 3;
        case VK_F2:     memcpy(out, "\x1BOQ",   3); return 3;
        case VK_F3:     memcpy(out, "\x1BOR",   3); return 3;
        case VK_F4:     memcpy(out, "\x1BOS",   3); return 3;
        case VK_F5:     memcpy(out, "\x1B[15~", 5); return 5;
        case VK_F6:     memcpy(out, "\x1B[17~", 5); return 5;  /* 16 is skipped */
        case VK_F7:     memcpy(out, "\x1B[18~", 5); return 5;
        case VK_F8:     memcpy(out, "\x1B[19~", 5); return 5;
        case VK_F9:     memcpy(out, "\x1B[20~", 5); return 5;
        case VK_F10:    memcpy(out, "\x1B[21~", 5); return 5;
        case VK_F11:    memcpy(out, "\x1B[23~", 5); return 5;  /* 22 is skipped */
        case VK_F12:    memcpy(out, "\x1B[24~", 5); return 5;
    }
    return 0;
}


/* ================================================================
 * CLIENT — non-elevated side
 * ================================================================ */

/*
 * run_client() — the non-elevated half of the ConPTY bridge.
 *
 * Steps:
 *   1. Sample the current console window dimensions (for ConPTY sizing).
 *   2. Generate a unique named-pipe name and elevate wsu.exe --broker <name>.
 *   3. Poll until the broker's pipe appears (broker may take a moment after
 *      UAC confirmation).  Bail out immediately if the broker process dies.
 *   4. Send the COORD handshake (4 bytes).
 *   5. Set the local console to raw mode (no echo, no cooked editing).
 *      Enable VT processing on stdout so the shell's ANSI sequences render.
 *   6. Poll loop: pipe output → local stdout; local keystrokes → pipe.
 *   7. On exit: restore original console modes.
 *
 * RAW MODE FLAGS
 * --------------
 * We set con_in mode to 0 (all flags off):
 *   - ENABLE_ECHO_INPUT off: keystrokes not echoed locally (the elevated
 *     shell echoes via the PTY → pipe → our stdout).
 *   - ENABLE_LINE_INPUT off: we read one event at a time, not a full line.
 *   - ENABLE_PROCESSED_INPUT off: Ctrl+C etc. come as byte 0x03, not signals
 *     (the ConPTY translates them for the elevated child).
 *
 * For con_out we keep the original flags and add:
 *   - ENABLE_VIRTUAL_TERMINAL_PROCESSING: process VT/ANSI sequences from pipe.
 *   - DISABLE_NEWLINE_AUTO_RETURN: prevent Windows from adding an extra \r
 *     before every \n in VT mode (would double-space output).
 */
static int run_client(void) {
    HANDLE con_in  = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE con_out = GetStdHandle(STD_OUTPUT_HANDLE);

    /* Sample the visible window size (not the scrollback buffer size). */
    COORD size = {80, 24};
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(con_out, &csbi)) {
        size.X = csbi.srWindow.Right  - csbi.srWindow.Left + 1;
        size.Y = csbi.srWindow.Bottom - csbi.srWindow.Top  + 1;
    }

    char pipe_name[256];
    make_pipe_name(pipe_name, sizeof(pipe_name));

    char self_path[MAX_PATH];
    GetModuleFileNameA(NULL, self_path, MAX_PATH);

    char broker_args[512];
    snprintf(broker_args, sizeof(broker_args), "--broker \"%s\"", pipe_name);

    /*
     * ShellExecuteEx with verb "runas" triggers UAC.
     * SEE_MASK_NOCLOSEPROCESS keeps the broker process handle so we can
     * detect early exit (e.g. UAC denied, or broker crashed before connecting).
     * SW_HIDE: the broker has no UI; hiding prevents a flash of an empty window.
     */
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
            fprintf(stderr, "wsu: elevation cancelled by user\n");
        else
            fprintf(stderr, "wsu: failed to elevate (error %lu)\n", err);
        return 1;
    }

    /*
     * Spin until the broker's pipe appears.  The broker creates the pipe
     * and calls ConnectNamedPipe before we can connect, so we may loop a
     * few times while UAC completes and the elevated process starts.
     * If the broker process exits before we connect, it failed — give up.
     */
    HANDLE pipe = INVALID_HANDLE_VALUE;
    for (;;) {
        pipe = CreateFileA(pipe_name, GENERIC_READ | GENERIC_WRITE,
                           0, NULL, OPEN_EXISTING, 0, NULL);
        if (pipe != INVALID_HANDLE_VALUE) break;
        if (sei.hProcess &&
            WaitForSingleObject(sei.hProcess, 0) == WAIT_OBJECT_0) {
            fprintf(stderr, "wsu: elevated broker exited before connecting\n");
            CloseHandle(sei.hProcess);
            return 1;
        }
        Sleep(50);
    }

    /* Protocol step 1: send console dimensions so broker can size the ConPTY. */
    DWORD written;
    WriteFile(pipe, &size, sizeof(COORD), &written, NULL);

    /* Enter raw mode. */
    DWORD orig_in, orig_out;
    GetConsoleMode(con_in,  &orig_in);
    GetConsoleMode(con_out, &orig_out);
    SetConsoleMode(con_in, 0);
    SetConsoleMode(con_out, orig_out
                   | ENABLE_VIRTUAL_TERMINAL_PROCESSING
                   | DISABLE_NEWLINE_AUTO_RETURN);

    char vt[32], io[IO_BUF];
    DWORD n, avail;

    /*
     * was_connected: PeekNamedPipe returns ERROR_PIPE_NOT_CONNECTED (233)
     * while the server has created the pipe but not yet called
     * ConnectNamedPipe — or briefly after we open but before the server
     * acknowledges.  Retry silently until the first successful Peek, then
     * treat subsequent 233s as "pipe closed" (broker exited).
     */
    BOOL was_connected = FALSE;

    for (;;) {
        BOOL did_work = FALSE;

        /*
         * DIRECTION 1: pipe output (VT sequences from elevated shell) → stdout.
         *
         * PeekNamedPipe first so we never block on ReadFile when there's
         * nothing to read.  This is the core of the single-threaded poll
         * pattern; see "SINGLE-THREADED POLL LOOP" in the file header.
         */
        avail = 0;
        if (!PeekNamedPipe(pipe, NULL, 0, NULL, &avail, NULL)) {
            if (GetLastError() == ERROR_PIPE_NOT_CONNECTED && !was_connected) {
                Sleep(10);
                continue;
            }
            break;  /* pipe closed — broker exited */
        }
        was_connected = TRUE;

        while (avail > 0) {
            DWORD to_read = avail < IO_BUF ? avail : IO_BUF;
            if (!ReadFile(pipe, io, to_read, &n, NULL) || n == 0) goto restore;
            WriteFile(con_out, io, n, &written, NULL);
            did_work = TRUE;
            avail = 0;
            if (!PeekNamedPipe(pipe, NULL, 0, NULL, &avail, NULL)) goto restore;
        }

        /*
         * DIRECTION 2: keyboard events → VT → pipe → elevated shell's PTY.
         *
         * GetNumberOfConsoleInputEvents is non-blocking; only call
         * ReadConsoleInputA when events are actually queued.
         * We discard all non-KEY events (mouse, resize, focus) and key-up
         * events; only key-down events carry useful data.
         */
        DWORD nevents = 0;
        GetNumberOfConsoleInputEvents(con_in, &nevents);
        if (nevents) {
            INPUT_RECORD irs[64];
            DWORD got;
            nevents = nevents < 64 ? nevents : 64;
            ReadConsoleInputA(con_in, irs, nevents, &got);
            for (DWORD i = 0; i < got; i++) {
                if (irs[i].EventType != KEY_EVENT) continue;
                if (!irs[i].Event.KeyEvent.bKeyDown) continue;
                int vlen = key_to_vt(&irs[i].Event.KeyEvent, vt);
                if (vlen > 0) {
                    WriteFile(pipe, vt, (DWORD)vlen, &written, NULL);
                    did_work = TRUE;
                }
            }
        }

        /* Nothing moved this iteration — yield briefly to avoid busy-spinning. */
        if (!did_work) Sleep(5);
    }

restore:
    SetConsoleMode(con_in,  orig_in);
    SetConsoleMode(con_out, orig_out);

    DWORD exit_code = 0;
    if (sei.hProcess) {
        WaitForSingleObject(sei.hProcess, 3000);
        GetExitCodeProcess(sei.hProcess, &exit_code);
        CloseHandle(sei.hProcess);
    }
    CloseHandle(pipe);
    return (int)exit_code;
}


/* ================================================================
 * BROKER — elevated side
 * ================================================================ */

/*
 * run_broker() — elevated half of the ConPTY bridge.
 *
 * Called as: wsu.exe --broker \\.\pipe\wsu_<PID>_<tick>
 *
 * Steps:
 *   1. FreeConsole() — broker has no visible window and must not inherit the
 *      non-elevated parent's console (cross-elevation console access is blocked
 *      by Windows integrity levels; inheriting it would cause every Console API
 *      call to fail).
 *   2. Create the named pipe with a NULL DACL (see PIPE SECURITY in header).
 *   3. Block on ConnectNamedPipe until the client connects.
 *   4. Read the 4-byte COORD handshake.
 *   5. Create two anonymous pipe pairs for the ConPTY's stdin/stdout.
 *   6. Create the ConPTY; close our copies of the pipe ends it now owns.
 *   7. Build STARTUPINFOEXA + attribute list; spawn winix.exe.
 *   8. Poll loop: PTY output → named pipe; named pipe → PTY input.
 *   9. On winix exit: drain remaining PTY output, then
 *      FlushFileBuffers + DisconnectNamedPipe to signal EOF to client.
 */
static int run_broker(const char *pipe_name) {
    /*
     * FreeConsole() before anything else.
     *
     * The elevated broker is launched SW_HIDE, so there's no window, but it
     * may still be implicitly attached to the parent's console object (Windows
     * sometimes does this for processes in the same job).  Freeing it ensures
     * all Console API calls go to the ConPTY we're about to create, not the
     * non-elevated parent's console (which we can't access anyway).
     */
    FreeConsole();

    /*
     * NULL DACL: grants access to all callers regardless of integrity level.
     * This is required because the named pipe owner is HIGH integrity but the
     * connecting client is MEDIUM integrity.  The pipe carries only terminal
     * I/O — no credentials or sensitive data — so the broad access is acceptable.
     */
    SECURITY_DESCRIPTOR sd;
    SECURITY_ATTRIBUTES pipe_sa;
    InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
    SetSecurityDescriptorDacl(&sd, TRUE, NULL, FALSE);
    pipe_sa.nLength              = sizeof(pipe_sa);
    pipe_sa.lpSecurityDescriptor = &sd;
    pipe_sa.bInheritHandle       = FALSE;

    HANDLE pipe = CreateNamedPipeA(pipe_name,
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        1,           /* max instances — only one client per session */
        IO_BUF,      /* outbound buffer */
        IO_BUF,      /* inbound buffer */
        PIPE_TIMEOUT, &pipe_sa);
    if (pipe == INVALID_HANDLE_VALUE) return 1;

    /* Block until the non-elevated client connects. */
    if (!ConnectNamedPipe(pipe, NULL) &&
        GetLastError() != ERROR_PIPE_CONNECTED) {
        CloseHandle(pipe);
        return 1;
    }

    /* Receive console dimensions from client. */
    COORD size = {80, 24};
    DWORD n;
    ReadFile(pipe, &size, sizeof(COORD), &n, NULL);

    /*
     * Create two anonymous pipe pairs for ConPTY I/O.
     *
     *   We write input  → pty_in_w  → [ConPTY] → pty_in_r  (PTY reads this)
     *   PTY writes output → pty_out_w → [ConPTY] → pty_out_r (we read this)
     *
     * After CreatePseudoConsole, pty_in_r and pty_out_w are owned by the PTY.
     * We close our copies immediately (see HANDLE OWNERSHIP section in header).
     */
    HANDLE pty_in_r, pty_in_w, pty_out_r, pty_out_w;
    if (!CreatePipe(&pty_in_r, &pty_in_w, NULL, 0)) {
        CloseHandle(pipe);
        return 1;
    }
    if (!CreatePipe(&pty_out_r, &pty_out_w, NULL, 0)) {
        CloseHandle(pty_in_r); CloseHandle(pty_in_w);
        CloseHandle(pipe);
        return 1;
    }

    HPCON hPC = NULL;
    if (FAILED(g_CreatePseudoConsole(size, pty_in_r, pty_out_w, 0, &hPC))) {
        CloseHandle(pty_in_r);  CloseHandle(pty_in_w);
        CloseHandle(pty_out_r); CloseHandle(pty_out_w);
        CloseHandle(pipe);
        return 1;
    }
    CloseHandle(pty_in_r);   /* PTY owns this now */
    CloseHandle(pty_out_w);  /* PTY owns this now */

    /*
     * Build a PROC_THREAD_ATTRIBUTE_LIST with the ConPTY handle.
     *
     * InitializeProcThreadAttributeList is called twice: first with NULL to
     * get the required buffer size, then with the allocated buffer to
     * initialise it.  This two-call pattern is a Windows API convention.
     *
     * UpdateProcThreadAttribute with PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
     * tells CreateProcess to attach the new process to hPC rather than
     * inheriting any existing console.
     */
    SIZE_T attrlist_sz = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &attrlist_sz);
    LPPROC_THREAD_ATTRIBUTE_LIST attrlist = malloc(attrlist_sz);
    if (!attrlist) {
        g_ClosePseudoConsole(hPC);
        CloseHandle(pty_in_w); CloseHandle(pty_out_r); CloseHandle(pipe);
        return 1;
    }
    InitializeProcThreadAttributeList(attrlist, 1, 0, &attrlist_sz);
    UpdateProcThreadAttribute(attrlist, 0,
        PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, hPC, sizeof(hPC), NULL, NULL);

    char winix_path[MAX_PATH];
    find_winix(winix_path, sizeof(winix_path));

    /*
     * STARTUPINFOEXA: the 'EX' variant is required to pass lpAttributeList.
     * cb must be sizeof(STARTUPINFOEXA), not sizeof(STARTUPINFOA), and
     * EXTENDED_STARTUPINFO_PRESENT must be in dwCreationFlags, or Windows
     * ignores the attribute list entirely.
     *
     * bInheritHandles = FALSE: the child inherits the ConPTY via the attribute
     * list, not via handle inheritance.  Inheriting our anonymous pipe handles
     * would cause them to stay open in the child, preventing clean EOF signals.
     */
    STARTUPINFOEXA si = {0};
    si.StartupInfo.cb  = sizeof(STARTUPINFOEXA);
    si.lpAttributeList = attrlist;

    PROCESS_INFORMATION pi = {0};
    BOOL ok = CreateProcessA(
        NULL, winix_path,
        NULL, NULL,
        FALSE,                              /* bInheritHandles */
        EXTENDED_STARTUPINFO_PRESENT,       /* dwCreationFlags */
        NULL, NULL,
        &si.StartupInfo, &pi);

    DeleteProcThreadAttributeList(attrlist);
    free(attrlist);

    if (!ok) {
        g_ClosePseudoConsole(hPC);
        CloseHandle(pty_in_w); CloseHandle(pty_out_r); CloseHandle(pipe);
        return 1;
    }

    /*
     * I/O BRIDGE: single-threaded PeekNamedPipe poll loop.
     *
     * DIRECTION 1: PTY output (pty_out_r) → named pipe (pipe).
     *   winix writes VT sequences to its stdout; ConPTY emits them on
     *   pty_out_w → we read from pty_out_r → forward to client via pipe.
     *
     * DIRECTION 2: named pipe (pipe) → PTY input (pty_in_w).
     *   Client sends VT byte sequences from keyboard → we read from pipe →
     *   write to pty_in_w → ConPTY delivers to winix's stdin.
     *
     * We alternate directions rather than threading because both directions
     * share different handles (pty_out_r vs pty_in_w on the broker side;
     * pipe for both), and blocking ReadFile on one prevents the other from
     * making progress.  See "SINGLE-THREADED POLL LOOP" in the file header.
     *
     * EXIT DRAIN: when winix exits, ConPTY may still have buffered output
     * (e.g. the final prompt repaint, exit message).  We sleep 50 ms to let
     * the PTY flush, then drain pty_out_r one last time before closing.
     * 50 ms is empirical — long enough for a typical prompt repaint but short
     * enough not to feel sluggish.
     */
    {
        char buf[IO_BUF];
        DWORD avail, written;
        for (;;) {
            BOOL did_work = FALSE;

            avail = 0;
            PeekNamedPipe(pty_out_r, NULL, 0, NULL, &avail, NULL);
            while (avail > 0) {
                DWORD to_read = avail < IO_BUF ? avail : IO_BUF;
                if (!ReadFile(pty_out_r, buf, to_read, &n, NULL) || n == 0) goto io_done;
                if (!WriteFile(pipe, buf, n, &written, NULL)) goto io_done;
                did_work = TRUE;
                avail = 0;
                PeekNamedPipe(pty_out_r, NULL, 0, NULL, &avail, NULL);
            }

            avail = 0;
            PeekNamedPipe(pipe, NULL, 0, NULL, &avail, NULL);
            while (avail > 0) {
                DWORD to_read = avail < IO_BUF ? avail : IO_BUF;
                if (!ReadFile(pipe, buf, to_read, &n, NULL) || n == 0) goto io_done;
                if (!WriteFile(pty_in_w, buf, n, &written, NULL)) goto io_done;
                did_work = TRUE;
                avail = 0;
                PeekNamedPipe(pipe, NULL, 0, NULL, &avail, NULL);
            }

            if (WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0) {
                /* winix exited; drain remaining PTY output. */
                Sleep(50);
                avail = 0;
                PeekNamedPipe(pty_out_r, NULL, 0, NULL, &avail, NULL);
                while (avail > 0) {
                    DWORD to_read = avail < IO_BUF ? avail : IO_BUF;
                    if (!ReadFile(pty_out_r, buf, to_read, &n, NULL) || n == 0) break;
                    WriteFile(pipe, buf, n, &written, NULL);
                    avail = 0;
                    PeekNamedPipe(pty_out_r, NULL, 0, NULL, &avail, NULL);
                }
                break;
            }

            if (!did_work) Sleep(5);
        }
        io_done:;
    }

    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    g_ClosePseudoConsole(hPC);
    CloseHandle(pty_in_w);
    CloseHandle(pty_out_r);

    /*
     * FlushFileBuffers ensures any data still in the kernel's pipe buffer
     * is delivered to the client before we close.  DisconnectNamedPipe then
     * causes the client's next PeekNamedPipe to return ERROR_PIPE_NOT_CONNECTED,
     * which breaks the client's poll loop cleanly.
     */
    FlushFileBuffers(pipe);
    DisconnectNamedPipe(pipe);
    CloseHandle(pipe);

    return (int)exit_code;
}


/* ================================================================
 * main
 * ================================================================ */

static void usage(void) {
    puts("Usage: wsu");
    puts("");
    puts("Launch an interactive elevated Winix shell in the current terminal.");
    puts("A UAC prompt appears if the session is not already elevated.");
    puts("Type 'exit' to return to the non-elevated shell.");
    puts("");
    puts("Requires Windows 10 Build 17763 (1809) or later.");
    puts("For single elevated commands use wsudo instead.");
    puts("");
    puts("Options:");
    puts("  --status     Print elevation status and exit (0=elevated, 1=not)");
    puts("  --version    Print version and exit");
    puts("  --help       Print this help and exit");
}

int main(int argc, char **argv) {
    /*
     * --broker is an internal flag used when wsu re-launches itself elevated.
     * It is not intended for end users; no help text mentions it.
     */
    if (argc >= 2 && strcmp(argv[1], "--broker") == 0) {
        if (argc < 3) {
            fprintf(stderr, "wsu: --broker requires a pipe name\n");
            return 1;
        }
        if (!conpty_load()) {
            fprintf(stderr,
                "wsu: requires Windows 10 Build 17763 (1809) or later\n");
            return 1;
        }
        return run_broker(argv[2]);
    }

    if (argc >= 2 &&
        (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        usage();
        return 0;
    }
    if (argc >= 2 &&
        (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0)) {
        printf("wsu %s (Winix %s)\n", WSU_VERSION, WINIX_VERSION);
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "--status") == 0) {
        int e = is_elevated();
        puts(e ? "elevated" : "not elevated");
        return e ? 0 : 1;
    }

    if (!conpty_load()) {
        fprintf(stderr,
            "wsu: requires Windows 10 Build 17763 (1809) or later\n");
        return 1;
    }

    /*
     * Already-elevated fast path: no bridge needed.  Spawn winix directly
     * in this console with handle inheritance so it uses our stdin/stdout.
     * This happens when wsu is invoked from inside an already-elevated shell.
     */
    if (is_elevated()) {
        char winix_path[MAX_PATH];
        find_winix(winix_path, sizeof(winix_path));
        STARTUPINFOA si = {0};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi = {0};
        if (!CreateProcessA(NULL, winix_path, NULL, NULL,
                            TRUE,  /* inherit stdin/stdout/stderr */
                            0, NULL, NULL, &si, &pi)) {
            fprintf(stderr,
                "wsu: failed to launch winix (error %lu)\n", GetLastError());
            return 1;
        }
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD ec = 0;
        GetExitCodeProcess(pi.hProcess, &ec);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return (int)ec;
    }

    return run_client();
}
