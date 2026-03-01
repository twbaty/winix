# Winix Testing Guide

---

## Overview

Winix currently uses two levels of testing:

1. **CI smoke tests** — automated, run on every push (GitHub Actions)
2. **Manual functional tests** — run locally against built binaries

A formal test harness (shell-script or Python-based) is planned for v1.0.

---

## 1. CI Smoke Tests

Defined in `.github/workflows/ci.yml`. They run automatically on every push to `main` and every pull request.

**What they check:**
- All 45 binaries compiled and exist in `build/`
- `true.exe` exits 0
- `false.exe` exits non-zero
- `echo.exe "hello from winix ci"` produces the expected output
- `pwd.exe` runs without error

**Viewing results:** https://github.com/twbaty/winix/actions

If CI is red, don't merge. Fix the build first.

---

## 2. Local Manual Tests

Run these after any change before committing. Each test shows the command and expected result.

### Setup

Build first:
```cmd
cd build
mingw32-make -j4
cd ..
```

All commands below assume you're running from the repo root and prefixing with `build\`.

---

### File Utilities

#### ls
```cmd
build\ls.exe src\coreutils
```
Expected: lists `.c` files, no error

```cmd
build\ls.exe -la src\coreutils
```
Expected: long listing with permissions and sizes

```cmd
build\ls.exe nonexistent
```
Expected: `ls: cannot open 'nonexistent': No such file or directory`, exit 1

#### cp
```cmd
echo test > tmp_src.txt
build\cp.exe tmp_src.txt tmp_dst.txt
build\cat.exe tmp_dst.txt
del tmp_src.txt tmp_dst.txt
```
Expected: `test` printed

```cmd
build\cp.exe src\coreutils\cp.c tmp_cp_test.c
build\cp.exe tmp_cp_test.c tmp_cp_test.c
del tmp_cp_test.c
```
Expected: error about file already existing (use -f to overwrite), exit 1

```cmd
mkdir tmp_dir_src
echo a > tmp_dir_src\a.txt
build\cp.exe -r tmp_dir_src tmp_dir_dst
build\ls.exe tmp_dir_dst
rmdir /s /q tmp_dir_src tmp_dir_dst
```
Expected: `a.txt` listed in `tmp_dir_dst`

#### mv
```cmd
echo test > tmp_mv.txt
build\mv.exe tmp_mv.txt tmp_mv2.txt
build\cat.exe tmp_mv2.txt
del tmp_mv2.txt
```
Expected: `test` printed, original gone

#### rm
```cmd
echo test > tmp_rm.txt
build\rm.exe tmp_rm.txt
build\ls.exe tmp_rm.txt
```
Expected: ls errors (file gone), exit 1 from ls

#### mkdir / rmdir
```cmd
build\mkdir.exe tmp_test_dir
build\ls.exe tmp_test_dir
build\rmdir.exe tmp_test_dir
```
Expected: directory created then removed with no errors

---

### Text Utilities

#### cat
```cmd
build\cat.exe README.md
```
Expected: README content printed

```cmd
build\cat.exe -n README.md
```
Expected: line numbers prepended

#### head / tail
```cmd
build\head.exe -n 5 README.md
build\tail.exe -n 5 README.md
```
Expected: first and last 5 lines respectively

#### grep
```cmd
build\grep.exe "Winix" README.md
```
Expected: matching lines highlighted

```cmd
build\grep.exe -i "winix" README.md
```
Expected: same lines (case-insensitive)

```cmd
build\grep.exe -r "copy_file" src\coreutils
```
Expected: matches in `cp.c` and any other files containing `copy_file`

#### wc
```cmd
build\wc.exe README.md
build\wc.exe -l README.md
build\wc.exe -w README.md
```
Expected: line/word/byte counts

#### sort / uniq
```cmd
echo -e "banana\napple\nbanana\napple\ncherry" > tmp_sort.txt
build\sort.exe tmp_sort.txt
build\sort.exe -r tmp_sort.txt
build\sort.exe -u tmp_sort.txt
del tmp_sort.txt
```
Expected: sorted, reverse-sorted, and deduplicated output

---

### System Utilities

#### echo
```cmd
build\echo.exe hello world
build\echo.exe -n no newline
build\echo.exe -e "tab:\there"
```

#### pwd
```cmd
build\pwd.exe
```
Expected: current directory path

#### chmod
```cmd
echo test > tmp_chmod.txt
build\chmod.exe -v 444 tmp_chmod.txt
build\chmod.exe -v +w tmp_chmod.txt
del tmp_chmod.txt
```
Expected: verbose output showing read-only then writable

#### true / false
```cmd
build\true.exe
echo Exit: %errorlevel%
build\false.exe
echo Exit: %errorlevel%
```
Expected: `Exit: 0` then `Exit: 1`

---

### Shell Features

Start the shell:
```cmd
build\winix.exe
```

#### Prompt customization
```
set PS1=[\u@\h \W]$
```
Expected: prompt changes immediately to `[username@hostname winix]$`

#### History
```
ls
pwd
echo hello
history
```
Expected: history lists your commands with line numbers

#### Tab completion
Type `ls` then press Tab — should complete to `ls.exe` or show options.
Type `src/` then press Tab — should list directory contents.

#### Piping
```
ls src\coreutils | grep cp
```
Expected: `cp.c` line

#### Redirection
```
ls src\coreutils > tmp_out.txt
cat tmp_out.txt
rm tmp_out.txt
```
Expected: directory listing captured to file

#### Chaining
```
true && echo "true worked"
false || echo "false failed as expected"
false ; echo "always runs"
```
Expected: all three echo lines printed (second because false failed, third unconditionally)

#### Tilde expansion
```
ls ~/
```
Expected: lists your home directory

#### $? tracking
```
true
echo $?
false
echo $?
```
Expected: `0` then `1`

#### Aliases
```
alias ll=ls -la
ll src\coreutils
```
Expected: long listing of coreutils

---

## 3. Regression Checklist

Before tagging a release, run through this list manually:

- [ ] Full build from clean (`rm -rf build && mkdir build && cd build && cmake ... && mingw32-make`)
- [ ] All binaries present in `build/`
- [ ] `winix.exe` starts, shows prompt, accepts input, exits cleanly
- [ ] `ls`, `cat`, `cp`, `mv`, `rm`, `mkdir`, `rmdir` — basic operations work
- [ ] `grep -r` works across the `src/` directory
- [ ] Piping: `ls | grep` produces correct output
- [ ] Redirection: `ls > out.txt` captures output, `cat < out.txt` reads it
- [ ] Chaining: `&&`, `||`, `;` all behave correctly
- [ ] `$?` reflects last exit code
- [ ] Aliases survive shell restart (persisted to `.winixrc`)
- [ ] PS1 change persists across restart
- [ ] CI is green on `main`

---

## 4. Future Testing Plans

For v1.0, a shell-script or Python test harness will be added to `tests/` that:
- Runs each coreutil with known inputs
- Compares stdout/stderr/exit code against expected values
- Integrates into CI as a dedicated `test` job

If you want to contribute tests now, add test scripts to `tests/` as `test_<utility>.sh` or `test_<utility>.py`. They will be wired into CI when the harness is ready.
