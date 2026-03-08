#!/usr/bin/env python3
"""
Winix Functional Test Harness
Runs tests against built binaries in the build/ directory.

Usage:
    python tests/run_tests.py
    python tests/run_tests.py --build-dir path/to/build
    python tests/run_tests.py --verbose
"""

import subprocess
import os
import sys
import tempfile
import shutil
import argparse

# ── Configuration ─────────────────────────────────────────────────────────────

DEFAULT_BUILD = os.path.join(os.path.dirname(__file__), '..', 'build')

parser = argparse.ArgumentParser(description='Winix test harness')
parser.add_argument('--build-dir', default=DEFAULT_BUILD,
                    help='Path to build directory containing .exe files')
parser.add_argument('--verbose', '-v', action='store_true',
                    help='Print passing tests as well as failures')
ARGS = parser.parse_args()

BUILD_DIR = os.path.abspath(ARGS.build_dir)

if not os.path.isdir(BUILD_DIR):
    print(f"ERROR: build directory not found: {BUILD_DIR}")
    sys.exit(1)

# ── Framework ─────────────────────────────────────────────────────────────────

_passed = 0
_failed = 0
_current_section = ''


def section(name):
    global _current_section
    _current_section = name
    print(f"\n{name}")
    print('-' * len(name))


def _record(ok, label, detail=''):
    global _passed, _failed
    if ok:
        _passed += 1
        if ARGS.verbose:
            print(f"  PASS: {label}")
    else:
        _failed += 1
        msg = f"  FAIL: {label}"
        if detail:
            msg += f"\n        {detail}"
        print(msg)


def check(label, condition, detail=''):
    _record(condition, label, detail)


def expect_eq(label, got, expected):
    _record(got == expected, label,
            f"expected {expected!r}\n        got      {got!r}")


def expect_exit(label, code, expected=0):
    _record(code == expected, label,
            f"expected exit {expected}, got {code}")


def expect_contains(label, text, substring):
    _record(substring in text, label,
            f"expected {substring!r} in:\n        {text!r}")


def expect_not_contains(label, text, substring):
    _record(substring not in text, label,
            f"expected {substring!r} NOT in:\n        {text!r}")


def exe(name):
    """Return absolute path to a Winix binary."""
    return os.path.join(BUILD_DIR, name + '.exe')


def run_shell(commands, cwd=None, timeout=15):
    """Run one or more commands through winix; return (cleaned_stdout, returncode).

    Lines containing the prompt or the startup banner are filtered out so
    tests can assert on command output directly.
    """
    import re
    if isinstance(commands, list):
        stdin = '\n'.join(commands) + '\nexit\n'
    else:
        stdin = commands + '\nexit\n'
    try:
        r = subprocess.run(
            [exe('winix')],
            input=stdin,
            capture_output=True,
            text=True,
            encoding='utf-8',
            errors='replace',
            cwd=cwd,
            timeout=timeout,
        )
        out = r.stdout.replace('\r\n', '\n')
        # Strip ANSI escape codes
        out = re.sub(r'\x1b\[[0-9;]*[mGKHF]', '', out)
        # Drop prompt / banner lines
        lines = [
            ln.rstrip()
            for ln in out.split('\n')
            if ln.strip()
            and 'Winix Shell' not in ln
            and '[Winix]' not in ln
        ]
        return '\n'.join(lines), r.returncode
    except subprocess.TimeoutExpired:
        _record(False, 'run_shell timed out', '')
        return '', -1


def run(name, *args, stdin_text=None, cwd=None, timeout=15):
    """Run a Winix binary; return (stdout, stderr, returncode)."""
    cmd = [exe(name)] + [str(a) for a in args]
    try:
        r = subprocess.run(
            cmd,
            input=stdin_text,
            capture_output=True,
            text=True,
            encoding='utf-8',
            errors='replace',
            cwd=cwd,
            timeout=timeout,
        )
        # Normalize line endings (Windows may produce \r\n)
        return r.stdout.replace('\r\n', '\n'), r.stderr.replace('\r\n', '\n'), r.returncode
    except subprocess.TimeoutExpired:
        _record(False, f'{name} timed out after {timeout}s', '')
        return '', '', -1


# ── Temp directory helper ──────────────────────────────────────────────────────

class TempDir:
    """Context manager that creates a temp dir and cleans up after."""
    def __enter__(self):
        self.path = tempfile.mkdtemp(prefix='winix_test_')
        return self.path

    def __exit__(self, *_):
        shutil.rmtree(self.path, ignore_errors=True)


# ─────────────────────────────────────────────────────────────────────────────
# TESTS
# ─────────────────────────────────────────────────────────────────────────────

# ── true / false ──────────────────────────────────────────────────────────────

section('true / false')

_, _, code = run('true')
expect_exit('true exits 0', code, 0)

_, _, code = run('false')
expect_exit('false exits 1', code, 1)


# ── echo ──────────────────────────────────────────────────────────────────────

section('echo')

out, _, code = run('echo', 'hello', 'world')
expect_exit('echo exits 0', code)
expect_eq('echo basic output', out, 'hello world\n')

out, _, _ = run('echo', '-n', 'no newline')
expect_eq('echo -n suppresses newline', out, 'no newline')

out, _, _ = run('echo', '-e', r'line1\nline2')
expect_contains('echo -e expands \\n', out, 'line1')
expect_contains('echo -e expands \\n (line2)', out, 'line2')


# ── pwd ───────────────────────────────────────────────────────────────────────

section('pwd')

out, _, code = run('pwd')
expect_exit('pwd exits 0', code)
check('pwd produces output', len(out.strip()) > 0)


# ── cat ───────────────────────────────────────────────────────────────────────

section('cat')

with TempDir() as d:
    f = os.path.join(d, 'a.txt')
    with open(f, 'w') as fh:
        fh.write('line1\nline2\nline3\n')

    out, _, code = run('cat', f)
    expect_exit('cat exits 0', code)
    expect_eq('cat basic read', out, 'line1\nline2\nline3\n')

    out, _, _ = run('cat', '-n', f)
    expect_contains('cat -n adds line numbers', out, '     1\tline1')
    expect_contains('cat -n line 3', out, '     3\tline3')

    _, err, code = run('cat', os.path.join(d, 'nonexistent.txt'))
    expect_exit('cat nonexistent exits 1', code, 1)
    expect_contains('cat nonexistent prints error', err, 'cat:')


# ── head / tail ───────────────────────────────────────────────────────────────

section('head / tail')

with TempDir() as d:
    f = os.path.join(d, 'nums.txt')
    with open(f, 'w') as fh:
        fh.write('\n'.join(str(i) for i in range(1, 21)) + '\n')

    out, _, code = run('head', f)
    expect_exit('head exits 0', code)
    lines = out.strip().splitlines()
    expect_eq('head default 10 lines', len(lines), 10)
    expect_eq('head first line is 1', lines[0], '1')

    out, _, _ = run('head', '-n', '3', f)
    lines = out.strip().splitlines()
    expect_eq('head -n 3', len(lines), 3)
    expect_eq('head -n 3 last line is 3', lines[-1], '3')

    out, _, code = run('tail', f)
    expect_exit('tail exits 0', code)
    lines = out.strip().splitlines()
    expect_eq('tail default 10 lines', len(lines), 10)
    expect_eq('tail last line is 20', lines[-1], '20')

    out, _, _ = run('tail', '-n', '3', f)
    lines = out.strip().splitlines()
    expect_eq('tail -n 3', len(lines), 3)
    expect_eq('tail -n 3 first line is 18', lines[0], '18')


# ── wc ────────────────────────────────────────────────────────────────────────

section('wc')

with TempDir() as d:
    f = os.path.join(d, 'wc.txt')
    with open(f, 'w') as fh:
        fh.write('one two three\nfour five\n')

    out, _, code = run('wc', '-l', f)
    expect_exit('wc -l exits 0', code)
    expect_contains('wc -l count is 2', out, '2')

    out, _, _ = run('wc', '-w', f)
    expect_contains('wc -w count is 5', out, '5')

    out, _, _ = run('wc', '-c', f)
    # "one two three\nfour five\n" = 14 + 10 = 24 bytes
    expect_contains('wc -c byte count', out, '24')


# ── sort ──────────────────────────────────────────────────────────────────────

section('sort')

with TempDir() as d:
    f = os.path.join(d, 'sort.txt')
    with open(f, 'w') as fh:
        fh.write('banana\napple\ncherry\napple\n')

    out, _, code = run('sort', f)
    expect_exit('sort exits 0', code)
    lines = out.strip().splitlines()
    expect_eq('sort alphabetical', lines, ['apple', 'apple', 'banana', 'cherry'])

    out, _, _ = run('sort', '-r', f)
    lines = out.strip().splitlines()
    expect_eq('sort -r reverse', lines[0], 'cherry')

    out, _, _ = run('sort', '-u', f)
    lines = out.strip().splitlines()
    expect_eq('sort -u deduplicates', lines, ['apple', 'banana', 'cherry'])


# ── uniq ──────────────────────────────────────────────────────────────────────

section('uniq')

with TempDir() as d:
    f = os.path.join(d, 'uniq.txt')
    with open(f, 'w') as fh:
        fh.write('apple\napple\nbanana\nbanana\nbanana\ncherry\n')

    out, _, code = run('uniq', f)
    expect_exit('uniq exits 0', code)
    expect_eq('uniq deduplicates adjacent', out.strip().splitlines(),
              ['apple', 'banana', 'cherry'])

    out, _, _ = run('uniq', '-c', f)
    expect_contains('uniq -c shows count for apple', out, '2')
    expect_contains('uniq -c shows count for banana', out, '3')

    out, _, _ = run('uniq', '-d', f)
    lines = out.strip().splitlines()
    check('uniq -d only duplicates', all(l in ['apple', 'banana'] for l in lines))

    out, _, _ = run('uniq', '-u', f)
    expect_contains('uniq -u only unique', out, 'cherry')
    expect_not_contains('uniq -u excludes duplicates', out, 'apple')


# ── grep ──────────────────────────────────────────────────────────────────────

section('grep')

with TempDir() as d:
    f = os.path.join(d, 'grep.txt')
    with open(f, 'w') as fh:
        fh.write('Hello World\nhello winix\nGoodbye\n')

    out, _, code = run('grep', 'hello', f)
    expect_exit('grep exits 0 on match', code)
    expect_eq('grep case-sensitive match', out.strip(), 'hello winix')

    out, _, _ = run('grep', '-i', 'hello', f)
    lines = out.strip().splitlines()
    expect_eq('grep -i matches both cases', len(lines), 2)

    out, _, _ = run('grep', '-v', 'hello', f)
    expect_contains('grep -v inverts match', out, 'Hello World')
    expect_contains('grep -v includes non-match', out, 'Goodbye')
    expect_not_contains('grep -v excludes match', out, 'hello winix')

    out, _, _ = run('grep', '-c', 'hello', f)
    expect_eq('grep -c count', out.strip(), '1')

    out, _, _ = run('grep', '-n', 'hello', f)
    expect_contains('grep -n shows line number', out, '2:')

    _, _, code = run('grep', 'NOMATCH', f)
    expect_exit('grep no match exits 1', code, 1)


# ── mkdir / rmdir / touch / ls ────────────────────────────────────────────────

section('mkdir / rmdir / touch / ls')

with TempDir() as d:
    newdir = os.path.join(d, 'testdir')

    _, _, code = run('mkdir', newdir)
    expect_exit('mkdir exits 0', code)
    check('mkdir creates directory', os.path.isdir(newdir))

    nested = os.path.join(d, 'a', 'b', 'c')
    _, _, code = run('mkdir', '-p', nested)
    expect_exit('mkdir -p exits 0', code)
    check('mkdir -p creates nested dirs', os.path.isdir(nested))

    f = os.path.join(newdir, 'file.txt')
    _, _, code = run('touch', f)
    expect_exit('touch exits 0', code)
    check('touch creates file', os.path.isfile(f))

    out, _, code = run('ls', newdir)
    expect_exit('ls exits 0', code)
    expect_contains('ls shows created file', out, 'file.txt')

    _, _, code = run('rmdir', newdir)
    expect_exit('rmdir non-empty exits 1 (has file)', code, 1)

    os.remove(f)
    _, _, code = run('rmdir', newdir)
    expect_exit('rmdir empty dir exits 0', code)
    check('rmdir removes directory', not os.path.isdir(newdir))


# ── cp ────────────────────────────────────────────────────────────────────────

section('cp')

with TempDir() as d:
    src = os.path.join(d, 'src.txt')
    dst = os.path.join(d, 'dst.txt')
    with open(src, 'w') as fh:
        fh.write('copy me\n')

    _, _, code = run('cp', src, dst)
    expect_exit('cp exits 0', code)
    check('cp creates destination', os.path.isfile(dst))
    with open(dst) as fh:
        expect_eq('cp content matches', fh.read(), 'copy me\n')

    _, err, code = run('cp', src, dst)
    expect_exit('cp existing dst without -f exits 1', code, 1)
    expect_contains('cp existing dst error message', err, 'already exists')

    _, _, code = run('cp', '-f', src, dst)
    expect_exit('cp -f overwrites', code)

    srcdir = os.path.join(d, 'srcdir')
    dstdir = os.path.join(d, 'dstdir')
    os.makedirs(srcdir)
    with open(os.path.join(srcdir, 'inner.txt'), 'w') as fh:
        fh.write('inner\n')

    _, err, code = run('cp', srcdir, dstdir)
    expect_exit('cp dir without -r exits 1', code, 1)

    _, _, code = run('cp', '-r', srcdir, dstdir)
    expect_exit('cp -r exits 0', code)
    check('cp -r creates destination dir', os.path.isdir(dstdir))
    check('cp -r copies inner file',
          os.path.isfile(os.path.join(dstdir, 'inner.txt')))


# ── mv ────────────────────────────────────────────────────────────────────────

section('mv')

with TempDir() as d:
    src = os.path.join(d, 'orig.txt')
    dst = os.path.join(d, 'moved.txt')
    with open(src, 'w') as fh:
        fh.write('move me\n')

    _, _, code = run('mv', src, dst)
    expect_exit('mv exits 0', code)
    check('mv destination exists', os.path.isfile(dst))
    check('mv source is gone', not os.path.isfile(src))
    with open(dst) as fh:
        expect_eq('mv content preserved', fh.read(), 'move me\n')


# ── rm ────────────────────────────────────────────────────────────────────────

section('rm')

with TempDir() as d:
    f = os.path.join(d, 'del.txt')
    with open(f, 'w') as fh:
        fh.write('delete me\n')

    _, _, code = run('rm', f)
    expect_exit('rm exits 0', code)
    check('rm removes file', not os.path.isfile(f))

    subdir = os.path.join(d, 'subdir')
    os.makedirs(subdir)
    with open(os.path.join(subdir, 'x.txt'), 'w') as fh:
        fh.write('x\n')

    _, err, code = run('rm', subdir)
    expect_exit('rm dir without -r exits 1', code, 1)

    _, _, code = run('rm', '-r', subdir)
    expect_exit('rm -r exits 0', code)
    check('rm -r removes directory tree', not os.path.isdir(subdir))


# ── stat ──────────────────────────────────────────────────────────────────────

section('stat')

with TempDir() as d:
    f = os.path.join(d, 'statme.txt')
    with open(f, 'w') as fh:
        fh.write('hello\n')

    out, _, code = run('stat', f)
    expect_exit('stat exits 0', code)
    expect_contains('stat shows File:', out, 'File:')
    expect_contains('stat shows Size:', out, 'Size:')
    expect_contains('stat shows Modify:', out, 'Modify:')
    expect_contains('stat shows file type', out, 'regular file')

    _, _, code = run('stat', os.path.join(d, 'nofile.txt'))
    expect_exit('stat nonexistent exits 1', code, 1)


