package com.lifepo4tester.app;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageInstaller;
import android.graphics.Color;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkRequest;
import android.net.Uri;
import android.net.nsd.NsdManager;
import android.net.nsd.NsdServiceInfo;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.text.InputType;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.webkit.URLUtil;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceError;
import android.webkit.WebResourceRequest;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.TextView;
import android.widget.Toast;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Guscio Android della web-app servita dall'ESP32 del tester LiFePO4.
 *
 * All'avvio cerca il tester in parallelo su: ultimo indirizzo funzionante, indirizzo di default
 * (Defaults.ADDRESS, da app_android/tester.properties), lifepo4tester.local, access point del
 * tester (192.168.4.1) e via mDNS/NSD. Ogni indirizzo viene provato sia sulla rete predefinita
 * di Android (Wi-Fi di casa, oppure dati mobili + VPN/Tailscale quando si e' fuori) sia forzando
 * il Wi-Fi: l'access point del tester non ha internet e Android altrimenti lo scavalca con i dati
 * mobili. Il primo che risponde a /api/status viene aperto nella WebView.
 *
 * Auto-aggiornamento: se il tester ospita un APK con versionCode maggiore (GET /api/app), l'app
 * propone di scaricarlo (/app.apk) e di installarlo, senza cavo.
 */
public class MainActivity extends Activity {

    private static final String AP_URL = "http://192.168.4.1/";
    private static final String MDNS_URL = "http://lifepo4tester.local/";
    private static final String SERVICE_NAME = "lifepo4";
    private static final int SEARCH_TIMEOUT_MS = 9000;
    private static final String ACTION_INSTALL_STATUS = "com.lifepo4tester.app.INSTALL_STATUS";
    private boolean updateChecked = false;

    private WebView web;
    private TextView status;
    private SharedPreferences prefs;
    private ConnectivityManager cm;
    private ConnectivityManager.NetworkCallback netCallback;
    private volatile Network wifiNet;

    private final Handler ui = new Handler(Looper.getMainLooper());
    private final AtomicBoolean found = new AtomicBoolean(false);
    private int searchId = 0;
    private NsdManager nsd;
    private NsdManager.DiscoveryListener nsdListener;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        prefs = getSharedPreferences("tester", MODE_PRIVATE);
        nsd = (NsdManager) getSystemService(Context.NSD_SERVICE);
        cm = (ConnectivityManager) getSystemService(Context.CONNECTIVITY_SERVICE);

        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Color.parseColor("#0f172a"));
        root.setFitsSystemWindows(true);
        web = new WebView(this);
        web.setBackgroundColor(Color.parseColor("#0f172a"));
        web.setVisibility(View.INVISIBLE);
        status = new TextView(this);
        status.setTextColor(Color.parseColor("#94a3b8"));
        status.setTextSize(16);
        status.setGravity(Gravity.CENTER);
        status.setPadding(48, 48, 48, 48);
        status.setOnClickListener(v -> search());
        root.addView(web, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        root.addView(status, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        setContentView(root);

        WebSettings ws = web.getSettings();
        ws.setJavaScriptEnabled(true);
        ws.setDomStorageEnabled(true);
        ws.setTextZoom(100);                     // ignora il "carattere grande" di sistema: il layout e' gia' per telefono

        web.setWebViewClient(new WebViewClient() {
            @Override
            public void onReceivedError(WebView view, WebResourceRequest request, WebResourceError error) {
                if (request.isForMainFrame()) {   // pagina principale non caricata: ricomincia la ricerca
                    web.setVisibility(View.INVISIBLE);
                    ui.postDelayed(MainActivity.this::search, 1500);
                }
            }

            @Override
            public boolean shouldOverrideUrlLoading(WebView view, WebResourceRequest request) {
                Uri u = request.getUrl();
                String cur = view.getUrl();
                if (cur != null && u.getHost() != null && u.getHost().equals(Uri.parse(cur).getHost())) return false;
                startActivity(new Intent(Intent.ACTION_VIEW, u));   // link esterni: browser
                return true;
            }
        });

        // Serve un WebChromeClient: senza, confirm()/prompt() della pagina vengono ignorati
        // (Avvia/Ferma test e la matita "rinomina" non farebbero nulla).
        web.setWebChromeClient(new WebChromeClient());

        // "Scarica CSV": salva nella cartella Download del telefono (DownloadManager rifiuta
        // l'HTTP in chiaro sugli Android recenti, quindi il file passa dall'app e da MediaStore).
        web.setDownloadListener((url, userAgent, contentDisposition, mimeType, contentLength) -> {
            Toast.makeText(this, "Scarico il CSV nella cartella Download…", Toast.LENGTH_SHORT).show();
            new Thread(() -> {
                String msg;
                try {
                    byte[] data = fetch(url);
                    String name = "lifepo4_" + URLUtil.guessFileName(url, contentDisposition, "text/csv");
                    saveToDownloads(name, data);
                    msg = "Salvato in Download: " + name;
                } catch (Exception e) {
                    msg = "Download fallito: " + e.getMessage();
                }
                final String m = msg;
                ui.post(() -> Toast.makeText(this, m, Toast.LENGTH_LONG).show());
            }).start();
        });

        watchWifi();
        search();
    }

    // ------------------------------------------------------------------ rete

    /** Tiene traccia della rete Wi-Fi (se c'e') senza forzarla: serve come seconda via nelle prove. */
    private void watchWifi() {
        NetworkRequest req = new NetworkRequest.Builder().addTransportType(NetworkCapabilities.TRANSPORT_WIFI).build();
        netCallback = new ConnectivityManager.NetworkCallback() {
            @Override public void onAvailable(Network network) {
                boolean first = wifiNet == null;
                wifiNet = network;
                if (first) ui.post(() -> { if (!found.get()) search(); });
            }
            @Override public void onLost(Network network) {
                if (network.equals(wifiNet)) { wifiNet = null; cm.bindProcessToNetwork(null); }
            }
        };
        try { cm.requestNetwork(req, netCallback); } catch (Exception e) { netCallback = null; }
    }

    private static HttpURLConnection open(String url, Network net) throws Exception {
        URL u = new URL(url);
        return (HttpURLConnection) (net != null ? net.openConnection(u) : u.openConnection());
    }

    private static byte[] fetch(String url) throws Exception {
        HttpURLConnection c = open(url, null);   // dopo bindProcessToNetwork usa la rete scelta
        c.setConnectTimeout(5000);
        c.setReadTimeout(20000);
        try {
            if (c.getResponseCode() != 200) throw new Exception("HTTP " + c.getResponseCode());
            InputStream in = c.getInputStream();
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buf = new byte[4096];
            int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
            return out.toByteArray();
        } finally {
            c.disconnect();
        }
    }

    private void saveToDownloads(String name, byte[] data) throws Exception {
        if (android.os.Build.VERSION.SDK_INT >= 29) {
            android.content.ContentValues v = new android.content.ContentValues();
            v.put(android.provider.MediaStore.Downloads.DISPLAY_NAME, name);
            v.put(android.provider.MediaStore.Downloads.MIME_TYPE, "text/csv");
            v.put(android.provider.MediaStore.Downloads.IS_PENDING, 1);
            Uri uri = getContentResolver().insert(android.provider.MediaStore.Downloads.EXTERNAL_CONTENT_URI, v);
            if (uri == null) throw new Exception("MediaStore non disponibile");
            try (java.io.OutputStream os = getContentResolver().openOutputStream(uri)) {
                os.write(data);
            }
            v.clear();
            v.put(android.provider.MediaStore.Downloads.IS_PENDING, 0);
            getContentResolver().update(uri, v, null, null);
        } else {
            java.io.File dir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS);
            dir.mkdirs();
            try (java.io.FileOutputStream fo = new java.io.FileOutputStream(new java.io.File(dir, name))) {
                fo.write(data);
            }
        }
    }

    // ------------------------------------------------------------------ ricerca

    private static String normalize(String s) {
        s = s.trim();
        if (s.isEmpty()) return s;
        if (!s.startsWith("http://") && !s.startsWith("https://")) s = "http://" + s;
        if (!s.endsWith("/")) s += "/";
        return s;
    }

    private List<String> candidates() {
        List<String> c = new ArrayList<>();
        String saved = prefs.getString("url", null);
        if (saved != null) c.add(saved);
        String[] defaults = { normalize(Defaults.ADDRESS), MDNS_URL, AP_URL };
        for (String d : defaults) if (!d.isEmpty() && !c.contains(d)) c.add(d);
        return c;
    }

    private void search() {
        final int id = ++searchId;
        found.set(false);
        cm.bindProcessToNetwork(null);
        status.setText("Cerco il tester sulla rete…");
        status.setVisibility(View.VISIBLE);
        for (final String url : candidates()) probeAsync(url, id);
        startNsd(id);
        ui.postDelayed(() -> {
            if (id == searchId && !found.get()) {
                stopNsd();
                status.setText("Tester non trovato.\n\nA casa: Wi-Fi di casa oppure rete «LiFePO4-Tester».\nFuori casa: accendi Tailscale.\n\nTocca qui per riprovare.");
                showAddressDialog();
            }
        }, SEARCH_TIMEOUT_MS);
    }

    /** Prova l'indirizzo sulla rete predefinita e, se c'e', anche forzando il Wi-Fi. */
    private void probeAsync(final String base, final int id) {
        new Thread(() -> { if (probe(base, null)) onFound(base, id, null); }).start();
        final Network w = wifiNet;
        if (w != null) new Thread(() -> { if (probe(base, w)) onFound(base, id, w); }).start();
    }

    /** true se all'indirizzo risponde davvero il firmware del tester. */
    private static boolean probe(String base, Network net) {
        HttpURLConnection c = null;
        try {
            c = open(base + "api/status", net);
            c.setConnectTimeout(3000);
            c.setReadTimeout(4000);
            if (c.getResponseCode() != 200) return false;
            InputStream in = c.getInputStream();
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buf = new byte[1024];
            int n;
            while ((n = in.read(buf)) > 0 && out.size() < 8192) out.write(buf, 0, n);
            return out.toString("UTF-8").contains("\"ah_out\"");
        } catch (Exception e) {
            return false;
        } finally {
            if (c != null) c.disconnect();
        }
    }

    private void onFound(final String base, int id, final Network net) {
        if (id != searchId || !found.compareAndSet(false, true)) return;
        ui.post(() -> {
            stopNsd();
            cm.bindProcessToNetwork(net);          // null = rete predefinita; altrimenti Wi-Fi forzato
            // l'indirizzo dell'access point non sostituisce quello di casa salvato
            if (!AP_URL.equals(base) || prefs.getString("url", null) == null) prefs.edit().putString("url", base).apply();
            status.setVisibility(View.GONE);
            web.setVisibility(View.VISIBLE);
            web.loadUrl(base);
            checkAppUpdate(base);
        });
    }

    // mDNS / DNS-SD: trova l'ESP32 anche se ha cambiato IP
    private void startNsd(final int id) {
        stopNsd();
        nsdListener = new NsdManager.DiscoveryListener() {
            @Override public void onServiceFound(NsdServiceInfo info) {
                String name = info.getServiceName();
                if (name == null || !name.toLowerCase().contains(SERVICE_NAME)) return;
                try {
                    nsd.resolveService(info, new NsdManager.ResolveListener() {
                        @Override public void onServiceResolved(NsdServiceInfo r) {
                            if (r.getHost() == null) return;
                            String h = r.getHost().getHostAddress();
                            if (h == null) return;
                            if (h.contains(":")) h = "[" + h + "]";
                            probeAsync("http://" + h + "/", id);
                        }
                        @Override public void onResolveFailed(NsdServiceInfo i, int code) { }
                    });
                } catch (Exception ignored) { }
            }
            @Override public void onServiceLost(NsdServiceInfo info) { }
            @Override public void onDiscoveryStarted(String type) { }
            @Override public void onDiscoveryStopped(String type) { }
            @Override public void onStartDiscoveryFailed(String type, int code) { }
            @Override public void onStopDiscoveryFailed(String type, int code) { }
        };
        try {
            nsd.discoverServices("_http._tcp", NsdManager.PROTOCOL_DNS_SD, nsdListener);
        } catch (Exception e) {
            nsdListener = null;
        }
    }

    private void stopNsd() {
        if (nsdListener == null) return;
        try { nsd.stopServiceDiscovery(nsdListener); } catch (Exception ignored) { }
        nsdListener = null;
    }

    // ------------------------------------------------------------------ auto-aggiornamento

    /** Una volta per avvio: se il tester ospita un APK piu' recente di questo, propone di installarlo. */
    private void checkAppUpdate(final String base) {
        if (updateChecked) return;
        updateChecked = true;
        new Thread(() -> {
            try {
                String js = new String(fetch(base + "api/app"), "UTF-8");
                java.util.regex.Matcher m = java.util.regex.Pattern.compile("\"version\":(\\d+)").matcher(js);
                if (!m.find()) return;
                final int remote = Integer.parseInt(m.group(1));
                final int mine = getPackageManager().getPackageInfo(getPackageName(), 0).versionCode;
                if (remote <= mine) return;
                ui.post(() -> {
                    if (isFinishing()) return;
                    new AlertDialog.Builder(this)
                            .setTitle("Aggiornamento disponibile")
                            .setMessage("C'è una nuova versione dell'app (n. " + remote + ", installata n. " + mine + "). La installo?")
                            .setPositiveButton("Aggiorna", (d, w) -> downloadAndInstall(base))
                            .setNegativeButton("Più tardi", null)
                            .show();
                });
            } catch (Exception ignored) { }
        }).start();
    }

    /** Scarica /app.apk dal tester e lo passa al programma di installazione di Android. */
    private void downloadAndInstall(final String base) {
        Toast.makeText(this, "Scarico l'aggiornamento…", Toast.LENGTH_SHORT).show();
        new Thread(() -> {
            try {
                byte[] apk = fetch(base + "app.apk");
                PackageInstaller installer = getPackageManager().getPackageInstaller();
                PackageInstaller.SessionParams params =
                        new PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL);
                int id = installer.createSession(params);
                PackageInstaller.Session session = installer.openSession(id);
                try (java.io.OutputStream out = session.openWrite("app", 0, apk.length)) {
                    out.write(apk);
                    session.fsync(out);
                }
                Intent cb = new Intent(this, MainActivity.class).setAction(ACTION_INSTALL_STATUS);
                PendingIntent pi = PendingIntent.getActivity(this, id, cb,
                        PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_MUTABLE);
                session.commit(pi.getIntentSender());
                session.close();
            } catch (Exception e) {
                final String m = "Aggiornamento fallito: " + e.getMessage();
                ui.post(() -> Toast.makeText(this, m, Toast.LENGTH_LONG).show());
            }
        }).start();
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        if (!ACTION_INSTALL_STATUS.equals(intent.getAction()) || intent.getExtras() == null) return;
        int st = intent.getIntExtra(PackageInstaller.EXTRA_STATUS, PackageInstaller.STATUS_FAILURE);
        if (st == PackageInstaller.STATUS_PENDING_USER_ACTION) {
            Intent confirm = intent.getParcelableExtra(Intent.EXTRA_INTENT);   // conferma di Android ("Installa")
            if (confirm != null) startActivity(confirm);
        } else if (st != PackageInstaller.STATUS_SUCCESS) {
            String msg = intent.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE);
            Toast.makeText(this, "Aggiornamento non installato" + (msg != null ? ": " + msg : ""), Toast.LENGTH_LONG).show();
        }
    }

    // ------------------------------------------------------------------ dialoghi

    private void showAddressDialog() {
        if (isFinishing()) return;
        final EditText input = new EditText(this);
        input.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_URI);
        input.setHint(Defaults.ADDRESS);
        String saved = prefs.getString("url", normalize(Defaults.ADDRESS));
        input.setText(saved.replace("http://", "").replaceAll("/$", ""));
        int pad = (int) (20 * getResources().getDisplayMetrics().density);
        FrameLayout box = new FrameLayout(this);
        box.setPadding(pad, pad / 2, pad, 0);
        box.addView(input);

        new AlertDialog.Builder(this)
                .setTitle("Indirizzo del tester")
                .setMessage("A casa il telefono deve stare sul Wi-Fi di casa o sulla rete «LiFePO4-Tester» (192.168.4.1). Fuori casa serve Tailscale acceso.")
                .setView(box)
                .setPositiveButton("Collega", (d, w) -> {
                    String url = normalize(input.getText().toString());
                    if (!url.isEmpty()) prefs.edit().putString("url", url).apply();
                    search();
                })
                .setNeutralButton("Riprova", (d, w) -> search())
                .setNegativeButton("Esci", (d, w) -> finish())
                .show();
    }

    /** Tasto Indietro: storia della pagina, altrimenti il menu (Ricarica / Cambia indirizzo / Esci). */
    @Override
    public void onBackPressed() {
        if (web.getVisibility() == View.VISIBLE && web.canGoBack()) { web.goBack(); return; }
        new AlertDialog.Builder(this)
                .setItems(new String[]{"Ricarica", "Cambia indirizzo", "Esci"}, (d, which) -> {
                    if (which == 0) { if (found.get()) web.reload(); else search(); }
                    else if (which == 1) showAddressDialog();
                    else finish();
                })
                .show();
    }

    @Override
    protected void onDestroy() {
        searchId++;
        stopNsd();
        if (netCallback != null) { try { cm.unregisterNetworkCallback(netCallback); } catch (Exception ignored) { } }
        web.destroy();
        super.onDestroy();
    }
}
