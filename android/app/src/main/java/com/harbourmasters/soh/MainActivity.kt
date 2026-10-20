package com.harbourmasters.soh

import android.os.Build
import android.os.Bundle
import android.view.InputDevice
import android.view.KeyEvent
import android.view.WindowManager
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import java.io.File
import org.libsdl.app.SDLActivity

class MainActivity : SDLActivity() {

    // org/libsdl/app stays byte-identical to the SDL release libultraship pins, so the library list is overridden here.
    override fun getLibraries(): Array<String> = arrayOf("SDL2", "main")

    // SDL derives the game folder from getExternalFilesDir(), so it has to match GameAssets.
    override fun getExternalFilesDir(type: String?): File? =
        if (type == null) appMediaDir(this) ?: super.getExternalFilesDir(null) else super.getExternalFilesDir(type)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        goFullscreen()
    }

    // From API 35 the old fullscreen window flags are ignored, so the bars are hidden through the insets controller.
    private fun goFullscreen() {
        WindowCompat.setDecorFitsSystemWindows(window, false)

        WindowInsetsControllerCompat(window, window.decorView).apply {
            hide(WindowInsetsCompat.Type.systemBars())
            systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            window.attributes.layoutInDisplayCutoutMode =
                WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS
        }
    }

    private external fun nativeRequestMenuToggle()

    // Handled here rather than through SDL so Select opens the menu whatever mapping the pad ends up with.
    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        val fromGamepad = (event.source and InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
        val isSelect = event.keyCode == KeyEvent.KEYCODE_BUTTON_SELECT ||
            (event.keyCode == KeyEvent.KEYCODE_BACK && fromGamepad)
        if (!isSelect) {
            return super.dispatchKeyEvent(event)
        }
        if (event.action == KeyEvent.ACTION_DOWN && event.repeatCount == 0) {
            try {
                nativeRequestMenuToggle()
            } catch (_: UnsatisfiedLinkError) {
                // Native libraries aren't loaded yet.
            }
        }
        return true
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) {
            goFullscreen()
        }
    }
}
