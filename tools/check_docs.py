"""Проверяет, что над каждым определением функции в src/ и tests/ стоит комментарий.

    python tools/check_docs.py

Определением считается строка с сигнатурой, за которой идёт тело функции
(`{` в той же строке или на следующей). Сразу над ней должен заканчиваться
комментарий `*/` (атрибуты `__attribute__` между ними допускаются).
Код выхода 1, если есть функции без описания — так проверка роняет CI.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
KEYWORDS = {"if", "for", "while", "switch", "return", "sizeof", "static_assert", "case", "catch"}
SIGNATURE = re.compile(r"^\s*(?:[\w:<>,\*&~\"\s]+?[\s\*&])?(~?[\w:]+|operator\S+)\s*\([^;]*\)\s*(const)?\s*(\{.*\})?\s*$")


def undocumented(path):
    """
    Ищет в файле определения функций без комментария над ними.

    Параметры:
      path — путь к файлу .cpp или .hpp.

    Возвращает: список пар (номер строки, текст строки) для каждой такой функции.
    """
    lines = path.read_text(encoding="utf-8").split("\n")
    found = []
    for i, line in enumerate(lines):
        m = SIGNATURE.match(line)
        if not m or m.group(1).split("::")[-1] in KEYWORDS or line.strip().startswith(("//", "*", "#")):
            continue
        has_body = m.group(3) is not None or ": " in line and "{" in line
        if not has_body and not (i + 1 < len(lines) and lines[i + 1].strip() == "{"):
            continue
        j = i - 1
        while j >= 0 and lines[j].strip().startswith("__attribute__"):
            j -= 1
        if j < 0 or not lines[j].strip().endswith("*/"):
            found.append((i + 1, line.strip()))
    return found


def main():
    """
    Проверяет все исходники и печатает функции без описания.
    Параметров нет.

    Возвращает: код выхода — 0, если описаны все функции, иначе 1.
    """
    files = sorted([*ROOT.glob("src/*.cpp"), *ROOT.glob("src/*.hpp"), *ROOT.glob("tests/*.cpp"), *ROOT.glob("tests/*.hpp")])
    bad = 0
    for path in files:
        for line_no, text in undocumented(path):
            print(f"{path.relative_to(ROOT).as_posix()}:{line_no}: нет описания над функцией: {text}")
            bad += 1
    print(f"проверено файлов: {len(files)}, функций без описания: {bad}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
