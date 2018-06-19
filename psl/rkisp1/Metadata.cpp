/*
 * Copyright (C) 2016-2017 Intel Corporation.
 * Copyright (c) 2017, Fuzhou Rockchip Electronics Co., Ltd
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "Metadata"

#include "Metadata.h"
#include "ControlUnit.h"
#include "LogHelper.h"
#include "SettingsProcessor.h"
#include "CameraMetadataHelper.h"

namespace android {
namespace camera2 {

Metadata::Metadata(int cameraId):
        mCameraId(cameraId)
{
}

Metadata::~Metadata()
{
}

status_t Metadata::init()
{
    return OK;
}

/**
 * Update the Jpeg metadata
 * Only copying from control to dynamic
 */
void Metadata::writeJpegMetadata(RequestCtrlState &reqState) const
{
    if (reqState.request == nullptr) {
        LOGE("nullptr request in RequestCtrlState - BUG.");
        return;
    }

    const CameraMetadata *settings = reqState.request->getSettings();

    if (settings == nullptr) {
        LOGE("No settings for JPEG in request - BUG.");
        return;
    }

    // TODO: Put JPEG settings to ProcessingUnitSettings, when implemented

    camera_metadata_ro_entry entry = settings->find(ANDROID_JPEG_GPS_COORDINATES);
    if (entry.count == 3) {
        reqState.ctrlUnitResult->update(ANDROID_JPEG_GPS_COORDINATES, entry.data.d, entry.count);
    }

    entry = settings->find(ANDROID_JPEG_GPS_PROCESSING_METHOD);
    if (entry.count > 0) {
        reqState.ctrlUnitResult->update(ANDROID_JPEG_GPS_PROCESSING_METHOD, entry.data.u8, entry.count);
    }

    entry = settings->find(ANDROID_JPEG_GPS_TIMESTAMP);
    if (entry.count == 1) {
        reqState.ctrlUnitResult->update(ANDROID_JPEG_GPS_TIMESTAMP, entry.data.i64, entry.count);
    }

    entry = settings->find(ANDROID_JPEG_ORIENTATION);
    if (entry.count == 1) {
        reqState.ctrlUnitResult->update(ANDROID_JPEG_ORIENTATION, entry.data.i32, entry.count);
    }

    entry = settings->find(ANDROID_JPEG_QUALITY);
    if (entry.count == 1) {
        reqState.ctrlUnitResult->update(ANDROID_JPEG_QUALITY, entry.data.u8, entry.count);
    }

    entry = settings->find(ANDROID_JPEG_THUMBNAIL_QUALITY);
    if (entry.count == 1) {
        reqState.ctrlUnitResult->update(ANDROID_JPEG_THUMBNAIL_QUALITY, entry.data.u8, entry.count);
    }

    entry = settings->find(ANDROID_JPEG_THUMBNAIL_SIZE);
    if (entry.count == 2) {
        reqState.ctrlUnitResult->update(ANDROID_JPEG_THUMBNAIL_SIZE, entry.data.i32, entry.count);
    }
}

} /* namespace camera2 */
} /* namespace android */
