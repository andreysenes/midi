#!/usr/bin/env python3
"""Banco de testes Casio SA-1 — http://127.0.0.1:8741"""

from __future__ import annotations

import json
import os
import queue
import shutil
import subprocess
import sys
import threading
import time
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse

ROOT = Path(__file__).resolve().parent
PROJECT = ROOT.parent
HOST = "127.0.0.1"
PORT = 8741
BAUD = 115200

FQBN_PROBE = "rp2040:rp2040:rpipico:usbstack=picosdk"
FQBN_PICO = "rp2040:rp2040:rpipico:usbstack=tinyusb"

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    print("Instale pyserial:  python3 -m pip install -r lab/requirements.txt")
    sys.exit(1)


class Hub:
    def __init__(self) -> None:
        self.lock = threading.Lock()
        self.listeners: list[queue.Queue] = []
        self.ser = None
        self.port = ""
        self.last_port = ""
        self.connected = False
        self.flashing = False
        self.reader_stop = threading.Event()
        self.reader_thread: threading.Thread | None = None
        self.io_lock = threading.Lock()

    def subscribe(self) -> queue.Queue:
        q: queue.Queue = queue.Queue(maxsize=2000)
        with self.lock:
            self.listeners.append(q)
        return q

    def unsubscribe(self, q: queue.Queue) -> None:
        with self.lock:
            if q in self.listeners:
                self.listeners.remove(q)

    def emit(self, msg: dict) -> None:
        with self.lock:
            dead = []
            for q in self.listeners:
                try:
                    q.put_nowait(msg)
                except queue.Full:
                    dead.append(q)
            for q in dead:
                self.listeners.remove(q)

    def ports(self) -> list[dict]:
        out = []
        for p in list_ports.comports():
            name = p.device
            desc = (p.description or "").lower()
            blob = f"{name} {desc}".lower()
            if os.name == "posix" and "/cu." not in name and "/tty." not in name:
                continue
            if not any(tok in blob for tok in ("usb", "pico", "wch", "modem", "acm", "serial")):
                continue
            if "bluetooth" in blob:
                continue
            out.append(
                {
                    "device": name,
                    "desc": p.description or "",
                    "usb": True,
                }
            )
        out.sort(key=lambda x: (not x["usb"], x["device"]))
        return out

    def _open_serial(self, port: str):
        self._open_gen = getattr(self, "_open_gen", 0) + 1
        gen = self._open_gen
        result: dict = {}

        def worker() -> None:
            try:
                ser = serial.Serial(
                    port,
                    BAUD,
                    timeout=0.25,
                    write_timeout=1,
                    exclusive=False,
                )
            except Exception as exc:
                if gen == self._open_gen:
                    result["err"] = exc
                return
            if gen != self._open_gen:
                try:
                    ser.close()
                except Exception:
                    pass
                return
            try:
                ser.dtr = False
                ser.rts = False
            except Exception:
                pass
            result["ser"] = ser

        t = threading.Thread(target=worker, daemon=True)
        t.start()
        deadline = time.time() + 18
        while t.is_alive() and time.time() < deadline:
            left = max(0, int(deadline - time.time()))
            self._log(f"abrindo Serial… CDC lento apos BOOTSEL ({left}s)")
            t.join(timeout=3)
        if "ser" in result:
            return result["ser"]
        if "err" in result:
            raise result["err"]
        self._open_gen += 1
        raise RuntimeError(
            f"timeout ao abrir {port}. Tire o cabo USB 2s e reconecte, depois Conectar de novo."
        )

    def connect(self, port: str) -> str:
        if not port:
            raise RuntimeError("porta vazia")
        self._log(f"abrindo {port}…")
        ser = self._open_serial(port)
        with self.io_lock:
            old = self.ser
            self.reader_stop.set()
            self.ser = None
            if old is not None:
                try:
                    old.close()
                except Exception:
                    pass
            t = self.reader_thread
            self.reader_thread = None
            if t is not None and t is not threading.current_thread():
                t.join(timeout=0.8)
            try:
                ser.reset_input_buffer()
                ser.reset_output_buffer()
            except Exception as exc:
                self._log(f"buffer: {exc}")
            self.reader_stop.clear()
            self.ser = ser
            self.port = port
            self.last_port = port
            self.connected = True
            self.reader_thread = threading.Thread(target=self._read_loop, args=(ser,), daemon=True)
            self.reader_thread.start()
        threading.Timer(0.3, self._kick_ui).start()
        self.emit({"type": "status", "connected": True, "port": port})
        self._log(f"Serial aberto {port}")
        return port

    def _kick_ui(self) -> None:
        try:
            self.send("u")
        except Exception as exc:
            self._log(f"kick UI: {exc}")

    def disconnect(self, quiet: bool = False) -> None:
        self.reader_stop.set()
        ser = self.ser
        self.ser = None
        self.connected = False
        if ser is not None:
            for fn in (getattr(ser, "cancel_read", None), getattr(ser, "cancel_write", None)):
                if fn is None:
                    continue
                try:
                    fn()
                except Exception:
                    pass
            try:
                ser.close()
            except Exception:
                pass
        t = self.reader_thread
        self.reader_thread = None
        if t is not None and t is not threading.current_thread():
            t.join(timeout=1.2)
        self.port = ""
        if not quiet:
            self.emit({"type": "status", "connected": False, "port": ""})
            self._log("Serial fechado")

    def send(self, text: str) -> None:
        ser = self.ser
        if ser is None:
            raise RuntimeError("Pico nao conectado")
        ser.write(text.encode("ascii", errors="ignore"))
        ser.flush()

    def _read_loop(self, ser) -> None:
        buf = b""
        while not self.reader_stop.is_set():
            try:
                chunk = ser.read(256)
            except Exception as exc:
                if self.reader_stop.is_set():
                    break
                msg = str(exc).lower()
                if "bad file descriptor" in msg or "device not configured" in msg:
                    break
                self.emit({"type": "line", "text": f"serial: {exc}"})
                self.connected = False
                self.emit({"type": "status", "connected": False, "port": ""})
                break
            if not chunk:
                continue
            buf += chunk
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                line = raw.decode("utf-8", errors="replace").strip("\r")
                if not line:
                    continue
                if line.startswith("J{"):
                    try:
                        payload = json.loads(line[1:])
                    except json.JSONDecodeError:
                        self.emit({"type": "line", "text": line})
                        continue
                    kind = payload.get("t")
                    if kind == "k":
                        self.emit({"type": "key", **payload})
                    else:
                        self.emit({"type": "state", **payload})
                    continue
                self.emit({"type": "line", "text": line})

    def _log(self, text: str) -> None:
        self.emit({"type": "log", "text": text})

    def _picotool(self) -> Path | None:
        root = Path.home() / "Library/Arduino15/packages/rp2040/tools/pqt-picotool"
        found = sorted(root.glob("*/picotool"))
        if found:
            return found[-1]
        which = shutil.which("picotool")
        return Path(which) if which else None

    def _picotool_env(self, tool: Path) -> dict[str, str]:
        env = os.environ.copy()
        lib = str(tool.parent)
        env["DYLD_LIBRARY_PATH"] = lib
        env["DYLD_FALLBACK_LIBRARY_PATH"] = lib
        return env

    def _volumes(self) -> list[str]:
        vol = Path("/Volumes")
        if not vol.exists():
            return []
        return sorted(p.name for p in vol.iterdir())

    def _diag(self, label: str) -> None:
        self._log(f"--- diag {label} ---")
        ports = self.ports()
        if ports:
            for p in ports:
                self._log(f"  porta {p['device']}  ({p.get('desc') or '-'})")
        else:
            self._log("  portas USB: nenhuma")
        self._log("  volumes: " + (", ".join(self._volumes()) or "(vazio)"))
        self._log("  RPI-RP2: " + ("sim" if Path("/Volumes/RPI-RP2").exists() else "nao"))
        pt = self._picotool()
        if pt is None:
            self._log("  picotool: nao encontrado")
            return
        self._log(f"  picotool: {pt}")
        self._run(
            [str(pt), "info", "-a"],
            "picotool info",
            timeout=8,
            check=False,
            env=self._picotool_env(pt),
        )

    def _find_uf2(self, sketch: Path) -> Path:
        cands = list(sketch.rglob("*.uf2"))
        if not cands:
            raise RuntimeError(f"UF2 nao gerado em {sketch}")
        uf2 = max(cands, key=lambda p: p.stat().st_mtime)
        self._log(f"UF2 {uf2}  ({uf2.stat().st_size} bytes)")
        return uf2

    def _reset_1200(self, port: str) -> None:
        self._log(f"reset 1200 baud em {port} (BOOTSEL)")
        try:
            s = serial.Serial(port, 1200)
            time.sleep(0.15)
            s.close()
            self._log("  1200 baud: porta aberta e fechada")
        except Exception as exc:
            self._log(f"  1200 baud: {exc}")

    def _wait_rp2(self, seconds: float = 12) -> Path | None:
        deadline = time.time() + seconds
        last = ""
        while time.time() < deadline:
            names = self._volumes()
            blob = ",".join(names)
            if blob != last:
                self._log(f"  volumes agora: {blob or '(nenhum)'}")
                last = blob
            p = Path("/Volumes/RPI-RP2")
            if p.exists():
                self._log("  RPI-RP2 montou")
                return p
            time.sleep(0.4)
        self._log("  RPI-RP2 nao montou")
        return None

    def _copy_uf2(self, uf2: Path, dest_vol: Path) -> None:
        dest = dest_vol / uf2.name
        self._log(f"copiando UF2 -> {dest}")
        shutil.copy2(uf2, dest)
        self._log("copia ok, aguardando Pico voltar…")
        time.sleep(2.0)

    def _upload_picotool(self, uf2: Path) -> None:
        pt = self._picotool()
        if pt is None:
            raise RuntimeError("picotool ausente")
        self._log("gravando via picotool load -f -x (nao depende do disco RPI-RP2)")
        self._run(
            [str(pt), "load", str(uf2), "-f", "-x"],
            "picotool load",
            timeout=60,
            env=self._picotool_env(pt),
        )

    def flash(self, fw: str) -> None:
        if self.flashing:
            raise RuntimeError("Ja tem um upload em curso")
        if fw == "probe":
            sketch = PROJECT / "firmware" / "probe"
            fqbn = FQBN_PROBE
        elif fw == "pico":
            sketch = PROJECT / "firmware" / "pico"
            fqbn = FQBN_PICO
        else:
            raise RuntimeError("firmware desconhecido")
        self.flashing = True
        threading.Thread(target=self._flash_job, args=(fw, sketch, fqbn), daemon=True).start()

    def _flash_job(self, fw: str, sketch: Path, fqbn: str) -> None:
        was_port = self.port or self.last_port
        try:
            cli = shutil.which("arduino-cli")
            self._log(f"=== gravar {fw} ===")
            self._log(f"arduino-cli: {cli or 'AUSENTE'}")
            if cli:
                self._run([cli, "version"], "arduino-cli version", timeout=10, check=False)
            self._diag("antes de soltar o Serial")
            self._log("desconectando Serial de proposito — o Pico precisa da porta livre para BOOTSEL")
            self.disconnect(quiet=True)
            time.sleep(0.5)
            self._diag("depois de soltar o Serial")
            if cli is None:
                self._log("arduino-cli nao encontrado. brew install arduino-cli")
                return
            self._log(f"compilando {fw}  fqbn={fqbn}")
            self._run(
                [
                    cli,
                    "compile",
                    "--fqbn",
                    fqbn,
                    "--export-binaries",
                    str(sketch),
                ],
                f"compilar {fw}",
                timeout=180,
            )
            uf2 = self._find_uf2(sketch)
            uploaded = False
            errors: list[str] = []

            try:
                self._diag("antes do picotool")
                self._upload_picotool(uf2)
                uploaded = True
                self._log("picotool: ok")
            except Exception as exc:
                errors.append(f"picotool: {exc}")
                self._log(f"picotool falhou: {exc}")

            if not uploaded and was_port:
                self._diag("antes do reset 1200")
                self._reset_1200(was_port)
                time.sleep(0.8)
                self._diag("depois do reset 1200")
                vol = self._wait_rp2(12)
                if vol is not None:
                    self._copy_uf2(uf2, vol)
                    uploaded = True
                else:
                    errors.append("RPI-RP2 nao montou apos 1200 baud")

            if not uploaded and cli and was_port:
                self._log("fallback: arduino-cli upload -v (UF2 drive)")
                try:
                    self._run(
                        [
                            cli,
                            "upload",
                            "-v",
                            "-p",
                            was_port,
                            "--fqbn",
                            fqbn,
                            str(sketch),
                        ],
                        f"arduino-cli upload {fw}",
                        timeout=90,
                    )
                    uploaded = True
                except Exception as exc:
                    errors.append(f"arduino-cli upload: {exc}")

            self._diag("depois da tentativa de gravar")
            if not uploaded:
                raise RuntimeError(
                    "nao gravou. Pico reseta, troca de porta (211401→212401) e o macOS "
                    "nao monta RPI-RP2. Segure BOOT, toque RESET, confirme o disco RPI-RP2, "
                    "e grave de novo.\n" + "\n".join(errors)
                )
            self._log(f"ok: {fw}")
            time.sleep(1.5)
            self._reconnect_after_flash(was_port)
        except Exception as exc:
            self._log(f"falhou: {exc}")
            self._diag("apos falha")
        finally:
            self.flashing = False
            self.emit({"type": "flash", "done": True, "fw": fw})
            self.emit({"type": "status", "connected": self.connected, "port": self.port})

    def _reconnect_after_flash(self, was_port: str) -> None:
        deadline = time.time() + 12
        target = ""
        while time.time() < deadline and not target:
            found = [p["device"] for p in self.ports()]
            self._log("  procurando Pico em: " + (", ".join(found) or "(nenhuma)"))
            if was_port in found:
                target = was_port
                break
            usb = [p for p in found if "usbmodem" in p or "usbserial" in p]
            if usb:
                target = usb[0]
                break
            time.sleep(0.5)
        if not target:
            self._log("gravou. Escolha a porta e Conectar.")
            return
        try:
            self.connect(target)
            self._log(f"reconectado {target}")
        except Exception as exc:
            self._log(f"reconectar falhou: {exc}")

    def _run(
        self,
        cmd: list[str],
        label: str,
        timeout: int = 180,
        check: bool = True,
        env: dict[str, str] | None = None,
    ) -> int:
        self._log(f"$ {' '.join(cmd)}")
        proc = subprocess.Popen(
            cmd,
            cwd=str(PROJECT),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
            env=env,
        )
        assert proc.stdout is not None
        lines: list[str] = []

        def pump() -> None:
            for line in proc.stdout:
                text = line.rstrip()
                if text:
                    lines.append(text)
                    self._log(text)

        reader = threading.Thread(target=pump, daemon=True)
        reader.start()
        try:
            code = proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            proc.kill()
            reader.join(timeout=1)
            raise RuntimeError(f"{label} estourou {timeout}s") from None
        reader.join(timeout=2)
        self._log(f"  exit {code}  ({label})")
        if check and code != 0:
            tail = "\n".join(lines[-20:]) if lines else "sem saida"
            raise RuntimeError(f"{label} saiu com codigo {code}\n{tail}")
        return code


