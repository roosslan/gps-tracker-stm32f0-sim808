"""Показывает отладочный лог трекера через ST-LINK (SEGGER RTT) без USB-UART переходника.

Запускает OpenOCD, тот находит в RAM платы блок "SEGGER RTT" и отдаёт канал 0
на TCP-порт; скрипт подключается к порту и печатает лог в терминал.
Ctrl+C — выход (OpenOCD при этом закрывается, прошивка продолжает работать).

    python tools/rtt_log.py                       # подключиться к работающей плате
    python tools/rtt_log.py --reset               # перезапустить плату и смотреть лог с начала
    python tools/rtt_log.py --openocd D:/.../bin/openocd.exe
"""
import argparse
import os
import shutil
import socket
import subprocess
import sys
import time

RAM_START = 0x20000000
RAM_SIZE = 8 * 1024  # STM32F051R8


def find_openocd(explicit):
    """
    Ищет openocd: сначала аргумент --openocd, потом переменная окружения
    OPENOCD, потом PATH.

    Параметры:
      explicit — путь из аргумента --openocd или None.

    Возвращает: путь к openocd или None, если не найден.
    """
    for candidate in (explicit, os.environ.get("OPENOCD")):
        if candidate and os.path.isfile(candidate):
            return candidate
    return shutil.which("openocd")


def start_openocd(openocd, port, reset):
    """
    Запускает OpenOCD в фоне: подключение к плате через встроенный ST-LINK,
    поиск RTT-блока в RAM и TCP-сервер для канала 0. Плата не останавливается.

    Параметры:
      openocd — путь к openocd;
      port    — TCP-порт, на котором OpenOCD отдаст лог;
      reset   — True: перезапустить плату перед подключением, чтобы увидеть
                лог с самого старта.

    Возвращает: запущенный процесс OpenOCD.
    """
    commands = ["init"]
    if reset:
        # Блок RTT создаётся прошивкой при старте — даём ей на это 200 мс.
        commands += ["reset run", "sleep 200"]
    commands += [
        f'rtt setup {RAM_START:#x} {RAM_SIZE} "SEGGER RTT"',
        "rtt start",
        f"rtt server start {port} 0",
    ]
    args = [openocd, "-f", "board/stm32f0discovery.cfg"]
    for c in commands:
        args += ["-c", c]
    return subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)


def connect(port, proc, timeout_s=10.0):
    """
    Ждёт, пока OpenOCD поднимет TCP-сервер, и подключается к нему.

    Параметры:
      port      — TCP-порт RTT-сервера;
      proc      — процесс OpenOCD: если он завершился, ждать бессмысленно;
      timeout_s — сколько секунд ждать.

    Возвращает: подключённый сокет или None, если OpenOCD упал или не успел.
    """
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        if proc.poll() is not None:
            return None
        try:
            return socket.create_connection(("127.0.0.1", port), timeout=1)
        except OSError:
            time.sleep(0.2)
    return None


def main():
    """
    Разбирает аргументы, запускает OpenOCD и печатает лог, пока не нажат
    Ctrl+C или пока OpenOCD не отключится от платы.
    Параметров нет.

    Возвращает: код выхода — 0 при выходе по Ctrl+C, 1 при ошибке.
    """
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--openocd", help="путь к openocd (иначе переменная OPENOCD или PATH)")
    parser.add_argument("--port", type=int, default=9090, help="TCP-порт для RTT (по умолчанию 9090)")
    parser.add_argument("--reset", action="store_true", help="перезапустить плату и показать лог с начала")
    args = parser.parse_args()

    openocd = find_openocd(args.openocd)
    if not openocd:
        print("openocd не найден: укажи --openocd или переменную окружения OPENOCD", file=sys.stderr)
        return 1

    proc = start_openocd(openocd, args.port, args.reset)
    sock = connect(args.port, proc)
    if sock is None:
        proc.kill()
        _, err = proc.communicate()
        print("OpenOCD не смог подключиться к плате:\n" + (err or ""), file=sys.stderr)
        return 1

    print(f"--- RTT-лог через {os.path.basename(openocd)}, Ctrl+C — выход ---", flush=True)
    sock.settimeout(None)
    try:
        return print_log(sock)
    finally:
        sock.close()
        if proc.poll() is None:
            proc.terminate()
            proc.wait(timeout=5)


def print_log(sock):
    """
    Печатает всё, что приходит из RTT-канала, пока не нажат Ctrl+C или пока
    OpenOCD не закроет соединение (плату отключили, OpenOCD завершился).

    Параметры:
      sock — подключённый к RTT-серверу OpenOCD сокет.

    Возвращает: 0 при выходе по Ctrl+C, 1 при разрыве соединения.
    """
    try:
        while True:
            data = sock.recv(1024)
            if not data:
                break
            sys.stdout.write(data.decode("ascii", errors="replace").replace("\r\n", "\n"))
            sys.stdout.flush()
    except KeyboardInterrupt:
        return 0
    except OSError:
        pass
    print("--- OpenOCD отключился ---", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
