#!/usr/bin/env python3
"""Simulatore del tester per provare la web-app sul PC senza ESP32.
Avvia:  python mock_server.py [porta]   e apri http://localhost:8080 (o la porta scelta)
Simula una scarica a corrente costante con cutoff, come farebbe il firmware."""
import http.server, json, math, re, time, os, sys, urllib.parse

HERE = os.path.dirname(os.path.abspath(__file__))
FW = os.path.join(HERE, "..", "firmware", "LiFePO4_Tester")

def extract(name):
    src = open(os.path.join(FW, "web_ui.h"), encoding="utf-8").read()
    m = re.search(name + r"\[\] PROGMEM = R\"(\w*)\((.*?)\)\1\"", src, re.S)
    return m.group(2)

INDEX, MANIFEST, SW = extract("WEB_INDEX"), extract("WEB_MANIFEST"), extract("WEB_SW")
ICON = {n: open(os.path.join(HERE, f"icon-{n}.png"), "rb").read() for n in (192, 512)}

S = dict(cfg=dict(vgain=1.0, voff=0, igain=1.0, ioff=0, inv=0, cap=100, cutoff=20.0, logint=10, relay_en=1, ntc_en=1,
                  vsrc=0, vhigh=29.6, imax=100, tmax=55, ssid="CasaWiFi", cells=8),
         soc_ah=92.0, ah_out=0.0, ah_in=0.0, wh_out=0.0, wh_in=0.0, relay=0, t0=time.time(),
         test=dict(state="IDLE", reason="", t0=0, ah=0, wh=0, vstart=0, vmin=99, isum=0, n=0, imax=0, logn=0),
         last=dict(id=3, name="B03", ts=1789000000, reason="cutoff", ah=97.4, wh=2480, dur=35064, iavg=10.0, vstart=26.9, vend=20.0, rint=0.0112),
         load_a=10.0, nextid=4,
         history=[dict(id=1, name="B01", ts=1788900000, reason="cutoff", ah=98.1, wh=2500, dur=17600, iavg=20.1, vstart=26.9, vend=20.0, rint=0.0110),
                  dict(id=2, name="B02", ts=1788950000, reason="bms", ah=71.3, wh=1830, dur=12800, iavg=20.0, vstart=26.8, vend=24.2, rint=0.0180),
                  dict(id=3, name="B03", ts=1789000000, reason="cutoff", ah=97.4, wh=2480, dur=17500, iavg=20.0, vstart=26.9, vend=20.0, rint=0.0112)])
last_t = time.time()

def volt():
    soc = S["soc_ah"] / S["cfg"]["cap"]
    ocv = 8 * (3.20 + 0.15 * soc + (0.25 if soc > 0.97 else 0) - (0.6 * (0.08 - soc) / 0.08 if soc < 0.08 else 0))
    i = cur()
    return ocv - i * 0.012 + 0.01 * math.sin(time.time())

def cur():
    return S["load_a"] if S["relay"] else 0.3 * math.sin(time.time() / 3)

