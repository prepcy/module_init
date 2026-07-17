#!/usr/bin/env python3
"""Small boolean configuration UI backed by per-module manifests."""

import argparse
import configparser
import curses
import glob
import os
import re
import tempfile

ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MANIFEST_PATTERN = os.path.join(ROOT_DIR, "app", "features", "*.module")
DEFAULT_CONFIG_FILE = os.path.join(ROOT_DIR, ".config")
ALLOWED_FIELDS = {"name", "id", "config", "prompt", "default", "sources", "depends"}
REQUIRED_FIELDS = {"name", "id", "config", "prompt", "default", "sources"}


def parse_bool(value):
    return value.strip().lower() in {"1", "on", "true", "y", "yes"}


def validate_dependency_graph(modules):
    dependencies = {module["config"]: module["depends"] for module in modules}
    visited = set()

    def visit(config, path):
        if config in path:
            cycle = " -> ".join(path + [config])
            raise ValueError(f"module dependency cycle: {cycle}")
        if config in visited:
            return
        for dependency in dependencies[config]:
            if dependency not in dependencies:
                raise ValueError(f"{config} has unknown dependency: {dependency}")
            visit(dependency, path + [config])
        visited.add(config)

    for config in dependencies:
        visit(config, [])


def load_modules(pattern=MANIFEST_PATTERN):
    modules = []
    known_ids = set()
    known_configs = set()
    known_names = set()
    for path in sorted(glob.glob(pattern)):
        parser = configparser.ConfigParser(interpolation=None)
        with open(path, "r", encoding="utf-8") as manifest:
            parser.read_string("[module]\n" + manifest.read())
        module = dict(parser["module"])
        unknown = module.keys() - ALLOWED_FIELDS
        if unknown:
            raise ValueError(f"{path} has unknown fields: {', '.join(sorted(unknown))}")
        missing = REQUIRED_FIELDS - module.keys()
        if missing:
            raise ValueError(f"{path} missing fields: {', '.join(sorted(missing))}")
        if not re.fullmatch(r"[a-z][a-z0-9_]*", module["name"]):
            raise ValueError(f"{path} has invalid module name: {module['name']}")
        if not re.fullmatch(r"[A-Z][A-Z0-9_]*", module["config"]):
            raise ValueError(f"{path} has invalid config name: {module['config']}")
        if module["default"].strip().lower() not in {"y", "n"}:
            raise ValueError(f"{path} default must be y or n")
        if not module["prompt"].strip():
            raise ValueError(f"{path} prompt must not be empty")
        try:
            module_id = int(module["id"], 0)
        except ValueError as error:
            raise ValueError(f"{path} has invalid module id: {module['id']}") from error
        if module_id <= 0 or module_id > 0xFFFFFFFF:
            raise ValueError(f"{path} module id is outside uint32 range: {module['id']}")
        if module_id in known_ids:
            raise ValueError(f"duplicate module id: {module['id']}")
        if module["config"] in known_configs:
            raise ValueError(f"duplicate module config: {module['config']}")
        if module["name"] in known_names:
            raise ValueError(f"duplicate module name: {module['name']}")
        sources = [item.strip() for item in module["sources"].split(",") if item.strip()]
        if not sources:
            raise ValueError(f"{path} must declare at least one source")
        manifest_dir = os.path.dirname(path)
        for source in sources:
            if os.path.isabs(source) or ".." in source.split(os.sep):
                raise ValueError(f"{path} has unsafe source path: {source}")
            if not os.path.isfile(os.path.join(manifest_dir, source)):
                raise ValueError(f"{path} source does not exist: {source}")

        known_ids.add(module_id)
        known_configs.add(module["config"])
        known_names.add(module["name"])
        module["id_value"] = module_id
        module["sources"] = sources
        module["depends"] = [item.strip() for item in module.get("depends", "").split(",") if item.strip()]
        module["enabled"] = parse_bool(module["default"])
        module["path"] = path
        modules.append(module)

    if not modules:
        raise ValueError("no module manifests found")
    validate_dependency_graph(modules)
    return modules


def normalize_dependencies(modules):
    enabled = {module["config"]: module["enabled"] for module in modules}
    changed = True
    while changed:
        changed = False
        for module in modules:
            if module["enabled"] and any(not enabled.get(dep, False) for dep in module["depends"]):
                module["enabled"] = False
                enabled[module["config"]] = False
                changed = True