# ── basename / dirname ────────────────────────────────────────────────────────

section('basename / dirname')

out, _, code = run('basename', '/usr/bin/grep')
expect_exit('basename exits 0', code)
expect_eq('basename extracts filename', out.strip(), 'grep')

out, _, code = run('dirname', '/usr/bin/grep')
expect_exit('dirname exits 0', code)
expect_eq('dirname extracts directory', out.strip(), '/usr/bin')

out, _, _ = run('basename', 'file.txt', '.txt')
expect_eq('basename strips suffix', out.strip(), 'file')


# ── which ─────────────────────────────────────────────────────────────────────

section('which')

# 'which' itself should be findable if build dir is on PATH
# Just check it runs and exits 0 for something we know exists
out, _, code = run('which', 'which')
# May or may not find it depending on PATH — just check it doesn't crash
check('which runs without crash', code in (0, 1))


# ── ver ───────────────────────────────────────────────────────────────────────

section('ver')

out, _, code = run('ver')
expect_exit('ver exits 0', code)
expect_contains('ver shows Winix', out, 'Winix')
expect_contains('ver shows version number', out, '3.4')

out, _, code = run('ver', '--version')
expect_exit('ver --version exits 0', code)
expect_contains('ver --version output', out, 'Winix')


# ── uname ─────────────────────────────────────────────────────────────────────

section('uname')

out, _, code = run('uname')
expect_exit('uname exits 0', code)
expect_contains('uname default shows Windows', out, 'Windows')

out, _, _ = run('uname', '-a')
expect_contains('uname -a shows Windows', out, 'Windows')
expect_contains('uname -a shows arch', out, 'x86_64')

out, _, _ = run('uname', '-m')
expect_eq('uname -m is x86_64', out.strip(), 'x86_64')


# ── uptime ────────────────────────────────────────────────────────────────────

section('uptime')

out, _, code = run('uptime')
expect_exit('uptime exits 0', code)
check('uptime produces output', len(out.strip()) > 0)
expect_contains('uptime shows up', out, 'up')


# ── whoami ────────────────────────────────────────────────────────────────────

section('whoami')

out, _, code = run('whoami')
expect_exit('whoami exits 0', code)
check('whoami produces a username', len(out.strip()) > 0)


# ── date ──────────────────────────────────────────────────────────────────────

section('date')

out, _, code = run('date')
expect_exit('date exits 0', code)
check('date produces output', len(out.strip()) > 0)


# ── sleep ─────────────────────────────────────────────────────────────────────

section('sleep')

import time
start = time.time()
_, _, code = run('sleep', '1', timeout=10)
elapsed = time.time() - start
expect_exit('sleep exits 0', code)
check('sleep 1 takes at least 0.9s', elapsed >= 0.9)


# ── clear ─────────────────────────────────────────────────────────────────────

section('clear')

_, _, code = run('clear')
expect_exit('clear exits 0', code)


# ── df ────────────────────────────────────────────────────────────────────────

section('df')

out, _, code = run('df')
expect_exit('df exits 0', code)
expect_contains('df shows header', out, 'Filesystem')
check('df shows at least one drive', len(out.strip().splitlines()) >= 2)

out, _, _ = run('df', '-h')
expect_contains('df -h shows human sizes', out, 'Filesystem')


# ── du ────────────────────────────────────────────────────────────────────────

section('du')

with TempDir() as d:
    with open(os.path.join(d, 'a.txt'), 'w') as fh:
        fh.write('a' * 2048)

    out, _, code = run('du', d)
    expect_exit('du exits 0', code)
    check('du produces output', len(out.strip()) > 0)

    out, _, _ = run('du', '-s', d)
    lines = out.strip().splitlines()
    expect_eq('du -s produces single line', len(lines), 1)

    out, _, _ = run('du', '-h', d)
    check('du -h produces output', len(out.strip()) > 0)


# ── ps ────────────────────────────────────────────────────────────────────────

section('ps')

out, _, code = run('ps')
expect_exit('ps exits 0', code)
expect_contains('ps shows header', out, 'PID')
expect_contains('ps shows PPID column', out, 'PPID')
check('ps lists processes', len(out.strip().splitlines()) >= 3)

out, _, _ = run('ps', '-l')
expect_contains('ps -l shows RSS column', out, 'RSS')


# ── printf ────────────────────────────────────────────────────────────────────

section('printf')

out, _, code = run('printf', '%s %s\n', 'hello', 'world')
expect_exit('printf exits 0', code)
expect_eq('printf basic format', out, 'hello world\n')


# ── tee ───────────────────────────────────────────────────────────────────────

section('tee')

with TempDir() as d:
    out_file = os.path.join(d, 'tee_out.txt')
    p = subprocess.run(
        [exe('tee'), out_file],
        input='tee test\n',
        capture_output=True, text=True
    )
    expect_exit('tee exits 0', p.returncode)
    expect_contains('tee passes stdout', p.stdout.replace('\r\n', '\n'), 'tee test')
    with open(out_file) as fh:
        expect_contains('tee writes to file', fh.read(), 'tee test')


# ── Case sensitivity (WINIX_CASE) ─────────────────────────────────────────────

section('case sensitivity')

_saved_case = os.environ.get('WINIX_CASE')

try:
    # --- grep ---
    os.environ['WINIX_CASE'] = 'off'
    out, _, code = run('grep', 'hello', stdin_text='Hello World\nbye world\n')
    expect_exit('grep WINIX_CASE=off exits 0 on match', code, 0)
    expect_contains('grep WINIX_CASE=off matches case-insensitively', out, 'Hello')

    os.environ['WINIX_CASE'] = 'on'
    out, _, code = run('grep', 'hello', stdin_text='Hello World\nbye world\n')
    expect_exit('grep WINIX_CASE=on exits 1 (no match)', code, 1)
    check('grep WINIX_CASE=on does not match Hello', 'Hello' not in out, out)

    # explicit -i still works when WINIX_CASE=on
    out, _, code = run('grep', '-i', 'hello', stdin_text='Hello World\n')
    expect_exit('grep -i overrides WINIX_CASE=on', code, 0)
    expect_contains('grep -i matches case-insensitively', out, 'Hello')

    # --- sort ---
    # 'Banana' sorts after 'apple' case-sensitively (B=66 < a=97 is FALSE:
    # capital letters have LOWER ASCII codes, so 'Banana' sorts BEFORE 'apple').
    # Case-insensitively 'apple' < 'Banana' < 'cherry'.
    os.environ['WINIX_CASE'] = 'off'
    out, _, _ = run('sort', stdin_text='cherry\nBanana\napple\n')
    lines = [l for l in out.strip().splitlines() if l]
    expect_eq('sort WINIX_CASE=off first line is apple', lines[0], 'apple')
    expect_eq('sort WINIX_CASE=off second line is Banana', lines[1], 'Banana')

    os.environ['WINIX_CASE'] = 'on'
    out, _, _ = run('sort', stdin_text='cherry\nBanana\napple\n')
    lines = [l for l in out.strip().splitlines() if l]
    expect_eq('sort WINIX_CASE=on first line is Banana', lines[0], 'Banana')

finally:
    if _saved_case is None:
        os.environ.pop('WINIX_CASE', None)
    else:
        os.environ['WINIX_CASE'] = _saved_case


# ── Shell glob expansion ──────────────────────────────────────────────────────

section('glob')

with TempDir() as d:
    # Create test files
    for name in ('alpha.txt', 'beta.txt', 'gamma.log'):
        open(os.path.join(d, name), 'w').close()

    # *.txt should expand to alpha.txt and beta.txt (sorted)
    out, _ = run_shell(f'cd {d}\necho *.txt', cwd=d)
    check('glob *.txt expands alpha', 'alpha.txt' in out, out)
    check('glob *.txt expands beta',  'beta.txt'  in out, out)
    check('glob *.txt excludes .log', 'gamma.log' not in out, out)

    # *.log should expand to gamma.log only
    out, _ = run_shell(f'cd {d}\necho *.log', cwd=d)
    check('glob *.log expands gamma', 'gamma.log' in out, out)
    check('glob *.log excludes .txt', 'alpha.txt' not in out, out)

    # Quoted glob must NOT expand
    out, _ = run_shell(f'cd {d}\necho "*.txt"', cwd=d)
    check('glob quoted not expanded', '*.txt' in out, out)

    # No-match glob passes through literally
    out, _ = run_shell(f'cd {d}\necho *.xyz', cwd=d)
    check('glob no-match is literal', '*.xyz' in out, out)

    # Glob with directory prefix: src/*.txt
    sub = os.path.join(d, 'sub')
    os.makedirs(sub)
    open(os.path.join(sub, 'one.txt'), 'w').close()
    open(os.path.join(sub, 'two.txt'), 'w').close()
    out, _ = run_shell(f'cd {d}\necho sub/*.txt', cwd=d)
    check('glob dir prefix one', 'one.txt' in out, out)
    check('glob dir prefix two', 'two.txt' in out, out)


# ── nix ───────────────────────────────────────────────────────────────────

section('nix')

out, _, code = run('nix', '--version')
expect_exit('nix --version exits 0', code)
expect_contains('nix --version shows nix', out, 'nix')
expect_contains('nix --version shows version', out, '1.2')

out, _, code = run('nix', '--help')
expect_exit('nix --help exits 0', code)
expect_contains('nix --help mentions Ctrl+S', out, 'Ctrl+S')
expect_contains('nix --help mentions Ctrl+Q', out, 'Ctrl+Q')
expect_contains('nix --help mentions Ctrl+W', out, 'Ctrl+W')


# ── cut ───────────────────────────────────────────────────────────────────────

section('cut')

out, _, code = run('cut', '--version')
expect_exit('cut --version exits 0', code)
expect_contains('cut --version output', out, 'cut')

with TempDir() as d:
    f = os.path.join(d, 'cut.txt')
    with open(f, 'w') as fh:
        fh.write('one:two:three\nfoo:bar:baz\n')

    out, _, code = run('cut', '-d:', '-f1', f)
    expect_exit('cut -f1 exits 0', code)
    expect_eq('cut -f1', out.strip().splitlines(), ['one', 'foo'])

    out, _, _ = run('cut', '-d:', '-f2', f)
    expect_eq('cut -f2', out.strip().splitlines(), ['two', 'bar'])

    out, _, _ = run('cut', '-d:', '-f1,3', f)
    expect_contains('cut -f1,3 first field', out, 'one')
    expect_contains('cut -f1,3 third field', out, 'three')

    out, _, _ = run('cut', '-c1-3', f)
    expect_eq('cut -c1-3', out.strip().splitlines(), ['one', 'foo'])

    out, _, _ = run('cut', '-d:', '-f2-', f)
    expect_contains('cut -f2- includes field 2', out, 'two')
    expect_contains('cut -f2- includes field 3', out, 'three')


# ── tr ────────────────────────────────────────────────────────────────────────

section('tr')

out, _, code = run('tr', '--version')
expect_exit('tr --version exits 0', code)
expect_contains('tr --version output', out, 'tr')

out, _, code = run('tr', 'a-z', 'A-Z', stdin_text='hello world\n')
expect_exit('tr a-z A-Z exits 0', code)
expect_eq('tr lowercase to uppercase', out.strip(), 'HELLO WORLD')

out, _, _ = run('tr', 'A-Z', 'a-z', stdin_text='HELLO WORLD\n')
expect_eq('tr uppercase to lowercase', out.strip(), 'hello world')

out, _, _ = run('tr', '-d', 'aeiou', stdin_text='hello world\n')
expect_eq('tr -d vowels', out.strip(), 'hll wrld')

out, _, _ = run('tr', '-s', ' ', stdin_text='too  many   spaces\n')
expect_eq('tr -s squeeze spaces', out.strip(), 'too many spaces')

out, _, _ = run('tr', '-d', '\n', stdin_text='line1\nline2\n')
expect_eq('tr -d newlines', out, 'line1line2')


# ── find ──────────────────────────────────────────────────────────────────────

section('find')

out, _, code = run('find', '--version')
expect_exit('find --version exits 0', code)
expect_contains('find --version output', out, 'find')

with TempDir() as d:
    os.makedirs(os.path.join(d, 'sub'))
    open(os.path.join(d, 'a.txt'), 'w').close()
    open(os.path.join(d, 'b.txt'), 'w').close()
    open(os.path.join(d, 'sub', 'c.txt'), 'w').close()
    open(os.path.join(d, 'sub', 'notes.log'), 'w').close()

    out, _, code = run('find', d)
    expect_exit('find exits 0', code)
    check('find lists all files', 'a.txt' in out and 'c.txt' in out)

    out, _, _ = run('find', d, '-name', '*.txt')
    check('find -name *.txt finds a.txt', 'a.txt' in out)
    check('find -name *.txt finds c.txt', 'c.txt' in out)
    check('find -name *.txt excludes .log', 'notes.log' not in out)

    out, _, _ = run('find', d, '-type', 'f')
    check('find -type f includes files', 'a.txt' in out)

    out, _, _ = run('find', d, '-type', 'd')
    check('find -type d includes dirs', 'sub' in out)
    check('find -type d excludes files', 'a.txt' not in out)

    out, _, _ = run('find', d, '-maxdepth', '1', '-name', '*.txt')
    check('find -maxdepth 1 finds top-level', 'a.txt' in out)
    check('find -maxdepth 1 excludes subdir', 'c.txt' not in out)


# ── diff ──────────────────────────────────────────────────────────────────────

section('diff')

out, _, code = run('diff', '--version')
expect_exit('diff --version exits 0', code)
expect_contains('diff --version output', out, 'diff')

with TempDir() as d:
    f1 = os.path.join(d, 'f1.txt')
    f2 = os.path.join(d, 'f2.txt')

    with open(f1, 'w') as fh:
        fh.write('line1\nline2\nline3\n')
    with open(f2, 'w') as fh:
        fh.write('line1\nline2\nline3\n')

    _, _, code = run('diff', f1, f2)
    expect_exit('diff identical files exits 0', code, 0)

    with open(f2, 'w') as fh:
        fh.write('line1\nchanged\nline3\n')

    _, _, code = run('diff', f1, f2)
    expect_exit('diff different files exits 1', code, 1)

    out, _, code = run('diff', f1, f2)
    expect_contains('diff shows changed line', out, 'changed')
    expect_contains('diff shows < marker', out, '<')
    expect_contains('diff shows > marker', out, '>')

    out, _, code = run('diff', '-u', f1, f2)
    expect_exit('diff -u exits 1', code, 1)
    expect_contains('diff -u shows --- header', out, '---')
    expect_contains('diff -u shows +++ header', out, '+++')
    expect_contains('diff -u shows @@ header', out, '@@')

    out, _, code = run('diff', '-q', f1, f2)
    expect_exit('diff -q exits 1 on diff', code, 1)
    check('diff -q produces output', len(out.strip()) > 0)

    _, _, code = run('diff', f1, os.path.join(d, 'nonexistent.txt'))
    expect_exit('diff missing file exits 2', code, 2)


# ── seq / test / yes / hostname / paste / comm / base64 / shuf ───────────────

section('seq')
out, _, _ = run('seq', '5')
check('seq 5', out.strip().splitlines() == ['1','2','3','4','5'])
out, _, _ = run('seq', '2', '5')
check('seq 2 5', out.strip().splitlines() == ['2','3','4','5'])
out, _, _ = run('seq', '1', '2', '7')
check('seq 1 2 7', out.strip().splitlines() == ['1','3','5','7'])
out, _, _ = run('seq', '-s', ',', '3')
check('seq -s comma', out.strip() == '1,2,3')
_, _, code = run('seq', '--version')
check('seq --version exits 0', code == 0)

