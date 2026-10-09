# Pipeline implementation and command-line reference

## Synchronization design

Each buffer starts with `empty = capacity` and `full = 0`. A producer waits for
an empty slot, locks the mutex, writes the packet and advances the insertion
index, unlocks, then posts `full`. A consumer waits on `full`, removes a packet
and advances the removal index under the mutex, then posts `empty`.

The semaphores reserve available slots and packets; the mutex protects the array
access and indices. Semaphore waits occur outside the buffer mutex, so a blocked
producer or consumer does not prevent the opposite operation from progressing.
During an operation, a reserved slot or packet may be in transit: the two
semaphore values need not sum to the buffer capacity at every instant.

The order counter and calls to `rand()` share `order_id_mutex`. Encoder token
assignments remain immutable while threads run. Only the logger accesses its
termination counter and writes normal output.

## Deadlock avoidance

Encoder `i` acquires the smaller of its two token IDs first, then the larger.
Every encoder follows this rule, including pairs supplied in reverse order.
For two distinct resources, dependencies therefore follow increasing token IDs
and cannot form a circular token wait. Both tokens are released before an encoder
waits for space in Buffer B.

The original report describes acquiring two **different** token types. This
version rejects equal IDs: two consecutive waits on the same semaphore can hang
when token instances are exhausted, and require a different resource strategy.
Every token count must be positive. Ordered acquisition does not guarantee fair
service, bounded waiting time, or real-time behavior.

## Termination and startup failure

The main thread first joins all quantizers. Only then does it enqueue `P` end
packets in Buffer A, after all real packets. Each encoder consumes one end packet,
forwards one to Buffer B after its own results, and exits. The logger exits after
receiving all `P` end packets. Other encoders may still be processing when the
first end packet arrives, so one end packet is insufficient to stop the logger.
All workers are joined before synchronization objects or arrays are destroyed.
Zero orders follow the same protocol and produce no result lines.

A startup semaphore holds every worker until all threads have been created.
If creation fails, main marks startup aborted, releases only the created workers,
and joins them. They exit without entering either buffer. Initialization failures
destroy only successfully initialized resources. Interrupted semaphore waits are
retried. Unexpected failures of runtime synchronization primitives terminate the
process with an error rather than attempt unsafe continued processing; this is
not a recoverable service.


## Usage

Arguments are supplied on the command line; the program does not read an input
file or standard input.

```text
./pipeline P M N num_orders T cnt_0 ... cnt_{T-1} tA_0 tB_0 ... tA_{P-1} tB_{P-1}
```

| Argument | Meaning | Valid values |
| --- | --- | --- |
| `P` | Number of quantizers and number of encoders | Positive integer |
| `M` | Buffer A capacity | Positive integer within the platform semaphore limit |
| `N` | Buffer B capacity | Positive integer within the platform semaphore limit |
| `num_orders` | Total packets generated across all quantizers | Integer from 0 to `INT_MAX` |
| `T` | Number of token types | Integer from 2 to `INT_MAX` |
| `cnt_i` | Available instances of token type `i` | Positive integer within the platform semaphore limit |
| `tA_i`, `tB_i` | Two token types required by encoder `i` | Distinct integers from 0 to `T - 1` |

All numeric arguments must fit in a C `int`. The exact argument count is checked,
as are token pairs whose maximum encoded value could overflow an `int`. Available
memory and OS thread limits can still prevent a valid configuration from starting.
Malformed, missing, and extra arguments produce an error, usage text, and a
nonzero exit status.

A minimal run with one quantizer, one encoder, and two token types:

```bash
./pipeline 1 1 1 1 2 1 1 0 1
```

A small example with two workers per stage:

```bash
./pipeline 2 2 2 6 3 1 1 1 0 1 1 2
```

A capacity-one configuration with token contention:

```bash
./pipeline 4 1 1 200 4 2 2 2 2 0 1 1 2 2 3 0 3
```

An empty workload:

```bash
./pipeline 3 1 1 0 2 1 1 0 1 1 0 0 1
```
