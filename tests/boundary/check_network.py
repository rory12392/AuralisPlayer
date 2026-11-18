#!/usr/bin/env python3
"""對外連線的程式碼掃描：只有歌詞模組（LYR）可以 include Qt Network 的標頭（安全底線：對外連線只有 LRCLIB）。

Qt6::Qml 的介面本身就帶 Qt6::Network，連結 Qml 的目標（介面殼、主程式、模組的 QML 函式庫）不寫 Network
也拿得到它的標頭，所以建置時的連結守門（cmake/AuralisNetworkGuard.cmake）擋不到，這裡另外掃 include。

用法：python check_network.py --root <repo>     # 結束碼 1 表示有越界
      python check_network.py --self-test        # 確認檢查本身抓得到越界
"""
from __future__ import annotations

import argparse
import re
import sys
import tempfile
from pathlib import Path

CPP_SUFFIXES = {".cpp", ".h"}
SCAN_DIRS = ("src", "tests")
# 相對於 repo 根目錄；LYR 的程式與它自己的測試
ALLOWED_DIRS = ("src/modules/lyrics", "tests/modules/lyrics")
INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]')
# QtNetwork 模組的標頭（Qt 6.11 include/QtNetwork 底下的類別名稱，大小寫不拘、可帶 .h）
NETWORK_HEADER_RE = re.compile(
    r"^q(network\w*|tcp\w*|udp\w*|ssl\w*|dtls\w*|dns\w*|http\w*|hostinfo|hostaddress|local(server|socket)"
    r"|abstractsocket|abstractnetworkcache|rest\w*|hsts\w*|authenticator|formdata\w*|ipv6address"
    r"|ocspresponse|passworddigestor)(\.h)?$",
    re.IGNORECASE,
)


def is_network_header(target: str) -> bool:
    parts = target.replace("\\", "/").split("/")
    if any(part.lower() == "qtnetwork" for part in parts):
        return True
    return bool(NETWORK_HEADER_RE.match(parts[-1]))


def scan(root: Path) -> list[str]:
    violations: list[str] = []
    for top in SCAN_DIRS:
        base = root / top
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*")):
            if not path.is_file() or path.suffix not in CPP_SUFFIXES:
                continue
            rel = path.relative_to(root).as_posix()
            if any(rel.startswith(f"{allowed}/") for allowed in ALLOWED_DIRS):
                continue
            for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
                match = INCLUDE_RE.match(line)
                if match and is_network_header(match.group(1)):
                    violations.append(f"{rel}:{number}: 歌詞模組以外 include Qt Network：{match.group(1)}")
    return violations


def self_test() -> int:
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        files = {
            "src/core/net/bad.cpp": "#include <QNetworkAccessManager>\n#include <QtNetwork/QTcpSocket>\n",
            "src/app/bad.h": '#include "qnetworkreply.h"\n#include <QtNetwork>\n',
            "src/modules/smtc/bad.cpp": "#include <QSslSocket>\n#include <QHostInfo>\n#include <QLocalSocket>\n",
            "tests/core/bad.cpp": "#include <QUdpSocket>\n",
            # 允許的資料夾與不相干的標頭
            "src/modules/lyrics/lrclib_client.cpp": "#include <QNetworkAccessManager>\n#include <QNetworkReply>\n",
            "tests/modules/lyrics/test_lrclib.cpp": "#include <QNetworkRequest>\n",
            "src/core/good.cpp": "#include <QString>\n#include <QHash>\n#include <QLocale>\n#include \"network_path.h\"\n",
        }
        for rel, text in files.items():
            path = root / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding="utf-8")
        found = scan(root)
    expected = 8
    ok = len(found) == expected and all("bad" in v for v in found)
    for line in found:
        print(f"  抓到：{line}")
    print(f"自我測試{'通過' if ok else '失敗'}：預期 {expected} 個越界，實際 {len(found)} 個")
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
    violations = scan(args.root)
    if violations:
        print("歌詞模組以外用到 Qt Network：")
        for line in violations:
            print(f"  {line}")
        return 1
    print("Qt Network 的 include 檢查通過")
    return 0


if __name__ == "__main__":
    sys.exit(main())
