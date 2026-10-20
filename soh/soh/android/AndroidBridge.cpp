#ifdef __ANDROID__

#include <jni.h>
#include <android/log.h>
#include <stdlib.h>

#include <atomic>
#include <exception>
#include <filesystem>
#include <string>

#include <imgui.h>
#include <ship/Context.h>
#include <ship/window/Window.h>
#include <ship/window/gui/Gui.h>

#include "soh/Extractor/Extract.h"
#include "soh/SohGui/SohMenu.h"
#include "AndroidBridge.h"

namespace {

std::atomic<bool> sMenuToggleRequested{ false };

constexpr const char* kLogTag = "SoH";

std::string ToStdString(JNIEnv* env, jstring value) {
    if (value == nullptr) {
        return {};
    }
    const char* chars = env->GetStringUTFChars(value, nullptr);
    std::string result = chars != nullptr ? chars : "";
    if (chars != nullptr) {
        env->ReleaseStringUTFChars(value, chars);
    }
    return result;
}

} // namespace

extern "C" {

// Called from the launcher before SDL exists, so paths come in as arguments instead of through Ship::Context.
JNIEXPORT jstring JNICALL Java_com_harbourmasters_soh_GameAssets_nativeGenerateGameArchive(JNIEnv* env, jobject,
                                                                                         jstring jRomPath,
                                                                                         jstring jAppDir) {
    const std::string romPath = ToStdString(env, jRomPath);
    const std::string appDir = ToStdString(env, jAppDir);

    const auto fail = [env](const std::string& message) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "Asset generation failed: %s", message.c_str());
        return env->NewStringUTF(message.c_str());
    };

    // Android has no /tmp, and CallTorch stages its output through temp_directory_path().
    const std::string tmpDir = appDir + "/tmp";
    std::error_code ec;
    std::filesystem::create_directories(tmpDir, ec);
    setenv("TMPDIR", tmpDir.c_str(), 1);

    Extractor extractor;
    if (!extractor.RunFileStandalone(romPath)) {
        return fail("That is not a supported Ocarina of Time ROM. Check it at https://ship.equipment/.");
    }

    __android_log_print(ANDROID_LOG_INFO, kLogTag, "Extracting %s into %s", romPath.c_str(), appDir.c_str());

    std::atomic<size_t> extracted{ 0 };
    std::atomic<size_t> total{ 0 };
    std::string error;
    try {
        if (!extractor.CallTorch(appDir, appDir, &extracted, &total)) {
            error = "Torch could not extract the ROM.";
        }
    } catch (const std::exception& e) {
        error = std::string("Torch could not extract the ROM: ") + e.what();
    } catch (...) { error = "Torch could not extract the ROM."; }

    std::filesystem::remove_all(tmpDir, ec);

    return error.empty() ? nullptr : fail(error);
}

JNIEXPORT void JNICALL Java_com_harbourmasters_soh_MainActivity_nativeRequestMenuToggle(JNIEnv*, jobject) {
    sMenuToggleRequested = true;
}

void AndroidBridge_PollMenuToggle(void) {
    if (!sMenuToggleRequested.exchange(false) || ImGui::GetCurrentContext() == nullptr) {
        return;
    }
    const auto gui = Ship::Context::GetRawInstance()->GetWindow()->GetGui();
    if (gui != nullptr && gui->GetMenu() != nullptr) {
        gui->GetMenu()->ToggleVisibility();
    }
}

} // extern "C"

#endif // __ANDROID__
