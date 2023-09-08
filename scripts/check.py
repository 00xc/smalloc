#!/usr/bin/env python3

import sys
from collections import defaultdict

PAGE_SIZE = 4096
ALLOC_SIZE = 64

def page_align(v):
	return v & ~(PAGE_SIZE - 1)

if __name__ == '__main__':

	pages = defaultdict(set)
	mem = set()

	if len(sys.argv) < 2:
		sys.exit("{} <trace file>".format(sys.argv[0]))

	with open(sys.argv[1], "r") as f:
		for i, line in enumerate(f):

			if i % 100000 == 0:
				print("\r{}".format(i), end="")

			line = line.rstrip()
			(t, addr, *_) = line.split()
			addr = int(addr, 16)
			page = page_align(addr)

			assert t == "a" or t == "f", "unknown prefix: {}".format(t)
			assert page_align(addr + ALLOC_SIZE) == page, \
				"allocation {:#x} crosses a page boundary".format(addr)

			if t == "a":
				assert addr not in mem, \
					"allocation {:#x} returned twice without being " \
					"freed".format(addr)
				mem.add(addr)

				assert addr not in pages[page]
				for ptr in pages[page]:
					diff = abs(addr - ptr)
					assert diff >= ALLOC_SIZE, \
						"allocations {:#x} and {:#x} are closer than" \
						" {} bytes".format(addr, ptr, ALLOC_SIZE)
					assert diff % ALLOC_SIZE == 0, \
						"allocations {:#x} and {:#x} are separated " \
						"by {} bytes (not a multiple of {})" \
						.format(addr, ptr, diff, ALLOC_SIZE)
				pages[page].add(addr)

			else:
				mem.remove(addr)
				pages[page].remove(addr)

	print("\nok")
