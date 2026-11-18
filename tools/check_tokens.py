#!/usr/bin/env python3
"""QML token 檢查（顏色、字級、間距一律用 Theme 的 token）。

用法：python tools/check_tokens.py [資料夾 ...]（預設 src）
結束碼：有「失敗」項目時為 1；只有「警告」時為 0。

規則的依據：元件只能用 QML 單例 Theme 的語意化 token。
例外位置：src/ui/theme/（token 定義本身）可以寫色碼與數值；
src/ui/components/ 可以匯入 QtQuick.Templates。
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

THEME_DIR = "src/ui/theme/"
COMPONENTS_DIR = "src/ui/components/"

# 失敗：一定要改
FAIL_RULES = [
    ("寫死的色碼", re.compile(r"""["']#[0-9a-fA-F]{3,8}["']""")),
    ("用函式組出顏色", re.compile(r"\bQt\.(rgba|hsla|hsva|color|lighter|darker|tint)\s*\(")),
    ("顏色屬性指定了字串（色名或色碼）", re.compile(r"""\b\w*[cC]olor\s*:\s*["']""")),
    ("寫死的字級", re.compile(r"\bfont\.(pixelSize|pointSize)\s*:\s*\d")),
    ("寫死的字級", re.compile(r"\b(pixelSize|pointSize)\s*:\s*\d")),
    ("寫死的字體", re.compile(r"""\bfont\.famil(y|ies)\s*:\s*[\["']""")),
    ("寫死的字體", re.compile(r"""\bfamily\s*:\s*["']""")),
    ("寫死的字重", re.compile(r"\bfont\.weight\s*:\s*(\d|Font\.)")),
    ("陰影或模糊效果（只有 Theme 的琥珀光暈可用）", re.compile(r"\b(DropShadow|MultiEffect|RectangularShadow|FastBlur|GaussianBlur)\s*\{")),
    ("漸層", re.compile(r"\b(Gradient|LinearGradient|RadialGradient|ConicalGradient)\s*\{")),
]
# 只在共用元件以外的地方失敗
FAIL_OUTSIDE_COMPONENTS = [
    ("直接匯入 QtQuick.Controls 或 Templates（請用 src/ui/components 的共用元件）",
     re.compile(r"^\s*import\s+QtQuick\.(Controls|Templates)\b")),
]
# 共用元件裡也不能用有內建外觀的 Controls
FAIL_IN_COMPONENTS = [
    ("共用元件請匯入 QtQuick.Templates，不要匯入 QtQuick.Controls",
     re.compile(r"^\s*import\s+QtQuick\.Controls\b")),
]
# 警告：不擋合併，同一條重複出現時改成失敗
WARN_RULES = [
    ("寫死的圓角（用 Theme.radius0 或 Theme.radiusFull）", re.compile(r"\bradius\s*:\s*\d")),
    ("寫死的間距（用 Theme.space*）", re.compile(r"\b(spacing|padding|leftPadding|rightPadding|topPadding|bottomPadding|margins|leftMargin|rightMargin|topMargin|bottomMargin|rowSpacing|columnSpacing)\s*:\s*[1-9]\d*\b")),
    ("寫死的動畫時間（用 Theme.motion*）", re.compile(r"\bduration\s*:\s*\d")),
    ("介面文字沒有包 qsTr()", re.compile(r"""\b(text|title|placeholderText|label)\s*:\s*["'][^"']*[一-鿿]""")),
    ("禁用的文案", re.compile(r"了解更多|立即體驗|點這裡|哎呀|！")),
    ("佔位字", re.compile(r"lorem ipsum|TODO|待補", re.IGNORECASE)),
]

ALLOW_MARK = "token-check: allow"  # 同一行加這個註解並寫理由，可放行一行


def strip_comment(line: str) -> str:
    """去掉行尾 // 註解（不處理字串裡的 //，QML 樣式值不會用到）。"""
    idx = line.find("//")
    return line if idx < 0 else line[:idx]


def check_file(path: Path, rel: str) -> tuple[list[str], list[str]]:
    fails: list[str] = []
    warns: list[str] = []
    in_theme = rel.startswith(THEME_DIR)
    in_components = rel.startswith(COMPONENTS_DIR)
    in_block_comment = False
    for no, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if in_block_comment:
            if "*/" in raw:
                in_block_comment = False
            continue
        if raw.lstrip().startswith("/*"):
            in_block_comment = "*/" not in raw
            continue
        if ALLOW_MARK in raw:
            continue
        line = strip_comment(raw)
        if not line.strip():
            continue
        if not in_theme:
            for name, rx in FAIL_RULES:
                if rx.search(line):
                    fails.append(f"{rel}:{no}: {name}: {raw.strip()}")
                    break
            for name, rx in WARN_RULES:
                if rx.search(line):
                    warns.append(f"{rel}:{no}: {name}: {raw.strip()}")
                    break
        extra = FAIL_IN_COMPONENTS if in_components else ([] if in_theme else FAIL_OUTSIDE_COMPONENTS)
        for name, rx in extra:
            if rx.search(line):
                fails.append(f"{rel}:{no}: {name}: {raw.strip()}")
    return fails, warns


def main(argv: list[str]) -> int:
    roots = [Path(a) for a in argv] or [Path("src")]
    fails: list[str] = []
    warns: list[str] = []
    count = 0
    for root in roots:
        for path in sorted(root.rglob("*.qml")):
            rel = path.as_posix()
            f, w = check_file(path, rel)
            fails += f
            warns += w
            count += 1
    for w in warns:
        print(f"警告 {w}")
    for f in fails:
        print(f"失敗 {f}")
    print(f"token 檢查：{count} 個 QML 檔，失敗 {len(fails)}，警告 {len(warns)}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
