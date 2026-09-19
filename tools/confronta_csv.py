#!/usr/bin/env python3
"""Confronto grafico dei test di capacità (curve V/Ah) a partire dai CSV del tester.

Uso:
  python confronta_csv.py cartella_con_i_csv          -> legge tutti i test_*.csv / t*.csv nella cartella
  python confronta_csv.py --tester 192.168.4.1        -> scarica storico e CSV direttamente dal tester (PC sul suo Wi-Fi)
  python confronta_csv.py file1.csv file2.csv ...     -> file specifici

Produce nella cartella corrente: confronto.png (grafico) e confronto_riepilogo.csv (tabella).
Richiede matplotlib (pip install matplotlib).
"""
import sys, os, glob, json, csv, urllib.request

def leggi_csv(path):
    nome = os.path.splitext(os.path.basename(path))[0]; pts = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if line.startswith("# test"):
                # "# test 3, batteria: B03, ts: 1789000000"
                for part in line[1:].split(","):
                    if "batteria:" in part: nome = part.split(":", 1)[1].strip() or nome
                continue
            if not line or line.startswith("t_s"): continue
            c = line.split(",")
            if len(c) < 6: continue
            try: t, v, i, ah, wh = float(c[0]), float(c[1]), float(c[2]), float(c[4]), float(c[5])
            except ValueError: continue
            pts.append((t, v, i, ah, wh))
    return nome, pts

def scarica_dal_tester(ip, dest):
    os.makedirs(dest, exist_ok=True)
    hist = json.load(urllib.request.urlopen(f"http://{ip}/api/history", timeout=10))
    files = []
    for r in hist:
        try:
            data = urllib.request.urlopen(f"http://{ip}/api/log?id={r['id']}", timeout=20).read()
        except Exception:
            print(f"  test #{r['id']} ({r.get('name')}): CSV non disponibile"); continue
        p = os.path.join(dest, f"test_{r['id']}_{r.get('name','')}.csv".replace(" ", "_"))
        open(p, "wb").write(data); files.append(p)
        print(f"  scaricato {p}")
    return files

def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__); return
    if args[0] == "--tester":
        files = scarica_dal_tester(args[1], "csv_tester")
    elif len(args) == 1 and os.path.isdir(args[0]):
        files = sorted(glob.glob(os.path.join(args[0], "*.csv")))
    else:
        files = args
    curve = []
    for p in files:
        nome, pts = leggi_csv(p)
        pts_ok = [q for q in pts if q[1] > 5]          # scarta i punti a 0 V dopo lo stacco del BMS
        if len(pts_ok) < 2: continue
        crollo = len(pts_ok) < len(pts)                   # tensione andata a zero = BMS ha staccato
        curve.append((nome, pts_ok, crollo))
    if not curve:
        print("Nessun CSV valido trovato."); return
    curve.sort(key=lambda c: -c[1][-1][3])

    # --- riepilogo ---
    with open("confronto_riepilogo.csv", "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f, delimiter=";")
        w.writerow(["batteria", "Ah erogati", "Wh", "durata h", "V iniziale", "V finale", "I media", "esito"])
        print(f"{'batteria':12} {'Ah':>7} {'Wh':>7} {'ore':>6} {'Vin':>6} {'Vfin':>6} {'Imed':>6}  esito")
        for nome, pts, crollo in curve:
            ah, wh, t = pts[-1][3], pts[-1][4], pts[-1][0] / 3600
            imed = sum(q[2] for q in pts) / len(pts)
            esito = ("BMS staccato in anticipo" if pts[-1][1] > 21.5 else "BMS a fine scarica") if crollo else "cutoff"
            w.writerow([nome, f"{ah:.2f}", f"{wh:.0f}", f"{t:.2f}", f"{pts[0][1]:.2f}", f"{pts[-1][1]:.2f}", f"{imed:.2f}", esito])
            print(f"{nome:12} {ah:7.2f} {wh:7.0f} {t:6.2f} {pts[0][1]:6.2f} {pts[-1][1]:6.2f} {imed:6.2f}  {esito}")

    # --- grafico ---
    try:
        import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
    except ImportError:
        print("matplotlib non installato: pip install matplotlib   (riepilogo salvato comunque)"); return
    fig, (ax1, ax2, ax3) = plt.subplots(3, 1, figsize=(11, 12), sharex=False)
    best = curve[0][1][-1][3]
    V_PRESTO = 21.5   # BMS staccato sopra questa tensione di pacco = intervento anticipato (cella debole); sotto e' normale fine scarica
    for nome, pts, crollo in curve:
        ah = [q[3] for q in pts]; v = [q[1] for q in pts]; h = [q[0] / 3600 for q in pts]
        presto = crollo and pts[-1][1] > V_PRESTO
        deb = presto or pts[-1][3] < 0.9 * best
        lab = f"{nome}: {pts[-1][3]:.1f} Ah" + (" (BMS presto)" if presto else " (BMS a fine scarica)" if crollo else "")
        ln, = ax1.plot(ah, v, lw=1.8, label=lab, ls="--" if deb else "-")
        ax1.plot(ah[-1], v[-1], "o", color="red" if presto else ln.get_color(), ms=6)
        ax2.plot(h, v, lw=1.8, color=ln.get_color(), ls="--" if deb else "-")
        ax3.plot(h, [q[2] for q in pts], lw=1.2, color=ln.get_color(), ls="--" if deb else "-")
    ax1.set_xlabel("Ah erogati"); ax1.set_ylabel("Tensione pacco [V]"); ax1.grid(alpha=.3)
    ax1.set_title("Confronto batterie – tensione vs Ah erogati (tratteggio = sospette, pallino rosso = BMS staccato in anticipo)")
    ax1.legend(fontsize=8, ncol=2)
    ax2.set_xlabel("Tempo [h]"); ax2.set_ylabel("Tensione pacco [V]"); ax2.grid(alpha=.3)
    ax3.set_xlabel("Tempo [h]"); ax3.set_ylabel("Corrente [A]"); ax3.grid(alpha=.3)
    fig.tight_layout(); fig.savefig("confronto.png", dpi=130)
    print("Salvato confronto.png e confronto_riepilogo.csv")

if __name__ == "__main__":
    main()