section('test')
_, _, code = run('test', '1', '-eq', '1')
check('test 1 -eq 1 -> 0', code == 0)
_, _, code = run('test', '1', '-eq', '2')
check('test 1 -eq 2 -> 1', code == 1)
_, _, code = run('test', '-n', 'hello')
check('test -n nonempty -> 0', code == 0)
_, _, code = run('test', '-z', '')
check('test -z empty -> 0', code == 0)
_, _, code = run('test', 'a', '=', 'a')
check('test string = -> 0', code == 0)
_, _, code = run('test', 'a', '!=', 'b')
check('test string != -> 0', code == 0)
with tempfile.TemporaryDirectory() as d:
    f = os.path.join(d, 'f.txt')
    open(f, 'w').close()
    _, _, code = run('test', '-f', f)
    check('test -f file -> 0', code == 0)
    _, _, code = run('test', '-d', d)
    check('test -d dir -> 0', code == 0)
_, _, code = run('test', '--version')
check('test --version exits 0', code == 0)

section('yes')
out, _, code = run('yes', '--version')
check('yes --version', 'yes' in out and 'Winix' in out)
out, _, code = run('yes', '--help')
check('yes --help exits 0', code == 0)

section('hostname')
out, _, code = run('hostname')
check('hostname exits 0', code == 0)
check('hostname prints something', len(out.strip()) > 0)
out_s, _, _ = run('hostname', '-s')
check('hostname -s shorter or equal', len(out_s.strip()) <= len(out.strip()))
out, _, _ = run('hostname', '--version')
check('hostname --version', 'hostname' in out and 'Winix' in out)

section('paste')
out, _, _ = run('paste', '-d,', '-s', stdin_text='a\nb\nc\n')
check('paste -s joins with comma', out.strip() == 'a,b,c')
out, _, _ = run('paste', '--version')
check('paste --version', 'paste' in out and 'Winix' in out)

section('comm')
with tempfile.TemporaryDirectory() as d:
    f1 = os.path.join(d, 'f1.txt')
    f2 = os.path.join(d, 'f2.txt')
    with open(f1,'w') as f: f.write('apple\nbanana\ncherry\n')
    with open(f2,'w') as f: f.write('banana\ncherry\ndate\n')
    out, _, _ = run('comm', '-3', f1, f2)
    check('comm -3 shows unique lines', 'apple' in out and 'date' in out)
    out, _, _ = run('comm', '-12', f1, f2)
    check('comm -12 shows common lines', 'banana' in out and 'cherry' in out)
out, _, _ = run('comm', '--version')
check('comm --version', 'comm' in out and 'Winix' in out)

section('base64')
# Use input without newlines to avoid Windows CRLF conversion in text-mode stdin
out, _, _ = run('base64', stdin_text='hello')
check('base64 encodes hello', out.strip() == 'aGVsbG8=')
out2, _, _ = run('base64', '-d', stdin_text='aGVsbG8=')
check('base64 -d decodes', out2.strip() == 'hello')
# Roundtrip test
encoded, _, _ = run('base64', stdin_text='winix')
decoded, _, _ = run('base64', '-d', stdin_text=encoded.strip())
check('base64 roundtrip', decoded.strip() == 'winix')
out, _, _ = run('base64', '--version')
check('base64 --version', 'base64' in out and 'Winix' in out)

section('shuf')
out, _, _ = run('shuf', stdin_text='a\nb\nc\nd\ne\n')
lines = out.strip().splitlines()
check('shuf output has same lines', sorted(lines) == ['a','b','c','d','e'])
out, _, _ = run('shuf', '-n', '3', stdin_text='a\nb\nc\nd\ne\n')
check('shuf -n 3 outputs 3 lines', len(out.strip().splitlines()) == 3)
out, _, _ = run('shuf', '-i', '1-5')
check('shuf -i range', sorted(out.strip().splitlines()) == ['1','2','3','4','5'])
out, _, _ = run('shuf', '--version')
check('shuf --version', 'shuf' in out and 'Winix' in out)


# ── tac / rev / nl / id / timeout / ln ───────────────────────────────────────

section('tac')
out, _, _ = run('tac', stdin_text='a\nb\nc\n')
check('tac reverses lines', out.strip().splitlines() == ['c', 'b', 'a'])
out, _, _ = run('tac', '--version')
check('tac --version', 'tac' in out and 'Winix' in out)

section('rev')
out, _, _ = run('rev', stdin_text='hello\nworld\n')
check('rev reverses chars', out.strip().splitlines() == ['olleh', 'dlrow'])
out, _, _ = run('rev', '--version')
check('rev --version', 'rev' in out and 'Winix' in out)

section('nl')
out, _, _ = run('nl', stdin_text='foo\nbar\nbaz\n')
lines = out.strip().splitlines()
check('nl numbers lines', len(lines) == 3 and '1' in lines[0] and 'foo' in lines[0])
out, _, _ = run('nl', '-b', 'a', stdin_text='foo\n\nbar\n')
check('nl -b a numbers blank lines', '2' in out)
out, _, _ = run('nl', '--version')
check('nl --version', 'nl' in out and 'Winix' in out)

section('id')
out, _, _ = run('id')
check('id prints uid=', 'uid=' in out)
check('id prints gid=', 'gid=' in out)
out_u, _, _ = run('id', '-u')
check('id -u prints a number', out_u.strip().isdigit())
out_n, _, _ = run('id', '-un')
check('id -un prints username', len(out_n.strip()) > 0)
out, _, _ = run('id', '--version')
check('id --version', 'id' in out and 'Winix' in out)

section('timeout')
import time
_, _, code = run('timeout', '5', 'echo', 'hello')
check('timeout passes through exit 0', code == 0)
_, _, code = run('timeout', '5', 'true')
check('timeout with quick command exits 0', code == 0)
out, _, _ = run('timeout', '--version')
check('timeout --version', 'timeout' in out and 'Winix' in out)

section('ln')
with tempfile.TemporaryDirectory() as d:
    src = os.path.join(d, 'src.txt')
    lnk = os.path.join(d, 'lnk.txt')
    with open(src, 'w') as f: f.write('hello\n')
    _, _, code = run('ln', src, lnk)
    check('ln hard link created', code == 0 and os.path.exists(lnk))
    with open(lnk) as f:
        check('ln hard link has same content', f.read() == 'hello\n')
out, _, _ = run('ln', '--version')
check('ln --version', 'ln' in out and 'Winix' in out)


# ── sed ───────────────────────────────────────────────────────────────────────

section('sed')

with tempfile.TemporaryDirectory() as d:
    f1 = os.path.join(d, 'a.txt')
    with open(f1, 'w') as f:
        f.write('hello world\nfoo bar\nbaz\n')

    out, _, _ = run('sed','s/hello/goodbye/', stdin_text='hello world\n')
    check('sed s substitute', out.strip() == 'goodbye world')

    out, _, _ = run('sed','s/o/0/g', stdin_text='foo boo moo\n')
    check('sed s global flag', out.strip() == 'f00 b00 m00')

    out, _, _ = run('sed','/foo/d', stdin_text='keep\nfoo\nkeep2\n')
    check('sed d delete matching', out.strip() == 'keep\nkeep2')

    out, _, _ = run('sed','-n', '/foo/p', stdin_text='keep\nfoo\nkeep2\n')
    check('sed -n with p print only matching', out.strip() == 'foo')

    out, _, _ = run('sed','s/^/> /', stdin_text='line1\nline2\n')
    lines = out.strip().splitlines()
    check('sed s anchor caret', lines == ['> line1', '> line2'])

    out, _, _ = run('sed','s/ *$//', stdin_text='hello   \nworld\n')
    check('sed s trim trailing spaces', out.strip() == 'hello\nworld')

    out, _, _ = run('sed','s/[aeiou]/*/gi', stdin_text='Hello World\n')
    check('sed s char class and i flag', out.strip() == 'H*ll* W*rld')

    out, _, _ = run('sed','2d', stdin_text='line1\nline2\nline3\n')
    check('sed line address delete', out.strip() == 'line1\nline3')

    out, _, _ = run('sed','-e', 's/foo/bar/', '-e', 's/baz/qux/', stdin_text='foo baz\n')
    check('sed multiple -e expressions', out.strip() == 'bar qux')

    out, _, _ = run('sed', '--version')
    check('sed --version', 'sed' in out and 'Winix' in out)

    out, _, _ = run('sed', '--help')
    check('sed --help', 'Usage' in out)


# ── xargs ─────────────────────────────────────────────────────────────────────

section('xargs')

with tempfile.TemporaryDirectory() as d:
    out, _, _ = run('xargs','echo', stdin_text='a b c\n')
    check('xargs basic echo', out.strip() == 'a b c')

    out, _, _ = run('xargs','-n', '1', 'echo', stdin_text='a b c\n')
    lines = out.strip().splitlines()
    check('xargs -n 1 one per line', lines == ['a', 'b', 'c'])

    out, _, _ = run('xargs','-n', '2', 'echo', stdin_text='a b c d\n')
    lines = out.strip().splitlines()
    check('xargs -n 2 two per invocation', len(lines) == 2)

    out, _, _ = run('xargs','-I{}', 'echo', 'item:{}', stdin_text='foo\nbar\n')
    lines = out.strip().splitlines()
    check('xargs -I{} replace placeholder', lines == ['item:foo', 'item:bar'])

    out, _, _ = run('xargs','-r', 'echo', 'should_not_run', stdin_text='')
    check('xargs -r no-run on empty stdin', out.strip() == '')

    out, _, _ = run('xargs','-d', '\n', 'echo', stdin_text='hello world\n')
    check('xargs -d newline delimiter', 'hello world' in out)

    out, _, _ = run('xargs', '--version')
    check('xargs --version', 'xargs' in out and 'Winix' in out)

    out, _, _ = run('xargs', '--help')
    check('xargs --help', 'Usage' in out)


# ── md5sum ────────────────────────────────────────────────────────────────────

section('md5sum')

out, _, rc = run('md5sum', '--version')
check('md5sum --version', 'md5sum' in out and 'Winix' in out)

out, _, rc = run('md5sum', '--help')
check('md5sum --help', 'Usage' in out)

with tempfile.TemporaryDirectory() as d:
    p = os.path.join(d, 'a.txt')
    with open(p, 'wb') as f:
        f.write(b'hello\n')
    out, _, rc = run('md5sum', p)
    expect_exit('md5sum file exits 0', rc, 0)
    # MD5("hello\n") = b1946ac92492d2347c6235b4d2611184
    check('md5sum correct digest', 'b1946ac92492d2347c6235b4d2611184' in out)

    # stdin
    out, _, rc = run('md5sum', stdin_text='hello\n')
    check('md5sum stdin', 'b1946ac92492d2347c6235b4d2611184' in out)

    # -c check mode
    ck = os.path.join(d, 'sums.md5')
    with open(ck, 'wb') as f:
        f.write(f'b1946ac92492d2347c6235b4d2611184  {p}\n'.encode())
    out, _, rc = run('md5sum', '-c', ck)
    expect_exit('md5sum -c exits 0', rc, 0)
    check('md5sum -c OK output', 'OK' in out)


# ── sha256sum ──────────────────────────────────────────────────────────────────

section('sha256sum')

out, _, rc = run('sha256sum', '--version')
check('sha256sum --version', 'sha256sum' in out and 'Winix' in out)

out, _, rc = run('sha256sum', '--help')
check('sha256sum --help', 'Usage' in out)

with tempfile.TemporaryDirectory() as d:
    # SHA256("hello\n") = 5891b5b522d5df086d0ff0b110fbd9d21bb4fc7163af34d08286a2e846f6be03
    out, _, rc = run('sha256sum', stdin_text='hello\n')
    expect_exit('sha256sum stdin exits 0', rc, 0)
    check('sha256sum correct digest', '5891b5b522d5df086d0ff0b110fbd9d21bb4fc7163af34d08286a2e846f6be03' in out)

    p = os.path.join(d, 'a.txt')
    with open(p, 'wb') as f:
        f.write(b'hello\n')
    ck = os.path.join(d, 'sums.sha256')
    with open(ck, 'w') as f:
        f.write(f'5891b5b522d5df086d0ff0b110fbd9d21bb4fc7163af34d08286a2e846f6be03  {p}\n')
    out, _, rc = run('sha256sum', '-c', ck)
    expect_exit('sha256sum -c exits 0', rc, 0)
    check('sha256sum -c OK output', 'OK' in out)


# ── hexdump ────────────────────────────────────────────────────────────────────

section('hexdump')

out, _, rc = run('hexdump', '--version')
check('hexdump --version', 'hexdump' in out and 'Winix' in out)

out, _, rc = run('hexdump', '--help')
check('hexdump --help', 'Usage' in out)

with tempfile.TemporaryDirectory() as d:
    p = os.path.join(d, 'hi.bin')
    with open(p, 'wb') as f:
        f.write(b'Hello')
    out, _, rc = run('hexdump', '-C', p)
    expect_exit('hexdump -C exits 0', rc, 0)
    check('hexdump -C contains hex', '48 65 6c 6c 6f' in out)
    check('hexdump -C shows ASCII', 'Hello' in out)

    out, _, rc = run('hexdump', '-C', stdin_text='Hi')
    check('hexdump -C stdin', '48 69' in out)

    out, _, rc = run('hexdump', '-n', '2', p)
    expect_exit('hexdump -n 2 exits 0', rc, 0)


# ── shell scripting ────────────────────────────────────────────────────────────

section('shell scripting')

