CUR_PATH := $(TOP)/hardware/rockchip/camera/etc
PRODUCT_COPY_FILES += \
	$(call find-copy-subdir-files,*,$(CUR_PATH)/camera,$(TARGET_COPY_OUT_SYSTEM)/etc/camera)
