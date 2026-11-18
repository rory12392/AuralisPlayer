"""編譯器啟動器：把 cl 的 /showIncludes 行改寫成固定的 ASCII 標記，讓 Ninja 的相依追蹤與主控台字碼頁無關。

為什麼：非英文版 MSVC 的前綴是本地化文字（例：「注意: 包含檔案: 」），cl 依「建置當下」的主控台
字碼頁輸出（65001 → UTF-8、950 → Big5），CMake 卻依「產生建置檔當下」的字碼頁把前綴寫進
rules.ninja。兩個時間點的字碼頁不同時，Ninja 認不出相依標頭，改了標頭不會重編。

用法（由 cmake/AuralisMsvcDeps.cmake 設成 CMAKE_CXX_COMPILER_LAUNCHER）：
    python -I -S msvc_showincludes.py <本地化前綴的 UTF-8 十六進位> <ASCII 標記> <cl 指令…>
"""

import ctypes
import subprocess
import sys


def main() -> int:
    prefix = bytes.fromhex(sys.argv[1]).decode("utf-8")
    marker = sys.argv[2].encode("ascii")
    command = sys.argv[3:]

    # cl 寫進管線的文字用主控台輸出字碼頁；沒有主控台時用系統 ANSI 字碼頁
    kernel32 = ctypes.windll.kernel32
    codepage = kernel32.GetConsoleOutputCP() or kernel32.GetACP()
    encoding = f"cp{codepage}"

    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

    lines = []
    for line in result.stdout.splitlines(keepends=True):
        text = line.decode(encoding, errors="replace")
        if text.startswith(prefix):
            # 標頭路徑改用 UTF-8（Ninja 1.12 的字碼頁是 UTF-8）
            lines.append(marker + text[len(prefix):].encode("utf-8"))
        else:
            # 錯誤與警告保持原樣，主控台才顯示得正確
            lines.append(line)
    sys.stdout.buffer.write(b"".join(lines))
    sys.stdout.buffer.flush()
    return result.returncode


if __name__ == "__main__":
    sys.exit(main())