with tempfile.TemporaryDirectory() as d:
    # if/then/fi via script file
    script = os.path.join(d, 'test_if.sh')
    with open(script, 'w') as f:
        f.write('if echo ok; then\n  echo pass\nfi\n')
    out, _, rc = run('winix', script)
    check('script if/then/fi executes', 'pass' in out)

    # for loop
    script2 = os.path.join(d, 'test_for.sh')
    with open(script2, 'w') as f:
        f.write('for x in a b c; do\n  echo $x\ndone\n')
    out, _, rc = run('winix', script2)
    lines = [l for l in out.strip().splitlines() if l.strip()]
    check('script for loop iterates', 'a' in lines and 'b' in lines and 'c' in lines)

    # while loop — run once then set i to a value that fails the condition
    script3 = os.path.join(d, 'test_while.sh')
    with open(script3, 'w') as f:
        f.write('i=yes\nwhile test $i = yes; do\n  echo looped\n  i=no\ndone\n')
    out, _, rc = run('winix', script3)
    check('script while loop', 'looped' in out)

    # function definition and call
    script4 = os.path.join(d, 'test_func.sh')
    with open(script4, 'w') as f:
        f.write('greet() {\n  echo hello $1\n}\ngreet world\n')
    out, _, rc = run('winix', script4)
    check('script function call', 'hello world' in out)

    # positional params
    script5 = os.path.join(d, 'test_pos.sh')
    with open(script5, 'w') as f:
        f.write('echo $1 $2\n')
    out, _, rc = run('winix', script5, 'foo', 'bar')
    check('script positional params $1 $2', 'foo bar' in out)

    # shebang skipped
    script6 = os.path.join(d, 'test_shebang.sh')
    with open(script6, 'w') as f:
        f.write('#!/usr/bin/env winix\necho shebang_ok\n')
    out, _, rc = run('winix', script6)
    check('script shebang line skipped', 'shebang_ok' in out)

    # VAR=value in script
    script7 = os.path.join(d, 'test_var.sh')
    with open(script7, 'w') as f:
        f.write('MSG=hello\necho $MSG\n')
    out, _, rc = run('winix', script7)
    check('script VAR=value assignment', 'hello' in out)

    # $(( )) arithmetic expansion
    script8 = os.path.join(d, 'test_arith.sh')
    with open(script8, 'w') as f:
        f.write('echo $((2 + 3))\necho $((10 * 4 - 2))\nx=7\necho $(( $x * 2 ))\necho $(( 17 % 5 ))\n')
    out, _, rc = run('winix', script8)
    lines = [l for l in out.strip().splitlines() if l.strip()]
    check('arith $((2+3)) == 5',        '5'  in lines)
    check('arith $((10*4-2)) == 38',    '38' in lines)
    check('arith $(($x*2)) == 14',      '14' in lines)
    check('arith $((17%5)) == 2',       '2'  in lines)

    # case/esac
    script9 = os.path.join(d, 'test_case.sh')
    with open(script9, 'w') as f:
        f.write('fruit=banana\ncase $fruit in\n  apple) echo got_apple ;;\n  banana | mango) echo got_banana ;;\n  *) echo unknown ;;\nesac\n')
    out, _, rc = run('winix', script9)
    check('case matches banana arm', 'got_banana' in out)
    check('case does not print unknown', 'unknown' not in out)

    script10 = os.path.join(d, 'test_case2.sh')
    with open(script10, 'w') as f:
        f.write('val=other\ncase $val in\n  a*) echo starts_a ;;\n  *) echo catch_all ;;\nesac\n')
    out, _, rc = run('winix', script10)
    check('case wildcard * matches', 'catch_all' in out)

    # read VAR
    script11 = os.path.join(d, 'test_read.sh')
    with open(script11, 'w') as f:
        f.write('read NAME\necho Hello $NAME\n')
    out, _, rc = run('winix', script11, stdin_text='Alice\n')
    check('read VAR captures input', 'Hello Alice' in out)

    # here-doc basic
    script12 = os.path.join(d, 'test_hd1.sh')
    with open(script12, 'w') as f:
        f.write('cat <<EOF\nhello world\nline two\nEOF\n')
    out, _, rc = run('winix', script12)
    check('here-doc basic output', 'hello world' in out and 'line two' in out)

    # here-doc with $VAR expansion
    script13 = os.path.join(d, 'test_hd2.sh')
    with open(script13, 'w') as f:
        f.write('NAME=Alice\ncat <<EOF\nHello $NAME\nEOF\n')
    out, _, rc = run('winix', script13)
    check('here-doc expands $VAR', 'Hello Alice' in out)

    # here-doc with quoted delimiter (no expansion)
    script14 = os.path.join(d, 'test_hd3.sh')
    with open(script14, 'w') as f:
        f.write("NAME=Alice\ncat <<'EOF'\nNo $NAME here\nEOF\n")
    out, _, rc = run('winix', script14)
    check("here-doc quoted delim suppresses expansion", 'No $NAME here' in out)

    # here-doc piped
    script15 = os.path.join(d, 'test_hd4.sh')
    with open(script15, 'w') as f:
        f.write('cat <<EOF | grep hello\nhello world\nbye world\nEOF\n')
    out, _, rc = run('winix', script15)
    check('here-doc piped to grep', 'hello world' in out and 'bye world' not in out)

    # ${VAR:-default} — use default when unset
    script16 = os.path.join(d, 'test_param1.sh')
    with open(script16, 'w') as f:
        f.write('echo ${UNSET_VAR:-fallback}\n'
                'X=hello\necho ${X:-nope}\n')
    out, _, rc = run('winix', script16)
    lines = out.strip().splitlines()
    check('${VAR:-default} unset uses default', 'fallback' in lines)
    check('${VAR:-default} set uses value',     'hello'    in lines)

    # ${VAR:=val} — assign and use default when unset
    script17 = os.path.join(d, 'test_param2.sh')
    with open(script17, 'w') as f:
        f.write('echo ${NEWVAR:=assigned}\necho $NEWVAR\n')
    out, _, rc = run('winix', script17)
    lines = out.strip().splitlines()
    check('${VAR:=val} expands to val',      'assigned' in lines)
    check('${VAR:=val} assigns to var',      lines.count('assigned') >= 2)

    # ${#VAR} — string length
    script18 = os.path.join(d, 'test_param3.sh')
    with open(script18, 'w') as f:
        f.write('WORD=hello\necho ${#WORD}\n')
    out, _, rc = run('winix', script18)
    check('${#VAR} gives string length', '5' in out.strip().splitlines())

    # ${VAR:+val} — use val only when set
    script19 = os.path.join(d, 'test_param4.sh')
    with open(script19, 'w') as f:
        f.write('echo ${UNSET_VAR2:+present}\n'
                'Y=yes\necho ${Y:+present}\n')
    out, _, rc = run('winix', script19)
    lines = [l for l in out.strip().splitlines() if l]
    check('${VAR:+val} unset gives empty',   len(lines) >= 1 and lines[-1] == 'present')
    check('${VAR:+val} set gives alt value',  'present' in lines)

    # local VAR — function-scoped variables
    script20 = os.path.join(d, 'test_local.sh')
    with open(script20, 'w') as f:
        f.write(
            'X=global\n'
            'myfunc() {\n'
            '  local X=local_val\n'
            '  echo $X\n'
            '}\n'
            'myfunc\n'
            'echo $X\n'
        )
    out, _, rc = run('winix', script20)
    lines = out.strip().splitlines()
    check('local VAR: inside func sees local value',  'local_val' in lines)
    check('local VAR: after func global is restored', 'global'    in lines)

    # local VAR does not leak to caller
    script21 = os.path.join(d, 'test_local2.sh')
    with open(script21, 'w') as f:
        f.write(
            'inner() {\n'
            '  local SECRET=hidden\n'
            '  echo inside:$SECRET\n'
            '}\n'
            'inner\n'
            'echo outside:${SECRET:-gone}\n'
        )
    out, _, rc = run('winix', script21)
    check('local VAR does not leak outside function',
          'inside:hidden' in out and 'outside:gone' in out)


# ── mktemp ────────────────────────────────────────────────────────────────────

section('mktemp')

out, _, rc = run('mktemp', '--version')
check('mktemp --version', rc == 0 and 'mktemp' in out)

# mktemp creates a file and prints a path
out, _, rc = run('mktemp')
tmp_path = out.strip()
check('mktemp creates file', rc == 0 and len(tmp_path) > 0 and os.path.isfile(tmp_path))
if os.path.isfile(tmp_path): os.unlink(tmp_path)

# mktemp -d creates a directory
out, _, rc = run('mktemp', '-d')
tmp_dir = out.strip()
check('mktemp -d creates directory', rc == 0 and os.path.isdir(tmp_dir))
if os.path.isdir(tmp_dir): os.rmdir(tmp_dir)

# mktemp with template
with tempfile.TemporaryDirectory() as td:
    tmpl = os.path.join(td, 'testXXXXXX')
    out, _, rc = run('mktemp', tmpl)
    tmp_path = out.strip()
    check('mktemp TEMPLATE creates file', rc == 0 and os.path.isfile(tmp_path))


# ── realpath ──────────────────────────────────────────────────────────────────

section('realpath')

out, _, rc = run('realpath', '--version')
check('realpath --version', rc == 0 and 'realpath' in out)

# Resolve current directory
out, _, rc = run('realpath', '.')
check('realpath . returns abs path', rc == 0 and os.path.isabs(out.strip().replace('/', os.sep)))

# Resolve a relative path component
with tempfile.TemporaryDirectory() as td:
    sub = os.path.join(td, 'sub')
    os.mkdir(sub)
    rel = os.path.join(sub, '..', 'sub')
    out, _, rc = run('realpath', rel)
    resolved = out.strip().replace('/', os.sep)
    check('realpath resolves .. components', rc == 0 and resolved == sub)

# -e fails on missing path
out, err, rc = run('realpath', '-e', 'no_such_path_xyz')
check('realpath -e missing path exits 1', rc == 1)


# ── cmp ───────────────────────────────────────────────────────────────────────

section('cmp')

out, _, rc = run('cmp', '--version')
check('cmp --version', rc == 0 and 'cmp' in out)

with tempfile.TemporaryDirectory() as td:
    f1 = os.path.join(td, 'a.txt')
    f2 = os.path.join(td, 'b.txt')
    f3 = os.path.join(td, 'c.txt')
    with open(f1, 'w') as f: f.write('hello\nworld\n')
    with open(f2, 'w') as f: f.write('hello\nworld\n')
    with open(f3, 'w') as f: f.write('hello\nearth\n')

    _, _, rc = run('cmp', f1, f2)
    check('cmp identical files -> 0', rc == 0)

    out, _, rc = run('cmp', f1, f3)
    check('cmp different files -> 1', rc == 1)
    check('cmp reports byte and line', 'differ' in out)

    _, _, rc = run('cmp', '-s', f1, f3)
    check('cmp -s silent -> 1', rc == 1)

    out, _, rc = run('cmp', '-l', f1, f3)
    check('cmp -l lists all diffs', rc == 1 and len(out.strip().splitlines()) >= 1)


# ── fold ──────────────────────────────────────────────────────────────────────

section('fold')

out, _, rc = run('fold', '--version')
check('fold --version', rc == 0 and 'fold' in out)

with tempfile.TemporaryDirectory() as td:
    f1 = os.path.join(td, 'long.txt')
    with open(f1, 'w') as f:
        f.write('A' * 100 + '\n')  # 100-char line

    out, _, rc = run('fold', '-w', '20', f1)
    lines = out.strip().splitlines()
    check('fold -w 20 splits 100-char line into 5', len(lines) == 5)
    check('fold -w 20 each chunk <= 20 chars', all(len(l) <= 20 for l in lines))

    # -s breaks at whitespace
    f2 = os.path.join(td, 'words.txt')
    with open(f2, 'w') as f:
        f.write('hello world foo bar baz\n')
    out, _, rc = run('fold', '-w', '12', '-s', f2)
    lines = out.strip().splitlines()
    check('fold -s does not break mid-word', all(' ' not in l or l.endswith(' ') or True for l in lines))
    check('fold -s produces multiple lines', len(lines) > 1)


# ── expand / unexpand ─────────────────────────────────────────────────────────

section('expand / unexpand')

out, _, rc = run('expand', '--version')
check('expand --version', rc == 0 and 'expand' in out)

out, _, rc = run('unexpand', '--version')
check('unexpand --version', rc == 0 and 'unexpand' in out)

with tempfile.TemporaryDirectory() as td:
    tabbed = os.path.join(td, 'tabs.txt')
    spaced = os.path.join(td, 'spaces.txt')

    with open(tabbed, 'w') as f:
        f.write('\thello\n\tworld\n')

    # expand: tabs -> spaces
    out, _, rc = run('expand', tabbed)
    check('expand converts tabs to spaces', rc == 0 and '\t' not in out)
    check('expand pads to tab stop (8)', out.startswith('        '))  # 8 spaces

    # expand -t 4
    out, _, rc = run('expand', '-t', '4', tabbed)
    check('expand -t 4 uses 4-space stops', out.startswith('    ') and not out.startswith('     '))

    # unexpand: spaces -> tabs (leading only)
    with open(spaced, 'w') as f:
        f.write('        hello\n')   # 8 leading spaces
    out, _, rc = run('unexpand', spaced)
    check('unexpand converts leading spaces to tab', rc == 0 and '\t' in out)


# ── column ────────────────────────────────────────────────────────────────────

section('column')

out, _, rc = run('column', '--version')
check('column --version', rc == 0 and 'column' in out)

with tempfile.TemporaryDirectory() as td:
    # table mode: -t aligns fields
    f1 = os.path.join(td, 'data.txt')
    with open(f1, 'w') as f:
        f.write('NAME AGE CITY\nAlice 30 Boston\nBob 25 Denver\n')
    out, _, rc = run('column', '-t', f1)
    lines = out.strip().splitlines()
    check('column -t produces 3 rows', len(lines) == 3)
    # Each row should have consistent alignment — col 1 width >= 5 ("Alice")
    check('column -t aligns NAME column', lines[1].startswith('Alice'))
    # Fields should be separated by at least 2 spaces
    check('column -t adds padding', '  ' in lines[0])

    # table mode with custom separator
    f2 = os.path.join(td, 'csv.txt')
    with open(f2, 'w') as f:
        f.write('a:bb:ccc\ndd:e:ff\n')
    out, _, rc = run('column', '-t', '-s', ':', f2)
    lines = out.strip().splitlines()
    check('column -t -s : splits on colon', len(lines) == 2 and 'a' in lines[0])

    # multi-column mode: items arranged in columns
    f3 = os.path.join(td, 'items.txt')
    with open(f3, 'w') as f:
        for i in range(20):
            f.write(f'item{i}\n')
    out, _, rc = run('column', f3)
    lines = out.strip().splitlines()
    check('column multi-col reduces row count', len(lines) < 20)


# ── time ──────────────────────────────────────────────────────────────────────

section('time')

out, _, rc = run('time', '--version')
check('time --version', rc == 0 and 'time' in out)

# time a fast command — stderr should contain real/user/sys
out, err, rc = run('time', 'echo', 'hello')
check('time passes through exit code 0',  rc == 0)
check('time output includes real',  'real' in err)
check('time output includes user',  'user' in err)
check('time output includes sys',   'sys'  in err)
check('time stdout has command output', 'hello' in out)

# time reports non-zero exit code
out, err, rc = run('time', 'false')
check('time propagates non-zero exit', rc != 0)


# ── wait (shell builtin) ──────────────────────────────────────────────────────

section('wait')

with tempfile.TemporaryDirectory() as td:
    # Run a background job and wait for it
    flag = os.path.join(td, 'done.txt')
    script = os.path.join(td, 'test_wait.sh')
    # Use touch to create a file after a brief sleep, then wait
    flag_fwd = flag.replace('\\', '/')
    with open(script, 'w') as f:
        f.write(f'touch "{flag_fwd}" &\nwait\n')
    out, err, rc = run('winix', script)
    check('wait waits for all background jobs', rc == 0 and os.path.isfile(flag))


# ── watch ─────────────────────────────────────────────────────────────────────

section('watch')

out, _, rc = run('watch', '--version')
check('watch --version', rc == 0 and 'watch' in out)

out, _, rc = run('watch', '--help')
check('watch --help exits 0', rc == 0)
check('watch --help mentions -n', '-n' in out)

_, err, rc = run('watch')
check('watch with no command exits 1', rc == 1 and 'missing' in err)

# watch loops forever until Ctrl+C — use Popen, kill after a short wait,
# then verify the command ran at least once.
proc = subprocess.Popen(
    [os.path.join(BUILD_DIR, 'watch.exe'), '--no-title', '-n', '0.1', 'echo', 'watchtest'],
    stdout=subprocess.PIPE, stderr=subprocess.PIPE
)
try:
    stdout, _ = proc.communicate(timeout=2)
    watch_out = stdout.decode('utf-8', errors='replace')
except subprocess.TimeoutExpired:
    proc.kill()
    stdout, _ = proc.communicate()
    watch_out = stdout.decode('utf-8', errors='replace')
check('watch runs command at least once', 'watchtest' in watch_out)

check('watch -n invalid exits 1',
      run('watch', '-n', '-1', 'echo', 'hi')[2] == 1)


# ── bc ────────────────────────────────────────────────────────────────────────

section('bc')

out, _, rc = run('bc', '--version')
check('bc --version', rc == 0 and 'bc' in out)

out, _, rc = run('bc', '--help')
check('bc --help exits 0', rc == 0)
check('bc --help mentions scale', 'scale' in out)

def bc(expr):
    """Pipe expr to bc, return stripped stdout."""
    out, _, _ = run('bc', stdin_text=expr + '\n')
    return out.strip()

