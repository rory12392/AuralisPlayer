#!/usr/bin/env python3
"""T-102：tools/collect_sources.py --dry-run 要列出發佈包的對應原始碼清單，而且不寫任何檔案。

用法：python check_collect_sources.py <repo 根目錄> <建置資料夾> <暫存資料夾>
結束碼 0：
- 有本專案原始碼（git archive，指定的 commit）；
- 發佈包用到的 vcpkg port（ffmpeg、sqlite3、taglib、utfcpp、uchardet、libebur128、zlib；ASIO=ON 加 asiosdk）
  每個都在 vcpkg 的 downloads 找到 SHA-512 相符的原始碼封存，port 目錄和 baseline 的樹一致；
- 只在測試用的 catch2 不在清單；
- Qt 的 qtbase、qtdeclarative、qtsvg 有 6.12.1 的 commit 與原始碼封存網址；
- 乾跑不建立輸出資料夾、不連網路；
- 主程式包（main）內建 ASIO 外掛：ASIO=ON 時它的對應原始碼有 asiosdk、OFF 時沒有（T-105）；
  ASIO=ON 時外掛包（plugin-asio）是 asiosdk＋qtbase（T-103）。
"""
from __future__ import annotations

import json
import os
import subprocess
import sys
from pathlib import Path

RUNTIME_PORTS = {"ffmpeg", "sqlite3", "taglib", "utfcpp", "uchardet", "libebur128", "zlib"}
QT_MODULES = {"qtbase", "qtdeclarative", "qtsvg"}


def main() -> int:
    repo, build, tmp = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
    if os.environ.get("GITHUB_ACTIONS") == "true":
        # CI 的 vcpkg 走二進位快取，downloads 沒有原始碼封存；留存原始碼只在發佈的機器上做
        print("略過：CI 上沒有 vcpkg 下載的原始碼封存")
        return 77
    out = tmp / "release-sources"
    run = subprocess.run(
        [sys.executable, str(repo / "tools" / "collect_sources.py"), "--dry-run", "--json",
         "--build-dir", str(build), "--out", str(out)],
        capture_output=True, timeout=600, cwd=repo)
    text = run.stdout.decode("utf-8", errors="replace")
    if run.returncode != 0:
        print(f"collect_sources.py 結束碼 {run.returncode}：\n{text}\n"
              f"{run.stderr.decode('utf-8', errors='replace')}")
        return 1
    try:
        plan = json.loads(text)
    except json.JSONDecodeError:
        print(f"stdout 不是 JSON：\n{text}")
        return 1
    problems = []
    if out.exists():
        problems.append(f"乾跑建立了輸出資料夾 {out}")
    project = plan.get("project", {})
    if not project.get("commit") or not project.get("archive"):
        problems.append("沒有本專案原始碼（commit、archive）")
    ports = {p["name"]: p for p in plan.get("vcpkgPorts", [])}
    expected = set(RUNTIME_PORTS)
    if plan.get("asio"):
        expected.add("asiosdk")
    for name in sorted(expected):
        port = ports.get(name)
        if port is None:
            problems.append(f"清單沒有 port {name}")
            continue
        if port.get("category") != "runtime":
            problems.append(f"{name} 的分類不是 runtime：{port.get('category')}")
        if not port.get("portTreeMatches"):
            problems.append(f"{name} 的 port 目錄和 baseline 的樹不一致")
        archives = port.get("archives", [])
        if not archives:
            problems.append(f"{name} 沒有原始碼封存")
        for archive in archives:
            if not archive.get("localFile"):
                problems.append(f"{name} 的封存 {archive.get('url')} 在 downloads 找不到 SHA-512 相符的檔案")
    if "catch2" in ports:
        problems.append("catch2 只在測試用，不該在清單")
    qt = {m["name"]: m for m in plan.get("qtModules", [])}
    for name in sorted(QT_MODULES):
        module = qt.get(name)
        if module is None:
            problems.append(f"清單沒有 Qt 模組 {name}")
        elif not module.get("commit") or not str(module.get("url", "")).endswith(
                f"{name}-everywhere-src-6.12.1.tar.xz"):
            problems.append(f"Qt 模組 {name} 沒有 commit 或網址不是 6.12.1 的原始碼封存")
    # T-105：主程式包內建 ASIO 外掛，ASIO=ON 時主程式包的對應原始碼也有 asiosdk；
    # 外掛包的對應原始碼是 asiosdk 與 qtbase（T-103）
    packages = plan.get("packages", {})
    main_ports = packages.get("main", {}).get("vcpkgPorts", [])
    if ("asiosdk" in main_ports) != bool(plan.get("asio")):
        problems.append(f"主程式包的對應原始碼有沒有 asiosdk 和 ASIO 建置不一致：{main_ports}")
    if not RUNTIME_PORTS <= set(main_ports):
        problems.append(f"主程式包的對應原始碼少了 port：{sorted(RUNTIME_PORTS - set(main_ports))}")
    if plan.get("asio"):
        plugin = packages.get("plugin-asio", {})
        if plugin.get("vcpkgPorts") != ["asiosdk"] or plugin.get("qtModules") != ["qtbase"]:
            problems.append(f"ASIO 外掛包的對應原始碼不是 asiosdk＋qtbase：{plugin}")
    elif "plugin-asio" in packages:
        problems.append("ASIO=OFF 卻列了外掛包")
    print(f"port {len(ports)} 個、Qt 模組 {len(qt)} 個、本機可收 {plan.get('localBytes')} bytes")
    for problem in problems:
        print(f"失敗：{problem}")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
