"""Check packet integrity and bounded completion, without assuming log order."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import unittest

parser = argparse.ArgumentParser()
parser.add_argument('binary', nargs='?', default='./pipeline')
parser.add_argument('--fault-binary', help='optional GNU linker-wrapper build')
options = parser.parse_args()
BINARY = str(Path(options.binary).resolve())
FAULT_BINARY = str(Path(options.fault_binary).resolve()) if options.fault_binary else None
LINE = re.compile(r'\[Logger\] order_id=(\d+), encoded_value=(\d+)')


def config(p=1, m=1, n=1, orders=1, counts=(1, 1), pairs=None):
    pairs = pairs or [(0, 1)] * p
    return [p, m, n, orders, len(counts), *counts,
            *(token for pair in pairs for token in pair)]


def run(args, binary=BINARY, env=None):
    return subprocess.run([binary, *map(str, args)], text=True,
                          capture_output=True, timeout=15, env=env)


class PipelineTests(unittest.TestCase):
    def check_run(self, args, binary=BINARY, env=None, fixed_raw=None):
        result = run(args, binary, env)
        self.assertEqual(result.returncode, 0, result.stderr)
        matches = [LINE.fullmatch(line) for line in result.stdout.splitlines()]
        self.assertTrue(all(matches), result.stdout[:1000])
        rows = [(int(m[1]), int(m[2])) for m in matches]
        self.assertEqual(sorted(order for order, _ in rows), list(range(args[3])))
        start = 5 + args[4]
        sums = [a+b for a,b in zip(args[start::2], args[start+1::2])]
        for _, encoded in rows:
            if fixed_raw is not None:
                self.assertIn(encoded, [2*fixed_raw+s for s in sums])
            else:
                self.assertTrue(any(0 <= encoded-s <= 198 and (encoded-s) % 2 == 0
                                    for s in sums), encoded)
        return result

    def test_basic(self):
        self.check_run(config())

    def test_original_configurations(self):
        for args in [config(3, 4, 4, 90, (2, 2, 2), [(0, 1), (1, 2), (0, 2)]),
                     config(2, 2, 2, 100, (1, 1, 1), [(0, 1), (1, 2)]),
                     config(1, 8, 8, 30, (2, 2)),
                     config(4, 1, 1, 200, (2, 2, 2, 2),
                            [(0, 1), (1, 2), (2, 3), (0, 3)])]:
            with self.subTest(args=args):
                self.check_run(args)

    def test_capacity_one_and_reversed_token_contention(self):
        for repeat in range(12):
            with self.subTest(repeat=repeat):
                self.check_run(config(8, 1, 1, 2000,
                                      pairs=[(0, 1), (1, 0)] * 4))

    def test_termination(self):
        for p in (1, 3, 12):
            for orders in (0, 1, 2, 13):
                with self.subTest(p=p, orders=orders):
                    self.check_run(config(p, orders=orders))

    def test_larger_buffers(self):
        self.check_run(config(6, 17, 23, 5000))

    def test_invalid_input(self):
        base = config()
        cases = [[], ['--help'], base[:4], base[:-1], base + [9],
                 ['--unsupported', *base[1:]]]
        for index, value in [(0, 'abc'), (0, '2x'), (0, 0), (0, -1),
                             (0, 2147483648), (0, '9'*50), (1, 0), (2, -1),
                             (3, -1), (4, 1), (5, 0), (5, -1),
                             (7, -1), (8, 2), (8, 0), (8, ''), (8, '1.2')]:
            args = base.copy()
            args[index] = value
            cases.append(args)
        for args in cases:
            with self.subTest(args=args):
                result = run(args)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('Error:', result.stderr)
                self.assertIn('Usage:', result.stderr)
                self.assertEqual(result.stdout, '')

    @unittest.skipUnless(FAULT_BINARY, 'optional fault build not supplied')
    def test_creation_and_initialization_failures(self):
        # 1 logger + 3 encoders + 3 quantizers; 5 semaphores + 2 tokens;
        # 3 mutexes; 9 allocations. Fail every position, including mid-startup.
        for variable, count in [('FAIL_CREATE', 7), ('FAIL_SEM_INIT', 7),
                                ('FAIL_MUTEX_INIT', 3), ('FAIL_CALLOC', 9)]:
            for position in range(1, count+1):
                env = {**os.environ, variable: str(position), 'CHECK_ACCOUNTING': '1'}
                with self.subTest(variable=variable, position=position):
                    result = run(config(3, orders=100), FAULT_BINARY, env)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn('Error:', result.stderr)
                    self.assertIn('Fault accounting: balanced', result.stderr)
                    self.assertEqual(result.stdout, '')

    @unittest.skipUnless(FAULT_BINARY, 'optional fault build not supplied')
    def test_interrupted_wait_and_encoding_formula(self):
        env = {**os.environ, 'INTERRUPT_WAIT': '1', 'FIXED_RAW': '1',
               'CHECK_ACCOUNTING': '1'}
        args = config(4, orders=1000, counts=(1, 1, 1),
                      pairs=[(0, 1), (1, 2), (2, 0), (1, 0)])
        result = self.check_run(args, FAULT_BINARY, env, fixed_raw=37)
        self.assertIn('Fault accounting: balanced', result.stderr)


if __name__ == '__main__':
    unittest.main(argv=['test_pipeline'], verbosity=2)
