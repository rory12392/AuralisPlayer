#!/usr/bin/env python3
"""模組邊界的 include 掃描：功能模組只能 include 核心公開介面（auralis/…）與自己的檔案。

建置時模組的 include 路徑只有 src/core/api/，但用相對路徑（../../core/…）或絕對路徑仍可能繞過，
所以另外掃描原始碼。QML 不得用相對路徑匯入別的資料夾。

用法：python check_includes.py --root <repo>     # 結束碼 1 表示有越界
      python check_includes.py --self-test        # 確認檢查本身抓得到越界
"""
from __future__ import annotations

import re
import sys
import tempfile
from pathlib import Path

import boundary_cli

CPP_SUFFIXES = {".cpp", ".h"}
INCLUDE_RE = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]')
QML_IMPORT_RE = re.compile(r'^\s*import\s+"([^"]+)"')
# 這些開頭代表核心內部、主程式、介面殼或其他模組
FORBIDDEN_PREFIXES = ("core/", "app/", "ui/", "modules/", "src/")
# 核心內部的子資料夾：核心自己的 include 以這些開頭（"library/…"、"audio/…"），模組不能這樣寫。
# 掃描時再加上 src/core/ 底下實際有的子資料夾（api 除外），核心之後新增資料夾不用回來改這裡
CORE_INTERNAL_DIRS = (
    "audio", "covers", "jobs", "library", "logging", "modules", "platform", "playback", "settings", "ui",
)


def core_internal_prefixes(root: Path) -> tuple[str, ...]:
    names = set(CORE_INTERNAL_DIRS)
    core_dir = root / "src" / "core"
    if core_dir.is_dir():
        names.update(p.name for p in core_dir.iterdir() if p.is_dir() and p.name != "api")
    return tuple(sorted(f"{name}/" for name in names))


def is_own_file(target: str, source: Path, module_dir: Path) -> bool:
    """引號 include 指到模組自己資料夾裡實際存在的檔案（相對於引用它的檔案或模組根目錄）。"""
    for base in (source.parent, module_dir):
        candidate = (base / target).resolve()
        if candidate.is_file() and candidate.is_relative_to(module_dir.resolve()):
            return True
    return False


def check_cpp_include(target: str, core_prefixes: tuple[str, ...] = (), own_file: bool = False) -> str | None:
    normalized = target.replace("\\", "/")
    if normalized.startswith("auralis/"):
        return None
    if ".." in normalized.split("/"):
        return "用相對路徑跳出模組資料夾"
    if re.match(r"^[A-Za-z]:/", normalized) or normalized.startswith("/"):
        return "用絕對路徑 include"
    if own_file:
        return None
    if normalized.startswith(FORBIDDEN_PREFIXES):
        return "include 核心內部或其他模組（只能用 auralis/…）"
    if normalized.startswith(core_prefixes):
        return "include 核心內部的路徑（只能用 auralis/…）"
    return None


def scan(root: Path) -> list[str]:
    violations: list[str] = []
    modules_dir = root / "src" / "modules"
    if not modules_dir.is_dir():
        return violations
    core_prefixes = core_internal_prefixes(root)
    for path in sorted(modules_dir.rglob("*")):
        if not path.is_file():
            continue
        rel = path.relative_to(root).as_posix()
        module_dir = modules_dir / path.relative_to(modules_dir).parts[0]
        if path.suffix in CPP_SUFFIXES:
            for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
                match = INCLUDE_RE.match(line)
                if match:
                    own = match.group(1) == '"' and is_own_file(match.group(2), path, module_dir)
                    reason = check_cpp_include(match.group(2), core_prefixes, own)
                    if reason:
                        violations.append(f"{rel}:{number}: {reason}：{match.group(2)}")
        elif path.suffix == ".qml":
            for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
                match = QML_IMPORT_RE.match(line)
                if match and (".." in match.group(1).replace("\\", "/").split("/")):
                    violations.append(f"{rel}:{number}: QML 用相對路徑匯入別的資料夾：{match.group(1)}")
    return violations


def self_test() -> int:
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        mod = root / "src" / "modules" / "demo"
        (mod / "qml").mkdir(parents=True)
        (mod / "good.cpp").write_text(
            '#include <auralis/module.h>\n#include "demo_page.h"\n#include <QString>\n#include <vector>\n',
            encoding="utf-8",
        )
        (mod / "bad.cpp").write_text(
            '#include "../../core/jobs/job_runner.cpp"\n'
            '#include "modules/module_manager.h"\n'
            '#include <modules/other/thing.h>\n'
            '#include "E:/Code/x.h"\n',
            encoding="utf-8",
        )
        (mod / "qml" / "Page.qml").write_text('import QtQuick\nimport "../../other/qml"\n', encoding="utf-8")
        # 核心內部的寫法（核心自己用 "library/…"、"audio/…"）：模組這樣寫要被擋，引號與角括號都算；
        # src/core/ 底下實際有、但不在固定清單裡的資料夾（newdir）也要擋，api 不算核心內部
        (root / "src" / "core" / "newdir").mkdir(parents=True)
        (root / "src" / "core" / "api").mkdir()
        (mod / "bad_core.cpp").write_text(
            '#include "library/library_manager.h"\n'
            '#include "audio/playback_engine.h"\n'
            '#include <playback/playback_queue.h>\n'
            '#include "covers/cover_service_impl.h"\n'
            '#include "settings/settings_store.h"\n'
            '#include "logging/log.h"\n'
            '#include "jobs/job_runner_impl.h"\n'
            '#include "platform/long_path.h"\n'
            '#include "newdir/thing.h"\n',
            encoding="utf-8",
        )
        # 模組自己的子資料夾可以和核心的資料夾同名：檔案真的在模組裡就放行；api/ 開頭不是核心內部的寫法
        (mod / "audio").mkdir()
        (mod / "audio" / "dsd_source.h").write_text("#pragma once\n", encoding="utf-8")
        (mod / "good_own.cpp").write_text(
            '#include "audio/dsd_source.h"\n#include "good_own.moc"\n#include <QtCore/QObject>\n#include "api/x.h"\n',
            encoding="utf-8",
        )
        (mod / "audio" / "good_nested.cpp").write_text('#include "dsd_source.h"\n', encoding="utf-8")
        found = scan(root)
    expected = 14
    ok = (
        len(found) == expected
        and all("good" not in v for v in found)
        and sum("bad_core.cpp" in v for v in found) == 9
    )
    return boundary_cli.report_self_test(found, expected, ok)


if __name__ == "__main__":
    sys.exit(boundary_cli.run(scan, self_test, "模組越界的 include：", "模組 include 檢查通過"))
