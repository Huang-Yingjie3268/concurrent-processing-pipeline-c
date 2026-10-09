CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -O2 -pthread
LDLIBS = -pthread
PYTHON = python3

.PHONY: all clean test
all: pipeline

pipeline: src/pipeline.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $< -o $@ $(LDLIBS)

test: pipeline
	$(PYTHON) tests/test_pipeline.py ./pipeline

clean:
	rm -f pipeline pipeline.exe
