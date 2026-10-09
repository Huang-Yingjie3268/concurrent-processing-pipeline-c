# Verification

Verification date: 2026-10-08. Temporary build tools and binaries are kept outside
the release. The source uses POSIX interfaces; the executed tests used the MSYS2
POSIX runtime on Windows (`MSYS_NT-10.0-26200`), GCC 15.3.0, GNU Make 4.4.1,
and Python 3.11. A temporary `python3` command invoked the installed Python;
no such machine-specific wrapper is included in this repository.

## Executed checks

| Check | Result |
| --- | --- |
| Clean build using the supplied Makefile | Passed |
| C11, `-Wall -Wextra -Wpedantic -Werror -O2 -pthread` | Passed without warnings |
| Linux x86_64 cross-build with musl libc | Passed; inspected the generated ELF64 executable |
| `make test` | Six normal test groups passed; two optional groups correctly skipped |
| Full suite with the optional fault build | Eight test groups passed; no skips |
| Four README command-line examples | Passed; empty workload emitted no lines |
| Recorded six-order example | Actual output saved in `examples/sample_output.txt` |
| Capacity-one buffers and reverse token ordering | Twelve runs of 2,000 orders completed with no missing/duplicate IDs |
| Original four Q1 configurations | Passed |
| Empty and tiny workloads | Twelve worker-count/workload combinations passed |
| Larger-buffer workload | 5,000 orders completed with no missing/duplicate IDs |
| Invalid input | 23 malformed, missing, extra, zero, negative, overflowing, or invalid-ID cases rejected |
| Thread creation failure | All seven creation positions in a three-worker-per-stage configuration unwound cleanly |
| Semaphore initialization failure | All seven initialization positions unwound cleanly |
| Mutex initialization failure | All three initialization positions unwound cleanly |
| Allocation failure | All nine allocation positions rejected without starting processing |
| Interrupted semaphore wait and encoding arithmetic | Passed with test-only fixed `rand()` value 37 |

Every child process has a 15-second timeout. The suite checks IDs as a set with
exact cardinality, not by arrival order. Fault tests check that every successfully
created thread was joined and every successfully initialized semaphore and mutex
was destroyed. Fixed-random tests check output against the real encoding formula
and configured token pairs; the normal binary still uses time-seeded random data.

## Optional fault tests

With GCC and GNU ld on Linux, build a test-only executable:

```bash
gcc -std=c11 -Wall -Wextra -Wpedantic -O2 -pthread \
  src/pipeline.c tests/fault_injection.c \
  -Wl,--wrap=pthread_create,--wrap=pthread_join,--wrap=sem_init,--wrap=sem_destroy,--wrap=pthread_mutex_init,--wrap=pthread_mutex_destroy,--wrap=calloc,--wrap=sem_wait,--wrap=rand \
  -o /tmp/pipeline-fault
python3 tests/test_pipeline.py ./pipeline --fault-binary /tmp/pipeline-fault
```

These same source files and wrapper options were executed under the MSYS2 GNU
toolchain; audit binaries were stored outside the release. The normal Makefile
does not link the wrappers.

## Sanitizer availability

ASan, UBSan, and TSan builds were each attempted separately with this GCC.
Each failed at link time because its runtime library was unavailable (`-lasan`,
`-lubsan`, or `-ltsan`). No sanitizer execution or sanitizer pass is claimed.
These checks should be run in a native Linux environment with the corresponding
runtimes available. Test success is not a formal proof of race freedom.

## Linux build check and execution boundary

The unchanged source was also cross-compiled using the official Zig 0.17.0
toolchain, whose downloaded archive was checked against its published SHA-256:

```bash
zig cc -target x86_64-linux-musl -std=c11 -Wall -Wextra -Wpedantic \
  -Werror -O2 -pthread src/pipeline.c -o /tmp/pipeline-linux
```

The resulting file was inspected as an x86_64 ELF64 executable. This checks Linux
headers, compilation, and linkage; it is not a native Linux runtime test. Linux
execution and a native Linux `make` remain untested because WSL was not installed.
The supplied GCC Makefile and all runtime checks were executed under MSYS2 as
described above. No Linux runtime test pass is claimed.

## Reasoning checked against the source

- Buffer writes/reads and ring indices are protected by each buffer's mutex.
- Empty/full semaphore reservations prevent overfilling and removing absent
  packets. Interrupted waits do not proceed without a reservation.
- Unique order IDs and the random generator use the same mutex.
- Distinct token IDs are acquired in increasing order and released before
  downstream buffer waits; circular token waits cannot form under that ordering.
- All quantizers finish before end packets enter A. Each encoder's final result
  enters B before its end packet; receiving all P ends therefore drains real work.
- Startup failure cannot strand a worker in a pipeline buffer, because workers
  remain behind a separate gate until creation has completed.
- Shared arrays outlive all normal and aborted-startup workers.

These invariants explain the design but do not establish scheduler fairness or
prove that every possible execution has been explored.
