#!/usr/bin/env python3
"""T-103：--self-check 的 JSON 有外掛清單（plugins），外掛壞掉不讓自我檢查失敗（程式照常運作）。

用法：python check_self_check_plugins.py <AuralisPlayer.exe> <資料夾>
1. 全新資料夾：除了程式資料夾的內建外掛（builtin 為 true、已啟用；T-105，建置了 ASIO 外掛時才有），
   plugins 沒有別的、結束碼 0。
2. 放一個 plugin.json 壞掉的外掛：plugins 列出它、state 是 invalid、有 reason，errors 仍是空的、結束碼 0。
"""
from __future__ import annotations

import json
import shutil
import subprocess
import sys
from pathlib import Path


def self_check(exe: str, data_dir: Path) -> tuple[int, dict | None, str]:
    run = subprocess.run([exe, "--self-check", "--data-dir", str(data_dir)],
                         capture_output=True, timeout=120)
    output = run.stdout.decode("utf-8", errors="replace")
    try:
        return run.returncode, json.loads(output), output
    except json.JSONDecodeError:
        return run.returncode, None, output


def main() -> int:
    exe, data_dir = sys.argv[1], Path(sys.argv[2])
    shutil.rmtree(data_dir, ignore_errors=True)
    data_dir.mkdir(parents=True)
    problems = []

    code, report, output = self_check(exe, data_dir)
    if report is None:
        print(f"stdout 不是 JSON（結束碼 {code}）：\n{output}")
        return 1
    print("全新資料夾 plugins：", json.dumps(report.get("plugins"), ensure_ascii=False))
    builtin = [p for p in report.get("plugins") or [] if p.get("builtin") is True]
    if [p for p in report.get("plugins") or [] if p not in builtin] != []:
        problems.append(f"全新資料夾的 plugins 除了內建外掛還有別的：{report.get('plugins')}")
    if any(p.get("state") != "enabled" for p in builtin):
        problems.append(f"內建外掛沒有預設啟用：{builtin}")
    if code != 0:
        problems.append(f"全新資料夾的結束碼是 {code}")

    broken = data_dir / "plugins" / "Broken"
    broken.mkdir(parents=True)
    (broken / "plugin.json").write_text("{ 不是 JSON", encoding="utf-8")
    code, report, output = self_check(exe, data_dir)
    if report is None:
        print(f"stdout 不是 JSON（結束碼 {code}）：\n{output}")
        return 1
    plugins = [p for p in report.get("plugins") or [] if p not in builtin]
    print("壞掉的外掛 plugins：", json.dumps(plugins, ensure_ascii=False))
    if not (len(plugins) == 1 and plugins[0].get("id") == "Broken" and plugins[0].get("builtin") is False
            and plugins[0].get("state") == "invalid" and plugins[0].get("reason")):
        problems.append(f"壞掉的外掛沒有列成 invalid：{plugins}")
    if code != 0 or report.get("errors"):
        problems.append(f"壞掉的外掛讓自我檢查失敗：結束碼 {code}、errors {report.get('errors')}")

    for problem in problems:
        print(f"失敗：{problem}")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
