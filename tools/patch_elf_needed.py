#!/usr/bin/env python3
"""Бинарно-безопасная замена NEEDED-имён в ELF-файлах payload.

Почему не sed: GNU sed в Git Bash повреждает ELF (меняет размер файла).
Замены равной длины -> перезаписываем байты на месте, размер не меняется.
"""
import sys
from pathlib import Path

# (что меняем, на что) - длины обязаны совпадать
PAIRS = [
    (b"libutils.so", b"libutdlb.so"),                                  # 11 -> 11
    (b"libhidlbase.so", b"libhidldlbs.so"),                            # 14 -> 14
    (b"libstagefright_foundation.so", b"libstagefright_fdtn_dolby.so") # 28 -> 28
]

def patch_file(path: Path) -> int:
    data = path.read_bytes()
    orig_len = len(data)
    total = 0
    for old, new in PAIRS:
        assert len(old) == len(new), f"length mismatch: {old} vs {new}"
        n = data.count(old)
        if n:
            data = data.replace(old, new)
            total += n
    if total and len(data) != orig_len:
        raise RuntimeError(f"{path}: size changed {orig_len} -> {len(data)}")
    if total:
        path.write_bytes(data)
    return total

def main() -> None:
    root = Path(sys.argv[1] if len(sys.argv) > 1 else "payload")
    # сами перенесённые копии не патчим повторно (в них нет этих ссылок, кроме libhidldlbs->libutils, что ок)
    skip = {"libutdlb.so"}
    changed = 0
    for f in sorted(root.rglob("*")):
        if not f.is_file():
            continue
        if f.name in skip:
            continue
        n = patch_file(f)
        if n:
            print(f"{f.as_posix()}: {n} замен")
            changed += 1
    print(f"Итого файлов изменено: {changed}")

if __name__ == "__main__":
    main()
