import curses
import os
import sys

os.environ.setdefault('ESCDELAY', '25')

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
KCONFIG_FILE = os.path.join(SCRIPT_DIR, "Kconfig")
CONFIG_FILE = os.path.join(os.path.dirname(SCRIPT_DIR), ".config")
DEFCONFIG_FILE = os.path.join(SCRIPT_DIR, "defconfig")

options = []
menus = {"main": {"title": "主菜单", "items": []}}
ACTIONS = ["< 选择/进入 >", "< 退出/返回 >", "< 保存 >"]


def normalize_choice_groups():
    """修复/约束 choice 组状态：每组最多且至少一个选中。"""
    choice_groups = {}
    for opt in options:
        if opt.get("type") == "choice_item":
            choice_groups.setdefault(opt.get("choice_group"), []).append(opt)

    for _, group_opts in choice_groups.items():
        if not group_opts:
            continue

        # 优先在依赖满足的成员中做归一化；若全部不满足则退回全组处理。
        candidates = [o for o in group_opts if is_dependency_met(o)]
        if not candidates:
            candidates = group_opts

        selected = [o for o in candidates if o.get("enabled", False)]
        if len(selected) > 1:
            keep = selected[0]
            for o in candidates:
                o["enabled"] = (o is keep)
        elif len(selected) == 0:
            candidates[0]["enabled"] = True


