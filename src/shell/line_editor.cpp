#include "line_editor.hpp"
#include <windows.h>
#include <iostream>
#include <algorithm>
#include <cctype>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Returns the visible (non-ANSI-escape) character count of a string.
static size_t visible_len(const std::string& s) {
    size_t n = 0;
    bool in_esc = false;
    for (unsigned char c : s) {
        if (c == '\x1b') { in_esc = true; continue; }
        if (in_esc)      { if (std::isalpha(c)) in_esc = false; continue; }
        ++n;
    }
    return n;
}

// Longest common prefix of a list of strings.
static std::string common_prefix(const std::vector<std::string>& v) {
    if (v.empty()) return {};
    std::string pfx = v[0];
    for (size_t i = 1; i < v.size(); ++i) {
        size_t j = 0;
        while (j < pfx.size() && j < v[i].size() && pfx[j] == v[i][j]) ++j;
        pfx = pfx.substr(0, j);
        if (pfx.empty()) break;
    }
    return pfx;
}

// ---------------------------------------------------------------------------
// LineEditor
// ---------------------------------------------------------------------------

LineEditor::LineEditor(CompletionFunc completer, const std::vector<std::string>* history)
    : completer_(std::move(completer)), history_(history) {}

std::vector<std::string> LineEditor::suggest(const std::string& partial) const {
    if (!completer_) return {};
    return completer_(partial);
}

