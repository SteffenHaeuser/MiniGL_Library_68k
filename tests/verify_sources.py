#!/usr/bin/env python3
"""Compatibility entry point: the current release is V29 / ABI 5."""
import sys
sys.dont_write_bytecode = True
from verify_v29 import main
if __name__ == '__main__': main()
