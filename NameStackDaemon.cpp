#include <stdlib.h>
#include <android/log.h>
#include <jni.h>
#include "daemon.h"

#define ANDROID_LOG_TAG "namestack daemon"
#define ANDROID_LOG(l, ...) __android_log_print((l), ANDROID_LOG_TAG, __VA_ARGS__)

#define LOGV(x, ...) ANDROID_LOG(ANDROID_LOG_VERBOSE, x, __VA_ARGS__)
#define LOGD(x, ...) ANDROID_LOG(ANDROID_LOG_DEBUG, x, __VA_ARGS__)
#define LOGI(x, ...) ANDROID_LOG(ANDROID_LOG_INFO, x, __VA_ARGS__)
#define LOGW(x, ...) ANDROID_LOG(ANDROID_LOG_WARN, x, __VA_ARGS__)
#define LOGE(x, ...) ANDROID_LOG(ANDROID_LOG_ERROR, x, __VA_ARGS__)

void
Java_com_ericsson_namestackd_daemon_Run(JNIEnv *env, jobject obj)
{
    run_daemon();
}

static int jniRegisterNativeMethods(JNIEnv *env, const char *className,
    const JNINativeMethod *gMethods, int numMethods)
{
    jclass clazz;

    LOGV("Registering %s natives\n", className);
    clazz = env->FindClass(className);
    if (clazz == NULL) {
        LOGE("Native registration unable to find class '%s'\n", className);
        return -1;
    }
    if (env->RegisterNatives(clazz, gMethods, numMethods) < 0) {
        LOGE("RegisterNatives failed for '%s'\n", className);
        return -1;
    }
    return 0;
}

static JNINativeMethod sMethods[] = {
     /* name, signature, funcPtr */
    { "Run", "()V", (void*)Java_com_ericsson_namestackd_daemon_Run },
};

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *reserved)
{
    JNIEnv* env = NULL;

    if (vm->GetEnv((void**)&env, JNI_VERSION_1_4) != JNI_OK)
        return -1;

    jniRegisterNativeMethods(env, "com/ericsson/namestackd/daemon",
                             sMethods, sizeof(sMethods) / sizeof(sMethods[0]));
    return JNI_VERSION_1_4;
}
