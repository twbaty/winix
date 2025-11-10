#include "line_editor.hpp"
#include <windows.h>
#include <iostream>
#include <filesystem>

static void putch(char c) {
    DWORD written;
    WriteConsoleA(GetStdHandle(STD_OUTPUT_HANDLE), &c, 1, &written, NULL);
}

static void putstr(const std::string& s) {
    DWORD written;
    WriteConsoleA(GetStdHandle(STD_OUTPUT_HANDLE), s.c_str(),
                  (DWORD)s.size(), &written, NULL);
}

std::string read_line_with_completion(const std::string& prompt,
                                      CompletionFunc complete)
{
    putstr(prompt);
    std::string buffer;

    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;
    GetConsoleMode(hIn, &mode);
    SetConsoleMode(hIn, ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);

    while (true) {
        INPUT_RECORD rec;
        DWORD count = 0;
        ReadConsoleInput(hIn, &rec, 1, &count);

        if (rec.EventType == KEY_EVENT && rec.Event.KeyEvent.bKeyDown) {
            auto key = rec.Event.KeyEvent;

            // ENTER
            if (key.wVirtualKeyCode == VK_RETURN) {
                putch('\n');
                return buffer;
            }

            // BACKSPACE
            if (key.wVirtualKeyCode == VK_BACK) {
                if (!buffer.empty()) {
                    buffer.pop_back();
                    putstr("\b \b");
                }
                continue;
            }

            // TAB → perform completion
            if (key.wVirtualKeyCode == VK_TAB) {
                auto list = complete(buffer);

                if (!list.empty()) {
                    // If only 1 match, commit it fully
                    if (list.size() == 1) {
                        // Clear current buffer from screen
                        while (!buffer.empty()) { putstr("\b \b"); buffer.pop_back(); }

                        buffer = list[0];
                        putstr(buffer);
                    }
                    else {
                        // Print a newline then all matches
                        putstr("\n");
                        for (auto& s : list)
                            putstr(s + "\n");
                        // Reprint prompt + buffer
                        putstr(prompt + buffer);
                    }
                }
                continue;
            }

            // REGULAR CHAR
            char c = key.uChar.AsciiChar;
            if (c >= 32 && c <= 126) {
                buffer.push_back(c);
                putch(c);
            }
        }
    }
}
