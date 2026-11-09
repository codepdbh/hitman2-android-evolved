package com.h2ae.android

import android.app.Activity
import android.os.Bundle
import android.util.Log
import android.widget.Button
import android.widget.TextView
import kotlin.concurrent.thread

/**
 * Diagnostic front end. It does one real thing today: load the native runtime
 * and run its self-test on this device. Everything not implemented is shown
 * disabled and labelled as pending rather than faked.
 *
 * Automation: `am start -n com.h2ae.android/.MainActivity --ez selftest true`
 * runs the self-test at launch; the result is logged under the tag H2AE.
 */
class MainActivity : Activity() {
    private lateinit var result: TextView
    private lateinit var report: TextView
    private lateinit var button: Button

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        result = findViewById(R.id.result)
        report = findViewById(R.id.report)
        button = findViewById(R.id.btnSelfTest)

        val version = packageManager.getPackageInfo(packageName, 0).versionName ?: "?"
        findViewById<TextView>(R.id.subtitle).text = getString(R.string.subtitle, version)

        val err = NativeBridge.loadError
        if (err != null) {
            result.text = getString(R.string.native_missing, err)
            button.isEnabled = false
            Log.e(TAG, "ANDROID    E native library failed to load: $err")
            return
        }
        report.text = NativeBridge.buildInfo()
        button.setOnClickListener { runSelfTest() }
        if (intent?.getBooleanExtra("selftest", false) == true) runSelfTest()
    }

    private fun runSelfTest() {
        button.isEnabled = false
        result.text = getString(R.string.selftest_running)
        thread(name = "h2ae-selftest") {
            val text = NativeBridge.runSelfTest()
            val m = Regex("""(\d+) passed, (\d+) failed""").find(text)
            val passed = m?.groupValues?.get(1)?.toIntOrNull() ?: 0
            val failed = m?.groupValues?.get(2)?.toIntOrNull() ?: -1
            runOnUiThread {
                // No summary line means the test did not finish: that is a failure, not a pass.
                result.text = if (failed == 0 && passed > 0) getString(R.string.selftest_ok, passed)
                              else getString(R.string.selftest_fail, if (failed < 0) 1 else failed)
                report.text = NativeBridge.buildInfo() + "\n\n" + text
                button.isEnabled = true
            }
        }
    }

    private companion object { const val TAG = "H2AE" }
}
