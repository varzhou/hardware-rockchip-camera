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

#define LOG_TAG "OutputFrameWorker"

#include "LogHelper.h"
#include "PerformanceTraces.h"
#include "OutputFrameWorker.h"
#include "ColorConverter.h"
#include "NodeTypes.h"
#include <libyuv.h>
#include <sys/mman.h>

namespace android {
namespace camera2 {

OutputFrameWorker::OutputFrameWorker(std::shared_ptr<V4L2VideoNode> node, int cameraId,
                camera3_stream_t* stream, NodeTypes nodeName, size_t pipelineDepth) :
                FrameWorker(node, cameraId, pipelineDepth, "OutputFrameWorker"),
                mOutputBuffer(nullptr),
                mStream(stream),
                mNeedPostProcess(false),
                mNodeName(nodeName),
                mPostPipeline(new PostProcessPipeLine(this, cameraId)),
                mPostProcItemsPool("PostBufPool"),
                mFrameCount(0),
                mLastFrameCount(0)
{
    if (mNode)
        LOGI("@%s, instance(%p), mStream(%p), device name:%s", __FUNCTION__, this, mStream, mNode->name());
}

OutputFrameWorker::~OutputFrameWorker()
{
    HAL_TRACE_CALL(CAM_GLBL_DBG_HIGH);
    if (mOutputForListener.get() && mOutputForListener->isLocked()) {
        mOutputForListener->unlock();
    }
}

status_t
OutputFrameWorker::stopWorker()
{
    FrameWorker::stopWorker();
    mPostPipeline->flush();
    mPostPipeline->stop();
    mPostWorkingBufs.clear();
    mPostPipeline.reset();

    return OK;
}

status_t
OutputFrameWorker::notifyNewFrame(const std::shared_ptr<PostProcBuffer>& buf,
                                  const std::shared_ptr<ProcUnitSettings>& settings,
                                  int err)
{
    CameraStream *stream = buf->cambuf->getOwner();
    stream->captureDone(buf->cambuf, buf->request);
    return OK;
}

void OutputFrameWorker::showDebugFPS(int streamType)
{
    double fps = 0;
    mFrameCount++;
    nsecs_t now = systemTime();
    nsecs_t diff = now - mLastFpsTime;
    if ((unsigned long)diff > 1000000000) {
        fps = (((double)(mFrameCount - mLastFrameCount)) *
                (double)(1000000000)) / (double)diff;
        switch(streamType) {
            case STREAM_PREVIEW:
                LOGI("%s: Preview FPS : %.4f: mFrameCount=%d", __func__, fps, mFrameCount);
                break;
            case STREAM_VIDEO:
                LOGI("%s: Video FPS : %.4f", __func__, fps);
                break;
            default:
                LOGW("%s: logging not supported for the stream(%d)", __FUNCTION__, streamType);
                break;
        }
        mLastFpsTime = now;
        mLastFrameCount = mFrameCount;
    }
}

void OutputFrameWorker::addListener(camera3_stream_t* stream)
{
    if (stream != nullptr) {
        LOGI("stream %p has listener %p", mStream, stream);
        mListeners.push_back(stream);
    }
}

void OutputFrameWorker::clearListeners()
{
    mListeners.clear();
}

status_t OutputFrameWorker::configure(std::shared_ptr<GraphConfig> &/*config*/)
{
    HAL_TRACE_CALL(CAM_GLBL_DBG_HIGH);

    status_t ret = mNode->getFormat(mFormat);
    if (ret != OK)
        return ret;

    LOGI("@%s allocate format: %s size: %d %dx%d", __func__, v4l2Fmt2Str(mFormat.pixelformat()),
            mFormat.sizeimage(),
            mFormat.width(),
            mFormat.height());

    FrameInfo sourceFmt;
    sourceFmt.width = mFormat.width();
    sourceFmt.height = mFormat.height();
    sourceFmt.size = mFormat.sizeimage();
    sourceFmt.format = mFormat.pixelformat();
    sourceFmt.stride = sourceFmt.width;
    std::vector<camera3_stream_t*> streams = mListeners;
    /* put the main stream to first */
    streams.insert(streams.begin(), mStream);
    /*
     * because this outputframeworker handles the requests
     * from listners also, so the number of driver used buffers
     * should contain the listners, otherwise it may cause no
     * available buffer exception when OutputFrameWorker::prepareRun
     * is called
     */
    mPipelineDepth *= mListeners.size() + 1;
    mPostWorkingBufs.resize(mPipelineDepth);
    mPostPipeline->prepare(sourceFmt, streams, mNeedPostProcess);
    mPostPipeline->start();
    mPostProcItemsPool.init(mPipelineDepth);
    for (size_t i = 0; i < mPipelineDepth; i++)
    {
        std::shared_ptr<PostProcBuffer> buffer= nullptr;
        mPostProcItemsPool.acquireItem(buffer);
        if (buffer.get() == nullptr) {
            LOGE("postproc task busy, no idle postproc frame!");
            return UNKNOWN_ERROR;
        }
        buffer->index = i;
    }

    mIndex = 0;
    mOutputBuffers.clear();
    mOutputBuffers.resize(mPipelineDepth);

    ret = setWorkerDeviceBuffers(
        mNeedPostProcess ? V4L2_MEMORY_MMAP : getDefaultMemoryType(mNodeName));
    CheckError((ret != OK), ret, "@%s set worker device buffers failed.",
               __FUNCTION__);

    // Allocate internal buffer.
    if (mNeedPostProcess) {
        ret = allocateWorkerBuffers();
        CheckError((ret != OK), ret, "@%s failed to allocate internal buffer.",
                   __FUNCTION__);
    }

    return OK;
}

status_t OutputFrameWorker::prepareRun(std::shared_ptr<DeviceMessage> msg)
{
    HAL_TRACE_CALL(CAM_GLBL_DBG_HIGH);
    mMsg = msg;
    status_t status = NO_ERROR;
    std::shared_ptr<CameraBuffer> buffer;

    mPollMe = false;

    if (!mStream)
        return NO_ERROR;

    Camera3Request* request = mMsg->cbMetadataMsg.request;
    request->setSequenceId(-1);

    std::shared_ptr<PostProcBuffer> postbuffer= nullptr;
    if (mPostProcItemsPool.acquireItem(postbuffer)) {
        LOGE("%s: %p no avl buffer now!", __FUNCTION__, this);
        status = UNKNOWN_ERROR;
        goto exit;
    }
    mIndex = postbuffer->index;
    buffer = findBuffer(request, mStream);
    mOutputBuffers[mIndex] = nullptr;
    if (buffer.get()) {
        // Work for mStream
        status = prepareBuffer(buffer);
        if (status != NO_ERROR) {
            LOGE("prepare buffer error!");
            goto exit;
        }

        // If output format is something else than
        // NV21 or Android flexible YCbCr 4:2:0, return
        if (buffer->format() != HAL_PIXEL_FORMAT_YCrCb_420_SP &&
                buffer->format() != HAL_PIXEL_FORMAT_YCbCr_420_888 &&
                buffer->format() != HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED &&
            buffer->format() != HAL_PIXEL_FORMAT_BLOB)  {
            LOGE("Bad format %d", buffer->format());
            status = BAD_TYPE;
            goto exit;
        }

        mOutputBuffers[mIndex] = buffer;
        mPollMe = true;
    } else if (checkListenerBuffer(request)) {
        // Work for listeners
        LOGD("%s: stream %p works for listener only in req %d",
             __FUNCTION__, mStream, request->getId());
        mPollMe = true;
    } else {
        LOGD("No work for this worker mStream: %p", mStream);
        mPollMe = false;
        return NO_ERROR;
    }

    /*
     * store the buffer in a map where the key is the terminal UID
     */
    if (!mNeedPostProcess) {
        // Use stream buffer for zero-copy
        unsigned long userptr;
        /*
         * If there exist linsteners, we force to use main stream buffer
         * as driver buffer directly, so when we handle the request that
         * contain only the listener's buffer, we should allocate extra
         * buffers.
         */
        if (buffer.get() == nullptr) {
            buffer = getOutputBufferForListener();
            CheckError((buffer.get() == nullptr), UNKNOWN_ERROR,
                       "failed to allocate listener buffer");
        }
        switch (mNode->getMemoryType()) {
        case V4L2_MEMORY_USERPTR:
            userptr = reinterpret_cast<unsigned long>(buffer->data());
            mBuffers[mIndex].userptr(userptr);
            break;
        case V4L2_MEMORY_DMABUF:
            mBuffers[mIndex].setFd(buffer->dmaBufFd(), 0);
            break;
        case V4L2_MEMORY_MMAP:
            break;
        default:
            LOGE("%s unsupported memory type.", __FUNCTION__);
            status = BAD_VALUE;
            goto exit;
        }
        postbuffer->cambuf = buffer;
    } else {
        postbuffer->cambuf = mCameraBuffers[mIndex];
    }
    LOGI("%s:%d:instance(%p), requestId(%d), index(%d)", __FUNCTION__, __LINE__, this, request->getId(), mIndex);
    status |= mNode->putFrame(mBuffers[mIndex]);
    mPostWorkingBufs[mIndex]= postbuffer;

exit:
    if (status < 0)
        returnBuffers(true);

    return status < 0 ? status : OK;
}

status_t OutputFrameWorker::run()
{
    status_t status = NO_ERROR;
    HAL_TRACE_CALL(CAM_GLBL_DBG_HIGH);

    V4L2BufferInfo outBuf;

    if (!mDevError)
        status = mNode->grabFrame(&outBuf);

    // Update request sequence if needed
    Camera3Request* request = mMsg->cbMetadataMsg.request;
    int sequence = outBuf.vbuffer.sequence();
    if (request->sequenceId() < sequence)
        request->setSequenceId(sequence);

    int index = outBuf.vbuffer.index();
    mPostWorkingBuf = mPostWorkingBufs[index];
    if (mDevError) {
        LOGE("%s:%d device error!", __FUNCTION__, __LINE__);
        /* get the prepared but undequed buffers */
        for (int i = 0; i < mPipelineDepth; i++)
            if (mOutputBuffers[(i + mIndex) % mPipelineDepth]) {
                index = (i + mIndex) % mPipelineDepth;
                break;
            }
    }
    mOutputBuffer = mOutputBuffers[index];
    mOutputBuffers[index] = nullptr;
    mPostWorkingBufs[index] = nullptr;

    ICaptureEventListener::CaptureMessage outMsg;
    outMsg.data.event.reqId = request->getId();
    outMsg.id = ICaptureEventListener::CAPTURE_MESSAGE_ID_EVENT;
    outMsg.data.event.type = ICaptureEventListener::CAPTURE_EVENT_SHUTTER;
    outMsg.data.event.timestamp = outBuf.vbuffer.timestamp();
    outMsg.data.event.sequence = outBuf.vbuffer.sequence();
    notifyListeners(&outMsg);

    LOGI("%s:%d:instance(%p), frame_id(%d), requestId(%d), index(%d)", __FUNCTION__, __LINE__, this, outBuf.vbuffer.sequence(), request->getId(), index);

    if (status < 0)
        returnBuffers(true);

    return (status < 0) ? status : OK;
}

status_t OutputFrameWorker::postRun()
{
    HAL_TRACE_CALL(CAM_GLBL_DBG_HIGH);

    status_t status = OK;
    CameraStream *stream;
    Camera3Request* request = nullptr;
    std::vector<std::shared_ptr<PostProcBuffer>> outBufs;
    std::shared_ptr<PostProcBuffer> postOutBuf;
    std::shared_ptr<PostProcBuffer> tempBuf = std::make_shared<PostProcBuffer> ();
    int stream_type;

    if (mMsg == nullptr) {
        LOGE("Message null - Fix the bug");
        status = UNKNOWN_ERROR;
        goto exit;
    }

    request = mMsg->cbMetadataMsg.request;
    if (request == nullptr) {
        LOGE("No request provided for captureDone");
        status = UNKNOWN_ERROR;
        goto exit;
    }

    // Handle for listeners at first
    for (size_t i = 0; i < mListeners.size(); i++) {
        camera3_stream_t* listener = mListeners[i];
        std::shared_ptr<CameraBuffer> listenerBuf = findBuffer(request, listener);
        if (listenerBuf.get() == nullptr) {
            continue;
        }

        stream = listenerBuf->getOwner();
        if (NO_ERROR != prepareBuffer(listenerBuf)) {
            LOGE("prepare listener buffer error!");
            listenerBuf->getOwner()->captureDone(listenerBuf, request);
            status = UNKNOWN_ERROR;
            continue;
        }
        postOutBuf = std::make_shared<PostProcBuffer> ();
        std::shared_ptr<PostProcBuffer> inPostBuf = std::make_shared<PostProcBuffer> ();
        postOutBuf->cambuf = listenerBuf;
        postOutBuf->request = request;
        outBufs.push_back(postOutBuf);
        postOutBuf = nullptr;
        if (mOutputBuffer == nullptr) {
            inPostBuf->cambuf = mPostWorkingBuf->cambuf;
            inPostBuf->request = mPostWorkingBuf->request;
            mPostPipeline->processFrame(inPostBuf, outBufs, mMsg->pMsg.processingSettings);
        }
    }
    if (status != OK)
        goto exit;

    // All done
    if (mOutputBuffer == nullptr)
        goto exit;

    postOutBuf = std::make_shared<PostProcBuffer> ();
    postOutBuf->cambuf = mOutputBuffer;
    postOutBuf->request = request;
    outBufs.push_back(postOutBuf);
    postOutBuf = nullptr;

    // can't pass mPostWorkingBuf to processFrame becasuse the life of
    // mPostWorkingBuf should not be managered by PostProcPine. if pass
    // mPostWorkingBuf directly to processFrame, acquire postproc buffer in
    // @prepareRun maybe failed dute to the shared_ptr of mPostWorkingBuf can be
    // held by PostProcPipeline
    tempBuf->cambuf = mPostWorkingBuf->cambuf;
    tempBuf->request = mPostWorkingBuf->request;

    mPostPipeline->processFrame(tempBuf, outBufs, mMsg->pMsg.processingSettings);
    stream = mOutputBuffer->getOwner();

    stream_type = stream->getStreamType();
    showDebugFPS(stream_type);

    // Dump the buffers if enabled in flags
    switch (stream_type) {
    case STREAM_PREVIEW:
        mOutputBuffer->dumpImage(CAMERA_DUMP_PREVIEW, "PREVIEW");
        break;
    case STREAM_CAPTURE:
        mOutputBuffer->dumpImage(CAMERA_DUMP_JPEG, ".jpg");
        break;
    case STREAM_VIDEO:
        mOutputBuffer->dumpImage(CAMERA_DUMP_VIDEO, "VIDEO");
        break;
    default :
        LOGW("%s:%d: dump not support for the stream", __func__, __LINE__);
        break;
    }
    // call capturedone for the stream of the buffer
    //stream->captureDone(mOutputBuffer, request);

exit:
    /* Prevent from using old data */
    mMsg = nullptr;
    mOutputBuffer = nullptr;
    mPostWorkingBuf = nullptr;

    if (status != OK)
        returnBuffers(false);

    return status;
}

void OutputFrameWorker::returnBuffers(bool returnListenerBuffers)
{
    if (!mMsg || !mMsg->cbMetadataMsg.request)
        return;

    Camera3Request* request = mMsg->cbMetadataMsg.request;
    std::shared_ptr<CameraBuffer> buffer;

    buffer = findBuffer(request, mStream);
    if (buffer.get() && buffer->isRegistered())
        buffer->getOwner()->captureDone(buffer, request);

    if (!returnListenerBuffers)
        return;

    for (size_t i = 0; i < mListeners.size(); i++) {
        camera3_stream_t* listener = mListeners[i];
        buffer = findBuffer(request, listener);
        if (buffer.get() == nullptr || !buffer->isRegistered())
            continue;

        buffer->getOwner()->captureDone(buffer, request);
    }
}

status_t
OutputFrameWorker::prepareBuffer(std::shared_ptr<CameraBuffer>& buffer)
{
    CheckError((buffer.get() == nullptr), UNKNOWN_ERROR, "null buffer!");

    status_t status = NO_ERROR;
    if (!buffer->isLocked()) {
        status = buffer->lock();
        if (CC_UNLIKELY(status != NO_ERROR)) {
            LOGE("Could not lock the buffer error %d", status);
            return UNKNOWN_ERROR;
        }
    }
    status = buffer->waitOnAcquireFence();
    if (CC_UNLIKELY(status != NO_ERROR)) {
        LOGW("Wait on fence for buffer %p timed out", buffer.get());
    }
    return status;
}

std::shared_ptr<CameraBuffer>
OutputFrameWorker::findBuffer(Camera3Request* request,
                              camera3_stream_t* stream)
{
    CheckError((request == nullptr || stream == nullptr), nullptr,
                "null request/stream!");

    CameraStream *s = nullptr;
    std::shared_ptr<CameraBuffer> buffer = nullptr;
    const std::vector<camera3_stream_buffer>* outBufs =
                                        request->getOutputBuffers();
    // don't deal with reprocess request in outputFrameWorker,
    // and we will process the prequest in inputFrameWorker insteadly.
    if (request->getInputBuffers()->size() > 0)
        return buffer;

    for (camera3_stream_buffer outputBuffer : *outBufs) {
        s = reinterpret_cast<CameraStream *>(outputBuffer.stream->priv);
        if (s->getStream() == stream) {
            buffer = request->findBuffer(s, false);
            if (CC_UNLIKELY(buffer == nullptr)) {
                LOGW("buffer not found for stream");
            }
            break;
        }
    }

    if (buffer.get() == nullptr) {
        LOGI("No buffer for stream %p in req %d", stream, request->getId());
    }
    return buffer;
}

bool OutputFrameWorker::checkListenerBuffer(Camera3Request* request)
{
    bool required = false;
    for (auto* s : mListeners) {
        std::shared_ptr<CameraBuffer> listenerBuf = findBuffer(request, s);
        if (listenerBuf.get()) {
            required = true;
            break;
        }
    }

    LOGI("%s, required is %s", __FUNCTION__, (required ? "true" : "false"));
    return required;
}

std::shared_ptr<CameraBuffer>
OutputFrameWorker::getOutputBufferForListener()
{
    // mOutputForListener buffer infor is same with mOutputBuffer,
    // and only allocated once
    if (mOutputForListener.get() == nullptr) {
        // Allocate buffer for listeners
        if (mNode->getMemoryType() == V4L2_MEMORY_DMABUF) {
            mOutputForListener = MemoryUtils::allocateHandleBuffer(
                    mFormat.width(),
                    mFormat.height(),
                    HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED,
                    GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_HW_CAMERA_WRITE,
                    mCameraId);
        } else if (mNode->getMemoryType() == V4L2_MEMORY_MMAP) {
            mOutputForListener = std::make_shared<CameraBuffer>(
                    mFormat.width(),
                    mFormat.height(),
                    mFormat.bytesperline(),
                    mNode->getFd(), -1, // dmabuf fd is not required.
                    mBuffers[0].length(),
                    mFormat.pixelformat(),
                    mBuffers[0].offset(), PROT_READ | PROT_WRITE, MAP_SHARED);
        } else if (mNode->getMemoryType() == V4L2_MEMORY_USERPTR) {
            mOutputForListener = MemoryUtils::allocateHeapBuffer(
                    mFormat.width(),
                    mFormat.height(),
                    mFormat.bytesperline(),
                    mFormat.pixelformat(),
                    mCameraId,
                    mBuffers[0].length());
        } else {
            LOGE("bad type for stream buffer %d", mNode->getMemoryType());
            return nullptr;
        }
        CheckError((mOutputForListener.get() == nullptr), nullptr,
                   "Can't allocate buffer for listeners!");
    }

    if (!mOutputForListener->isLocked()) {
        mOutputForListener->lock();
    }

    LOGI("%s, get output buffer for Listeners", __FUNCTION__);
    return mOutputForListener;
}

} /* namespace camera2 */
} /* namespace android */
