#include <jni.h>
#include "daemon.h"

void
Java_com_ericsson_namestackd_NameStackDaemon_Run(JNIEnv *env, jobject obj)
{
    run_daemon();
}
