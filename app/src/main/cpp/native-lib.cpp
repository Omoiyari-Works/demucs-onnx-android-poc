#include <jni.h>
#include <string>

#include <onnxruntime_cxx_api.h>

extern "C" JNIEXPORT jstring JNICALL
Java_com_omoiyariworks_demucspoc_MainActivity_nativeGetOrtVersion(JNIEnv* env, jobject /* this */) {
    std::string version = Ort::GetVersionString();
    return env->NewStringUTF(version.c_str());
}