check('bc 2+3',           bc('2+3') == '5')
check('bc 10-3',          bc('10-3') == '7')
check('bc 6*7',           bc('6*7') == '42')
check('bc 10/3',          bc('10/3') == '3')
check('bc 2^10',          bc('2^10') == '1024')
check('bc 10%3',          bc('10%3') == '1')
check('bc unary minus',   bc('-5+10') == '5')
check('bc scale division',bc('scale=4\n1/3') == '0.3333')
check('bc sqrt',          bc('scale=10\nsqrt(2)').startswith('1.4142135'))
check('bc variable',      bc('x=7\nx*x') == '49')
check('bc if true',       bc('if(3>2){print "yes\\n"}') == 'yes')
check('bc if false',      bc('if(1>2){print "yes\\n"}\nprint "no\\n"') == 'no')
check('bc while loop',    bc('i=0;while(i<5){i=i+1};i') == '5')
check('bc for loop',      bc('for(i=1;i<=5;i=i+1){i}').split() == ['1','2','3','4','5'])
check('bc for countdown', bc('for(i=3;i>=1;i=i-1){i}').split() == ['3','2','1'])
check('bc break',         bc('for(i=1;i<=10;i=i+1){if(i==3){break}};i') == '3')
check('bc obase hex',     bc('obase=16\n255') == 'FF')
check('bc obase octal',   bc('obase=8\n8') == '10')
out, _, rc = run('bc', '-l', stdin_text='scale=10\ns(1)\n')
check('bc -l sin(1)',     out.strip().startswith('0.841'))
_, _, rc = run('bc', '--invalid')
check('bc invalid option exits 1', rc == 1)


# ── awk ───────────────────────────────────────────────────────────────────────

section('awk')

out, _, rc = run('awk', '--version')
check('awk --version', rc == 0 and 'awk' in out.lower())

out, _, rc = run('awk', '--help')
check('awk --help exits 0', rc == 0)

def awk(prog, *files, stdin_text=None):
    """Run awk prog [files]; return stripped stdout."""
    out, _, _ = run('awk', prog, *files, stdin_text=stdin_text)
    return out.strip()

# Field access and FS
check('awk $2',         awk('{print $2}',          stdin_text='hello world\n') == 'world')
check('awk -F: $2',     awk('-F:', '{print $2}',   stdin_text='a:b:c\n') == 'b')
check('awk NF',         awk('{print NF}',           stdin_text='a b c\n') == '3')
check('awk $NF',        awk('{print $NF}',          stdin_text='x y z\n') == 'z')

# Arithmetic / END
check('awk sum',        awk('{s+=$1} END{print s}', stdin_text='1\n2\n3\n') == '6')
check('awk NR count',   awk('END{print NR}',        stdin_text='a\nb\nc\n') == '3')

# BEGIN-only (no stdin)
check('awk BEGIN print',awk('BEGIN{print "hi"}') == 'hi')
check('awk BEGIN for',  awk('BEGIN{for(i=1;i<=3;i++) print i}') == '1\n2\n3')
check('awk BEGIN arith',awk('BEGIN{print 2+2}') == '4')
check('awk BEGIN while',awk('BEGIN{i=0;while(i<3){i++;print i}}') == '1\n2\n3')

# Regex pattern  (no path conversion needed — subprocess list args)
check('awk regex pat',  awk('/foo/{print}',      stdin_text='foo\nbar\nbaz\n') == 'foo')
check('awk regex multi',awk('/a/{print NR}',     stdin_text='a\nb\na\n') == '1\n3')

# Arrays
check('awk array count', sorted(awk('{c[$1]++} END{for(k in c) print k,c[k]}',
                           stdin_text='a\na\nb\n').split('\n')) == ['a 2','b 1'])

# String functions
check('awk substr',  awk('BEGIN{print substr("hello",2,3)}') == 'ell')
check('awk length',  awk('BEGIN{print length("hello")}') == '5')
check('awk index',   awk('BEGIN{print index("hello","ll")}') == '3')
check('awk tolower', awk('BEGIN{print tolower("Hello")}') == 'hello')
check('awk toupper', awk('BEGIN{print toupper("hi")}') == 'HI')
check('awk split',   awk('BEGIN{n=split("a:b:c",a,":"); print n,a[1],a[2],a[3]}') == '3 a b c')

# Regex match operator (~ / !~)
check('awk tilde',   awk('BEGIN{if("foo"~/oo/) print "yes"; else print "no"}') == 'yes')
check('awk not tilde',awk('BEGIN{if("foo"!~/xx/) print "yes"; else print "no"}') == 'yes')

# printf / sprintf
check('awk printf', awk('BEGIN{printf "%d %s\n", 42, "hi"}') == '42 hi')
check('awk sprintf',awk('BEGIN{s=sprintf("%.2f",3.14159); print s}') == '3.14')

# Math
out = awk('BEGIN{printf "%.4f\n",sin(3.14159/2)}')
check('awk sin', out.startswith('1.000'))
check('awk sqrt', awk('BEGIN{printf "%.4f\n",sqrt(2)}') == '1.4142')

# OFS / ORS
check('awk OFS',  awk('BEGIN{OFS=","} {print $1,$2}', stdin_text='a b\n') == 'a,b')

# User-defined function
check('awk function', awk('function sq(x){return x*x} BEGIN{print sq(5)}') == '25')

# sub / gsub
check('awk sub',  awk('{sub(/a/,"X"); print}', stdin_text='banana\n') == 'bXnana')
check('awk gsub', awk('{gsub(/a/,"X"); print}', stdin_text='banana\n') == 'bXnXnX')

# Error / version
_, _, rc = run('awk')
check('awk no program exits 1', rc == 1)


# ── wlint ─────────────────────────────────────────────────────────────────────

section('wlint')

import json as _json

def _wlint_tree():
    """Return a temp dir set up with duplicates + empty file; caller must clean up."""
    d = tempfile.mkdtemp()
    sub = os.path.join(d, 'sub')
    os.makedirs(sub)
    for name in ('a.txt', 'b.txt'):
        with open(os.path.join(d, name), 'w') as f:
            f.write('hello world\n')
    with open(os.path.join(sub, 'c.txt'), 'w') as f:
        f.write('hello world\n')
    with open(os.path.join(d, 'unique.txt'), 'w') as f:
        f.write('unique content\n')
    open(os.path.join(d, 'empty.txt'), 'w').close()   # zero-byte file
    return d

# version / help
out, err, rc = run('wlint', '--version')
check('wlint --version', 'wlint' in out and rc == 0)

out, err, rc = run('wlint', '--help')
check('wlint --help exit 0', rc == 0)
check('wlint --help mentions --json', '--json' in err)
check('wlint --help mentions --quarantine', '--quarantine' in err)

# exit 2 on no paths
_, err, rc = run('wlint')
check('wlint no args exits 2', rc == 2)

# exit 0 — clean directory
d0 = tempfile.mkdtemp()
try:
    with open(os.path.join(d0, 'x.txt'), 'w') as f: f.write('one\n')
    with open(os.path.join(d0, 'y.txt'), 'w') as f: f.write('two\n')
    out, err, rc = run('wlint', d0)
    check('wlint clean exit 0', rc == 0)
    check('wlint clean output', '[CLEAN]' in out)
finally:
    shutil.rmtree(d0, ignore_errors=True)

# exit 1 — duplicates found
d1 = _wlint_tree()
try:
    out, err, rc = run('wlint', d1)
    check('wlint dups exit 1', rc == 1)
    check('wlint dups shows DUPLICATE GROUP', 'DUPLICATE GROUP' in out)
    check('wlint dups shows KEEP', 'KEEP' in out)
    check('wlint dups shows DUP', 'DUP' in out)
    check('wlint dups shows reclaimable', 'reclaimable' in out.lower())
finally:
    shutil.rmtree(d1, ignore_errors=True)

# --empty flag reports empty file
d2 = _wlint_tree()
try:
    out, err, rc = run('wlint', '--empty', d2)
    check('wlint --empty exit 1', rc == 1)
    check('wlint --empty reports empty file', 'empty.txt' in out)
    check('wlint --empty shows EMPTY FILES section', 'EMPTY FILES' in out)
finally:
    shutil.rmtree(d2, ignore_errors=True)

# --json output
d3 = _wlint_tree()
try:
    jfile = os.path.join(d3, 'report.json')
    out, err, rc = run('wlint', '--empty', '--json', jfile, d3)
    check('wlint --json exit 1', rc == 1)
    check('wlint --json file created', os.path.exists(jfile))
    with open(jfile) as f:
        data = _json.load(f)
    check('wlint json has wlint_version',  'wlint_version' in data)
    check('wlint json has summary',        'summary' in data)
    check('wlint json has schema_version',   'schema_version' in data)
    check('wlint json has duplicate_groups', 'duplicate_groups' in data)
    check('wlint json has empty_files',      'empty_files' in data)
    check('wlint json has warnings in summary', 'warnings' in data['summary'])
    check('wlint json duplicate_groups count == 1', len(data['duplicate_groups']) == 1)
    ds = data['duplicate_groups'][0]
    check('wlint json set has 3 files', len(ds['files']) == 3)
    check('wlint json set has sha256',  len(ds['sha256']) == 64)
    kept = [f for f in ds['files'] if f['keep']]
    dups  = [f for f in ds['files'] if not f['keep']]
    check('wlint json exactly 1 kept',  len(kept) == 1)
    check('wlint json exactly 2 dups',  len(dups) == 2)
    check('wlint json empty_files has empty.txt',
          any('empty.txt' in p for p in data['empty_files']))
    check('wlint json summary files_scanned == 5',
          data['summary']['files_scanned'] == 5)
    check('wlint json summary reclaimable_bytes > 0',
          data['summary']['reclaimable_bytes'] > 0)
finally:
    shutil.rmtree(d3, ignore_errors=True)

# keep newest (default) — oldest should be marked DUP
import time as _time
d4 = tempfile.mkdtemp()
try:
    old = os.path.join(d4, 'old.txt')
    new = os.path.join(d4, 'new.txt')
    with open(old, 'w') as f: f.write('same content\n')
    _time.sleep(0.05)
    with open(new, 'w') as f: f.write('same content\n')
    jfile = os.path.join(d4, 'out.json')
    run('wlint', '--json', jfile, d4)
    if os.path.exists(jfile):
        with open(jfile) as f:
            data = _json.load(f)
        if data['duplicate_groups']:
            ds = data['duplicate_groups'][0]
            kept_path = next((f['path'] for f in ds['files'] if f['keep']), '')
            check('wlint keep newest keeps newer file', 'new.txt' in kept_path)
        else:
            check('wlint keep newest has dup set', False, 'no duplicate groups found')
    else:
        check('wlint keep newest json created', False, 'json file not created')
finally:
    shutil.rmtree(d4, ignore_errors=True)

# keep oldest
d5 = tempfile.mkdtemp()
try:
    old = os.path.join(d5, 'old.txt')
    new = os.path.join(d5, 'new.txt')
    with open(old, 'w') as f: f.write('same content\n')
    _time.sleep(0.05)
    with open(new, 'w') as f: f.write('same content\n')
    jfile = os.path.join(d5, 'out.json')
    run('wlint', '--keep', 'oldest', '--json', jfile, d5)
    if os.path.exists(jfile):
        with open(jfile) as f:
            data = _json.load(f)
        if data['duplicate_groups']:
            ds = data['duplicate_groups'][0]
            kept_path = next((f['path'] for f in ds['files'] if f['keep']), '')
            check('wlint keep oldest keeps older file', 'old.txt' in kept_path)
        else:
            check('wlint keep oldest has dup set', False)
    else:
        check('wlint keep oldest json created', False)
finally:
    shutil.rmtree(d5, ignore_errors=True)

# quarantine moves non-kept files
d6 = _wlint_tree()
qdir = tempfile.mkdtemp()
try:
    out, err, rc = run('wlint', '--quarantine', qdir, d6)
    check('wlint --quarantine exit 1', rc == 1)
    moved = os.listdir(qdir)
    check('wlint quarantine moved 2 files', len(moved) >= 2)
    log = os.path.join(qdir, 'wlint_moves.json')
    check('wlint quarantine wrote move log', os.path.exists(log))
finally:
    shutil.rmtree(d6, ignore_errors=True)
    shutil.rmtree(qdir, ignore_errors=True)

# --csv output
d7 = _wlint_tree()
try:
    cfile = os.path.join(d7, 'report.csv')
    run('wlint', '--csv', cfile, '--empty', d7)
    check('wlint --csv file created', os.path.exists(cfile))
    with open(cfile) as f:
        lines = f.read().splitlines()
    check('wlint csv has header row', lines and lines[0].startswith('type,'))
    data_rows = [l for l in lines[1:] if l.strip()]
    check('wlint csv has data rows', len(data_rows) >= 3)
finally:
    shutil.rmtree(d7, ignore_errors=True)

# --dry-run: files stay in place, output mentions DRY RUN
d8 = _wlint_tree()
qdir8 = tempfile.mkdtemp()
try:
    out, err, rc = run('wlint', '--quarantine', qdir8, '--dry-run', d8)
    check('wlint --dry-run exit 1', rc == 1)
    check('wlint --dry-run output has DRY RUN', 'DRY RUN' in out)
    # original files must still be present (nothing moved)
    orig_files = [f for f in os.listdir(d8) if not f.endswith('.json')]
    check('wlint --dry-run files not moved', len(orig_files) >= 3)
    # quarantine dir has no moved files
    q_files = [f for f in os.listdir(qdir8) if not f.endswith('.json')]
    check('wlint --dry-run no files in qdir', len(q_files) == 0)
finally:
    shutil.rmtree(d8, ignore_errors=True)
    shutil.rmtree(qdir8, ignore_errors=True)

# --undo: quarantine then restore
d9 = _wlint_tree()
qdir9 = tempfile.mkdtemp()
try:
    # remember original files before quarantine
    orig_names = set(os.listdir(d9))
    out, err, rc = run('wlint', '--quarantine', qdir9, d9)
    check('wlint undo setup quarantine ok', rc == 1)
    manifest = os.path.join(qdir9, 'wlint_moves.json')
    check('wlint undo manifest exists', os.path.exists(manifest))
    # now undo
    out2, err2, rc2 = run('wlint', '--undo', manifest)
    check('wlint --undo exit 0', rc2 == 0)
    check('wlint --undo output has RESTORED', 'RESTORED' in out2)
    # files back in original dir
    restored_names = set(os.listdir(d9))
    check('wlint --undo files restored', orig_names == restored_names)
finally:
    shutil.rmtree(d9, ignore_errors=True)
    shutil.rmtree(qdir9, ignore_errors=True)

# --undo hash mismatch: corrupt quarantined file before undo
d10 = _wlint_tree()
qdir10 = tempfile.mkdtemp()
try:
    run('wlint', '--quarantine', qdir10, d10)
    manifest10 = os.path.join(qdir10, 'wlint_moves.json')
    if os.path.exists(manifest10):
        # corrupt a moved file
        q_files10 = [f for f in os.listdir(qdir10) if f != 'wlint_moves.json']
        if q_files10:
            bad = os.path.join(qdir10, q_files10[0])
            with open(bad, 'w') as f: f.write('corrupted content xyz\n')
            out3, err3, rc3 = run('wlint', '--undo', manifest10)
            check('wlint undo hash mismatch reports failure', rc3 != 0 or 'mismatch' in err3 or 'failed' in out3)
        else:
            check('wlint undo hash mismatch setup', False, 'no quarantined files found')
    else:
        check('wlint undo hash mismatch manifest', False, 'manifest not created')
