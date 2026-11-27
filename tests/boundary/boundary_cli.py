"""邊界檢查腳本共用的命令列：--root <repo> 掃描、--self-test 跑腳本自己的自我測試。"""
from __future__ import annotations

import argparse
from pathlib import Path
from typing import Callable


def report_self_test(found: list[str], expected: int, ok: bool, noun: str = "越界") -> int:
    """印出自我測試抓到的每一筆與結論；ok 是呼叫端另外判斷的條件（含數量相符）。"""
    for line in found:
        print(f"  抓到：{line}")
    print(f"自我測試{'通過' if ok else '失敗'}：預期 {expected} 個{noun}，實際 {len(found)} 個")
    return 0 if ok else 1


def run(scan: Callable[[Path], list[str]], self_test: Callable[[], int],
        failed_title: str, passed_text: str) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    if args.root is None:
        parser.error("需要 --root 或 --self-test")
    violations = scan(args.root)
    if violations:
        print(failed_title)
        for line in violations:
            print(f"  {line}")
        return 1
    print(passed_text)
    return 0