hub = Hub()


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt: str, *args) -> None:
        sys.stderr.write("[%s] %s\n" % (self.address_string(), fmt % args))

    def _json(self, code: int, payload: dict | list) -> None:
        body = json.dumps(payload).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _read_json(self) -> dict:
        n = int(self.headers.get("Content-Length") or "0")
        if n <= 0:
            return {}
        return json.loads(self.rfile.read(n).decode("utf-8") or "{}")

    def do_GET(self) -> None:
        path = urlparse(self.path).path
        if path in ("/", "/index.html"):
            self._file(ROOT / "static" / "index.html", "text/html; charset=utf-8")
            return
        if path == "/api/ports":
            self._json(200, {"ports": hub.ports(), "connected": hub.connected, "port": hub.port})
            return
        if path == "/api/status":
            self._json(
                200,
                {
                    "connected": hub.connected,
                    "port": hub.port,
                    "flashing": hub.flashing,
                    "cli": shutil.which("arduino-cli") is not None,
                },
            )
            return
        if path == "/api/events":
            self._sse()
            return
        self._json(404, {"error": "not found"})

    def do_POST(self) -> None:
        path = urlparse(self.path).path
        try:
            body = self._read_json()
            if path == "/api/connect":
                port = hub.connect(str(body.get("port") or ""))
                self._json(200, {"ok": True, "port": port})
                return
            if path == "/api/disconnect":
                hub.disconnect()
                self._json(200, {"ok": True})
                return
            if path == "/api/cmd":
                cmd = str(body.get("c") or "")
                if not cmd:
                    raise RuntimeError("comando vazio")
                hub.send(cmd)
                self._json(200, {"ok": True})
                return
            if path == "/api/flash":
                hub.flash(str(body.get("fw") or "probe"))
                self._json(200, {"ok": True})
                return
            self._json(404, {"error": "not found"})
        except (BrokenPipeError, ConnectionResetError):
            return
        except Exception as exc:
            try:
                self._json(400, {"error": str(exc)})
            except (BrokenPipeError, ConnectionResetError, OSError):
                hub._log(f"POST {path}: {exc}")

    def _file(self, path: Path, mime: str) -> None:
        if not path.exists():
            self._json(404, {"error": "missing " + str(path)})
            return
        data = path.read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", mime)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def _sse(self) -> None:
        q = hub.subscribe()
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Connection", "keep-alive")
        self.end_headers()
        self.wfile.write(b": ping\n\n")
        self.wfile.flush()
        try:
            while True:
                try:
                    msg = q.get(timeout=15)
                    blob = json.dumps(msg, ensure_ascii=False).encode("utf-8")
                    self.wfile.write(b"data: " + blob + b"\n\n")
                    self.wfile.flush()
                except queue.Empty:
                    self.wfile.write(b": ping\n\n")
                    self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError, OSError):
            pass
        finally:
            hub.unsubscribe(q)


class LabServer(ThreadingHTTPServer):
    allow_reuse_address = True
    allow_reuse_port = True


def main() -> None:
    os.chdir(PROJECT)
    httpd = LabServer((HOST, PORT), Handler)
    url = f"http://{HOST}:{PORT}"
    print(f"Casio SA-1 Lab  {url}")
    print("Conecte o Pico com a sonda gravada. Nao precisa do Arduino IDE.")
    threading.Timer(0.6, lambda: webbrowser.open(url)).start()
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nencerrado")
        hub.disconnect()
        httpd.server_close()


if __name__ == "__main__":
    main()