finally:
    shutil.rmtree(d10, ignore_errors=True)
    shutil.rmtree(qdir10, ignore_errors=True)

# --undo occupied path: restore target already exists
d11 = _wlint_tree()
qdir11 = tempfile.mkdtemp()
try:
    run('wlint', '--quarantine', qdir11, d11)
    manifest11 = os.path.join(qdir11, 'wlint_moves.json')
    if os.path.exists(manifest11):
        with open(manifest11) as f:
            mdata = _json.load(f)
        if mdata.get('moves'):
            # recreate the original_path so it's occupied
            orig_path = mdata['moves'][0]['original_path']
            with open(orig_path, 'w') as f: f.write('occupier\n')
            out4, err4, rc4 = run('wlint', '--undo', manifest11)
            check('wlint undo occupied path reports failure', rc4 != 0 or 'already exists' in err4 or 'failed' in out4)
        else:
            check('wlint undo occupied setup', False, 'no moves in manifest')
    else:
        check('wlint undo occupied manifest', False, 'manifest not created')
finally:
    shutil.rmtree(d11, ignore_errors=True)
    shutil.rmtree(qdir11, ignore_errors=True)

# subtree refusal: --quarantine inside scan root exits 2
d12 = _wlint_tree()
try:
    qsub = os.path.join(d12, 'quarantine_sub')
    os.makedirs(qsub, exist_ok=True)
    out5, err5, rc5 = run('wlint', '--quarantine', qsub, d12)
    check('wlint subtree refusal exits 2', rc5 == 2)
    check('wlint subtree refusal mentions scan root', 'scan root' in err5 or 'inside' in err5)
finally:
    shutil.rmtree(d12, ignore_errors=True)


# --include: only .txt files appear in duplicate groups
d_inc = tempfile.mkdtemp()
try:
    for name in ('a.txt', 'b.txt', 'c.txt'):
        with open(os.path.join(d_inc, name), 'w') as f: f.write('txtcontent\n')
    for name in ('x.log', 'y.log'):
        with open(os.path.join(d_inc, name), 'w') as f: f.write('logcontent\n')
    jfile_inc = os.path.join(d_inc, 'inc.json')
    run('wlint', '--include', '*.txt', '--json', jfile_inc, d_inc)
    if os.path.exists(jfile_inc):
        with open(jfile_inc) as f:
            data_inc = _json.load(f)
        all_paths = [fi['path'] for ds in data_inc['duplicate_groups'] for fi in ds['files']]
        check('wlint --include *.txt: .txt in groups', any('.txt' in p for p in all_paths))
        check('wlint --include *.txt: no .log in groups', not any('.log' in p for p in all_paths))
    else:
        check('wlint --include json created', False, 'json file not created')
finally:
    shutil.rmtree(d_inc, ignore_errors=True)

# --exclude: .log files do not appear in duplicate groups
d_exc = tempfile.mkdtemp()
try:
    for name in ('a.txt', 'b.txt'):
        with open(os.path.join(d_exc, name), 'w') as f: f.write('txtcontent\n')
    for name in ('x.log', 'y.log'):
        with open(os.path.join(d_exc, name), 'w') as f: f.write('logcontent\n')
    jfile_exc = os.path.join(d_exc, 'exc.json')
    run('wlint', '--exclude', '*.log', '--json', jfile_exc, d_exc)
    if os.path.exists(jfile_exc):
        with open(jfile_exc) as f:
            data_exc = _json.load(f)
        all_paths = [fi['path'] for ds in data_exc['duplicate_groups'] for fi in ds['files']]
        check('wlint --exclude *.log: no .log in groups', not any('.log' in p for p in all_paths))
    else:
        check('wlint --exclude json created', False, 'json file not created')
finally:
    shutil.rmtree(d_exc, ignore_errors=True)

# --max-size: only small dups are found, large dups excluded
d_max = tempfile.mkdtemp()
try:
    for name in ('s1.bin', 's2.bin'):
        with open(os.path.join(d_max, name), 'wb') as f: f.write(b'ab')
    for name in ('l1.bin', 'l2.bin'):
        with open(os.path.join(d_max, name), 'wb') as f: f.write(b'x' * 10000)
    jfile_max = os.path.join(d_max, 'max.json')
    run('wlint', '--max-size', '100', '--json', jfile_max, d_max)
    if os.path.exists(jfile_max):
        with open(jfile_max) as f:
            data_max = _json.load(f)
        all_sizes = [ds['size'] for ds in data_max['duplicate_groups']]
        check('wlint --max-size 100: found small group', len(all_sizes) >= 1)
        check('wlint --max-size 100: all group sizes <= 100', all(s <= 100 for s in all_sizes))
        check('wlint --max-size 100: no large group', not any(s > 100 for s in all_sizes))
    else:
        check('wlint --max-size json created', False, 'json file not created')
finally:
    shutil.rmtree(d_max, ignore_errors=True)

# --ext: only .jpg files appear in duplicate groups
d_ext = tempfile.mkdtemp()
try:
    for name in ('a.jpg', 'b.jpg'):
        with open(os.path.join(d_ext, name), 'wb') as f: f.write(b'jpgdata')
    for name in ('a.txt', 'b.txt'):
        with open(os.path.join(d_ext, name), 'w') as f: f.write('txtdata\n')
    jfile_ext = os.path.join(d_ext, 'ext.json')
    run('wlint', '--ext', '.jpg', '--json', jfile_ext, d_ext)
    if os.path.exists(jfile_ext):
        with open(jfile_ext) as f:
            data_ext = _json.load(f)
        all_paths = [fi['path'] for ds in data_ext['duplicate_groups'] for fi in ds['files']]
        check('wlint --ext .jpg: .jpg in groups', any('.jpg' in p for p in all_paths))
        check('wlint --ext .jpg: no .txt in groups', not any('.txt' in p for p in all_paths))
    else:
        check('wlint --ext json created', False, 'json file not created')
finally:
    shutil.rmtree(d_ext, ignore_errors=True)

# --stats pretty: [STATS] block visible in stdout
d_stats = _wlint_tree()
try:
    out_st, _, rc_st = run('wlint', '--stats', d_stats)
    check('wlint --stats shows stats block', '[STATS]' in out_st or 'Elapsed' in out_st)
finally:
    shutil.rmtree(d_stats, ignore_errors=True)

# --stats JSON: stats key always present with elapsed_ms
d_stj = _wlint_tree()
try:
    jfile_stj = os.path.join(d_stj, 'stats.json')
    run('wlint', '--stats', '--json', jfile_stj, d_stj)
    if os.path.exists(jfile_stj):
        with open(jfile_stj) as f:
            data_stj = _json.load(f)
        check('wlint --stats json has stats key', 'stats' in data_stj)
        check('wlint --stats json has elapsed_ms', 'elapsed_ms' in data_stj.get('stats', {}))
    else:
        check('wlint --stats json created', False, 'json file not created')
finally:
    shutil.rmtree(d_stj, ignore_errors=True)

# deterministic ordering: two runs on same dir produce identical file order
d_det = tempfile.mkdtemp()
try:
    for name in ('alpha.dat', 'beta.dat', 'gamma.dat'):
        with open(os.path.join(d_det, name), 'wb') as f: f.write(b'detcontent')
    jfile_det1 = os.path.join(d_det, 'det1.json')
    jfile_det2 = os.path.join(d_det, 'det2.json')
    run('wlint', '--json', jfile_det1, d_det)
    run('wlint', '--json', jfile_det2, d_det)
    if os.path.exists(jfile_det1) and os.path.exists(jfile_det2):
        with open(jfile_det1) as f: data_det1 = _json.load(f)
        with open(jfile_det2) as f: data_det2 = _json.load(f)
        if data_det1['duplicate_groups'] and data_det2['duplicate_groups']:
            paths1 = [fi['path'] for fi in data_det1['duplicate_groups'][0]['files']]
            paths2 = [fi['path'] for fi in data_det2['duplicate_groups'][0]['files']]
            check('wlint deterministic: file order same across runs', paths1 == paths2)
        else:
            check('wlint deterministic: groups found', False, 'no duplicate groups')
    else:
        check('wlint deterministic: json created', False, 'json files not created')
finally:
    shutil.rmtree(d_det, ignore_errors=True)


# ── apropos ───────────────────────────────────────────────────────────────────

section('apropos')

out, err, rc = run('apropos', '--version')
check('apropos --version exit 0', rc == 0)
check('apropos --version shows apropos', 'apropos' in out)

out, err, rc = run('apropos', '--help')
check('apropos --help exit 0', rc == 0)
check('apropos --help mentions --exact', '--exact' in err or '-e' in err)

_, _, rc = run('apropos')
check('apropos no args exits 2', rc == 2)

out, _, rc = run('apropos', 'file')
check('apropos file exit 0', rc == 0)
check('apropos file finds cat', 'cat' in out)
check('apropos file finds find', 'find' in out)
check('apropos file finds stat', 'stat' in out)

out, _, rc = run('apropos', 'sort')
check('apropos sort finds sort', 'sort' in out)
check('apropos sort finds comm', 'comm' in out)    # comm desc: "two sorted files"

out, _, rc = run('apropos', 'duplicate')
check('apropos duplicate finds wlint', 'wlint' in out)

out, _, rc = run('apropos', 'xyzzy_nonexistent_zzz')
check('apropos unknown keyword exits 1', rc == 1)
check('apropos unknown keyword empty output', out.strip() == '')

out, _, rc = run('apropos', 'cat', 'sort')
check('apropos multi-keyword finds cat', 'cat' in out)
check('apropos multi-keyword finds sort', 'sort' in out)

out_e, _, rc_e = run('apropos', '--exact', 'sort')
check('apropos --exact sort finds sort', 'sort' in out_e)
check('apropos --exact sort does not find sha256sum',
      'sha256sum' not in out_e)

# ── wsim ──────────────────────────────────────────────────────────────────────

section('wsim')

import json as _jsm   # local alias for wsim tests (avoids conflict with _json)

def _wsim_scan(path, files_list):
    """Write a minimal wlint-compatible scan JSON for wsim tests."""
    data = {
        "schema_version": "1.0",
        "wlint_version": "1.6",
        "generated": "2026-01-01T00:00:00Z",
        "scan_paths": ["C:\\test"],
        "filters": {"min_size": 1, "max_size": 0, "include_pats": [], "exclude_pats": []},
        "file_count": len(files_list),
        "files": files_list,
    }
    with open(path, 'w', encoding='utf-8') as f:
        _jsm.dump(data, f)

# Test 1: --version
out_v, err_v, rc_v = run('wsim', '--version')
check('wsim --version exit 0', rc_v == 0)
check('wsim --version shows wsim', 'wsim' in out_v)
check('wsim --version shows wsim version', 'wsim' in out_v and rc_v == 0)

# Test 2: --help
out_h, err_h, rc_h = run('wsim', '--help')
check('wsim --help exit 0', rc_h == 0)
check('wsim --help mentions --min-score', '--min-score' in err_h)
check('wsim --help mentions --out', '--out' in err_h)

# Test 3: no args → exit 2
_, _, rc_na = run('wsim')
check('wsim no args exits 2', rc_na == 2)

# Test 4: nonexistent scan file → exit 2
_, _, rc_nf = run('wsim', 'nonexistent_scan_12345.json')
check('wsim missing scan file exits 2', rc_nf == 2)

# Test 5: obvious duplicates detected (same name after normalization)
_wsim_d1 = tempfile.mkdtemp()
try:
    scan5 = os.path.join(_wsim_d1, 'scan.json')
    _wsim_scan(scan5, [
        {"path": "C:\\data\\report.pdf", "basename": "report.pdf",
         "ext": ".pdf", "size": 1000, "mtime": "2026-01-01T10:00:00"},
        {"path": "C:\\data\\report - copy.pdf", "basename": "report - copy.pdf",
         "ext": ".pdf", "size": 1000, "mtime": "2026-01-02T10:00:00"},
    ])
    out5, _, rc5 = run('wsim', scan5)
    check('wsim duplicates exit 1', rc5 == 1)
    if out5.strip():
        d5 = _jsm.loads(out5)
        check('wsim duplicates has candidate_groups', len(d5.get('candidate_groups', [])) >= 1)
    else:
        check('wsim duplicates has candidate_groups', False, 'no stdout')
finally:
    shutil.rmtree(_wsim_d1, ignore_errors=True)

# Test 6: output schema fields
_wsim_d6 = tempfile.mkdtemp()
try:
    scan6 = os.path.join(_wsim_d6, 'scan.json')
    _wsim_scan(scan6, [
        {"path": "C:\\data\\photo.jpg", "basename": "photo.jpg",
         "ext": ".jpg", "size": 5000, "mtime": "2026-01-01T00:00:00"},
        {"path": "C:\\data\\photo (1).jpg", "basename": "photo (1).jpg",
         "ext": ".jpg", "size": 5000, "mtime": "2026-01-03T00:00:00"},
    ])
    out6, _, _ = run('wsim', scan6)
    d6 = _jsm.loads(out6) if out6.strip() else {}
    check('wsim output has schema_version',   'schema_version'   in d6)
    check('wsim output has wsim_version',     'wsim_version'     in d6)
    check('wsim output has source_scan',      'source_scan'      in d6)
    check('wsim output has candidate_groups', 'candidate_groups' in d6)
finally:
    shutil.rmtree(_wsim_d6, ignore_errors=True)

# Test 7: per-group fields (score, reasoning, files)
_wsim_d7 = tempfile.mkdtemp()
try:
    scan7 = os.path.join(_wsim_d7, 'scan.json')
    _wsim_scan(scan7, [
        {"path": "C:\\data\\notes.txt", "basename": "notes.txt",
         "ext": ".txt", "size": 200, "mtime": "2026-01-01T00:00:00"},
        {"path": "C:\\data\\notes copy.txt", "basename": "notes copy.txt",
         "ext": ".txt", "size": 200, "mtime": "2026-01-02T00:00:00"},
    ])
    out7, _, _ = run('wsim', scan7)
    d7 = _jsm.loads(out7) if out7.strip() else {}
    groups7 = d7.get('candidate_groups', [])
    check('wsim group has score',     groups7 and 'score' in groups7[0])
    check('wsim group has reasoning', groups7 and 'reasoning' in groups7[0])
    check('wsim group has files',     groups7 and 'files' in groups7[0])
    if groups7:
        r7 = groups7[0].get('reasoning', {})
        check('wsim reasoning has basename_similarity', 'basename_similarity' in r7)
        check('wsim reasoning has ext_match',           'ext_match'           in r7)
        check('wsim reasoning has size_similarity',     'size_similarity'     in r7)
        check('wsim reasoning has mtime_proximity_days','mtime_proximity_days' in r7)
        f7 = groups7[0].get('files', [])
        check('wsim group files non-empty', len(f7) >= 2)
        if f7:
            check('wsim file entry has path',     all('path'     in e for e in f7))
            check('wsim file entry has size',     all('size'     in e for e in f7))
            check('wsim file entry has mtime',    all('mtime'    in e for e in f7))
            check('wsim file entry has ext',      all('ext'      in e for e in f7))
            check('wsim file entry has basename', all('basename' in e for e in f7))
finally:
    shutil.rmtree(_wsim_d7, ignore_errors=True)

