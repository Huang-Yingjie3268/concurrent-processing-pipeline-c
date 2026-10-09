# Changes from the original Question 1

The original producer-consumer architecture, packet types, ring-buffer operations,
encoding formula, token ordering, and end-packet protocol are retained. The
quantizer and encoder counts still both equal `P`. This pass did not import
teammates' implementations or replace the pipeline with serial processing.

| Area | Original behavior | Public version |
| --- | --- | --- |
| Arguments | Read `argv` before checking `argc`; `atoi` silently accepted malformed data | Check counts first; use `strtol`, range checks, and clear error/usage messages |
| Buffer/token configuration | Zero capacities, zero tokens, invalid IDs could hang or access invalid memory | Reject invalid values before creating workers |
| Equal token IDs | Waited twice on one semaphore, potentially exhausting all instances | Reject duplicate IDs, consistent with the report's two-different-token explanation |
| Encoded arithmetic | Large token IDs could overflow the result | Check each pair against the maximum raw value of 99 |
| Allocation | Unchecked allocations; variable-length thread arrays on the stack | Checked, overflow-aware heap allocation and cleanup |
| Thread creation | Ignored errors; missing workers could strand other stages | Startup semaphore prevents processing until every thread exists; failed startup wakes and joins only created workers |
| Semaphore waits | Ignored errors and interruption | Retry `EINTR`; report unexpected errors and terminate the process |
| Initialization | Ignored failures | Track each initialized primitive and unwind partial initialization |
| Cleanup | Freed arrays without destroying synchronization objects | Join workers, destroy initialized primitives, then free arrays |
| Random generation | Concurrent `rand()` calls relied on implementation behavior | Protect `rand()` with the existing order-counter mutex |
| Logging | Ignored output errors | Check `printf` and final `fflush` |
| Termination | Joined producers, sent `P` ends, logger counted `P` forwarded ends | Same protocol; no redesign was needed |

Zero orders are explicitly supported and emit no result lines. A runtime failure
of an already active synchronization primitive is a process-level error; normal
cleanup and safe startup unwinding do not promise recovery from such a failure.

## File selection

- Renamed `src/problem1.c` to `src/pipeline.c`, keeping its core routines.
- Omitted the two teammates' source files, their inputs, and their documentation.
- Omitted `helpers.c` and `helpers.h`: Question 1 neither included their header
  nor called their delay functions. Their authorship does not need to be assumed.
- Replaced the group Makefile and submission README with a Q1-only build and
  English documentation.
- Reviewed the full archive inventory and the report; redrew only the Q1
  architecture rather than publish a page containing teammates' diagrams.
- Converted the four Q1 parameter configurations into executable automated tests.
  Original text fixtures contained parenthesized pairs and were not directly
  consumable by the command-line interface.
- Excluded student identifiers, the group report, compiled binaries, temporary
  tools, audit copies, and captured material unrelated to Question 1.

AI assistance was used for this preparation pass. These changes should not be
represented as features that were already present in the original submission.
