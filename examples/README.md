# Captured example

From the project root:

```bash
make
./pipeline 2 2 2 6 3 1 1 1 0 1 1 2
```

`sample_output.txt` is the unedited standard output of an actual run on
2026-10-08, using the MSYS2 POSIX runtime on Windows and GCC 15.3.0.
It contains all six result lines. It is an example, not an expected-output
fixture: both values and order can vary between executions and platforms.

The executable accepts command-line arguments only. No input file is needed.