def parse_kconfig():
    global options, menus
    options.clear()
    menus = {"main": {"title": "主菜单", "items": []}}

    if not os.path.exists(KCONFIG_FILE):
        print(f"错误: 找不到内容文件 {KCONFIG_FILE}！")
        sys.exit(1)

    parse_stack = ["main"]
    current_opt = None
    current_submenu_entry = None  # 【核心升级】记录当前刚创建的子菜单或 choice 入口
    in_choice = False
    current_choice_id = None
    choice_group_dependency = None  # 【核心升级】用于记录 choice 组的公共依赖

    def extract_string(line):
        start = line.find('"')
        end = line.rfind('"')
        return line[start+1:end] if start != -1 and end != -1 else ""

    def flush_opt():
        nonlocal current_opt
        if current_opt:
            if current_opt.get("type") == "subrepo_temp":
                sub_id = current_opt["id"]
                prompt = current_opt["prompt"] or sub_id
                url = current_opt["url"]
                branch = current_opt["branch"]
                dep = current_opt["depends_on"]

                # 1. Parent bool switch
                opt_bool = {
                    "id": sub_id, "name": prompt, "type": "bool",
                    "enabled": False, "required": False, "value": "",
                    "depends_on": dep
                }
                options.append(opt_bool)
                menus[parse_stack[-1]]["items"].append({"type": "config", "data": opt_bool})

                # 2. URL config (depends on bool switch)
                url_dep = f"{sub_id}" if not dep else f"{sub_id} && ({dep})"
                opt_url = {
                    "id": f"{sub_id}_URL", "name": "仓库地址", "type": "string",
                    "enabled": False, "required": False, "value": url,
                    "depends_on": url_dep, "is_subrepo_child": True
                }
                options.append(opt_url)
                menus[parse_stack[-1]]["items"].append({"type": "config", "data": opt_url})

                # 3. Branch config (depends on bool switch)
                opt_branch = {
                    "id": f"{sub_id}_BRANCH", "name": "分支/Commit号", "type": "string",
                    "enabled": False, "required": False, "value": branch,
                    "depends_on": url_dep, "is_subrepo_child": True
                }
                options.append(opt_branch)
                menus[parse_stack[-1]]["items"].append({"type": "config", "data": opt_branch})
            else:
                options.append(current_opt)
                menus[parse_stack[-1]]["items"].append({"type": "config", "data": current_opt})
            current_opt = None

    with open(KCONFIG_FILE, 'r', encoding='utf-8') as f:
        in_help = False
        help_indent = 0
        for raw_line in f:
            line = raw_line.strip()

            if in_help:
                if not line:
                    if current_opt:
                        current_opt.setdefault("help", []).append("")
                    continue

                leading_spaces = len(raw_line) - len(raw_line.lstrip())
                if leading_spaces > help_indent:
                    if current_opt:
                        current_opt.setdefault("help", []).append(line)
                    continue
                else:
                    in_help = False

            if not line or line.startswith("#"):
                continue

            current_menu_id = parse_stack[-1]

            if line.startswith("menu ") or line.startswith("choice "):
                flush_opt()
                is_choice = line.startswith("choice ")
                title = extract_string(line) or line.split(" ", 1)[-1]

                menu_id = f"menu_{len(menus)}"
                menus[menu_id] = {"title": title, "items": []}

                # 【核心升级】创建子菜单入口，并附加 depends_on 属性
                current_submenu_entry = {
                    "type": "submenu", "target": menu_id, "name": f"{title} --->", "depends_on": None
                }
                menus[current_menu_id]["items"].append(current_submenu_entry)
                parse_stack.append(menu_id)

                if is_choice:
                    in_choice = True
                    current_choice_id = menu_id
                    choice_group_dependency = None

            elif line == "endmenu" or line == "endchoice":
                flush_opt()
                if len(parse_stack) > 1:
                    parse_stack.pop()
                if line == "endchoice":
                    in_choice = False
                    choice_group_dependency = None
                current_submenu_entry = None

            elif line.startswith("config "):
                flush_opt()
                current_submenu_entry = None  # 清除菜单入口焦点
                config_id = line.split(" ")[1].strip()
                current_opt = {
                    "id": config_id, "name": config_id, "type": "bool",
                    "enabled": False, "required": False, "value": "",
                    "depends_on": None
                }
                if in_choice:
                    current_opt["type"] = "choice_item"
                    current_opt["choice_group"] = current_choice_id
                    # 【核心升级】choice 内的选项自动继承该组的公共依赖
                    current_opt["depends_on"] = choice_group_dependency

            elif line.startswith("subrepo "):
                flush_opt()
                current_submenu_entry = None
                config_id = line.split(" ")[1].strip()
                current_opt = {
                    "id": config_id, "name": config_id, "type": "subrepo_temp",
                    "prompt": "", "url": "", "branch": "",
                    "depends_on": None
                }

            elif current_opt or current_submenu_entry:
                if line.startswith("bool ") and current_opt:
                    current_opt["name"] = extract_string(line)
                elif line.startswith("prompt ") and current_opt and current_opt.get("type") == "subrepo_temp":
                    current_opt["prompt"] = extract_string(line)
                elif line.startswith("default_url ") and current_opt and current_opt.get("type") == "subrepo_temp":
                    current_opt["url"] = extract_string(line)
                elif line.startswith("default_branch ") and current_opt and current_opt.get("type") == "subrepo_temp":
                    current_opt["branch"] = extract_string(line)
                elif line.startswith("string ") and current_opt:
                    current_opt["type"] = "string"
                    current_opt["name"] = extract_string(line)
                elif line.startswith("default ") and current_opt:
                    val = line.split(" ", 1)[1].strip()

                    if current_opt["type"] == "string":
                        current_opt["value"] = val.strip('"')
                    else:
                        current_opt["enabled"] = (val.lower() == 'y')
                elif line == "required" and current_opt:
                    current_opt["required"] = True
                elif (line.startswith("help") or line.startswith("---help---")) and current_opt:
                    in_help = True
                    help_indent = len(raw_line) - len(raw_line.lstrip())
                    current_opt["help"] = []
                # 【核心升级】灵活解析 depends on 语法
                elif line.startswith("depends on "):
                    dep = line.split("depends on ")[1].strip()
                    if current_opt:
                        current_opt["depends_on"] = dep
                    elif current_submenu_entry:
                        # 如果紧跟在 choice/menu 声明后面，将依赖挂载到菜单入口上
                        current_submenu_entry["depends_on"] = dep
                        if in_choice:
                            choice_group_dependency = dep
    flush_opt()
    normalize_choice_groups()


def is_dependency_met(opt):
    """递归检查配置项或子菜单的依赖是否满足，支持 !, &&, ||"""
    dep_str = opt.get("depends_on")
    if not dep_str:
        return True

    def get_var_value(var_name):
        var_name = var_name.strip()
        if not var_name:
            return False

        is_neg = False
        if var_name.startswith('!'):
            is_neg = True
            var_name = var_name[1:].strip()

        val = False
        for parent_opt in options:
            if parent_opt["id"] == var_name:
                val = parent_opt.get("enabled", False) and is_dependency_met(parent_opt)
                break
        return not val if is_neg else val

    # 先解析逻辑或 (||) 运算符，其优先级低于逻辑与 (&&)
    if '||' in dep_str:
        parts = dep_str.split('||')
        return any(is_dependency_met({"depends_on": p.strip()}) for p in parts)

    # 再解析逻辑与 (&&) 运算符
    if '&&' in dep_str:
        parts = dep_str.split('&&')
        return all(is_dependency_met({"depends_on": p.strip()}) for p in parts)

    # 单个依赖项（可能带 !）
    return get_var_value(dep_str.strip())


