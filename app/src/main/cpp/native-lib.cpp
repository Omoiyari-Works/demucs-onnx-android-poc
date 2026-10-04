#include <jni.h>

#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>

#include <array>
#include <onnxruntime_cxx_api.h>
#include <string>

#include "demucs_separator.h"

namespace {

// RAII wrapper so GetStringUTFChars is always released, even if an exception
// is thrown while this is in scope.
class JavaUtf8String {
public:
    JavaUtf8String(JNIEnv* env, jstring value) : env_(env), value_(value), chars_(env->GetStringUTFChars(value, nullptr)) {}
    ~JavaUtf8String() {
        if (chars_ != nullptr) {
            env_->ReleaseStringUTFChars(value_, chars_);
        }
    }
    JavaUtf8String(const JavaUtf8String&) = delete;
    JavaUtf8String& operator=(const JavaUtf8String&) = delete;

    [[nodiscard]] const char* c_str() const { return chars_; }

private:
    JNIEnv* env_;
    jstring value_;
    const char* chars_;
};

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_omoiyariworks_demucspoc_MainActivity_nativeGetOrtVersion(JNIEnv* env, jobject /* this */) {
    std::string version = Ort::GetVersionString();
    return env->NewStringUTF(version.c_str());
}

extern "C" JNIEXPORT jlongArray JNICALL
Java_com_omoiyariworks_demucspoc_MainActivity_nativeSeparateVocals(
    JNIEnv* env, jobject /* thiz */, jobject assetManager, jstring modelAssetName, jstring inputWavPath,
    jstring outputWavPath) {
    try {
        AAssetManager* nativeAssetManager = AAssetManager_fromJava(env, assetManager);
        const JavaUtf8String modelAssetNameUtf8(env, modelAssetName);
        const JavaUtf8String inputWavPathUtf8(env, inputWavPath);
        const JavaUtf8String outputWavPathUtf8(env, outputWavPath);

        demucspoc::DemucsSeparator separator;
        const demucspoc::SeparationTimings timings = separator.separateVocals(
            nativeAssetManager, modelAssetNameUtf8.c_str(), inputWavPathUtf8.c_str(), outputWavPathUtf8.c_str());

        const std::array<jlong, 5> result{timings.modelLoadMs, timings.preprocessMs, timings.inferenceMs,
                                           timings.postprocessMs, timings.endToEndMs};
        jlongArray resultArray = env->NewLongArray(static_cast<jsize>(result.size()));
        env->SetLongArrayRegion(resultArray, 0, static_cast<jsize>(result.size()), result.data());
        return resultArray;
    } catch (const std::exception& e) {
        jclass exceptionClass = env->FindClass("java/lang/RuntimeException");
        env->ThrowNew(exceptionClass, e.what());
        return nullptr;
    } catch (...) {
        jclass exceptionClass = env->FindClass("java/lang/RuntimeException");
        env->ThrowNew(exceptionClass, "DemucsSeparator: unknown native exception");
        return nullptr;
    }
}
