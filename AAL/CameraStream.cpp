/*
 * Copyright (C) 2013-2017 Intel Corporation
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

#define LOG_TAG "Stream"
#include <stddef.h>

#include "LogHelper.h"
#include "CameraStream.h"
#include "PlatformData.h"
#include "PerformanceTraces.h"

NAMESPACE_DECLARATION {
CameraStream::CameraStream(int seqNo, camera3_stream_t * stream,
                           IRequestCallback * callback) : mActive(false),
                                                          mSeqNo(seqNo),
                                                          mCallback(callback),
                                                          mOutputBuffersInHal(0),
                                                          mStream3(stream)
{
}

CameraStream::~CameraStream()
{
    LOGI("%s, pending request size=%zu", __FUNCTION__, mPendingRequests.size());

    std::unique_lock<std::mutex> l(mPendingLock);
    mPendingRequests.clear();
    l.unlock();

    mCamera3Buffers.clear();
}

void CameraStream::setActive(bool active)
{
    LOGI("CameraStream [%d] set %s", mSeqNo, active? " Active":" Inactive");
    mActive = active;
}

void CameraStream::dump(bool dumpBuffers) const
{
    LOGI("Stream %d (IO type %d) dump: -----", mSeqNo, mStream3->stream_type);
    LOGI("    %dx%d, fmt%d usage %x, buffers num %d (available %zu)",
        mStream3->width, mStream3->height,
        mStream3->format,
        mStream3->usage,
        mStream3->max_buffers, mCamera3Buffers.size());
    if (dumpBuffers) {
        for (unsigned int i = 0; i < mCamera3Buffers.size(); i++) {
            LOGI("        %d: handle %p, dataPtr %p", i,
                mCamera3Buffers[i]->getBufferHandle(), mCamera3Buffers[i]->data());
        }
    }
}

status_t CameraStream::query(FrameInfo * info)
{
    LOGI("%s", __FUNCTION__);
    info->width= mStream3->width;
    info->height= mStream3->height;
    info->format =  mStream3->format;
    return NO_ERROR;
}

status_t CameraStream::capture(std::shared_ptr<CameraBuffer> aBuffer,
                               Camera3Request* request)
{
    LOGE("ERROR @%s: this is consumer node is nullptr", __FUNCTION__);
    UNUSED(aBuffer);
    UNUSED(request);
    return NO_ERROR;
}

status_t CameraStream::captureDone(std::shared_ptr<CameraBuffer> aBuffer,
                                   Camera3Request* request)
{
    std::lock_guard<std::mutex> l(mPendingLock);
    /* Usually the correct request is found at index 0 in the mPendingRequests
     * Vector, but reprocessing requests are allowed to deviate from the FIFO
     * rule. See camera3.h section "S10.3 Reprocessing pipeline characteristics"
     *
     * The PSL shall be responsible for maintaining per-stream FIFO processing
     * order among all the normal output requests and among the reprocessing
     * requests, but reprocessing requests may be completed before normal output
     * requests.
     */
    for (uint32_t i = 0; i < mPendingRequests.size(); i++) {
        Camera3Request *pendingRequest;
        pendingRequest = mPendingRequests.at(i);
        if (request == nullptr || request->getId() == pendingRequest->getId()) {
            switch (mStreamType) {
            case STREAM_PREVIEW:
                LOGI("%s:%d: Preview buffer done, instance(%p), requestId(%d)", __FUNCTION__, __LINE__, this, request->getId());
                break;
            case STREAM_CAPTURE:
                LOGI("%s:%d: Capture buffer done, instance(%p), requestId(%d)", __FUNCTION__, __LINE__, this, request->getId());
                break;
            case STREAM_VIDEO:
                LOGI("%s:%d: Video buffer done, instance(%p), requestId(%d)", __FUNCTION__, __LINE__, this, request->getId());
                break;
            default :
                LOGW("%s:%d: Not support the stream type, is a buf, fix me", __FUNCTION__, __LINE__);
                break;
            }
            mPendingRequests.erase(mPendingRequests.begin() + i);
            mCallback->bufferDone(pendingRequest, aBuffer);
            if (request != nullptr)
                PERFORMANCE_HAL_ATRACE_PARAM1("seqId", request->sequenceId());
            break;
        }
    }

    return NO_ERROR;
}

status_t CameraStream::reprocess(std::shared_ptr<CameraBuffer> aBuffer,
                                 Camera3Request* request)
{
    LOGW("@%s: not implemented", __FUNCTION__);
    UNUSED(aBuffer);
    UNUSED(request);
    return NO_ERROR;
}

status_t CameraStream::processRequest(Camera3Request* request)
{
    LOGD("@%s %d, capture mProducer:%p, mConsumer:%p", __FUNCTION__, mSeqNo, mProducer, mConsumer);
    int status = NO_ERROR;
    std::shared_ptr<CameraBuffer> buffer;
    if (mProducer == nullptr) {
        LOGE("ERROR @%s: mProducer is nullptr", __FUNCTION__);
        return BAD_VALUE;
    }

    std::unique_lock<std::mutex> l(mPendingLock);
    mPendingRequests.push_back(request);
    l.unlock();

    buffer = request->findBuffer(this);
    if (CC_UNLIKELY(buffer == nullptr)) {
        LOGE("@%s No buffer associated with stream.", __FUNCTION__);
        return NO_MEMORY;
    }
    status = mProducer->capture(buffer, request);

    return status;
}

status_t CameraStream::configure(void)
{
    LOGI("@%s, %d, mProducer:%p  (%p)", __FUNCTION__, mSeqNo, mProducer, this);
    if (!mProducer) {
        LOGE("mProducer = nullptr");
        return BAD_VALUE;
    }
    /* TODO : stream tpye choose should refer outputFramework*/
    if (mStream3->format == HAL_PIXEL_FORMAT_BLOB) {
        mStreamType = STREAM_CAPTURE;
    } else if (mStream3->format == HAL_PIXEL_FORMAT_YCbCr_420_888) {
        mStreamType = STREAM_VIDEO;
    } else {
        mStreamType = STREAM_PREVIEW;
    }
    LOGI("%s:%d: format %d, usage %d, stream type %d", __func__, __LINE__, mStream3->format, mStream3->usage, mStreamType);

    FrameInfo info;
    mProducer->query(&info);
    if ((info.width == (int)mStream3->width) &&
        (info.height == (int)mStream3->height) &&
        (info.format == mStream3->format)) {
        return NO_ERROR;
    }

    LOGE("@%s configure error : w %d x h %d F:%d vs w %d x h %d F:%d", __FUNCTION__,
         mStream3->width, mStream3->height, mStream3->format,
         info.width, info.height, info.format);
    return UNKNOWN_ERROR;
}

void CameraStream::dump(int fd) const
{
    if (mProducer != nullptr)
        mProducer->dump(fd);
}

} NAMESPACE_DECLARATION_END

