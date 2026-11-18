#!/usr/bin/env python3
"""能力合約：每個模組規格「需要核心提供的能力」列的入口，核心公開介面都要有。

入口可以還沒有方法（由對應的核心任務補），但類別要存在；服務類別另外要能從 ModuleHost 取得。
新模組宣告了核心沒有的能力時，這個測試在派工前就失敗。
方法層級（例：PlaybackService::playFrom）不在這裡檢查，由實作該服務的核心任務的單元測試負責。

用法：python check_capabilities.py --root <repo>   # 結束碼 1 表示缺入口
      python check_capabilities.py --self-test
"""
from __future__ import annotations

import argparse
import re
import sys
import tempfile
from pathlib import Path

HEADING = "需要核心提供的能力"
NOT_NEEDED = ("不需要", "不由核心提供")

# 能力欄的文字 → 入口類別（規格多半寫能力的種類，不一定寫類別名稱）
KEYWORDS = [
    ("讀資料", ["LibraryQuery"]),
    ("讀內嵌歌詞", ["LibraryQuery"]),
    ("掃描完成", ["LibraryQuery"]),
    ("寫資料", ["ModuleDatabase"]),
    ("背景工作", ["JobRunner"]),
    ("導航", ["UiRegistry", "Navigator"]),
    ("面板槽位", ["UiRegistry"]),
    ("全螢幕頁", ["UiRegistry"]),
    ("設定區塊", ["UiRegistry"]),
    ("檔案選擇", ["Dialogs"]),
    ("確認對話框", ["Dialogs"]),
    ("通知列", ["Notifier"]),
    ("播放", ["PlaybackService"]),
    ("輸出後端", ["OutputRegistry"]),
    ("音源", ["SourceRegistry"]),
    ("處理器", ["ProcessorChain"]),
    ("封面", ["CoverService"]),
    ("文字解碼", ["TextDecoder"]),
    ("主視窗", ["PlatformWindow"]),
    ("生命週期", ["IModule"]),
]
# 這些是服務：除了類別存在，ModuleHost 也要有回傳它的入口
SERVICES = {
    "LibraryQuery", "ModuleDatabase", "ModuleSettings", "PlaybackService", "OutputRegistry",
    "SourceRegistry", "ProcessorChain", "JobRunner", "CoverService", "UiRegistry", "Navigator",
    "Dialogs", "Notifier", "PlatformWindow", "PluginRegistry",
}
BACKTICK_RE = re.compile(r"`([A-Z][A-Za-z0-9]*)(?:::[A-Za-z0-9_]+(?:\(\))?)?`")


def required_capabilities(spec: Path) -> dict[str, list[str]]:
    """回傳 {入口類別: [來源描述]}。"""
    needed: dict[str, list[str]] = {}
    in_section = False
    for line in spec.read_text(encoding="utf-8").splitlines():
        if HEADING in line:
            in_section = True
            continue
        if not in_section:
            continue
        stripped = line.strip()
        if not stripped.startswith("|"):
            if stripped.startswith("- **") or stripped.startswith("#"):
                break
            continue
        cells = [c.strip() for c in stripped.strip("|").split("|")]
        if len(cells) < 3 or cells[0] in ("能力", "") or set(cells[0]) <= {"-"}:
            continue
        capability, status = cells[0], cells[-1]
        if any(word in status for word in NOT_NEEDED):
            continue
        names = {m.group(1) for m in BACKTICK_RE.finditer(capability)}
        names = {n for n in names if not n.startswith("Q")}  # Qt 的類別不由核心提供
        for keyword, mapped in KEYWORDS:
            if keyword in capability:
                names.update(mapped)
        for name in names:
            needed.setdefault(name, []).append(f"{spec.name}：{capability}")
    return needed


def declared_types(api_dir: Path) -> tuple[set[str], str]:
    text = "\n".join(p.read_text(encoding="utf-8") for p in sorted(api_dir.rglob("*.h")))
    # 只算定義（class X { 或 struct X {、class X : …），不算前置宣告 class X;
    names = set(re.findall(r"\b(?:class|struct)\s+([A-Z][A-Za-z0-9]*)\s*(?:final\s*)?[:{]", text))
    return names, text


def check(root: Path) -> list[str]:
    api_dir = root / "src" / "core" / "api"
    declared, api_text = declared_types(api_dir)
    problems: list[str] = []
    for spec in sorted((root / "docs" / "specs").glob("*.md")):
        for name, sources in sorted(required_capabilities(spec).items()):
            if name not in declared:
                problems.append(f"缺少入口 {name}（{sources[0]}）")
            elif name in SERVICES and not re.search(rf"\b{name}&\s*\w+\(\)", api_text):
                problems.append(f"ModuleHost 沒有回傳 {name}& 的入口（{sources[0]}）")
    return problems


def self_test() -> int:
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        (root / "docs" / "specs").mkdir(parents=True)
        (root / "src" / "core" / "api" / "auralis").mkdir(parents=True)
        (root / "docs" / "specs" / "demo.md").write_text(
            "- **需要核心提供的能力**：\n\n"
            "  | 能力 | 用在哪個 AC | 狀態 |\n  |---|---|---|\n"
            "  | 讀資料：`LibraryQuery` | AC-1 | 待核心任務 T-09 |\n"
            "  | 封面 `CoverService` | AC-2 | 待核心任務 T-12 |\n"
            "  | 網路 | AC-3 | 不由核心提供：自己建立 `QNetworkAccessManager` |\n"
            "  | 寫資料 | — | 不需要 |\n\n- **擁有的資料**：無\n",
            encoding="utf-8",
        )
        (root / "src" / "core" / "api" / "auralis" / "x.h").write_text(
            "class CoverService;\nclass LibraryQuery {};\n"
            "class ModuleHost { virtual LibraryQuery& library() = 0; };\n",
            encoding="utf-8",
        )
        problems = check(root)
    for line in problems:
        print(f"  抓到：{line}")
    # 預期：CoverService 只有前置宣告 → 缺少；網路與寫資料不檢查
    ok = len(problems) == 1 and "CoverService" in problems[0]
    print(f"自我測試{'通過' if ok else '失敗'}")
    return 0 if ok else 1


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    if args.root is None:
        parser.error("需要 --root 或 --self-test")
    problems = check(args.root)
    if problems:
        print("能力合約不符：")
        for line in problems:
            print(f"  {line}")
        return 1
    print("能力合約檢查通過")
    return 0


if __name__ == "__main__":
    sys.exit(main())
