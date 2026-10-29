#!/usr/bin/env python3
"""Fake cmplxty.music for tests/update_test.cpp --net.  usage: update_server.py PORT LOGFILE   (prints 'ready' when listening)"""
import sys, time, json, threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
port, log = int(sys.argv[1]), sys.argv[2]
cur = sys.argv[3] if len(sys.argv) > 3 else "1.0.0"
NEW = {"version": "99.0.0", "url": "https://cmplxty.music/plugins/granular/", "installer": "https://cmplxty.music/plugins/granular/CMPLXTY-Granular-Setup-99.0.0-win.exe", "notes": "net test"}
class H(BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def _send(self, code, body=b"", ctype="application/json", extra=None):
        self.send_response(code); self.send_header("Content-Type", ctype); self.send_header("Content-Length", str(len(body)))
        for k, v in (extra or {}).items(): self.send_header(k, v)
        self.end_headers(); self.wfile.write(body)
    def do_GET(self):
        with open(log, "a") as f: f.write(json.dumps({"path": self.path, "headers": dict(self.headers.items()), "client": self.client_address[0]}) + "\n")
        p = self.path
        if p == "/new.json": self._send(200, json.dumps(NEW).encode())
        elif p == "/same.json": self._send(200, json.dumps(dict(NEW, version=cur)).encode())
        elif p == "/evilurl.json": self._send(200, json.dumps(dict(NEW, url="https://evil.example/")).encode())
        elif p == "/garbage": self._send(200, b"<html>not json</html>", "text/html")
        elif p == "/big": self._send(200, b'{"version":"99.0.0","url":"https://cmplxty.music/","notes":"' + b"x" * 100000 + b'"}')
        elif p == "/redirect": self._send(302, b"", extra={"Location": "/new.json"})
        elif p == "/slow": time.sleep(25); self._send(200, json.dumps(NEW).encode())
        else: self._send(404, b"nope", "text/plain")
srv = ThreadingHTTPServer(("127.0.0.1", port), H); srv.daemon_threads = True
print("ready", flush=True); srv.serve_forever()