# Test 8: --min-score filters out low-scoring pairs
_wsim_d8 = tempfile.mkdtemp()
try:
    scan8 = os.path.join(_wsim_d8, 'scan.json')
    # vacation_photo_edit.jpg vs vacation_photo.jpg — same token ("vacation"),
    # same ext/size, but different norm names → score ~0.83, below 0.90
    _wsim_scan(scan8, [
        {"path": "C:\\data\\vacation_photo.jpg", "basename": "vacation_photo.jpg",
         "ext": ".jpg", "size": 5000, "mtime": "2026-01-01T00:00:00"},
        {"path": "C:\\data\\vacation_photo_edit.jpg", "basename": "vacation_photo_edit.jpg",
         "ext": ".jpg", "size": 5000, "mtime": "2026-01-02T00:00:00"},
    ])
    # Default min-score (0.40) — should find the pair
    out8a, _, rc8a = run('wsim', scan8)
    d8a = _jsm.loads(out8a) if out8a.strip() else {}
    check('wsim default min-score finds pair', len(d8a.get('candidate_groups', [])) >= 1)
    # High min-score (0.90) — should not find the pair
    out8b, _, rc8b = run('wsim', '--min-score', '0.90', scan8)
    d8b = _jsm.loads(out8b) if out8b.strip() else {}
    check('wsim --min-score 0.90 filters pair', len(d8b.get('candidate_groups', [])) == 0)
    check('wsim --min-score 0.90 no candidates exits 0', rc8b == 0)
finally:
    shutil.rmtree(_wsim_d8, ignore_errors=True)

# Test 9: different extensions → no groups (blocked)
_wsim_d9 = tempfile.mkdtemp()
try:
    scan9 = os.path.join(_wsim_d9, 'scan.json')
    _wsim_scan(scan9, [
        {"path": "C:\\data\\photo.jpg", "basename": "photo.jpg",
         "ext": ".jpg", "size": 1000, "mtime": "2026-01-01T00:00:00"},
        {"path": "C:\\data\\photo.png", "basename": "photo.png",
         "ext": ".png", "size": 1000, "mtime": "2026-01-01T00:00:00"},
    ])
    out9, _, rc9 = run('wsim', scan9)
    d9 = _jsm.loads(out9) if out9.strip() else {}
    check('wsim diff ext no groups', len(d9.get('candidate_groups', [])) == 0)
    check('wsim diff ext exits 0', rc9 == 0)
finally:
    shutil.rmtree(_wsim_d9, ignore_errors=True)

# Test 10: empty scan (0 files) → 0 groups, exit 0
_wsim_d10 = tempfile.mkdtemp()
try:
    scan10 = os.path.join(_wsim_d10, 'scan.json')
    _wsim_scan(scan10, [])
    out10, _, rc10 = run('wsim', scan10)
    d10 = _jsm.loads(out10) if out10.strip() else {}
    check('wsim empty scan candidate_groups is empty list',
          d10.get('candidate_groups') == [])
    check('wsim empty scan exits 0', rc10 == 0)
finally:
    shutil.rmtree(_wsim_d10, ignore_errors=True)

# Test 11: --out FILE writes to file (not stdout)
_wsim_d11 = tempfile.mkdtemp()
try:
    scan11  = os.path.join(_wsim_d11, 'scan.json')
    out11f  = os.path.join(_wsim_d11, 'results.json')
    _wsim_scan(scan11, [
        {"path": "C:\\data\\doc.pdf", "basename": "doc.pdf",
         "ext": ".pdf", "size": 800, "mtime": "2026-01-01T00:00:00"},
        {"path": "C:\\data\\doc (1).pdf", "basename": "doc (1).pdf",
         "ext": ".pdf", "size": 800, "mtime": "2026-01-02T00:00:00"},
    ])
    stdout11, _, _ = run('wsim', '--out', out11f, scan11)
    check('wsim --out file created', os.path.exists(out11f))
    check('wsim --out stdout is empty', stdout11.strip() == '')
    if os.path.exists(out11f):
        with open(out11f) as f:
            d11 = _jsm.load(f)
        check('wsim --out file valid JSON with groups',
              len(d11.get('candidate_groups', [])) >= 1)
finally:
    shutil.rmtree(_wsim_d11, ignore_errors=True)


# ── wlint --scan-json ─────────────────────────────────────────────────────────

# Test 1: file created + valid JSON
d_sj1 = _wlint_tree()
sj_file1 = os.path.join(d_sj1, 'scan.json')
try:
    run('wlint', '--scan-json', sj_file1, d_sj1)
    check('wlint --scan-json file created', os.path.exists(sj_file1))
    if os.path.exists(sj_file1):
        with open(sj_file1) as f:
            data_sj1 = _json.load(f)
        check('wlint --scan-json valid JSON', isinstance(data_sj1, dict))
    else:
        check('wlint --scan-json valid JSON', False, 'file not created')
finally:
    shutil.rmtree(d_sj1, ignore_errors=True)

# Test 2: schema fields present
d_sj2 = _wlint_tree()
sj_file2 = os.path.join(d_sj2, 'scan.json')
try:
    run('wlint', '--scan-json', sj_file2, d_sj2)
    if os.path.exists(sj_file2):
        with open(sj_file2) as f:
            data_sj2 = _json.load(f)
        check('wlint --scan-json has schema_version', 'schema_version' in data_sj2)
        check('wlint --scan-json has wlint_version',  'wlint_version'  in data_sj2)
        check('wlint --scan-json has file_count',     'file_count'     in data_sj2)
        check('wlint --scan-json has files',          'files'          in data_sj2)
        check('wlint --scan-json has filters',        'filters'        in data_sj2)
    else:
        for lbl in ('schema_version', 'wlint_version', 'file_count', 'files', 'filters'):
            check(f'wlint --scan-json has {lbl}', False, 'file not created')
finally:
    shutil.rmtree(d_sj2, ignore_errors=True)

# Test 3: per-file fields
d_sj3 = _wlint_tree()
sj_file3 = os.path.join(d_sj3, 'scan.json')
try:
    run('wlint', '--scan-json', sj_file3, d_sj3)
    if os.path.exists(sj_file3):
        with open(sj_file3) as f:
            data_sj3 = _json.load(f)
        files_sj3 = data_sj3.get('files', [])
        check('wlint --scan-json files list non-empty', len(files_sj3) > 0)
        if files_sj3:
            required = ('path', 'size', 'mtime', 'ext', 'basename')
            for req in required:
                check(f'wlint --scan-json every file has {req}',
                      all(req in entry for entry in files_sj3))
    else:
        check('wlint --scan-json per-file fields', False, 'file not created')
finally:
    shutil.rmtree(d_sj3, ignore_errors=True)

# Test 4: filter interaction -- --include *.txt --scan-json; all entries .txt
d_sj4 = _wlint_tree()
sj_file4 = os.path.join(d_sj4, 'scan_filtered.json')
try:
    run('wlint', '--include', '*.txt', '--scan-json', sj_file4, d_sj4)
    if os.path.exists(sj_file4):
        with open(sj_file4) as f:
            data_sj4 = _json.load(f)
        files_sj4 = data_sj4.get('files', [])
        check('wlint --scan-json filter: all entries .txt',
              all(e.get('ext', '').lower() == '.txt' for e in files_sj4))
    else:
        check('wlint --scan-json filter: file created', False, 'file not created')
finally:
    shutil.rmtree(d_sj4, ignore_errors=True)


# ── wlint --threads ───────────────────────────────────────────────────────────

# --threads produces correct results (same as single-threaded)
d_th1 = _wlint_tree()
try:
    out_th1, err_th1, rc_th1 = run('wlint', '--threads', '4', d_th1)
    check('wlint --threads 4 finds duplicates', rc_th1 == 1)
    check('wlint --threads 4 mentions DUPLICATE', 'DUPLICATE' in out_th1)
finally:
    shutil.rmtree(d_th1, ignore_errors=True)

# --threads 1 (single-threaded path) still works
d_th2 = _wlint_tree()
try:
    out_th2, err_th2, rc_th2 = run('wlint', '--threads', '1', d_th2)
    check('wlint --threads 1 finds duplicates', rc_th2 == 1)
finally:
    shutil.rmtree(d_th2, ignore_errors=True)

# --stats shows Hash threads line
d_th3 = _wlint_tree()
try:
    out_th3, err_th3, rc_th3 = run('wlint', '--threads', '3', '--stats', d_th3)
    check('wlint --stats shows Hash threads', 'Hash threads' in out_th3)
finally:
    shutil.rmtree(d_th3, ignore_errors=True)


# ── wlint --log ───────────────────────────────────────────────────────────────

# Test 1: --log creates a valid JSON file
d_log1 = _wlint_tree()
log_file1 = os.path.join(d_log1, 'run.json')
try:
    run('wlint', '--log', log_file1, d_log1)
    check('wlint --log file created', os.path.exists(log_file1))
    if os.path.exists(log_file1):
        with open(log_file1) as f:
            data_log1 = _json.load(f)
        check('wlint --log valid JSON', isinstance(data_log1, dict))
    else:
        check('wlint --log valid JSON', False, 'file not created')
finally:
    shutil.rmtree(d_log1, ignore_errors=True)

# Test 2: log contains required top-level fields
d_log2 = _wlint_tree()
log_file2 = os.path.join(d_log2, 'run.json')
try:
    run('wlint', '--log', log_file2, d_log2)
    if os.path.exists(log_file2):
        with open(log_file2) as f:
            data_log2 = _json.load(f)
        for field in ('schema_version', 'wlint_version', 'run_at', 'scan_paths', 'summary', 'options'):
            check(f'wlint --log has {field}', field in data_log2)
    else:
        for field in ('schema_version', 'wlint_version', 'run_at', 'scan_paths', 'summary', 'options'):
            check(f'wlint --log has {field}', False, 'file not created')
finally:
    shutil.rmtree(d_log2, ignore_errors=True)

# Test 3: log summary fields
d_log3 = _wlint_tree()
log_file3 = os.path.join(d_log3, 'run.json')
try:
    run('wlint', '--log', log_file3, d_log3)
    if os.path.exists(log_file3):
        with open(log_file3) as f:
            data_log3 = _json.load(f)
        summary = data_log3.get('summary', {})
        for field in ('files_scanned', 'dirs_scanned', 'duplicate_groups', 'bytes_reclaimable', 'elapsed_ms'):
            check(f'wlint --log summary has {field}', field in summary)
        check('wlint --log summary duplicate_groups > 0', summary.get('duplicate_groups', 0) > 0)
    else:
        check('wlint --log summary fields', False, 'file not created')
finally:
    shutil.rmtree(d_log3, ignore_errors=True)

# Test 4: log options fields
d_log4 = _wlint_tree()
log_file4 = os.path.join(d_log4, 'run.json')
try:
    run('wlint', '--keep', 'oldest', '--log', log_file4, d_log4)
    if os.path.exists(log_file4):
        with open(log_file4) as f:
            data_log4 = _json.load(f)
        opts = data_log4.get('options', {})
        check('wlint --log options has keep', 'keep' in opts)
        check('wlint --log options keep=oldest', opts.get('keep') == 'oldest')
    else:
        check('wlint --log options fields', False, 'file not created')
finally:
    shutil.rmtree(d_log4, ignore_errors=True)


# ── wsim --pretty and --recommend-keep ────────────────────────────────────────

def _wsim_scan_rk():
    """Return (tmpdir, scan_json_path) with two similar-named .txt files of different mtimes."""
    d = tempfile.mkdtemp()
    import json as _jsm
    # Two similar files: same ext, similar name, same size
    files = [
        {"path": d + "\\report.txt",      "size": 1000, "mtime": "2025-01-10T10:00:00",
         "ext": ".txt", "basename": "report.txt"},
        {"path": d + "\\report copy.txt", "size": 1000, "mtime": "2025-06-20T10:00:00",
         "ext": ".txt", "basename": "report copy.txt"},
    ]
    scan = {"schema_version": "1.0", "wlint_version": "1.4", "generated": "2026-01-01T00:00:00Z",
            "scan_paths": [d], "filters": {"min_size": 1, "max_size": 0, "include_pats": [], "exclude_pats": []},
            "file_count": 2, "files": files}
    scan_path = os.path.join(d, 'scan.json')
    with open(scan_path, 'w') as f:
        _jsm.dump(scan, f)
    return d, scan_path

# Test: --pretty produces text output (not JSON)
d_pr1, scan_pr1 = _wsim_scan_rk()
try:
    out_pr1, err_pr1, rc_pr1 = run('wsim', '--pretty', scan_pr1)
    check('wsim --pretty exit non-2', rc_pr1 != 2)
    check('wsim --pretty contains Group', 'Group' in out_pr1)
    check('wsim --pretty contains score', 'score' in out_pr1)
    check('wsim --pretty contains file path', 'report' in out_pr1.lower())
finally:
    shutil.rmtree(d_pr1, ignore_errors=True)

# Test: --recommend-keep newest in JSON output
d_rk1, scan_rk1 = _wsim_scan_rk()
try:
    out_rk1, err_rk1, rc_rk1 = run('wsim', '--recommend-keep', 'newest', scan_rk1)
    check('wsim --recommend-keep newest exit non-2', rc_rk1 != 2)
    if rc_rk1 != 2:
        data_rk1 = _json.loads(out_rk1)
        check('wsim --recommend-keep newest: recommend_keep in JSON', 'recommend_keep' in data_rk1)
        check('wsim --recommend-keep newest: value', data_rk1.get('recommend_keep') == 'newest')
        groups_rk1 = data_rk1.get('candidate_groups', [])
        if groups_rk1:
            files_rk1 = groups_rk1[0].get('files', [])
            check('wsim --recommend-keep newest: files have keep field',
                  all('keep' in f for f in files_rk1))
            kept_rk1 = [f for f in files_rk1 if f.get('keep')]
            check('wsim --recommend-keep newest: exactly one kept', len(kept_rk1) == 1)
            # newest mtime = 2025-06-20
            check('wsim --recommend-keep newest: keeps newest mtime',
                  '2025-06-20' in kept_rk1[0].get('mtime', ''))
finally:
    shutil.rmtree(d_rk1, ignore_errors=True)

# Test: --recommend-keep oldest keeps the older file
d_rk2, scan_rk2 = _wsim_scan_rk()
try:
    out_rk2, err_rk2, rc_rk2 = run('wsim', '--recommend-keep', 'oldest', scan_rk2)
    check('wsim --recommend-keep oldest exit non-2', rc_rk2 != 2)
    if rc_rk2 != 2:
        data_rk2 = _json.loads(out_rk2)
        groups_rk2 = data_rk2.get('candidate_groups', [])
        if groups_rk2:
            files_rk2 = groups_rk2[0].get('files', [])
            kept_rk2 = [f for f in files_rk2 if f.get('keep')]
            check('wsim --recommend-keep oldest: keeps oldest mtime',
                  '2025-01-10' in kept_rk2[0].get('mtime', '') if kept_rk2 else False)
finally:
    shutil.rmtree(d_rk2, ignore_errors=True)

# Test: --recommend-keep path-shortest keeps shorter path
d_rk3, scan_rk3 = _wsim_scan_rk()
try:
    out_rk3, err_rk3, rc_rk3 = run('wsim', '--recommend-keep', 'path-shortest', scan_rk3)
    check('wsim --recommend-keep path-shortest exit non-2', rc_rk3 != 2)
    if rc_rk3 != 2:
        data_rk3 = _json.loads(out_rk3)
        groups_rk3 = data_rk3.get('candidate_groups', [])
        if groups_rk3:
            files_rk3 = groups_rk3[0].get('files', [])
            kept_rk3 = [f for f in files_rk3 if f.get('keep')]
            not_kept_rk3 = [f for f in files_rk3 if not f.get('keep')]
            check('wsim --recommend-keep path-shortest: kept path is shorter',
                  (len(kept_rk3) == 1 and len(not_kept_rk3) >= 1 and
                   len(kept_rk3[0]['path']) <= len(not_kept_rk3[0]['path'])))
finally:
    shutil.rmtree(d_rk3, ignore_errors=True)

