"""從 Qt 官方線上倉庫安裝預編譯的 Qt（CI 用；只用 Python 標準函式庫）。

aqtinstall 3.3.0 裝不了 Qt 6.11：官方倉庫從 6.11 起把每個架構拆到自己的資料夾
（qt6_6113/qt6_6113_msvc2022_64/），aqt 找不到 Updates.xml。這個腳本照倉庫的 Updates.xml
下載需要的 .7z、用同名 .sha1 驗證，再用 tar（Windows 10 起內建的 bsdtar 能解 7z）解壓。
aqt 支援新結構後，CI 改回 jurplel/install-qt-action。

用法：
    python tools/install_qt.py --version 6.12.1 --arch win64_msvc2022_64 --out <資料夾>
解壓後的 Qt 在 <資料夾>（裡面直接是 bin/、lib/…，新格式的壓縮檔沒有版本資料夾前綴）。
"""
import argparse
import hashlib
import pathlib
import subprocess
import sys
import tempfile
import urllib.request
import xml.etree.ElementTree as ET

BASE = "https://download.qt.io/online/qtsdkrepository/windows_x86/desktop"
# 本專案用到的模組（qtbase、qtdeclarative、qtsvg、qttools、qtshadertools）；不裝 qtdoc、翻譯、
# d3dcompiler_47 與 opengl32sw（發佈包不帶它們）
WANTED = ("qtbase-", "qtdeclarative-", "qtsvg-", "qttools-", "qtshadertools-")


def fetch(url: str) -> bytes:
    with urllib.request.urlopen(url, timeout=300) as response:
        return response.read()


def main() -> int:
    # CI 的主控台是 cp1252，印中文會丟 UnicodeEncodeError
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--version", required=True, help="例：6.11.3")
    ap.add_argument("--arch", required=True, help="例：win64_msvc2022_64")
    ap.add_argument("--out", required=True, type=pathlib.Path)
    args = ap.parse_args()

    tag = args.version.replace(".", "")
    folder = f"{BASE}/qt6_{tag}/qt6_{tag}_{args.arch.split('_', 1)[1]}"
    packages = (f"qt.qt6.{tag}.{args.arch}", f"qt.qt6.{tag}.addons.qtshadertools.{args.arch}")
    root = ET.fromstring(fetch(f"{folder}/Updates.xml"))
    archives = []
    for update in root.findall("PackageUpdate"):
        name = update.findtext("Name")
        if name not in packages:
            continue
        version = update.findtext("Version")
        for archive in (update.findtext("DownloadableArchives") or "").split(","):
            archive = archive.strip()
            if archive.startswith(WANTED):
                archives.append(f"{folder}/{name}/{version}{archive}")
    found = {a.rsplit("/", 1)[-1] for a in archives}
    missing = [w for w in WANTED if not any(w in f for f in found)]
    if missing:
        print(f"Updates.xml 找不到這些模組：{missing}", file=sys.stderr)
        return 1

    args.out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        for url in archives:
            data = fetch(url)
            expected = fetch(url + ".sha1").decode("ascii").split()[0].strip().lower()
            actual = hashlib.sha1(data).hexdigest()
            if actual != expected:
                print(f"SHA1 不符：{url}（{actual} ≠ {expected}）", file=sys.stderr)
                return 1
            path = pathlib.Path(tmp) / url.rsplit("/", 1)[-1]
            path.write_bytes(data)
            subprocess.run(["tar", "-xf", str(path), "-C", str(args.out)], check=True)
            print(f"已安裝 {path.name}（{len(data):,} bytes）")
    if not (args.out / "bin" / "qmake.exe").exists() and not (args.out / "bin" / "qtpaths.exe").exists():
        print(f"解壓後找不到 {args.out / 'bin'} 的 Qt 工具", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