def snapshot_initial_state():
    for opt in options:
        opt["initial_enabled"] = opt.get("enabled", False)
        opt["initial_value"] = opt.get("value", "")


def check_unsaved_changes():
    for opt in options:
        if opt.get("enabled", False) != opt.get("initial_enabled", False):
            return True
        if opt.get("value", "") != opt.get("initial_value", ""):
            return True
    return False


def load_config():
    if not os.path.exists(CONFIG_FILE):
        if os.path.exists(DEFCONFIG_FILE):
            try:
                import shutil
                shutil.copyfile(DEFCONFIG_FILE, CONFIG_FILE)
            except Exception:
                pass
        else:
            return
    try:
        with open(CONFIG_FILE, 'r', encoding='utf-8') as f:
            for line in f:
                line = line.strip()
                if line.startswith("CONFIG_") and '="' in line:
                    parts = line.split('="', 1)
                    config_id = parts[0].replace("CONFIG_", "")
                    val = parts[1].rstrip('"')
                    for opt in options:
                        if opt["id"] == config_id:
                            opt["value"] = val
                elif line.startswith("CONFIG_") and line.endswith("=y"):
                    config_id = line.replace("CONFIG_", "").replace("=y", "")
                    for opt in options:
                        if opt["id"] == config_id:
                            opt["enabled"] = True
                elif line.startswith("# CONFIG_") and "is not set" in line:
                    config_id = line.replace("# CONFIG_", "").replace(" is not set", "")
                    for opt in options:
                        if opt["id"] == config_id and not opt["required"]:
                            opt["enabled"] = False
    except Exception:
        pass
    normalize_choice_groups()
    snapshot_initial_state()


def save_config():
    try:
        with open(CONFIG_FILE, 'w', encoding='utf-8') as f:
            f.write("# 自动生成的产品配置文件\n\n")
            for opt in options:
                if not is_dependency_met(opt):
                    continue

                if opt["type"] == "string":
                    f.write(f'CONFIG_{opt["id"]}="{opt["value"]}"\n')
                elif opt["enabled"]:
                    f.write(f'CONFIG_{opt["id"]}=y\n')
                else:
                    f.write(f'# CONFIG_{opt["id"]} is not set\n')
        snapshot_initial_state()
        return True
    except Exception:
        return False


def prompt_string_input(stdscr, title, current_value):
    h, w = stdscr.getmaxyx()
    box_h, box_w = 7, min(w - 10, 60)
    start_y, start_x = h//2 - box_h//2, w//2 - box_w//2

    win = curses.newwin(box_h, box_w, start_y, start_x)
    win.keypad(True)
    curses.curs_set(1)

    input_chars = list(current_value)

    while True:
        win.clear()
        win.box()
        win.addstr(1, 2, f" 输入文本: {title} ", curses.A_BOLD | curses.color_pair(3))
        win.addstr(3, 2, "> " + "".join(input_chars))
        win.addstr(5, 2, "[Enter] 确认    [ESC] 取消", curses.color_pair(2))
        win.refresh()

        key = win.getch()

        if key in [10, 13]:
            break
        elif key == 27:
            curses.curs_set(0)
            return current_value
        elif key in [curses.KEY_BACKSPACE, 8, 127]:
            if input_chars:
                input_chars.pop()
        elif 32 <= key <= 126:
            if len(input_chars) < box_w - 6:
                input_chars.append(chr(key))

    curses.curs_set(0)
    return "".join(input_chars)


def get_indent_level(opt, visible_items):
    if opt.get("is_subrepo_child"):
        return 1
    return 0


