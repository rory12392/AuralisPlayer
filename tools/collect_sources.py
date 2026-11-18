"""發佈時留存 GPLv3 要求的對應原始碼（T-102）。

用法（在發佈用的建置資料夾打包之後跑）：
    python tools/collect_sources.py --dry-run            # 只列清單與大小，不寫檔、不連網路
    python tools/collect_sources.py --dry-run --check-remote   # 另外對 Qt 原始碼封存送 HEAD 查大小（不下載）
    python tools/collect_sources.py                      # 收本機已有的部分到 .localdata/release-sources/<版本>/
    python tools/collect_sources.py --download-qt        # 連 Qt 原始碼封存一起下載（約 90 MB，先確認要下載）

收的東西：
1. 本專案原始碼：`git archive <commit>`（含 CMake、presets、vcpkg.json 與 baseline、cmake/、打包腳本）。
2. 發佈包用到的 vcpkg port：每個 port 從建置資料夾的 `vcpkg_installed/<triplet>/share/<port>/vcpkg.spdx.json`
   讀版本、port 目錄的 git 樹與原始碼封存的 SHA-512；封存從 vcpkg 的 `downloads/` 找 SHA-512 相符的檔案，
   port 目錄（portfile、補丁）用 `git archive <樹>` 從 vcpkg repo 取出，並核對和 vcpkg.json 的 baseline 一致。
   ASIO SDK 也在這裡（asiosdk port 下載的 Steinberg 封存）。只在測試用的 port（catch2）不收；
   建置用的 vcpkg 輔助 port 分類為 build，照樣列出。
   每個 port 標出屬於哪個發佈包（T-103）：asiosdk 標 plugin-asio。主程式包（main）內建 ASIO 外掛（T-105），
   所以它的對應原始碼是全部的 port；只有外掛的包（plugin-asio）是 asiosdk 與外掛連結的 Qt Core（qtbase）。
   MANIFEST.json 的 packages 列出兩個包各自的對應原始碼。
3. Qt：發佈包裡的 Qt DLL 來自 qtbase、qtdeclarative、qtsvg；版本與 commit 從 Qt 安裝的 `sbom/*.spdx.json` 讀，
   原始碼封存是 download.qt.io 的 `<模組>-everywhere-src-<版本>.tar.xz`。預設只列網址，不下載。
最後寫 MANIFEST.json 與 SHA256SUMS.txt。
"""

from __future__ import annotations

import argparse
import hashlib
import http.client
import json
import re
import shutil
import subprocess
import sys
import urllib.parse
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TEST_ONLY_PORTS = {"catch2"}
BUILD_HELPER_PORTS = {"ffmpeg-bin2c", "pkgconf"}  # 另外 vcpkg-* 開頭的都是建置輔助
QT_RUNTIME_MODULES = ["qtbase", "qtdeclarative", "qtsvg"]
# 只屬於 ASIO 外掛包的 port（T-103）；外掛另外連結 Qt Core，所以外掛包的對應原始碼也列 qtbase
PLUGIN_ASIO_PORTS = {"asiosdk"}
PLUGIN_ASIO_QT = ["qtbase"]


def read_cmake_cache(build: Path) -> dict[str, str]:
    cache = {}
    for line in (build / "CMakeCache.txt").read_text(encoding="utf-8", errors="replace").splitlines():
        match = re.match(r"^([A-Za-z0-9_]+):[A-Z]+=(.*)$", line)
        if match:
            cache[match.group(1)] = match.group(2)
    return cache


def git(cwd: Path, *args: str) -> bytes:
    return subprocess.run(["git", "-C", str(cwd), *args], check=True, capture_output=True).stdout


