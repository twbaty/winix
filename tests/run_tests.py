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
expect_contains('ver shows version number', out, '1.0')

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
expect_contains('nix --version shows 1.0', out, '1.0')

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
