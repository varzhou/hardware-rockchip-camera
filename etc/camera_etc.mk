# prebuilt for config xml files in /vendor/etc/camera or /system/etc/camera
CUR_PATH := $(TOP)/hardware/rockchip/camera/etc
ifeq (1,$(strip $(shell expr $(PLATFORM_VERSION) \>= 8.0)))
PRODUCT_COPY_FILES += \
	$(call find-copy-subdir-files,*,$(CUR_PATH)/camera,$(TARGET_COPY_OUT_VENDOR)/etc/camera)
else
PRODUCT_COPY_FILES += \
	$(call find-copy-subdir-files,*,$(CUR_PATH)/camera,$(TARGET_COPY_OUT_SYSTEM)/etc/camera)
endif