def sha256_of(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def file_hash(path: Path, algorithm: str) -> str:
    digest = hashlib.new(algorithm)
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


class Downloads:
    """vcpkg 的 downloads/ 資料夾：依 SHA-512 找檔案（先試推得出的檔名，找不到才全部算一次）。"""

    def __init__(self, folder: Path) -> None:
        self.folder = folder
        self._all: dict[str, Path] | None = None

    def find(self, sha512: str, names: list[str]) -> Path | None:
        for name in names:
            candidate = self.folder / name
            if candidate.is_file() and file_hash(candidate, "sha512") == sha512:
                return candidate
        if self._all is None:
            self._all = {}
            for candidate in self.folder.iterdir():
                if candidate.is_file():
                    self._all[file_hash(candidate, "sha512")] = candidate
        return self._all.get(sha512)


def archive_url_and_names(location: str, spdx_name: str) -> tuple[str, list[str]]:
    """把 spdx 的 downloadLocation 轉成可下載的網址與 vcpkg 慣用的檔名。"""
    match = re.match(r"^git\+https://github\.com/([^/]+)/([^@]+)@(.+)$", location)
    if match:
        owner, repo, ref = match.groups()
        url = f"https://github.com/{owner}/{repo}/archive/{ref}.tar.gz"
        return url, [f"{owner}-{repo}-{ref.replace('/', '-')}.tar.gz"]
    match = re.match(r"^git\+(https://gitlab\.[^/]+)/([^/]+)/([^@]+)@(.+)$", location)
    if match:
        host, owner, repo, ref = match.groups()
        url = f"{host}/{owner}/{repo}/-/archive/{ref}/{repo}-{ref}.tar.gz"
        return url, [f"{owner}-{repo}-{ref.replace('/', '-')}.tar.gz"]
    return location, [spdx_name, Path(urllib.parse.urlparse(location).path).name]


def collect_ports(installed: Path, vcpkg_root: Path, baseline: str) -> list[dict]:
    downloads = Downloads(vcpkg_root / "downloads")
    ports = []
    for spdx_file in sorted(installed.glob("share/*/vcpkg.spdx.json")):
        spdx = json.loads(spdx_file.read_text(encoding="utf-8"))
        packages = spdx["packages"]
        port_pkg = packages[0]
        name = port_pkg["name"]
        if name in TEST_ONLY_PORTS:
            continue
        tree = port_pkg["downloadLocation"].rsplit("@", 1)[-1]
        try:
            baseline_tree = git(vcpkg_root, "rev-parse", f"{baseline}:ports/{name}").decode().strip()
        except subprocess.CalledProcessError:
            baseline_tree = ""
        archives = []
        for pkg in packages[2:]:
            checksums = {c["algorithm"]: c["checksumValue"] for c in pkg.get("checksums", [])}
            if "SHA512" not in checksums:
                continue
            url, names = archive_url_and_names(pkg["downloadLocation"], pkg["name"])
            local = downloads.find(checksums["SHA512"].lower(), names)
            archives.append({
                "url": url,
                "sha512": checksums["SHA512"].lower(),
                "localFile": str(local) if local else None,
                "bytes": local.stat().st_size if local else None,
            })
        category = "build" if name.startswith("vcpkg-") or name in BUILD_HELPER_PORTS else "runtime"
        port_archive = git(vcpkg_root, "archive", "--format=tar.gz", f"--prefix=ports/{name}/", tree)
        ports.append({
            "name": name,
            "version": port_pkg.get("versionInfo"),
            "category": category,
            "package": "plugin-asio" if name in PLUGIN_ASIO_PORTS else "main",
            "portTree": tree,
            "portTreeMatches": tree == baseline_tree,
            "portArchiveBytes": len(port_archive),
            "archives": archives,
        })
    return ports


def head_size(url: str) -> int | None:
    """只送 HEAD（跟著轉址，最多 5 次），拿 Content-Length；不下載內容。"""
    for _ in range(5):
        parts = urllib.parse.urlsplit(url)
        conn = http.client.HTTPSConnection(parts.netloc, timeout=30)
        conn.request("HEAD", parts.path + (f"?{parts.query}" if parts.query else ""))
        response = conn.getresponse()
        conn.close()
        if response.status in (301, 302, 303, 307, 308):
            url = urllib.parse.urljoin(url, response.getheader("Location", ""))
            continue
        length = response.getheader("Content-Length")
        return int(length) if response.status == 200 and length else None
    return None


def collect_qt(qt_dir: Path, check_remote: bool) -> list[dict]:
    modules = []
    for name in QT_RUNTIME_MODULES:
        spdx_files = sorted((qt_dir / "sbom").glob(f"{name}-*.spdx.json"))
        if not spdx_files:
            modules.append({"name": name, "version": None, "commit": None, "url": None})
            continue
        version = spdx_files[0].name[len(name) + 1:-len(".spdx.json")]
        spdx = json.loads(spdx_files[0].read_text(encoding="utf-8"))
        commit = next((p["downloadLocation"].rsplit("@", 1)[-1] for p in spdx["packages"]
                       if p["name"] == name and "@" in p.get("downloadLocation", "")), None)
        major_minor = ".".join(version.split(".")[:2])
        url = (f"https://download.qt.io/official_releases/qt/{major_minor}/{version}/submodules/"
               f"{name}-everywhere-src-{version}.tar.xz")
        modules.append({"name": name, "version": version, "commit": commit, "url": url,
                        "remoteBytes": head_size(url) if check_remote else None})
    return modules


def main() -> int:
    parser = argparse.ArgumentParser(description="留存發佈包的對應原始碼（T-102）")
    parser.add_argument("--build-dir", type=Path, default=REPO / "build" / "windows-release")
    parser.add_argument("--commit", default="HEAD", help="本專案的發佈 commit（Release 頁標示的那一個）")
    parser.add_argument("--out", type=Path, help="預設 .localdata/release-sources/<版本>")
    parser.add_argument("--dry-run", action="store_true", help="只列清單與大小，不寫檔")
    parser.add_argument("--json", action="store_true", help="乾跑時輸出 JSON")
    parser.add_argument("--check-remote", action="store_true", help="對 Qt 原始碼封存送 HEAD 查大小")
    parser.add_argument("--download-qt", action="store_true", help="下載 Qt 原始碼封存（先取得同意）")
    args = parser.parse_args()

    build = args.build_dir.resolve()
    cache = read_cmake_cache(build)
    vcpkg_root = Path(cache["CMAKE_TOOLCHAIN_FILE"]).resolve().parents[2]
    qt_dir = Path(cache["Qt6_DIR"]).resolve().parents[2]
    triplet = cache.get("VCPKG_TARGET_TRIPLET", "x64-windows")
    installed = Path(cache.get("VCPKG_INSTALLED_DIR", str(build / "vcpkg_installed"))) / triplet
    asio = cache.get("AURALIS_WITH_ASIO", "OFF").upper() in ("ON", "TRUE", "1")
    manifest = json.loads((REPO / "vcpkg.json").read_text(encoding="utf-8"))
    baseline = manifest["builtin-baseline"]
    version = manifest["version"]
    commit = git(REPO, "rev-parse", args.commit).decode().strip()
    out = (args.out or REPO / ".localdata" / "release-sources" / version).resolve()

    project_archive = git(REPO, "archive", "--format=tar.gz", f"--prefix=AuralisPlayer-{version}/", commit)
    ports = collect_ports(installed, vcpkg_root, baseline)
    qt = collect_qt(qt_dir, args.check_remote)
    # 同一個封存可能被兩個 port 用（ffmpeg 與 ffmpeg-bin2c），大小只算一次
    unique_archives = {a["localFile"]: a["bytes"] for p in ports for a in p["archives"] if a["localFile"]}
    local_bytes = (len(project_archive) + sum(p["portArchiveBytes"] for p in ports)
                   + sum(unique_archives.values()))
    plan = {
        "version": version,
        "asio": asio,
        "vcpkgBaseline": baseline,
        "project": {"commit": commit, "archive": f"AuralisPlayer-{version}-{commit[:12]}.tar.gz",
                    "bytes": len(project_archive)},
        "vcpkgPorts": ports,
        "qtModules": qt,
        # 各發佈包的對應原始碼（本專案原始碼兩個包都有）；主程式包內建 ASIO 外掛（T-105），所以也含 asiosdk
        "packages": {
            "main": {"vcpkgPorts": [p["name"] for p in ports],
                     "qtModules": [m["name"] for m in qt]},
            **({"plugin-asio": {"vcpkgPorts": [p["name"] for p in ports if p["package"] == "plugin-asio"],
                                "qtModules": PLUGIN_ASIO_QT}} if asio else {}),
        },
        "localBytes": local_bytes,
        "qtRemoteBytes": sum(m.get("remoteBytes") or 0 for m in qt) if args.check_remote else None,
        "out": str(out),
    }

    if args.dry_run:
        if args.json:
            print(json.dumps(plan, ensure_ascii=False, indent=2))
            return 0
        print(f"版本 {version}，commit {commit}，ASIO {'ON' if asio else 'OFF'}，vcpkg baseline {baseline}")
        print(f"本專案原始碼：{plan['project']['archive']}  {len(project_archive):,} bytes")
        for p in ports:
            flag = "" if p["portTreeMatches"] else "  ！port 樹和 baseline 不一致"
            print(f"[{p['category']}][{p['package']}] {p['name']} {p['version']}  "
                  f"port 目錄 {p['portArchiveBytes']:,} bytes{flag}")
            for a in p["archives"]:
                where = f"{a['bytes']:,} bytes  {Path(a['localFile']).name}" if a["localFile"] else "本機找不到"
                print(f"    {a['url']}  {where}")
        for m in qt:
            size = f"{m['remoteBytes']:,} bytes" if m.get("remoteBytes") else "大小未查（--check-remote）"
            print(f"[qt] {m['name']} {m['version']} commit {m['commit']}\n    {m['url']}  {size}")
        print(f"本機可收合計 {local_bytes:,} bytes；Qt 要另外下載"
              + (f" {plan['qtRemoteBytes']:,} bytes" if args.check_remote else ""))
        return 0

    missing = [f"{p['name']}: {a['url']}" for p in ports for a in p["archives"] if not a["localFile"]]
    if missing:
        print("下列原始碼封存在 vcpkg 的 downloads 找不到，沒有收：\n  " + "\n  ".join(missing))
        return 1
    files: dict[str, bytes | Path] = {plan["project"]["archive"]: project_archive}
    for p in ports:
        files[f"vcpkg/{p['category']}/{p['name']}/port-{p['name']}.tar.gz"] = git(
            vcpkg_root, "archive", "--format=tar.gz", f"--prefix=ports/{p['name']}/", p["portTree"])
        for a in p["archives"]:
            files[f"vcpkg/{p['category']}/{p['name']}/{Path(a['localFile']).name}"] = Path(a["localFile"])
    out.mkdir(parents=True, exist_ok=True)
    for relative, content in files.items():
        target = out / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        if isinstance(content, Path):
            shutil.copyfile(content, target)
        else:
            target.write_bytes(content)
    if args.download_qt:
        import urllib.request
        for m in qt:
            target = out / "qt" / Path(urllib.parse.urlparse(m["url"]).path).name
            target.parent.mkdir(parents=True, exist_ok=True)
            print(f"下載 {m['url']}")
            urllib.request.urlretrieve(m["url"], target)
    sums = []
    for path in sorted(p for p in out.rglob("*") if p.is_file() and p.name not in ("SHA256SUMS.txt", "MANIFEST.json")):
        sums.append(f"{file_hash(path, 'sha256')}  {path.relative_to(out).as_posix()}")
    (out / "MANIFEST.json").write_text(json.dumps(plan, ensure_ascii=False, indent=2), encoding="utf-8")
    (out / "SHA256SUMS.txt").write_text("\n".join(sums) + "\n", encoding="utf-8")
    print(f"已收 {len(sums)} 個檔案到 {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