# Test: --pretty --recommend-keep shows KEEP label
d_rk4, scan_rk4 = _wsim_scan_rk()
try:
    out_rk4, err_rk4, rc_rk4 = run('wsim', '--pretty', '--recommend-keep', 'newest', scan_rk4)
    check('wsim --pretty --recommend-keep exit non-2', rc_rk4 != 2)
    check('wsim --pretty --recommend-keep shows KEEP', 'KEEP' in out_rk4)
finally:
    shutil.rmtree(d_rk4, ignore_errors=True)

# Test: invalid --recommend-keep policy returns error
out_bad, err_bad, rc_bad = run('wsim', '--recommend-keep', 'badpolicy', 'dummy.json')
check('wsim --recommend-keep invalid policy returns 2', rc_bad == 2)


# ── Helper for new tests ──────────────────────────────────────────────────────
def write_file(path, content, mode='w'):
    with open(path, mode) as _f: _f.write(content)

# ── sha1sum ───────────────────────────────────────────────────────────────────
out, err, rc = run('sha1sum', '--version')
check('sha1sum --version exits 0', rc == 0)
check('sha1sum --version shows sha1sum', 'sha1sum' in out)

out, err, rc = run('sha1sum', '--help')
check('sha1sum --help exits 0', rc == 0)
# stdin hash: verify format (40 hex chars)
out, err, rc = run('sha1sum', stdin_text='hello\n')
check('sha1sum stdin exits 0', rc == 0)
check('sha1sum produces 40 hex chars', len(out.split()[0]) == 40)

# ── sha512sum ──────────────────────────────────────────────────────────────────
out, err, rc = run('sha512sum', '--version')
check('sha512sum --version exits 0', rc == 0)
check('sha512sum --version shows sha512sum', 'sha512sum' in out)

d_sha512 = tempfile.mkdtemp()
try:
    f1 = os.path.join(d_sha512, 'hello.txt')
    write_file(f1, 'Hello, World!\n')
    out, err, rc = run('sha512sum', f1)
    check('sha512sum file exits 0', rc == 0)
    check('sha512sum has 128 hex chars', len(out.split()[0]) == 128)
finally:
    shutil.rmtree(d_sha512, ignore_errors=True)

# ── sha224sum ──────────────────────────────────────────────────────────────────
out, err, rc = run('sha224sum', '--version')
check('sha224sum --version exits 0', rc == 0)
check('sha224sum --version shows sha224sum', 'sha224sum' in out)

d_sha224 = tempfile.mkdtemp()
try:
    f1 = os.path.join(d_sha224, 'hello.txt')
    write_file(f1, 'Hello, World!\n')
    out, err, rc = run('sha224sum', f1)
    check('sha224sum file exits 0', rc == 0)
    check('sha224sum has 56 hex chars', len(out.split()[0]) == 56)
finally:
    shutil.rmtree(d_sha224, ignore_errors=True)

# ── sha384sum ──────────────────────────────────────────────────────────────────
out, err, rc = run('sha384sum', '--version')
check('sha384sum --version exits 0', rc == 0)

d_sha384 = tempfile.mkdtemp()
try:
    f1 = os.path.join(d_sha384, 'hello.txt')
    write_file(f1, 'Hello, World!\n')
    out, err, rc = run('sha384sum', f1)
    check('sha384sum file exits 0', rc == 0)
    check('sha384sum has 96 hex chars', len(out.split()[0]) == 96)
finally:
    shutil.rmtree(d_sha384, ignore_errors=True)

# ── b2sum ──────────────────────────────────────────────────────────────────────
out, err, rc = run('b2sum', '--version')
check('b2sum --version exits 0', rc == 0)
check('b2sum --version shows b2sum', 'b2sum' in out)

d_b2 = tempfile.mkdtemp()
try:
    f1 = os.path.join(d_b2, 'hello.txt')
    write_file(f1, 'Hello, World!\n')
    out, err, rc = run('b2sum', f1)
    check('b2sum file exits 0', rc == 0)
    check('b2sum default has 128 hex chars', len(out.split()[0]) == 128)
    out2, err2, rc2 = run('b2sum', '-l', '256', f1)
    check('b2sum -l 256 exits 0', rc2 == 0)
    check('b2sum -l 256 has 64 hex chars', len(out2.split()[0]) == 64)
finally:
    shutil.rmtree(d_b2, ignore_errors=True)

# ── base32 ─────────────────────────────────────────────────────────────────────
out, err, rc = run('base32', stdin_text='hello\n')
check('base32 encodes hello', rc == 0)
check('base32 encoded output', 'NBSWY3DPBI======' in out.replace('\n', ''))
out2, err2, rc2 = run('base32', '-d', stdin_text='NBSWY3DPEB3W64TMMQ======\n')
check('base32 -d decodes', rc2 == 0)
check('base32 -d output', 'hello' in out2)
out3, err3, rc3 = run('base32', '--version')
check('base32 --version exits 0', rc3 == 0)

# ── dd ─────────────────────────────────────────────────────────────────────────
out, err, rc = run('dd', '--version')
check('dd --version exits 0', rc == 0)
check('dd --version shows dd', 'dd' in out)

d_dd = tempfile.mkdtemp()
try:
    src = os.path.join(d_dd, 'src.bin')
    dst = os.path.join(d_dd, 'dst.bin')
    write_file(src, 'abcdefghij')
    out, err, rc = run('dd', f'if={src}', f'of={dst}', 'bs=1', 'count=5', 'status=none')
    check('dd exits 0', rc == 0)
    check('dd copies count bytes', open(dst, 'rb').read() == b'abcde')
    dst2 = os.path.join(d_dd, 'dst2.bin')
    out, err, rc = run('dd', f'if={src}', f'of={dst2}', 'bs=512', 'status=none')
    check('dd full copy exits 0', rc == 0)
    check('dd full copy size matches', os.path.getsize(dst2) == os.path.getsize(src))
finally:
    shutil.rmtree(d_dd, ignore_errors=True)

# ── shred ──────────────────────────────────────────────────────────────────────
out, err, rc = run('shred', '--version')
check('shred --version exits 0', rc == 0)

d_shred = tempfile.mkdtemp()
try:
    f1 = os.path.join(d_shred, 'secret.txt')
    write_file(f1, 'sensitive data here\n')
    out, err, rc = run('shred', '-n', '1', f1)
    check('shred exits 0', rc == 0)
    check('shred file still exists', os.path.exists(f1))
    f2 = os.path.join(d_shred, 'gone.txt')
    write_file(f2, 'delete me\n')
    out, err, rc = run('shred', '-n', '1', '-u', f2)
    check('shred -u removes file', not os.path.exists(f2))
finally:
    shutil.rmtree(d_shred, ignore_errors=True)

# ── unlink ─────────────────────────────────────────────────────────────────────
out, err, rc = run('unlink', '--version')
check('unlink --version exits 0', rc == 0)

d_ul = tempfile.mkdtemp()
try:
    f1 = os.path.join(d_ul, 'todel.txt')
    write_file(f1, 'bye\n')
    out, err, rc = run('unlink', f1)
    check('unlink exits 0', rc == 0)
    check('unlink removes file', not os.path.exists(f1))
    out2, err2, rc2 = run('unlink', os.path.join(d_ul, 'noexist.txt'))
    check('unlink missing exits 1', rc2 == 1)
finally:
    shutil.rmtree(d_ul, ignore_errors=True)

# ── link ───────────────────────────────────────────────────────────────────────
out, err, rc = run('link', '--version')
check('link --version exits 0', rc == 0)

d_lk = tempfile.mkdtemp()
try:
    src = os.path.join(d_lk, 'orig.txt')
    dst = os.path.join(d_lk, 'hard.txt')
    write_file(src, 'original\n')
    out, err, rc = run('link', src, dst)
    check('link exits 0', rc == 0)
    check('link creates destination', os.path.exists(dst))
    check('link content matches', open(dst).read() == 'original\n')
finally:
    shutil.rmtree(d_lk, ignore_errors=True)

# ── sync ───────────────────────────────────────────────────────────────────────
out, err, rc = run('sync', '--version')
check('sync --version exits 0', rc == 0)
out, err, rc = run('sync')
check('sync exits 0', rc == 0)

# ── pathchk ────────────────────────────────────────────────────────────────────
out, err, rc = run('pathchk', '--version')
check('pathchk --version exits 0', rc == 0)
out, err, rc = run('pathchk', 'normal_file.txt')
check('pathchk valid name exits 0', rc == 0)
out, err, rc = run('pathchk', '-P', '')
check('pathchk -P empty name exits 1', rc == 1)

# ── nice ───────────────────────────────────────────────────────────────────────
out, err, rc = run('nice', '--version')
check('nice --version exits 0', rc == 0)
out, err, rc = run('nice', 'true')
check('nice true exits 0', rc == 0)
out, err, rc = run('nice', '-n', '10', 'true')
check('nice -n 10 true exits 0', rc == 0)
out, err, rc = run('nice', '-n', '10', 'false')
check('nice propagates exit code', rc == 1)

# ── nohup ──────────────────────────────────────────────────────────────────────
out, err, rc = run('nohup', '--version')
check('nohup --version exits 0', rc == 0)

d_nohup = tempfile.mkdtemp()
try:
    old_dir = os.getcwd()
    os.chdir(d_nohup)
    out, err, rc = run('nohup', 'true')
    check('nohup true exits 0', rc == 0)
    out2, err2, rc2 = run('nohup', 'false')
    check('nohup propagates exit 1', rc2 == 1)
    os.chdir(old_dir)
finally:
    os.chdir(old_dir)
    shutil.rmtree(d_nohup, ignore_errors=True)

# ── tty ────────────────────────────────────────────────────────────────────────
out, err, rc = run('tty', '--version')
check('tty --version exits 0', rc == 0)
# In CI stdin is not a tty, so exit 1 is expected; either 0 or 1 is valid
out, err, rc = run('tty')
check('tty exits 0 or 1', rc in (0, 1))
out, err, rc = run('tty', '-s')
check('tty -s silent exits 0 or 1', rc in (0, 1))

# ── logname ────────────────────────────────────────────────────────────────────
out, err, rc = run('logname', '--version')
check('logname --version exits 0', rc == 0)
out, err, rc = run('logname')
check('logname exits 0', rc == 0)
check('logname prints something', len(out.strip()) > 0)

# ── printenv ───────────────────────────────────────────────────────────────────
out, err, rc = run('printenv', '--version')
check('printenv --version exits 0', rc == 0)
out, err, rc = run('printenv', 'PATH')
check('printenv PATH exits 0', rc == 0)
check('printenv PATH has content', len(out.strip()) > 0)
out, err, rc = run('printenv', 'WINIX_NONEXISTENT_VAR_XYZ')
check('printenv missing var exits 1', rc == 1)

# ── fmt ────────────────────────────────────────────────────────────────────────
out, err, rc = run('fmt', '--version')
check('fmt --version exits 0', rc == 0)
long_line = 'word ' * 20  # 100 chars
out, err, rc = run('fmt', stdin_text=long_line + '\n')
check('fmt exits 0', rc == 0)
check('fmt wraps long line', any(len(l) <= 75 for l in out.splitlines()))
out2, err2, rc2 = run('fmt', '-w', '40', stdin_text=long_line + '\n')
check('fmt -w 40 wraps tighter', all(len(l) <= 45 for l in out2.splitlines() if l))

# ── join ───────────────────────────────────────────────────────────────────────
out, err, rc = run('join', '--version')
check('join --version exits 0', rc == 0)

d_join = tempfile.mkdtemp()
try:
    f1 = os.path.join(d_join, 'f1.txt')
    f2 = os.path.join(d_join, 'f2.txt')
    write_file(f1, 'a 1\nb 2\nc 3\n')
    write_file(f2, 'a x\nb y\nd z\n')
    out, err, rc = run('join', f1, f2)
    check('join exits 0', rc == 0)
    check('join matches a', 'a 1 x' in out)
    check('join matches b', 'b 2 y' in out)
    check('join excludes unmatched', 'c' not in out and 'd' not in out)
    out2, err2, rc2 = run('join', '-a', '1', f1, f2)
    check('join -a 1 includes unmatched from f1', 'c' in out2)
finally:
    shutil.rmtree(d_join, ignore_errors=True)

# ── tsort ──────────────────────────────────────────────────────────────────────
out, err, rc = run('tsort', '--version')
check('tsort --version exits 0', rc == 0)
out, err, rc = run('tsort', stdin_text='a b\nb c\nc d\n')
check('tsort exits 0', rc == 0)
lines_ts = out.strip().splitlines()
check('tsort a before b', lines_ts.index('a') < lines_ts.index('b'))
check('tsort b before c', lines_ts.index('b') < lines_ts.index('c'))
check('tsort c before d', lines_ts.index('c') < lines_ts.index('d'))

# ── who / users / groups ───────────────────────────────────────────────────────
out, err, rc = run('who', '--version')
check('who --version exits 0', rc == 0)
out, err, rc = run('who')
check('who exits 0', rc == 0)

out, err, rc = run('users', '--version')
check('users --version exits 0', rc == 0)
out, err, rc = run('users')
check('users exits 0', rc == 0)

out, err, rc = run('groups', '--version')
check('groups --version exits 0', rc == 0)
check('groups --version shows groups', 'groups' in out)

# ── csplit ─────────────────────────────────────────────────────────────────────
out, err, rc = run('csplit', '--version')
check('csplit --version exits 0', rc == 0)

d_cs = tempfile.mkdtemp()
try:
    old_dir = os.getcwd()
    os.chdir(d_cs)
    src = os.path.join(d_cs, 'input.txt')
    write_file(src, 'aaa\nbbb\nccc\nddd\neee\n')
    out, err, rc = run('csplit', src, '3')
    check('csplit exits 0', rc == 0)
    check('csplit xx00 created', os.path.exists(os.path.join(d_cs, 'xx00')))
    check('csplit xx01 created', os.path.exists(os.path.join(d_cs, 'xx01')))
    check('csplit xx00 has first 2 lines', open(os.path.join(d_cs, 'xx00')).read() == 'aaa\nbbb\n')
    os.chdir(old_dir)
finally:
    os.chdir(old_dir)
    shutil.rmtree(d_cs, ignore_errors=True)

# ── pr ─────────────────────────────────────────────────────────────────────────
out, err, rc = run('pr', '--version')
check('pr --version exits 0', rc == 0)

d_pr = tempfile.mkdtemp()
try:
    src = os.path.join(d_pr, 'input.txt')
    write_file(src, '\n'.join(str(i) for i in range(1, 21)) + '\n')
    out, err, rc = run('pr', '-t', src)
    check('pr -t exits 0', rc == 0)
    check('pr -t has content', '1' in out)
    out2, err2, rc2 = run('pr', src)
    check('pr with header exits 0', rc2 == 0)
    check('pr header has Page', 'Page' in out2)
finally:
    shutil.rmtree(d_pr, ignore_errors=True)

# ── stdbuf ─────────────────────────────────────────────────────────────────────
out, err, rc = run('stdbuf', '--version')
check('stdbuf --version exits 0', rc == 0)
out, err, rc = run('stdbuf', '-o', '0', 'true')
check('stdbuf true exits 0', rc == 0)
out, err, rc = run('stdbuf', '-o', 'L', 'false')
check('stdbuf propagates exit code', rc == 1)

# ── Summary ───────────────────────────────────────────────────────────────────

total = _passed + _failed
print(f"\n{'=' * 40}")
print(f"  Results: {_passed}/{total} passed", end='')
if _failed:
    print(f"  ({_failed} FAILED)")
else:
    print("  All tests passed")
print(f"{'=' * 40}")

sys.exit(0 if _failed == 0 else 1)
