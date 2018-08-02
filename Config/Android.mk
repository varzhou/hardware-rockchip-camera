# Copyright (C) 201 Fuzhou Rockchip Electronics Co., Ltd. All rights reserved.
# BY DOWNLOADING, INSTALLING, COPYING, SAVING OR OTHERWISE USING THIS SOFTWARE,
# YOU ACKNOWLEDGE THAT YOU AGREE THE SOFTWARE RECEIVED FORM ROCKCHIP IS PROVIDED
# TO YOU ON AN "AS IS" BASIS and ROCKCHP DISCLAIMS ANY AND ALL WARRANTIES AND
# REPRESENTATIONS WITH RESPECT TO SUCH FILE, WHETHER EXPRESS, IMPLIED, STATUTORY
# OR OTHERWISE, INCLUDING WITHOUT LIMITATION, ANY IMPLIED WARRANTIES OF TITLE,
# NON-INFRINGEMENT, MERCHANTABILITY, SATISFACTROY QUALITY, ACCURACY OR FITNESS FOR
# A PARTICULAR PURPOSE. 
# Rockchip hereby grants to you a limited, non-exclusive, non-sublicensable and 
# non-transferable license (a) to install, save and use the Software; (b) to copy 
# and distribute the Software in binary code format only. 
# Except as expressively authorized by Rockchip in writing, you may NOT: (a) distribute 
# the Software in source code; (b) distribute on a standalone basis but you may distribute 
# the Software in conjunction with platforms incorporating Rockchip integrated circuits;
# (c) modify the Software in whole or part;(d) decompile, reverse-engineer, dissemble,
# or attempt to derive any source code from the Software;(e) remove or obscure any copyright,
# patent, or trademark statement or notices contained in the Software.

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_PROPRIETARY_MODULE := true
ifeq (1,$(strip $(shell expr $(PLATFORM_VERSION) \>= 8.0)))
	ifeq ($(strip $(TARGET_BOARD_PLATFORM)),rk3326)
		LOCAL_MODULE := lib_rkisp12_api
		LOCAL_SRC_FILES_arm := $(LOCAL_MODULE)_32bit.so
		LOCAL_SRC_FILES_arm64 := $(LOCAL_MODULE)_64bit.so
	else
		LOCAL_MODULE := lib_rkisp1_api
		LOCAL_SRC_FILES_arm := $(LOCAL_MODULE)_32bit.so
		LOCAL_SRC_FILES_arm64 := $(LOCAL_MODULE)_64bit.so
	endif
endif

#include $(CLEAR_VARS)
ifneq ($(strip $(TARGET_2ND_ARCH)), )
	LOCAL_MULTILIB := both
endif
LOCAL_MODULE_RELATIVE_PATH :=

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := SHARED_LIBRARIES
LOCAL_MODULE_STEM := $(LOCAL_MODULE)
LOCAL_MODULE_SUFFIX := .so
include $(BUILD_PREBUILT)