def step():
    global last_t
    now = time.time(); dt = now - last_t; last_t = now
    i = cur(); v = volt(); ah = i * dt / 3600; wh = v * ah
    if i >= 0: S["ah_out"] += ah; S["wh_out"] += wh
    else: S["ah_in"] -= ah; S["wh_in"] -= wh
    S["soc_ah"] = max(0, min(S["cfg"]["cap"], S["soc_ah"] - ah * 60))   # x60: scarica accelerata per la demo
    T = S["test"]
    if T["state"] == "RUN":
        T["ah"] += ah * 60; T["wh"] += wh * 60; T["vmin"] = min(T["vmin"], v)
        if i > 0.2: T["isum"] += i; T["n"] += 1; T["imax"] = max(T["imax"], i)
        T["logn"] = int((now - T["t0"]) // S["cfg"]["logint"])
        if v <= S["cfg"]["cutoff"]:
            T["state"] = "DONE"; T["reason"] = "cutoff"; S["relay"] = 0
            S["last"] = dict(id=T["id"], name=T["name"], ts=T["ts"], reason="cutoff", ah=T["ah"], wh=T["wh"], dur=int(now - T["t0"]),
                             iavg=T["isum"] / max(T["n"], 1), vstart=T["vstart"], vend=v, rint=0.0118)
            S["history"].append(S["last"])
    return v, i

class H(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def send(self, code, ctype, body):
        if isinstance(body, str): body = body.encode()
        self.send_response(code); self.send_header("Content-Type", ctype); self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*"); self.send_header("Cache-Control", "no-store"); self.end_headers()
        self.wfile.write(body)
    def do_GET(self):
        p = urllib.parse.urlparse(self.path).path
        if p in ("/", "/index.html"): return self.send(200, "text/html", INDEX)
        if p == "/manifest.json": return self.send(200, "application/manifest+json", MANIFEST)
        if p == "/sw.js": return self.send(200, "application/javascript", SW)
        if p == "/icon-192.png": return self.send(200, "image/png", ICON[192])
        if p == "/icon-512.png": return self.send(200, "image/png", ICON[512])
        if p == "/api/config": return self.send(200, "application/json", json.dumps(S["cfg"]))
        if p == "/api/trend":
            n = 540; v = []; i = []                                   # 3 ore finte: riposo, poi scarica a 20,8 A
            for k in range(n):
                run = k > 60; x = max(0, k - 60) / 480
                v.append(int(1000 * ((26.9 - 0.9 * x - 0.35 * math.exp(-x * 25)) if run else 27.3 - 0.002 * k))); i.append(2080 if run else 0)
            return self.send(200, "application/json", json.dumps(dict(dt=20, n=n, age=7, tstart=(n - 60) * 20, v=v, i=i)))
        if p == "/api/history": return self.send(200, "application/json", json.dumps(S["history"]))
        if p == "/api/log":
            q = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query); tid = int(q.get("id", ["3"])[0])
            prof = {1: (98.1, None), 2: (71.3, 24.2), 3: (97.4, None), 4: (0.4, None)}.get(tid, (90.0, None))
            rows = ["# test %d" % tid, "t_s,V,I_A,P_W,Ah,Wh,T_C"]; ah_end, vtrip = prof; i = 20.0
            n = int(ah_end / i * 3600 / 10) + 1
            for k in range(n):
                t = k * 10; ah = i * t / 3600; x = ah / max(ah_end, 1)
                v = 26.9 - 0.9 * x - 0.15 * math.exp(-x * 12) - (7.0 * (x - 0.93) / 0.07 if x > 0.93 else 0)
                if vtrip and ah >= ah_end - 0.1: v = vtrip
                rows.append("%d,%.3f,%.3f,%.1f,%.4f,%.2f,24.0" % (t, v, i, v * i, ah, ah * v))
            return self.send(200, "text/csv", "\n".join(rows) + "\n")
        if p == "/api/status":
            v, i = step(); T = S["test"]; c = S["cfg"]
            al = []
            if v < c["cutoff"]: al.append("vlow")
            st = dict(fw="mock", sensor="ADS1115", up=int(time.time() - S["t0"]), v=v, vadc=v + 0.03, vina=v, i=i, p=v * i, mvsh=i * 0.75, t=24.3,
                      soc=S["soc_ah"] / c["cap"] * 100, ah_rem=S["soc_ah"], cap=c["cap"], ah_out=S["ah_out"], ah_in=S["ah_in"],
                      wh_out=S["wh_out"], wh_in=S["wh_in"], rint=0.0121, relay=S["relay"], ina_ok=1, alarm=al,
                      test=dict(state=T["state"], name=T.get("name", ""), id=T.get("id", 0), ts=T.get("ts", 0), reason=T["reason"], elapsed=int(time.time() - T["t0"]) if T["t0"] else 0,
                                ah=T["ah"], wh=T["wh"], vstart=T["vstart"], vmin=T["vmin"], iavg=T["isum"] / max(T["n"], 1),
                                imax=T["imax"], rint=0.0118, cutoff=c["cutoff"], logn=T["logn"]),
                      last=S["last"], wifi=dict(ap_ip="192.168.4.1", sta_ip="192.168.1.50", ssid=c["ssid"], rssi=-58))
            return self.send(200, "application/json", json.dumps(st))
        self.send(404, "text/plain", "not found")
    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0)); body = self.rfile.read(n).decode()
        a = {k: v[0] for k, v in urllib.parse.parse_qs(body).items()}
        p = urllib.parse.urlparse(self.path).path
        print("POST", p, a)
        if p == "/api/test":
            T = S["test"]
            if a.get("cmd") == "start":
                S["test"] = dict(state="RUN", reason="", t0=time.time(), ah=0, wh=0, vstart=volt(), vmin=99, isum=0, n=0, imax=0, logn=0,
                                 name=a.get("name", "batteria"), ts=int(a.get("ts", 0)), id=S["nextid"]); S["nextid"] += 1
                S["ah_out"] = S["ah_in"] = S["wh_out"] = S["wh_in"] = 0
                if S["cfg"]["relay_en"]: S["relay"] = 1
            elif a.get("cmd") == "stop" and T["state"] == "RUN":
                T["state"] = "DONE"; T["reason"] = "manual"; S["relay"] = 0
                S["last"] = dict(id=T["id"], name=T["name"], ts=T["ts"], reason="manual", ah=T["ah"], wh=T["wh"], dur=int(time.time() - T["t0"]),
                                 iavg=T["isum"] / max(T["n"], 1), vstart=T["vstart"], vend=volt(), rint=0.0118)
                S["history"].append(S["last"])
        elif p == "/api/history":
            if a.get("cmd") == "clear": S["history"] = []
            elif a.get("cmd") == "delete": S["history"] = [h for h in S["history"] if h["id"] != int(a.get("id", 0))]
        elif p == "/api/config":
            for k, v in a.items():
                S["cfg"][k] = type(S["cfg"].get(k, 0.0))(float(v)) if k != "ssid" else v
        elif p == "/api/soc": S["soc_ah"] = float(a["value"]) * S["cfg"]["cap"] / 100
        elif p == "/api/counters": S["ah_out"] = S["ah_in"] = S["wh_out"] = S["wh_in"] = 0
        elif p == "/api/relay": S["relay"] = int(a.get("state", 0))
        elif p == "/api/wifi": S["cfg"]["ssid"] = a.get("ssid", "")
        self.send(200, "text/plain", "OK")

if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
    print(f"Mock tester su http://localhost:{port}  (Ctrl+C per uscire)")
    http.server.ThreadingHTTPServer(("127.0.0.1", port), H).serve_forever()