def load_config(modules, path):
    if not os.path.exists(path):
        normalize_dependencies(modules)
        return
    values = {}
    with open(path, "r", encoding="utf-8") as config_file:
        for raw_line in config_file:
            line = raw_line.strip()
            if line.startswith("CONFIG_") and line.endswith("=y"):
                values[line[len("CONFIG_"):-2]] = True
            elif line.startswith("# CONFIG_") and line.endswith(" is not set"):
                values[line[len("# CONFIG_"):-len(" is not set")]] = False
            elif line.startswith("CONFIG_"):
                raise ValueError(f"invalid configuration line: {line}")
    known_configs = {module["config"] for module in modules}
    unknown_configs = values.keys() - known_configs
    if unknown_configs:
        raise ValueError(f"unknown configuration entries: {', '.join(sorted(unknown_configs))}")
    for module in modules:
        if module["config"] in values:
            module["enabled"] = values[module["config"]]
    normalize_dependencies(modules)


def save_config(modules, path):
    normalize_dependencies(modules)
    directory = os.path.dirname(os.path.abspath(path))
    os.makedirs(directory, exist_ok=True)
    descriptor, temporary_path = tempfile.mkstemp(prefix=".config.", dir=directory, text=True)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as config_file:
            config_file.write("# Automatically generated from module manifests.\n\n")
            for module in modules:
                config = module["config"]
                if module["enabled"]:
                    config_file.write(f"CONFIG_{config}=y\n")
                else:
                    config_file.write(f"# CONFIG_{config} is not set\n")
        os.replace(temporary_path, path)
    except Exception:
        if os.path.exists(temporary_path):
            os.unlink(temporary_path)
        raise


def dependency_text(module):
    if not module["depends"]:
        return ""
    return " 依赖: " + ", ".join(module["depends"])


def draw_screen(screen, modules, selected, dirty, message):
    screen.erase()
    height, width = screen.getmaxyx()
    title = " App-Core Module Configuration "
    screen.addstr(1, max(0, (width - len(title)) // 2), title, curses.A_BOLD)
    status = "有未保存修改" if dirty else "配置已保存"
    screen.addstr(3, 2, message or status)
    screen.addstr(5, 2, "↑/↓选择  空格切换  S保存  ESC退出")

    visible_count = max(1, height - 10)
    offset = min(max(0, selected - visible_count + 1), max(0, len(modules) - visible_count))
    for row, module in enumerate(modules[offset:offset + visible_count], start=0):
        index = offset + row
        marker = "[*]" if module["enabled"] else "[ ]"
        text = f"{marker} {module['prompt']}{dependency_text(module)}"
        attribute = curses.A_REVERSE if index == selected else curses.A_NORMAL
        screen.addstr(7 + row, 4, text[:max(1, width - 8)], attribute)
    screen.refresh()


def run_menu(screen, modules, config_path):
    curses.curs_set(0)
    selected = 0
    dirty = False
    message = ""
    while True:
        draw_screen(screen, modules, selected, dirty, message)
        message = ""
        key = screen.getch()
        if key == curses.KEY_UP:
            selected = max(0, selected - 1)
        elif key == curses.KEY_DOWN:
            selected = min(len(modules) - 1, selected + 1)
        elif key == ord(" "):
            module = modules[selected]
            if not module["enabled"]:
                enabled = {item["config"]: item["enabled"] for item in modules}
                missing = [dep for dep in module["depends"] if not enabled.get(dep, False)]
                if missing:
                    message = "请先启用依赖: " + ", ".join(missing)
                    continue
            module["enabled"] = not module["enabled"]
            normalize_dependencies(modules)
            dirty = True
        elif key in {ord("s"), ord("S")}:
            save_config(modules, config_path)
            dirty = False
            message = f"已保存: {config_path}"
        elif key == 27:
            if not dirty:
                break
            message = "存在未保存修改，再按ESC放弃，按S保存"
            draw_screen(screen, modules, selected, dirty, message)
            second_key = screen.getch()
            if second_key == 27:
                break
            if second_key in {ord("s"), ord("S")}:
                save_config(modules, config_path)
                break


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", default=DEFAULT_CONFIG_FILE)
    parser.add_argument("--write-default", metavar="FILE")
    parser.add_argument("--validate", action="store_true")
    args = parser.parse_args()

    modules = load_modules()
    if args.write_default:
        normalize_dependencies(modules)
        save_config(modules, args.write_default)
        return
    load_config(modules, args.config)
    if args.validate:
        return
    os.environ.setdefault("ESCDELAY", "25")
    curses.wrapper(run_menu, modules, args.config)


if __name__ == "__main__":
    main()
