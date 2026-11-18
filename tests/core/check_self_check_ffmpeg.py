#!/usr/bin/env python3
"""T-98：--self-check 的 JSON 要有 FFmpeg 的版本與建置設定，發佈前的授權檢查照它判斷（STACK §10）。

用法：python check_self_check_ffmpeg.py <AuralisPlayer.exe> <資料夾>
結束碼 0：有 ffmpeg 欄位、建置設定不是空的、沒有 --enable-gpl／--enable-nonfree、授權是 LGPL。
--disable-network 照實記在 networkDisabled，不在這裡判斷（vcpkg 設定不由本專案改）。
"""
from __future__ import annotations

import json
import subprocess
import sys


def main() -> int:
    exe, data_dir = sys.argv[1], sys.argv[2]
    run = subprocess.run([exe, "--self-check", "--data-dir", data_dir],
                         capture_output=True, timeout=120)
    output = run.stdout.decode("utf-8", errors="replace")
    try:
        report = json.loads(output)
    except json.JSONDecodeError:
        print(f"stdout 不是 JSON（結束碼 {run.returncode}）：\n{output}")
        return 1
    ffmpeg = report.get("ffmpeg")
    print(json.dumps(ffmpeg, ensure_ascii=False, indent=2))
    problems = []
    if not isinstance(ffmpeg, dict):
        problems.append("JSON 沒有 ffmpeg 物件")
    else:
        if not ffmpeg.get("version"):
            problems.append("ffmpeg.version 是空的")
        configuration = ffmpeg.get("configuration", "")
        if not configuration:
            problems.append("ffmpeg.configuration 是空的")
        if "--enable-gpl" in configuration or ffmpeg.get("gpl") is not False:
            problems.append("FFmpeg 開了 --enable-gpl，或 gpl 不是 false")
        if "--enable-nonfree" in configuration or ffmpeg.get("nonfree") is not False:
            problems.append("FFmpeg 開了 --enable-nonfree，或 nonfree 不是 false")
        if not str(ffmpeg.get("license", "")).startswith("LGPL"):
            problems.append(f"ffmpeg.license 不是 LGPL：{ffmpeg.get('license')}")
        if not isinstance(ffmpeg.get("networkDisabled"), bool):
            problems.append("ffmpeg.networkDisabled 不是 true／false")
    for problem in problems:
        print(f"失敗：{problem}")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
