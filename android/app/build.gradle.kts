import org.jetbrains.kotlin.gradle.dsl.JvmTarget
import java.security.MessageDigest
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

val repositoryRoot: File = rootProject.projectDir.parentFile

val projectVersion: String =
    Regex("""project\s*\([^)]*VERSION\s+([0-9.]+)""")
        .find(File(repositoryRoot, "CMakeLists.txt").readText())
        ?.groupValues?.get(1)
        ?: "0.0.0"

// Torch runs on the device and reads its recipes (config.yml + per-version ymls) from disk,
// so they ship as one zip that the launcher unpacks on first run.
abstract class PackGameData : DefaultTask() {

    @get:InputDirectory
    @get:PathSensitive(PathSensitivity.RELATIVE)
    abstract val recipes: DirectoryProperty

    // A collection so a missing soh.o2r reaches the message below instead of Gradle's own validation error.
    @get:InputFiles
    @get:PathSensitive(PathSensitivity.NONE)
    abstract val portArchive: ConfigurableFileCollection

    @get:OutputDirectory
    abstract val outputDir: DirectoryProperty

    @TaskAction
    fun pack() {
        val archive = portArchive.files.singleOrNull()?.takeIf(File::isFile)
            ?: throw GradleException(
                "soh.o2r not found at ${portArchive.asPath}. Run the GenerateSohOtr target on a desktop " +
                    "build (or configure with -DSOH_TOOLS_ONLY=ON) before building the APK."
            )

        val recipesRoot = recipes.get().asFile
        val entries = buildList {
            add(archive.name to archive)
            recipesRoot.walkTopDown().filter(File::isFile).forEach {
                add("assets/${it.relativeTo(recipesRoot).invariantSeparatorsPath}" to it)
            }
        }.sortedBy { it.first }

        val target = outputDir.get().asFile
        target.mkdirs()
        val zipFile = File(target, "gamedata.zip")

        // Sorted entries at a fixed timestamp keep the digest stable, so an update only re-unpacks when contents change.
        ZipOutputStream(zipFile.outputStream().buffered()).use { zip ->
            entries.forEach { (name, file) ->
                zip.putNextEntry(ZipEntry(name).apply { time = FIXED_ENTRY_TIME })
                file.inputStream().use { it.copyTo(zip) }
                zip.closeEntry()
            }
        }

        val digest = MessageDigest.getInstance("MD5").digest(zipFile.readBytes())
        File(target, "gamedata.version").writeText(digest.joinToString("") { "%02x".format(it) })
    }

    private companion object {
        // 1980-02-01, safely after the DOS timestamp floor.
        const val FIXED_ENTRY_TIME = 318211200000L
    }
}

// The variant API is needed here: a plain assets srcDir would skip the dependency on this task.
androidComponents {
    onVariants { variant ->
        val packGameData =
            tasks.register<PackGameData>("pack${variant.name.replaceFirstChar(Char::titlecase)}GameData") {
                description = "Bundles the Torch recipes and soh.o2r into the APK."
                recipes.set(File(repositoryRoot, "soh/assets/yml"))
                portArchive.from(File(repositoryRoot, "soh.o2r"))
            }
        variant.sources.assets?.addGeneratedSourceDirectory(packGameData, PackGameData::outputDir)
    }
}

android {
    namespace = "com.harbourmasters.soh"
    compileSdk = 36
    ndkVersion = "30.0.15729638"

    defaultConfig {
        applicationId = "com.harbourmasters.soh"
        minSdk = 26
        targetSdk = 36
        versionCode = 1
        versionName = projectVersion

        ndk {
            abiFilters += listOf("arm64-v8a")
        }

        externalNativeBuild {
            cmake {
                arguments += listOf(
                    "-DANDROID_STL=c++_shared",
                    "-DUSE_OPENGLES=ON",
                    "-DSDL_SHARED=ON",
                    "-DSDL_STATIC=OFF",
                    "-DHAVE_LD_VERSION_SCRIPT=OFF",
                    "-DCMAKE_BUILD_TYPE=Release"
                )
                targets += "soh"
            }
        }
    }

    signingConfigs {
        System.getenv("SOH_KEYSTORE_FILE")?.let { keystore ->
            create("shared") {
                storeFile = file(keystore)
                storeType = "pkcs12"
                storePassword = System.getenv("SOH_KEYSTORE_PASSWORD")
                keyAlias = "soh"
                keyPassword = System.getenv("SOH_KEYSTORE_PASSWORD")
            }
        }
    }

    buildTypes {
        debug {
            applicationIdSuffix = ".debug"
        }
        release {
            // The Kotlin layer is tiny and the JNI bridge looks classes up by name.
            isMinifyEnabled = false
            // Falls back to the debug key when no keystore is configured, e.g. on forks.
            signingConfig = signingConfigs.findByName("shared") ?: signingConfigs.getByName("debug")
            ndk {
                debugSymbolLevel = "SYMBOL_TABLE"
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = File(repositoryRoot, "CMakeLists.txt")
            // libultraship needs CMake >= 3.24; the NDK-bundled 3.22.1 is too old.
            version = "3.30.3+"
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    lint {
        abortOnError = false
    }
}

kotlin {
    compilerOptions {
        jvmTarget.set(JvmTarget.JVM_17)
    }
}

dependencies {
    implementation("androidx.core:core-ktx:1.13.1")
    implementation("androidx.activity:activity-ktx:1.9.3")
}