std::optional<std::string> LineEditor::read_line(const std::string& prompt_str) {
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);

    // If stdin is not a real console (e.g. redirected pipe), fall back to
    // simple line input so winix can be driven by piped scripts/tests.
    DWORD orig_mode = 0;
    if (!GetConsoleMode(hIn, &orig_mode)) {
        std::cout << prompt_str;
        std::cout.flush();
        std::string line;
        if (!std::getline(std::cin, line)) return std::nullopt;
        return line;
    }

    // Save console mode and switch to raw input.
    SetConsoleMode(hIn, orig_mode & ~(ENABLE_LINE_INPUT |
                                      ENABLE_ECHO_INPUT |
                                      ENABLE_PROCESSED_INPUT));

    auto restore = [&]() { SetConsoleMode(hIn, orig_mode); };

    // Print prompt.
    std::cout << prompt_str;
    std::cout.flush();

    std::string buf;
    size_t cursor = 0;

    // History navigation state.
    size_t hist_idx   = history_ ? history_->size() : 0;
    std::string saved_input;   // preserves typed text when scrolling history

    // Tab completion state.
    std::vector<std::string> tab_matches;
    bool tab_active = false;

    // Redraws the current line in-place.
    auto redraw = [&]() {
        std::cout << '\r' << prompt_str << buf << "\x1b[K";
        size_t back = buf.size() - cursor;
        if (back > 0)
            std::cout << "\x1b[" << back << "D";
        std::cout.flush();
    };

    while (true) {
        INPUT_RECORD ir{};
        DWORD nread = 0;
        if (!ReadConsoleInputA(hIn, &ir, 1, &nread) || nread == 0)
            continue;

        // We only care about key-down events.
        if (ir.EventType != KEY_EVENT || !ir.Event.KeyEvent.bKeyDown)
            continue;

        WORD  vk   = ir.Event.KeyEvent.wVirtualKeyCode;
        char  ch   = ir.Event.KeyEvent.uChar.AsciiChar;
        DWORD ctrl = ir.Event.KeyEvent.dwControlKeyState;
        bool  ctrl_held = (ctrl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;

        // ---- Ctrl+C: cancel line, show new prompt ----
        if (ctrl_held && vk == 'C') {
            std::cout << '\n';
            restore();
            return std::string{};   // empty → shell loops back to prompt
        }

        // ---- Ctrl+D: EOF only on an empty line ----
        if (ctrl_held && vk == 'D') {
            if (buf.empty()) {
                std::cout << '\n';
                restore();
                return std::nullopt;
            }
            continue;
        }

        // ---- Ctrl+L: clear screen ----
        if (ctrl_held && vk == 'L') {
            std::cout << "\x1b[2J\x1b[H" << prompt_str << buf;
            size_t back = buf.size() - cursor;
            if (back > 0) std::cout << "\x1b[" << back << "D";
            std::cout.flush();
            tab_active = false;
            continue;
        }

        // ---- Enter ----
        if (vk == VK_RETURN) {
            std::cout << '\n';
            restore();
            return buf;
        }

        // ---- Backspace ----
        if (vk == VK_BACK) {
            if (cursor > 0) {
                buf.erase(cursor - 1, 1);
                --cursor;
                redraw();
            }
            tab_active = false;
            continue;
        }

        // ---- Delete (forward) ----
        if (vk == VK_DELETE) {
            if (cursor < buf.size()) {
                buf.erase(cursor, 1);
                redraw();
            }
            tab_active = false;
            continue;
        }

        // ---- Left arrow ----
        if (vk == VK_LEFT) {
            if (cursor > 0) {
                --cursor;
                std::cout << "\x1b[1D";
                std::cout.flush();
            }
            tab_active = false;
            continue;
        }

        // ---- Right arrow ----
        if (vk == VK_RIGHT) {
            if (cursor < buf.size()) {
                ++cursor;
                std::cout << "\x1b[1C";
                std::cout.flush();
            }
            tab_active = false;
            continue;
        }

        // ---- Home ----
        if (vk == VK_HOME) {
            if (cursor > 0) {
                std::cout << "\x1b[" << cursor << "D";
                std::cout.flush();
                cursor = 0;
            }
            tab_active = false;
            continue;
        }

        // ---- End ----
        if (vk == VK_END) {
            size_t back = buf.size() - cursor;
            if (back > 0) {
                std::cout << "\x1b[" << back << "C";
                std::cout.flush();
                cursor = buf.size();
            }
            tab_active = false;
            continue;
        }

        // ---- Up arrow: older history ----
        if (vk == VK_UP) {
            if (!history_ || history_->empty()) continue;
            if (hist_idx == history_->size())
                saved_input = buf;          // save current typing before scrolling
            if (hist_idx > 0) {
                --hist_idx;
                buf    = (*history_)[hist_idx];
                cursor = buf.size();
                redraw();
            }
            tab_active = false;
            continue;
        }

        // ---- Down arrow: newer history ----
        if (vk == VK_DOWN) {
            if (!history_) continue;
            if (hist_idx < history_->size()) {
                ++hist_idx;
                buf    = (hist_idx == history_->size())
                             ? saved_input
                             : (*history_)[hist_idx];
                cursor = buf.size();
                redraw();
            }
            tab_active = false;
            continue;
        }

        // ---- Tab: completion ----
        if (vk == VK_TAB) {
            if (!completer_) continue;

            // Find the start of the word the cursor is currently inside.
            size_t word_start = cursor;
            while (word_start > 0 && buf[word_start - 1] != ' ') --word_start;
            std::string current_word = buf.substr(word_start, cursor - word_start);

            // Build match list once per Tab sequence.
            if (!tab_active) {
                tab_matches = completer_(current_word);
                tab_active  = !tab_matches.empty();
            }
            if (!tab_active) continue;

            if (tab_matches.size() == 1) {
                // Single match: replace the current word with it.
                buf    = buf.substr(0, word_start) + tab_matches[0] + buf.substr(cursor);
                cursor = word_start + tab_matches[0].size();
                redraw();
                tab_active = false;
            } else {
                std::string pfx = common_prefix(tab_matches);
                if (pfx.size() > current_word.size()) {
                    // Extend to common prefix.
                    buf    = buf.substr(0, word_start) + pfx + buf.substr(cursor);
                    cursor = word_start + pfx.size();
                    redraw();
                } else {
                    // Already at common prefix — show all candidates.
                    std::cout << '\n';
                    for (auto& m : tab_matches)
                        std::cout << m << "  ";
                    std::cout << '\n' << prompt_str << buf;
                    size_t back = buf.size() - cursor;
                    if (back > 0) std::cout << "\x1b[" << back << "D";
                    std::cout.flush();
                }
            }
            continue;
        }

        // ---- Printable character ----
        if (ch >= 0x20 && (unsigned char)ch != 0x7F) {
            buf.insert(cursor, 1, ch);
            ++cursor;
            redraw();
            tab_active = false;
            continue;
        }
    }
}
