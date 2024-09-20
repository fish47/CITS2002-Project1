import os
import re
import sys
import typing
import tempfile
import argparse
import subprocess

__SEPARATOR_WIDTH = 80
__SUBPROCESS_TIMEOUT = 2

__RE_PRAGMA_ONCE = re.compile(r'\s*#pragma\s+once\s+')
__RE_INCLUDE_LOCAL = re.compile(r'\s*#include\s+".+')

__ML_TEST_SOURCE = """\
function func a b c
\t  return (a + b) * c + arg0

print arg0
print (1 + 3) * 0.5 / 2 / 16
print func(1, 2, arg47)
print func(arg1, arg2, 4) + func(1, 2, arg3) - 1
"""
__ML_TEST_ARGS = ("1", "2", "3")
__ML_TEST_RESULT = """\
1
0.062500
1
21
"""


def _iter_file_lines(p: str) -> typing.Iterator[str]:
    with open(p) as f:
        for line in f.readlines():
            if __RE_PRAGMA_ONCE.match(line) or __RE_INCLUDE_LOCAL.match(line):
                yield "// " + line
            else:
                yield line


def __do_write_separator(out_file: typing.IO, tag: str|None = None):
    center = len(tag) + 2 if tag else 0
    left = (__SEPARATOR_WIDTH - center) // 2
    right = __SEPARATOR_WIDTH - center - left
    out_file.write("// ")
    if left > 0:
        out_file.write("=" * left)
    if center > 0:
        out_file.writelines((' ', tag, ' '))
    if right > 0:
        out_file.write("=" * right)
    out_file.write("\n")


def _do_write_files(suffix: str, out_file: typing.IO, in_paths: list[str]):
    for in_path in in_paths:
        if not in_path.endswith(suffix):
            continue

        with open(in_path) as f:
            out_file.write("\n\n")
            __do_write_separator(out_file, os.path.basename(in_path))
            for line in f.readlines():
                if __RE_PRAGMA_ONCE.match(line) or __RE_INCLUDE_LOCAL.match(line):
                    line = "// " + line
                out_file.write(line)
            __do_write_separator(out_file)


def _write_combined_file(labels: list[str], in_paths: list[str], out_path: str):
    with open(out_path, "w") as out_file:
        # header
        out_file.writelines((
            "//  CITS2002 Project 1 2024\n",
            "//  Student1:   %s   %s\n" % tuple(labels[:2]),
            "//  Student2:   %s   %s\n" % tuple(labels[2:]),
            "//  Platform:   Linux\n"
            "\n\n",
            "//  THIS IS GENERATED FROM MULTIPLE HEADER AND SOURCE FILES.\n"
        ))

        _do_write_files(".h", out_file, in_paths)
        _do_write_files(".c", out_file, in_paths)


def _compile_and_check(out_path: str):
    if not os.path.isfile(out_path):
        sys.exit("output file not exist")

    with tempfile.TemporaryDirectory() as tmp_dir:
        exec_path = os.path.join(tmp_dir, "exec_file")
        subprocess.run(["cc", "-std=c11", "-Wall", "-Werror", "-o", exec_path, out_path],
                       check=True, cwd=tmp_dir, timeout=__SUBPROCESS_TIMEOUT)
        ml_path = os.path.join(tmp_dir, "test.ml")
        with open(ml_path, "w") as f:
            f.write(__ML_TEST_SOURCE)
        p = subprocess.run([exec_path, ml_path, *__ML_TEST_ARGS],
                           check=True, cwd=tmp_dir, timeout=__SUBPROCESS_TIMEOUT,
                           stdout=subprocess.PIPE)
        if p.stdout != __ML_TEST_RESULT.encode():
            sys.exit("test case failed")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-i', nargs='+', dest='inputs')
    parser.add_argument('-l', nargs=4, dest='labels')
    parser.add_argument('-o', dest='output')
    args = parser.parse_args()
    _write_combined_file(args.labels, args.inputs, args.output)
    _compile_and_check(args.output)


if __name__ == '__main__':
    main()
