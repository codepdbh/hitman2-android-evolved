/*
 * h2ae_jni.c - JNI surface of libh2ae.so. Android glue only: it forwards to the
 * platform-neutral runtime in <repo>/runtime and holds no game logic.
 */
#include <jni.h>
#include <stdio.h>

#include "h2_log.h"
#include "h2_selftest.h"

#ifndef H2AE_ABI
#define H2AE_ABI "unknown-abi"
#endif

JNIEXPORT jstring JNICALL
Java_com_h2ae_android_NativeBridge_buildInfo(JNIEnv *env, jobject thiz)
{
    char buf[256];
    (void)thiz;
    snprintf(buf, sizeof buf, "libh2ae.so · %s · punteros de %u bits · compilado %s %s",
             H2AE_ABI, (unsigned)(sizeof(void *) * 8), __DATE__, __TIME__);
    return (*env)->NewStringUTF(env, buf);
}

JNIEXPORT jstring JNICALL
Java_com_h2ae_android_NativeBridge_runSelfTest(JNIEnv *env, jobject thiz)
{
    static char report[8192];
    (void)thiz;
    H2_INFO(H2_LOG_ANDROID, "self-test requested from the app");
    h2_selftest(report, sizeof report);
    return (*env)->NewStringUTF(env, report);
}
