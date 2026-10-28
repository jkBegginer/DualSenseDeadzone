#include <jni.h>

#include "bridge.h"
#include "deadzone.h"

/* All functions back com.dualsensedeadzone.NativeBridge and run inside the Shizuku (shell uid) process. */

JNIEXPORT jstring JNICALL
Java_com_dualsensedeadzone_NativeBridge_nativeStart(JNIEnv *env, jobject thiz) {
    (void) thiz;
    char err[256] = {0};
    if (bridge_start(err, sizeof(err)) < 0) {
        return (*env)->NewStringUTF(env, err);
    }
    return NULL;
}

JNIEXPORT void JNICALL
Java_com_dualsensedeadzone_NativeBridge_nativeStop(JNIEnv *env, jobject thiz) {
    (void) env;
    (void) thiz;
    bridge_stop();
}

JNIEXPORT jboolean JNICALL
Java_com_dualsensedeadzone_NativeBridge_nativeIsRunning(JNIEnv *env, jobject thiz) {
    (void) env;
    (void) thiz;
    return bridge_is_running() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_dualsensedeadzone_NativeBridge_nativeSetConfig(JNIEnv *env, jobject thiz, jfloatArray values) {
    (void) thiz;
    jsize count = (*env)->GetArrayLength(env, values);
    if (count < DEADZONE_CFG_FLOATS) return;
    jfloat buf[DEADZONE_CFG_FLOATS];
    (*env)->GetFloatArrayRegion(env, values, 0, DEADZONE_CFG_FLOATS, buf);
    bridge_set_config(buf, DEADZONE_CFG_FLOATS);
}

JNIEXPORT jintArray JNICALL
Java_com_dualsensedeadzone_NativeBridge_nativeGetSnapshot(JNIEnv *env, jobject thiz) {
    (void) thiz;
    bridge_snapshot s;
    bridge_get_snapshot(&s);
    /* Order matches StickSnapshot.fromArray(). */
    jint values[12] = {
        s.raw_lx, s.raw_ly, s.raw_rx, s.raw_ry,
        s.out_lx, s.out_ly, s.out_rx, s.out_ry,
        s.raw_l2, s.raw_r2, s.out_l2, s.out_r2,
    };
    jintArray arr = (*env)->NewIntArray(env, 12);
    if (arr) (*env)->SetIntArrayRegion(env, arr, 0, 12, values);
    return arr;
}
