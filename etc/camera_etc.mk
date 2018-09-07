# prebuilt for config xml files in /vendor/etc/camera or /system/etc/camera
CUR_PATH := $(TOP)/hardware/rockchip/camera/etc
ifeq ($(filter box atv vr stbvr, $(strip $(TARGET_BOARD_PLATFORM_PRODUCT))), )
ifeq (1,$(strip $(shell expr $(PLATFORM_VERSION) \>= 8.0)))

PRODUCT_COPY_FILES += \
	$(CUR_PATH)/camera/camera3_profiles_$(TARGET_BOARD_PLATFORM).xml:$(TARGET_COPY_OUT_VENDOR)/etc/camera/camera3_profiles.xml \
	$(call find-copy-subdir-files,*,$(CUR_PATH)/camera,$(TARGET_COPY_OUT_VENDOR)/etc/camera)
else
PRODUCT_COPY_FILES += \
	$(CUR_PATH)/camera/camera3_profiles_$(TARGET_BOARD_PLATFORM).xml:$(TARGET_COPY_OUT_SYSTEM)/etc/camera/camera3_profiles.xml \
	$(call find-copy-subdir-files,*,$(CUR_PATH)/camera,$(TARGET_COPY_OUT_SYSTEM)/etc/camera)

endif
endif