def draw_menu(stdscr, title_name, visible_items, current_row, current_action, has_unsaved_changes, scroll_offset=0, save_banner_text=None):
    stdscr.clear()
    h, w = stdscr.getmaxyx()

    title = f" MenuConfig - {title_name} "
    stdscr.addstr(1, w//2 - len(title)//2, title, curses.A_BOLD | curses.A_UNDERLINE)

    # 状态 / 提示行
    if save_banner_text:
        stdscr.addstr(3, 2, save_banner_text, curses.color_pair(3) | curses.A_BOLD)
    else:
        status_text = "状态: 存在未保存的更改 *" if has_unsaved_changes else "状态: 已保存"
        stdscr.addstr(3, 2, status_text, curses.color_pair(3) if has_unsaved_changes else curses.A_NORMAL)

    stdscr.addstr(5, 2, "使用 [↑/↓] 选择，[空格/回车] 执行，[←/→] 切换底栏，[ESC] 返回/退出：")

    max_visible_items = max(1, h - 12)
    visible_slice = visible_items[scroll_offset:scroll_offset + max_visible_items]

    for idx, item in enumerate(visible_slice):
        actual_idx = scroll_offset + idx
        x = 4
        y = 7 + idx

        if item["type"] == "submenu":
            display_text = f"    {item['name']}"
        else:
            opt = item["data"]
            indent_spaces = "    " * get_indent_level(opt, visible_items)
            if opt["type"] == "string":
                display_text = f"{indent_spaces}[{opt['value']}] {opt['name']}"
            elif opt["type"] == "choice_item":
                checkbox = "(*)" if opt["enabled"] else "( )"
                display_text = f"{indent_spaces}{checkbox} {opt['name']}"
            else:
                checkbox = "[*]" if opt["enabled"] else "[ ]"
                if opt["required"]:
                    checkbox = "[-]"
                display_text = f"{indent_spaces}{checkbox} {opt['name']}"

        if actual_idx == current_row:
            stdscr.addstr(y, x, display_text[:w - 6], curses.color_pair(1))
        else:
            stdscr.addstr(y, x, display_text[:w - 6])

    # 滚动辅助提示
    if len(visible_items) > max_visible_items:
        scroll_hint = f" [ 更多选项在下方 (共 {len(visible_items)} 项，当前展示第 {scroll_offset+1}-{scroll_offset+len(visible_slice)} 项) ] "
        if scroll_offset > 0 and scroll_offset + len(visible_slice) < len(visible_items):
            scroll_hint = f" [ 更多选项在上下方 (第 {scroll_offset+1}-{scroll_offset+len(visible_slice)} 项 / 共 {len(visible_items)} 项) ] "
        elif scroll_offset > 0:
            scroll_hint = f" [ 更多选项在上方 (第 {scroll_offset+1}-{scroll_offset+len(visible_slice)} 项 / 共 {len(visible_items)} 项) ] "
        stdscr.addstr(7 + len(visible_slice), 4, scroll_hint, curses.A_DIM)

    action_y = h - 3
    action_spacing = w // (len(ACTIONS) + 1)

    for idx, action in enumerate(ACTIONS):
        x = action_spacing * (idx + 1) - len(action) // 2
        if idx == current_action:
            stdscr.addstr(action_y, x, action, curses.color_pair(2))
        else:
            stdscr.addstr(action_y, x, action)

    stdscr.refresh()


def prompt_unsaved(stdscr):
    h, w = stdscr.getmaxyx()
    box_h, box_w = 7, 42
    start_y, start_x = h//2 - box_h//2, w//2 - box_w//2

    win = curses.newwin(box_h, box_w, start_y, start_x)
    win.keypad(True)
    dialog_buttons = ["< 保存 >", "< 不保存 >"]
    current_btn = 1

    while True:
        win.clear()
        win.box()
        win.addstr(2, 6, "警告：您有未保存的配置更改！", curses.A_BOLD | curses.color_pair(3))
        btn_spacing = box_w // (len(dialog_buttons) + 1)
        for idx, btn in enumerate(dialog_buttons):
            x = btn_spacing * (idx + 1) - len(btn) // 2
            if idx == current_btn:
                win.addstr(4, x, btn, curses.color_pair(2))
            else:
                win.addstr(4, x, btn)
        win.refresh()
        key = win.getch()

        if key == curses.KEY_LEFT:
            current_btn = max(0, current_btn - 1)
        elif key == curses.KEY_RIGHT:
            current_btn = min(len(dialog_buttons) - 1, current_btn + 1)
        elif key in [10, 13, ord(' ')]:
            return "SAVE" if current_btn == 0 else "DISCARD"
        elif key == 27:
            return "CANCEL"


def handle_exit_attempt(stdscr, has_unsaved_changes):
    if not has_unsaved_changes:
        return True
    choice = prompt_unsaved(stdscr)
    if choice == "SAVE":
        save_config()
        return True
    elif choice == "DISCARD":
        return True
    else:
        return False


def main(stdscr):
    curses.curs_set(0)
    curses.start_color()
    curses.use_default_colors()
    curses.init_pair(1, curses.COLOR_BLACK, curses.COLOR_CYAN)
    curses.init_pair(2, curses.COLOR_BLACK, curses.COLOR_WHITE)
    curses.init_pair(3, curses.COLOR_YELLOW, -1)

    menu_stack = [{"id": "main", "row": 0, "scroll_offset": 0}]
    current_action = 0
    save_banner_text = None

    while True:
        current_menu_id = menu_stack[-1]["id"]

        visible_items = []
        for item in menus[current_menu_id]["items"]:
            # 现在对 submenu 也会进行 is_dependency_met 的检查！
            if item["type"] == "submenu":
                if is_dependency_met(item):
                    visible_items.append(item)
            else:
                if is_dependency_met(item["data"]):
                    visible_items.append(item)

        current_row = menu_stack[-1]["row"]
        current_row = min(current_row, max(0, len(visible_items) - 1))
        menu_stack[-1]["row"] = current_row

        # 计算并跟焦视口滚动偏移
        h, w = stdscr.getmaxyx()
        max_visible_items = max(1, h - 12)
        scroll_offset = menu_stack[-1].get("scroll_offset", 0)

        if current_row < scroll_offset:
            scroll_offset = current_row
        elif current_row >= scroll_offset + max_visible_items:
            scroll_offset = current_row - max_visible_items + 1

        menu_stack[-1]["scroll_offset"] = scroll_offset

        unsaved_changes = check_unsaved_changes()

        title_name = menus[current_menu_id]["title"]
        draw_menu(stdscr, title_name, visible_items, current_row, current_action,
                  unsaved_changes, scroll_offset, save_banner_text)

        key = stdscr.getch()
        save_banner_text = None  # 任何新按键都会清空瞬时保存成功状态行

        # 窗口大小改变事件
        if key == curses.KEY_RESIZE or key == 410:
            stdscr.clear()
            curses.resizeterm(*stdscr.getmaxyx())
            continue

        if key == 27:
            if len(menu_stack) > 1:
                menu_stack.pop()
                current_action = 0
            else:
                if handle_exit_attempt(stdscr, unsaved_changes):
                    break
                current_action = 0

        elif key == curses.KEY_UP:
            menu_stack[-1]["row"] = max(0, current_row - 1)
        elif key == curses.KEY_DOWN:
            menu_stack[-1]["row"] = min(len(visible_items) - 1, current_row + 1)
        elif key == curses.KEY_LEFT:
            current_action = max(0, current_action - 1)
        elif key == curses.KEY_RIGHT:
            current_action = min(len(ACTIONS) - 1, current_action + 1)

        elif key in [10, 13, ord(' ')]:
            if key in [10, 13] and not visible_items:
                pass
            elif key in [10, 13] and ACTIONS[current_action] == "< 退出/返回 >":
                if len(menu_stack) > 1:
                    menu_stack.pop()
                    current_action = 0
                elif handle_exit_attempt(stdscr, unsaved_changes):
                    break
                else:
                    current_action = 0

            elif key in [10, 13] and ACTIONS[current_action] == "< 保存 >":
                if save_config():
                    save_banner_text = " [ 保存成功！配置已成功写入项目根目录 .config ] "
                    curses.flash()
                current_action = 0

            elif (key == ord(' ') or (key in [10, 13] and ACTIONS[current_action] == "< 选择/进入 >")):
                selected_item = visible_items[current_row]

                if selected_item["type"] == "submenu":
                    menu_stack.append({"id": selected_item["target"], "row": 0, "scroll_offset": 0})

                elif selected_item["type"] == "config":
                    opt = selected_item["data"]

                    if opt["type"] == "bool" and not opt["required"]:
                        opt["enabled"] = not opt["enabled"]

                    elif opt["type"] == "choice_item":
                        for o in options:
                            if o.get("choice_group") == opt["choice_group"] and o.get("enabled", False):
                                o["enabled"] = False
                        opt["enabled"] = True

                    elif opt["type"] == "string":
                        new_val = prompt_string_input(stdscr, opt["name"], opt["value"])
                        if new_val != opt["value"]:
                            opt["value"] = new_val


if __name__ == "__main__":
    try:
        parse_kconfig()
        load_config()
        curses.wrapper(main)
        print(f"\n配置向导已退出。")
        print(f"您的配置文件已保存至: {CONFIG_FILE}\n")
    except KeyboardInterrupt:
        pass
