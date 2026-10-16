package com.harbourmasters.soh

import android.content.Intent
import android.graphics.Color
import android.net.Uri
import android.os.Bundle
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.ScrollView
import android.widget.TextView
import androidx.activity.ComponentActivity
import androidx.activity.result.contract.ActivityResultContracts
import java.io.File
import java.io.FileOutputStream
import kotlin.concurrent.thread

class LauncherActivity : ComponentActivity() {

    private lateinit var status: TextView
    private lateinit var chooseButton: Button
    private lateinit var progress: ProgressBar

    private val pickRom =
        registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
            if (uri != null) importRom(uri)
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        buildUi()
        prepare()
    }

    private fun prepare() {
        setBusy(true, getString(R.string.launcher_preparing))

        thread(name = "soh-prepare") {
            val stagingError = GameAssets.stageBundledAssets(this)

            runOnUiThread {
                when {
                    stagingError != null -> {
                        setBusy(false, stagingError)
                        chooseButton.isEnabled = false
                    }
                    GameAssets.isExtracted(this) -> startGame()
                    GameAssets.romFile(this).isFile -> extractRom()
                    else -> setBusy(false, romInstructions)
                }
            }
        }
    }

    private fun buildUi() {
        val padding = (24 * resources.displayMetrics.density).toInt()
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER
            setPadding(padding, padding, padding, padding)
            setBackgroundColor(Color.rgb(8, 16, 28))
        }

        status = TextView(this).apply {
            setTextColor(Color.WHITE)
            textSize = 16f
            gravity = Gravity.CENTER
        }
        chooseButton = Button(this).apply {
            text = getString(R.string.launcher_choose_rom)
            setOnClickListener { pickRom.launch(arrayOf("*/*")) }
        }
        progress = ProgressBar(this).apply { visibility = View.GONE }

        val scroller = ScrollView(this).apply {
            addView(
                status,
                ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)
            )
        }

        root.addView(scroller, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f))
        root.addView(chooseButton)
        root.addView(progress)
        setContentView(root)
        root.padForSystemBars()
    }

    private fun importRom(uri: Uri) {
        setBusy(true, getString(R.string.launcher_importing))

        thread(name = "soh-rom-import") {
            val result = runCatching {
                val target = GameAssets.romFile(this)
                val partial = File(target.parentFile, "${GameAssets.ROM_NAME}.part")
                partial.delete()

                contentResolver.openInputStream(uri).use { input ->
                    requireNotNull(input) { "The document provider did not open the file." }
                    FileOutputStream(partial).use { output ->
                        input.copyTo(output, COPY_BUFFER_BYTES)
                        output.fd.sync()
                    }
                }

                if (target.exists() && !target.delete()) {
                    error("Could not replace the previous ROM.")
                }
                check(partial.renameTo(target)) { "Could not store the ROM in the app's files directory." }
            }

            runOnUiThread {
                result.fold(
                    onSuccess = { extractRom() },
                    onFailure = { setBusy(false, "${it.message ?: it}\n\n$romInstructions") }
                )
            }
        }
    }

    private fun extractRom() {
        setBusy(true, getString(R.string.launcher_extracting))

        thread(name = "soh-extract") {
            val error = GameAssets.generateGameArchive(this)
            // The ROM is 32-64 MB and only needed for this one run; a bad one must not be retried forever.
            GameAssets.romFile(this).delete()

            runOnUiThread {
                if (error == null) {
                    startGame()
                } else {
                    setBusy(false, "$error\n\n$romInstructions")
                }
            }
        }
    }

    // Extraction has no progress to report and can outlast the display timeout.
    private fun setBusy(busy: Boolean, message: String) {
        status.text = message
        chooseButton.isEnabled = !busy
        progress.visibility = if (busy) View.VISIBLE else View.GONE
        if (busy) {
            window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        } else {
            window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        }
    }

    private fun startGame() {
        startActivity(Intent(this, MainActivity::class.java))
        finish()

        // Torch's Companion is never freed, so exiting this process is what hands its memory back.
        Runtime.getRuntime().exit(0)
    }

    private val romInstructions: String
        get() = "Select your own Ocarina of Time .z64, .n64 or .v64 ROM (Master Quest works too). It is " +
            "checked, converted into the game's asset archive on this device and then deleted — " +
            "no game data ships with the app.\n\nSee https://ship.equipment/ for supported dumps."

    private companion object {
        const val COPY_BUFFER_BYTES = 1 shl 17
    }
}
