LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := namestackd
LOCAL_SRC_FILES := daemon.c dns.c NameStackDaemon.cpp
LOCAL_CPP_FLAGS := -DANDROID

include $(BUILD_SHARED_LIBRARY)
