package com.harbourmasters.soh

import android.content.Context
import android.util.Log
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.util.zip.ZipInputStream

// Everything lives in getExternalFilesDir(): soh.o2r and assets/ come from the APK, oot.o2r is built from the ROM.
object GameAssets {

    const val ROM_NAME = "baserom.z64"

    private const val TAG = "GameAssets"
    private const val GAMEDATA_ZIP = "gamedata.zip"
    private const val GAMEDATA_VERSION = "gamedata.version"
    private const val STAMP = ".gamedata.version"
    private const val COPY_BUFFER_BYTES = 1 shl 17

    fun gameDir(context: Context): File =
        (context.getExternalFilesDir(null) ?: context.filesDir).also { it.mkdirs() }

    fun romFile(context: Context) = File(gameDir(context), ROM_NAME)

    fun isExtracted(context: Context): Boolean =
        listOf("oot.o2r", "oot-mq.o2r").any { File(gameDir(context), it).length() > 0 }

    fun stageBundledAssets(context: Context): String? {
        val target = gameDir(context)
        val stamp = File(target, STAMP)

        return try {
            val expected = context.assets.open(GAMEDATA_VERSION).use { it.readBytes().decodeToString() }
            if (stamp.takeIf { it.isFile }?.readText() == expected) {
                return null
            }

            context.assets.open(GAMEDATA_ZIP).use { input ->
                ZipInputStream(input.buffered()).use { zip ->
                    while (true) {
                        val entry = zip.nextEntry ?: break
                        val destination = File(target, entry.name).canonicalFile

                        if (!destination.path.startsWith(target.canonicalPath + File.separator)) {
                            Log.w(TAG, "Skipping zip entry outside the game dir: ${entry.name}")
                            zip.closeEntry()
                            continue
                        }

                        if (entry.isDirectory) {
                            destination.mkdirs()
                        } else {
                            destination.parentFile?.mkdirs()
                            FileOutputStream(destination).use { output -> zip.copyTo(output, COPY_BUFFER_BYTES) }
                        }
                        zip.closeEntry()
                    }
                }
            }

            stamp.writeText(expected)
            null
        } catch (error: IOException) {
            Log.e(TAG, "Could not unpack the bundled assets", error)
            "Could not unpack the bundled game files: ${error.message}"
        }
    }

    // Blocks for minutes; returns an error message or null.
    fun generateGameArchive(context: Context): String? =
        nativeGenerateGameArchive(romFile(context).absolutePath, gameDir(context).absolutePath)

    private external fun nativeGenerateGameArchive(rom: String, appDir: String): String?

    init {
        // libmain.so carries both the game and the extractor.
        System.loadLibrary("SDL2")
        System.loadLibrary("main")
    }
}
