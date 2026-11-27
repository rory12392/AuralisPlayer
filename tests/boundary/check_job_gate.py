#!/usr/bin/env python3
"""模組背景工作的停止閘門檢查（T-97，P-LYR-10、P-PL-24 的機器檢查）。

核心的 JobRunner 最後才解構，模組停止後音樂庫、資料庫、封面服務先解構，被取消的排隊工作照樣會開始。
所以每個模組送進 JobRunner 的工作都要用 auralis::JobGate：工作開頭 `JobGate::Pass`，stop() 時 `close(`。
規則（每個 src/modules/<模組>/ 目錄，掃 .cpp 與 .h，略過註解）：
  1. `.submit(` 或 `->submit(` 的次數不能多於 `JobGate::Pass` 的次數（每個工作一個 Pass）；
  2. 有 submit 的模組至少要有一個 `close(`（停止時關閉閘門並等）。
以模組為單位而不是以檔案：工作的內容可以寫在別的檔（例：SMTC 由模組進入點 submit、工作在 smtc_refresh）。

用法：python check_job_gate.py --root <repo>     # 結束碼 1 表示有模組沒用閘門
      python check_job_gate.py --self-test        # 確認檢查本身抓得到
"""
from __future__ import annotations

import re
import sys
import tempfile
from pathlib import Path

import boundary_cli

CPP_SUFFIXES = {".cpp", ".h"}
SUBMIT_RE = re.compile(r"(?:\.|->)submit\s*\(")
PASS_RE = re.compile(r"\bJobGate::Pass\b")
# 閘門的 close（名稱含 gate，例 m_gate->close(）；QFile 等的 close() 不算
CLOSE_RE = re.compile(r"\w*[Gg]ate\w*\s*(?:\.|->)close\s*\(")
LINE_COMMENT_RE = re.compile(r"//.*$")
BLOCK_COMMENT_RE = re.compile(r"/\*.*?\*/", re.DOTALL)


def strip_comments(text: str) -> str:
    text = BLOCK_COMMENT_RE.sub("", text)
    return "\n".join(LINE_COMMENT_RE.sub("", line) for line in text.splitlines())


def scan(root: Path) -> list[str]:
    violations: list[str] = []
    modules = root / "src" / "modules"
    if not modules.is_dir():
        return violations
    for module in sorted(p for p in modules.iterdir() if p.is_dir()):
        submits: list[str] = []
        passes = 0
        closes = 0
        for path in sorted(module.rglob("*")):
            if not path.is_file() or path.suffix not in CPP_SUFFIXES:
                continue
            code = strip_comments(path.read_text(encoding="utf-8"))
            rel = path.relative_to(root).as_posix()
            for number, line in enumerate(code.splitlines(), 1):
                if SUBMIT_RE.search(line):
                    submits.append(f"{rel}:{number}")
            passes += len(PASS_RE.findall(code))
            closes += len(CLOSE_RE.findall(code))
        name = module.relative_to(root).as_posix()
        if len(submits) > passes:
            violations.append(
                f"{name}：送背景工作 {len(submits)} 處（{'、'.join(submits)}），"
                f"JobGate::Pass 只有 {passes} 個；每個工作開頭要 `const JobGate::Pass pass(*gate); if (!pass) return;`"
            )
        if submits and closes == 0:
            violations.append(f"{name}：有背景工作但沒有 close(；stop() 要先取消、再 gate->close(<時限>)")
    return violations


def self_test() -> int:
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        files = {
            # 有 submit、沒有 Pass 也沒有 close：兩條都違反
            "src/modules/bad_none/a.cpp": "void f() { m_jobs->submit(tr(\"x\"), [](auto&) {}); }\n",
            # 兩個 submit 只有一個 Pass（第四個模組寫錯的情況）
            "src/modules/bad_count/a.cpp": (
                "void f() { jobs.submit(n, [g](auto&) { const JobGate::Pass pass(*g); }); }\n"
                "void h() { m_jobs->submit(n, [](auto&) {}); }\n"
                "void stop() { m_gate->close(7000); }\n"
            ),
            # 註解裡的 Pass 與 close 不算
            "src/modules/bad_comment/a.cpp": (
                "void f() { m_jobs->submit(n, [](auto&) {}); }\n"
                "// const JobGate::Pass pass(*gate); m_gate->close(7000);\n"
                "/* JobGate::Pass */\n"
            ),
            # 工作內容在別的檔（SMTC 的寫法）：以模組計算，通過
            "src/modules/good_split/module.cpp": "void s() { jobs.submit(n, std::move(job)); }\nvoid stop() { r->shutdown(7000); }\n",
            "src/modules/good_split/refresh.cpp": (
                "void r() { m_submit([gate](auto&) { const JobGate::Pass pass(*gate); }); }\n"
                "void shutdown(int t) { m_gate->close(t); }\n"
            ),
            # 沒有背景工作的模組（m_submit( 不是 JobRunner 的 submit）
            "src/modules/good_none/a.cpp": "void f() { m_submit(job); m_pool.start(task); }\n",
            # 檔案的 close() 不算閘門的 close
            "src/modules/bad_file_close/a.cpp": (
                "void f() { m_jobs->submit(n, [g](auto&) { const JobGate::Pass pass(*g); }); }\n"
                "void w() { file.close(); m_file->close(); }\n"
            ),
        }
        for rel, text in files.items():
            path = root / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding="utf-8")
        found = scan(root)
    expected = 6  # bad_none 2、bad_count 1、bad_comment 2、bad_file_close 1
    ok = len(found) == expected and all("bad_" in v for v in found)
    return boundary_cli.report_self_test(found, expected, ok, noun="違反")


if __name__ == "__main__":
    sys.exit(boundary_cli.run(scan, self_test,
                              "模組的背景工作沒有用停止閘門（auralis/job_gate.h）：",
                              "背景工作停止閘門檢查通過"))
