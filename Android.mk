LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := namestackd
LOCAL_SRC_FILES := daemon.c NameStackDaemon.cpp

include $(BUILD_SHARED_LIBRARY)
