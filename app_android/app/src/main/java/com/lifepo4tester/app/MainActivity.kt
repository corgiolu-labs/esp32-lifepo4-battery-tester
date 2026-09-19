package com.lifepo4tester.app

import android.annotation.SuppressLint
import android.app.AlertDialog
import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.os.Bundle
import android.text.InputType
import android.view.Menu
import android.view.MenuItem
import android.view.View
import android.webkit.WebChromeClient
import android.webkit.WebResourceError
import android.webkit.WebResourceRequest
import android.webkit.WebSettings
import android.webkit.WebView
import android.webkit.WebViewClient
import android.widget.EditText
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity

/**
 * LiFePO4 Tester - app Android (wrapper).
 * Mostra a schermo intero la web-app servita dall'ESP32 e ricorda l'indirizzo del tester.
 * Indirizzo di default: 192.168.4.1 (access point "LiFePO4-Tester").
 */
class MainActivity : AppCompatActivity() {

    private lateinit var web: WebView
    private lateinit var errorView: TextView
    private val prefs by lazy { getSharedPreferences("tester", MODE_PRIVATE) }
    private val address: String get() = prefs.getString("addr", "192.168.4.1") ?: "192.168.4.1"
    private var netCallback: ConnectivityManager.NetworkCallback? = null

    @SuppressLint("SetJavaScriptEnabled")
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        supportActionBar?.title = getString(R.string.app_name)

        web = findViewById(R.id.web)
        errorView = findViewById(R.id.error)
        errorView.setOnClickListener { load() }

        web.settings.apply {
            javaScriptEnabled = true
            domStorageEnabled = true
            cacheMode = WebSettings.LOAD_DEFAULT
            useWideViewPort = true
            loadWithOverviewMode = true
            mixedContentMode = WebSettings.MIXED_CONTENT_ALWAYS_ALLOW
        }
        // Senza WebChromeClient le finestre confirm()/alert() della pagina vengono ignorate (confirm = sempre "annulla")
        web.webChromeClient = WebChromeClient()
        web.webViewClient = object : WebViewClient() {
            override fun onPageFinished(view: WebView?, url: String?) {
                errorView.visibility = View.GONE
                web.visibility = View.VISIBLE
            }
            override fun onReceivedError(view: WebView?, request: WebResourceRequest?, error: WebResourceError?) {
                if (request?.isForMainFrame == true) {
                    web.visibility = View.GONE
                    errorView.visibility = View.VISIBLE
                    errorView.text = getString(R.string.error_connect, address)
                }
            }
        }
        bindToWifi()
        load()
    }

    /** Forza tutto il traffico dell'app sulla rete Wi-Fi anche se Android usa i dati mobili come rete predefinita
     *  (succede quando il Wi-Fi del tester non ha internet). */
    private fun bindToWifi() {
        val cm = getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
        val req = NetworkRequest.Builder().addTransportType(NetworkCapabilities.TRANSPORT_WIFI).build()
        val cb = object : ConnectivityManager.NetworkCallback() {
            override fun onAvailable(network: Network) {
                cm.bindProcessToNetwork(network)
                runOnUiThread { load() }
            }
            override fun onLost(network: Network) {
                cm.bindProcessToNetwork(null)
            }
        }
        try { cm.requestNetwork(req, cb); netCallback = cb } catch (_: Exception) { }
    }

    override fun onDestroy() {
        netCallback?.let {
            try { (getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager).unregisterNetworkCallback(it) } catch (_: Exception) { }
        }
        super.onDestroy()
    }

    private fun load() {
        errorView.visibility = View.GONE
        web.visibility = View.VISIBLE
        web.loadUrl("http://$address/")
    }

    override fun onCreateOptionsMenu(menu: Menu): Boolean {
        menuInflater.inflate(R.menu.main, menu)
        return true
    }

    override fun onOptionsItemSelected(item: MenuItem): Boolean {
        when (item.itemId) {
            R.id.action_reload -> load()
            R.id.action_address -> askAddress()
            else -> return super.onOptionsItemSelected(item)
        }
        return true
    }

    private fun askAddress() {
        val input = EditText(this).apply {
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_URI
            setText(address)
            hint = "192.168.4.1"
        }
        AlertDialog.Builder(this)
            .setTitle(R.string.address_title)
            .setMessage(R.string.address_msg)
            .setView(input)
            .setPositiveButton(android.R.string.ok) { _, _ ->
                val a = input.text.toString().trim().removePrefix("http://").trimEnd('/')
                prefs.edit().putString("addr", if (a.isEmpty()) "192.168.4.1" else a).apply()
                load()
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    @Deprecated("Deprecated in Java")
    override fun onBackPressed() {
        if (web.canGoBack()) web.goBack() else super.onBackPressed()
    }

    override fun onResume() {
        super.onResume()
        web.onResume()
    }

    override fun onPause() {
        web.onPause()
        super.onPause()
    }
}
