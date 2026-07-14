#!/usr/bin/env python3
# coding: utf-8
#
# Minimal UBSIO-KV Python example.
#
# Prerequisites:
# - pykvc has been installed in the current Python environment.
# - BoostIO standalone configuration has been prepared.
# - UBSIO_CONFIG_PATH points to the runtime bio.conf when a non-default
#   configuration file is used.

import pykvc


def require_ok(operation, result):
    if result != 0:
        raise RuntimeError(f"{operation} failed, result={result}")


def main():
    key = "ubsio-python-minimal"
    value = b"hello-ubsio-kv"

    require_ok("initialize", pykvc.initialize(device_id=-1))
    try:
        require_ok("put", pykvc.put(key, value))

        read_buffer = bytes(len(value))
        require_ok("get", pykvc.get(key, read_buffer))

        exists = pykvc.exist(key)
        print(f"exist({key}) = {exists}")

        length = pykvc.get_length(key)
        print(f"get_length({key}) = {length}")

        require_ok("delete", pykvc.delete(key))
    finally:
        pykvc.exit()


if __name__ == "__main__":
    main()
