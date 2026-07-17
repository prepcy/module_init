#!/usr/bin/env python3
"""Validate strict module-manifest parsing without touching the source tree."""

import os
import sys
import tempfile

ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT_DIR, "tools"))

import menuconfig


def write_module(directory, filename, content):
    path = os.path.join(directory, filename)
    with open(path, "w", encoding="utf-8") as manifest:
        manifest.write(content)
    return path


def expect_failure(callback):
    try:
        callback()
    except ValueError:
        return
    raise AssertionError("validation unexpectedly succeeded")


def main():
    with tempfile.TemporaryDirectory() as directory:
        for source in ("first.c", "second.c"):
            open(os.path.join(directory, source), "w", encoding="utf-8").close()
        first = "name=first\nid=1\nconfig=FIRST\nprompt=First\ndefault=y\nsources=first.c\ndepends=\n"
        second = "name=second\nid=2\nconfig=SECOND\nprompt=Second\ndefault=n\nsources=second.c\ndepends=FIRST\n"
        write_module(directory, "first.module", first)
        write_module(directory, "second.module", second)
        modules = menuconfig.load_modules(os.path.join(directory, "*.module"))
        assert len(modules) == 2

        duplicate_id = second.replace("id=2", "id=0x1")
        write_module(directory, "second.module", duplicate_id)
        expect_failure(lambda: menuconfig.load_modules(os.path.join(directory, "*.module")))

        cycle = second.replace("depends=FIRST", "depends=FIRST")
        write_module(directory, "second.module", cycle)
        write_module(directory, "first.module", first.replace("depends=", "depends=SECOND"))
        expect_failure(lambda: menuconfig.load_modules(os.path.join(directory, "*.module")))

        write_module(directory, "first.module", first)
        write_module(directory, "second.module", second.replace("depends=FIRST", "depends=MISSING"))
        expect_failure(lambda: menuconfig.load_modules(os.path.join(directory, "*.module")))

        write_module(directory, "second.module", second.replace("sources=second.c", "sources=../second.c"))
        expect_failure(lambda: menuconfig.load_modules(os.path.join(directory, "*.module")))

        write_module(directory, "second.module", second + "unknown=value\n")
        expect_failure(lambda: menuconfig.load_modules(os.path.join(directory, "*.module")))

        write_module(directory, "second.module", second)
        config_path = os.path.join(directory, ".config")
        with open(config_path, "w", encoding="utf-8") as config_file:
            config_file.write("CONFIG_STALE=y\n")
        expect_failure(lambda: menuconfig.load_config(modules, config_path))


if __name__ == "__main__":
    main()
