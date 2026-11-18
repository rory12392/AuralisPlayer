#!/usr/bin/env python3
"""T-105：主程式包的打包後檢查（cmake/AuralisNoAsioCheck.cmake）對內建 ASIO 外掛的要求。

用法：python check_bundled_asio.py <cmake> <AuralisNoAsioCheck.cmake> <暫存資料夾>
用假的包資料夾（exe 是沒有 ASIO 字串的小檔）跑檢查：
1. 建置了 ASIO 外掛（AURALIS_BUNDLED_ASIO=ON）、外掛、標誌、標誌說明與 ASIO 授權都在：通過。
2. 少了任一個（例：沒有 SDK 的標誌）：打包失敗，訊息寫出少了哪個檔。
3. 沒有建置 ASIO 外掛（OFF，例：CI）：包裡沒有外掛也通過。
4. exe 裡有 ASIO 的字串：失敗（exe 不連 SDK）。
"""
from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path

PLUGIN_FILES = [
    "plugins/AuralisAsio/AuralisAsio.dll",
    "plugins/AuralisAsio/plugin.json",
    "plugins/AuralisAsio/logo-dark.svg",
    "plugins/AuralisAsio/logo-light.svg",
    "plugins/AuralisAsio/ASIO-LOGO-README.txt",
    "licenses/ASIO-SDK.txt",
    "licenses/ASIO-SDK-host.txt",
]


def make_package(root: Path, files: list[str], exe: bytes = b"MZ no sdk here") -> None:
    shutil.rmtree(root, ignore_errors=True)
    root.mkdir(parents=True)
    (root / "AuralisPlayer.exe").write_bytes(exe)
    for name in files:
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b"x")


def run_check(cmake: str, script: str, root: Path, bundled: str) -> tuple[int, str]:
    run = subprocess.run([cmake, f"-DAURALIS_PACKAGE_ROOT={root}", f"-DAURALIS_BUNDLED_ASIO={bundled}",
                          "-P", script], capture_output=True, timeout=60)
    return run.returncode, (run.stdout + run.stderr).decode("utf-8", errors="replace")


def main() -> int:
    cmake, script, tmp = sys.argv[1], sys.argv[2], Path(sys.argv[3])
    problems = []

    root = tmp / "complete"
    make_package(root, PLUGIN_FILES)
    code, output = run_check(cmake, script, root, "ON")
    print("完整的包：", code)
    if code != 0:
        problems.append(f"外掛、標誌與授權都在卻失敗：{output}")

    for missing in PLUGIN_FILES:
        root = tmp / "missing"
        make_package(root, [f for f in PLUGIN_FILES if f != missing])
        code, output = run_check(cmake, script, root, "ON")
        print(f"少了 {missing}：", code)
        if code == 0 or missing not in output:
            problems.append(f"少了 {missing} 卻沒有失敗或沒寫出檔名：{output}")

    root = tmp / "no-asio"
    make_package(root, [])
    code, output = run_check(cmake, script, root, "OFF")
    print("沒有建置 ASIO 外掛：", code)
    if code != 0:
        problems.append(f"沒有建置 ASIO 外掛的包被擋下：{output}")

    root = tmp / "exe-with-asio"
    make_package(root, PLUGIN_FILES, exe=b"MZ ... ASIOInit ...")
    code, output = run_check(cmake, script, root, "ON")
    print("exe 有 ASIO 字串：", code)
    if code == 0:
        problems.append("exe 裡有 ASIO 的字串卻沒有失敗")

    for problem in problems:
        print(f"失敗：{problem}")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
