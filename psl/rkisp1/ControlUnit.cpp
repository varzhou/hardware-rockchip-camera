/*
 * Copyright (C) 2017 Intel Corporation.
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

#define LOG_TAG "ControlUnit"

#include "LogHelper.h"
#include "PerformanceTraces.h"
#include "ControlUnit.h"
#include "RKISP1CameraHw.h"
#include "ImguUnit.h"
#include "CameraStream.h"
#include "RKISP1CameraCapInfo.h"
#include "CameraMetadataHelper.h"
#include "PlatformData.h"
#include "ProcUnitSettings.h"
#include "SettingsProcessor.h"
#include "Metadata.h"
#include "MediaEntity.h"
using ::android::hardware::camera::common::V1_0::helper::CameraMetadata;
static const int SETTINGS_POOL_SIZE = MAX_REQUEST_IN_PROCESS_NUM * 2;

namespace android {
namespace camera2 {

ControlUnit::ControlUnit(ImguUnit *thePU,
                         int cameraId,
                         IStreamConfigProvider &aStreamCfgProv,
                         std::shared_ptr<MediaController> mc) :
        mRequestStatePool("CtrlReqState"),
        mCaptureUnitSettingsPool("CapUSettings"),
        mProcUnitSettingsPool("ProcUSettings"),
        mLatestRequestId(-1),
        mImguUnit(thePU),
        mCtrlLoop(nullptr),
        mCameraId(cameraId),
        mMediaCtl(mc),
        mThreadRunning(false),
        mMessageQueue("CtrlUnitThread", static_cast<int>(MESSAGE_ID_MAX)),
        mMessageThread(nullptr),
        mStreamCfgProv(aStreamCfgProv),
        mSettingsProcessor(nullptr),
        mMetadata(nullptr),
        mSensorSettingsDelay(0),
        mGainDelay(0),
        mLensSupported(false),
        mSofSequence(0),
        mShutterDoneReqId(-1)
{
    cl_result_callback_ops::metadata_result_callback = &sMetadatCb;
}

status_t
ControlUnit::getDevicesPath()
{
    std::shared_ptr<MediaEntity> mediaEntity = nullptr;
    std::string entityName;
    const RKISP1CameraCapInfo *cap = getRKISP1CameraCapInfo(mCameraId);
    std::shared_ptr<V4L2Subdevice> subdev = nullptr;
    status_t status = OK;

    if (cap == nullptr) {
        LOGE("Failed to get cameraCapInfo");
        return UNKNOWN_ERROR;
    }

    // get isp subdevice path
    entityName = cap->getMediaCtlEntityName("isys_backend");
    if (entityName == "none") {
        LOGE("%s: No isys_backend found", __FUNCTION__);
        return UNKNOWN_ERROR;
    } else {
        status = mMediaCtl->getMediaEntity(mediaEntity, entityName.c_str());
        if (mediaEntity == nullptr || status != NO_ERROR) {
            LOGE("Could not retrieve media entity %s", entityName.c_str());
            return UNKNOWN_ERROR;
        }

        mediaEntity->getDevice((std::shared_ptr<V4L2DeviceBase>&) subdev);
        if (subdev.get())
            mDevPathsMap[KDevPathTypeIspDevNode] = subdev->name();
    }
    // get sensor device path
    entityName = cap->getMediaCtlEntityName("pixel_array");
    if (entityName == "none") {
        LOGE("%s: No pixel_array found", __FUNCTION__);
        return UNKNOWN_ERROR;
    } else {
        status = mMediaCtl->getMediaEntity(mediaEntity, entityName.c_str());
        if (mediaEntity == nullptr || status != NO_ERROR) {
            LOGE("Could not retrieve media entity %s", entityName.c_str());
            return UNKNOWN_ERROR;
        }

        mediaEntity->getDevice((std::shared_ptr<V4L2DeviceBase>&) subdev);
        if (subdev.get())
            mDevPathsMap[KDevPathTypeSensorNode] = subdev->name();
    }

    // get isp input params device path
    entityName = "rkisp1-input-params";
    status = mMediaCtl->getMediaEntity(mediaEntity, entityName.c_str());
    if (mediaEntity == nullptr || status != NO_ERROR) {
        LOGE("%s, Could not retrieve Media Entity %s", __FUNCTION__, entityName.c_str());
        return UNKNOWN_ERROR;
    }

    mediaEntity->getDevice((std::shared_ptr<V4L2DeviceBase>&) subdev);
    if (subdev.get())
        mDevPathsMap[KDevPathTypeIspInputParamsNode] = subdev->name();

    // get isp stats device path
    entityName = "rkisp1-statistics";
    status = mMediaCtl->getMediaEntity(mediaEntity, entityName.c_str());
    if (mediaEntity == nullptr || status != NO_ERROR) {
        LOGE("%s, Could not retrieve Media Entity %s", __FUNCTION__, entityName.c_str());
        return UNKNOWN_ERROR;
    }

    mediaEntity->getDevice((std::shared_ptr<V4L2DeviceBase>&) subdev);
    if (subdev.get())
        mDevPathsMap[KDevPathTypeIspStatsNode] = subdev->name();

    // get lens device path
    entityName = cap->getMediaCtlEntityName("lens");
    if (entityName == "none") {
        LOGW("%s: No lens found", __FUNCTION__);
    } else {
        status = mMediaCtl->getMediaEntity(mediaEntity, entityName.c_str());
        if (mediaEntity == nullptr || status != NO_ERROR) {
            LOGE("%s, Could not retrieve Media Entity %s", __FUNCTION__, entityName.c_str());
        } else {
            mediaEntity->getDevice((std::shared_ptr<V4L2DeviceBase>&) subdev);
            if (subdev.get())
                mDevPathsMap[KDevPathTypeLensNode] = subdev->name();
        }
    }

    return OK;
}

/**
 * initStaticMetadata
 *
 * Create CameraMetadata object to retrieve the static tags used in this class
 * we cache them as members so that we do not need to query CameraMetadata class
 * everytime we need them. This is more efficient since find() is not cheap
 */
status_t ControlUnit::initStaticMetadata()
{
    //Initialize the CameraMetadata object with the static metadata tags
    camera_metadata_t* plainStaticMeta;
    plainStaticMeta = (camera_metadata_t*)PlatformData::getStaticMetadata(mCameraId);
    if (plainStaticMeta == nullptr) {
        LOGE("Failed to get camera %d StaticMetadata", mCameraId);
        return UNKNOWN_ERROR;
    }

    CameraMetadata staticMeta(plainStaticMeta);
    camera_metadata_entry entry;
    entry = staticMeta.find(ANDROID_LENS_INFO_MINIMUM_FOCUS_DISTANCE);
    if (entry.count == 1) {
        LOGI("camera %d minimum focus distance:%f", mCameraId, entry.data.f[0]);
        mLensSupported = (entry.data.f[0] > 0) ? true : false;
        LOGI("Lens movement %s for camera id %d",
             mLensSupported ? "supported" : "NOT supported", mCameraId);
    }
    staticMeta.release();

    const RKISP1CameraCapInfo *cap = getRKISP1CameraCapInfo(mCameraId);
    if (cap == nullptr) {
        LOGE("Failed to get cameraCapInfo");
        return UNKNOWN_ERROR;
    }
    mSensorSettingsDelay = MAX(cap->mExposureLag, cap->mGainLag);
    mGainDelay = cap->mGainLag;

    return NO_ERROR;
}

status_t
ControlUnit::init()
{
    HAL_TRACE_CALL(CAM_GLBL_DBG_HIGH);
    status_t status = OK;
    const char *sensorName = nullptr;

    //Cache the static metadata values we are going to need in the capture unit
    if (initStaticMetadata() != NO_ERROR) {
        LOGE("Cannot initialize static metadata");
        return NO_INIT;
    }

    mMessageThread = std::unique_ptr<MessageThread>(new MessageThread(this, "CtrlUThread"));
    mMessageThread->run();

    const RKISP1CameraCapInfo *cap = getRKISP1CameraCapInfo(mCameraId);
    if (!cap) {
        LOGE("Not enough information for getting NVM data");
    } else {
        sensorName = cap->getSensorName();
    }

    if (!cap || cap->sensorType() == SENSOR_TYPE_RAW) {
        mCtrlLoop = new RkCtrlLoop(mCameraId);
        if (mCtrlLoop->init(sensorName, this) != NO_ERROR) {
            LOGE("Error initializing 3A control");
            return UNKNOWN_ERROR;
        }
    } else {
        LOGW("SoC camera 3A control missing");
        //return UNKNOWN_ERROR;
    }

    mSettingsProcessor = new SettingsProcessor(mCameraId, mStreamCfgProv);
    mSettingsProcessor->init();

    mMetadata = new Metadata(mCameraId);
    status = mMetadata->init();
    if (CC_UNLIKELY(status != OK)) {
        LOGE("Error Initializing metadata");
        return UNKNOWN_ERROR;
    }

    /*
     * init the pools of Request State structs and CaptureUnit settings
     * and Processing Unit Settings
     */
    mRequestStatePool.init(MAX_REQUEST_IN_PROCESS_NUM, RequestCtrlState::reset);
    mCaptureUnitSettingsPool.init(SETTINGS_POOL_SIZE + 2);
    mProcUnitSettingsPool.init(SETTINGS_POOL_SIZE, ProcUnitSettings::reset);

    mSettingsHistory.clear();

    /* Set digi gain support */
    bool supportDigiGain = false;
    if (cap)
        supportDigiGain = cap->digiGainOnSensor();

    return status;
}

/**
 * reset
 *
 * This method is called by the SharedPoolItem when the item is recycled
 * At this stage we can cleanup before recycling the struct.
 * In this case we reset the TracingSP of the capture unit settings and buffers
 * to remove this reference. Other references may be still alive.
 */
void RequestCtrlState::reset(RequestCtrlState* me)
{
    if (CC_LIKELY(me != nullptr)) {
        me->captureSettings.reset();
        me->processingSettings.reset();
        me->graphConfig.reset();
    } else {
        LOGE("Trying to reset a null CtrlState structure !! - BUG ");
    }
}

void RequestCtrlState::init(Camera3Request *req,
                                         std::shared_ptr<GraphConfig> aGraphConfig)
{
    request = req;
    graphConfig = aGraphConfig;
    if (CC_LIKELY(captureSettings)) {
        captureSettings->aeRegion.init(0);
        captureSettings->makernote.data = nullptr;
        captureSettings->makernote.size = 0;
    } else {
        LOGE(" Failed to init Ctrl State struct: no capture settings!! - BUG");
        return;
    }
    if (CC_LIKELY(processingSettings.get() != nullptr)) {
        processingSettings->captureSettings = captureSettings;
        processingSettings->graphConfig = aGraphConfig;
        processingSettings->request = req;
    } else {
        LOGE(" Failed to init Ctrl State: no processing settings!! - BUG");
        return;
    }
    ctrlUnitResult = request->getPartialResultBuffer(CONTROL_UNIT_PARTIAL_RESULT);
    shutterDone = false;
    intent = ANDROID_CONTROL_CAPTURE_INTENT_PREVIEW;
    if (CC_UNLIKELY(ctrlUnitResult == nullptr)) {
        LOGE("no partial result buffer - BUG");
        return;
    }

    mClMetaReceived = false;
    mImgProcessDone = false;

    /**
     * Apparently we need to have this tags in the results
     */
    const CameraMetadata* settings = request->getSettings();

    if (CC_UNLIKELY(settings == nullptr)) {
        LOGE("no settings in request - BUG");
        return;
    }

    int64_t id = request->getId();
    camera_metadata_ro_entry entry;
    entry = settings->find(ANDROID_REQUEST_ID);
    if (entry.count == 1) {
        ctrlUnitResult->update(ANDROID_REQUEST_ID, (int *)&id,
                entry.count);
    }
    ctrlUnitResult->update(ANDROID_SYNC_FRAME_NUMBER,
                          &id, 1);

    entry = settings->find(ANDROID_CONTROL_CAPTURE_INTENT);
    if (entry.count == 1) {
        intent = entry.data.u8[0];
    }
    LOGI("%s:%d: request id(%lld), capture_intent(%d)", __FUNCTION__, __LINE__, id, intent);
    ctrlUnitResult->update(ANDROID_CONTROL_CAPTURE_INTENT, entry.data.u8,
                                                           entry.count);
}

ControlUnit::~ControlUnit()
{
    HAL_TRACE_CALL(CAM_GLBL_DBG_HIGH);

    mSettingsHistory.clear();

    requestExitAndWait();

    if (mMessageThread != nullptr) {
        mMessageThread.reset();
        mMessageThread = nullptr;
    }

    if (mSettingsProcessor) {
        delete mSettingsProcessor;
        mSettingsProcessor = nullptr;
    }

    if (mCtrlLoop) {
        mCtrlLoop->deinit();
        delete mCtrlLoop;
        mCtrlLoop = nullptr;
    }

    delete mMetadata;
    mMetadata = nullptr;

}

status_t
ControlUnit::configStreamsDone(bool configChanged)
{
    LOGI("@%s: config changed: %d", __FUNCTION__, configChanged);
    status_t status = OK;
    if (configChanged) {
        mLatestRequestId = -1;
        mWaitingForCapture.clear();
        mSettingsHistory.clear();

        struct rkisp_cl_prepare_params_s prepareParams;

        memset(&prepareParams, 0, sizeof(struct rkisp_cl_prepare_params_s));
        prepareParams.staticMeta = PlatformData::getStaticMetadata(mCameraId);
        if (prepareParams.staticMeta == nullptr) {
            LOGE("Failed to get camera %d StaticMetadata for CL", mCameraId);
            return UNKNOWN_ERROR;
        }

        //start 3a when config video stream done
        getDevicesPath();

        for (auto &it : mDevPathsMap) {
            switch (it.first) {
            case KDevPathTypeIspDevNode:
                prepareParams.isp_sd_node_path = it.second.c_str();
                break;
            case KDevPathTypeIspStatsNode:
                prepareParams.isp_vd_stats_path = it.second.c_str();
                break;
            case KDevPathTypeIspInputParamsNode:
                prepareParams.isp_vd_params_path = it.second.c_str();
                break;
            case KDevPathTypeSensorNode:
                prepareParams.sensor_sd_node_path = it.second.c_str();
                break;
            case KDevPathTypeLensNode:
                prepareParams.lens_sd_node_path = it.second.c_str();
                break;
            default:
                continue;
            }
        }

        if (mCtrlLoop) {
            status = mCtrlLoop->start(prepareParams);
            if (CC_UNLIKELY(status != OK)) {
                LOGE("Failed to start 3a control loop!");
                return status;
            }
        }
    }

    return NO_ERROR;
}

status_t
ControlUnit::requestExitAndWait()
{
    Message msg;
    msg.id = MESSAGE_ID_EXIT;
    status_t status = mMessageQueue.send(&msg, MESSAGE_ID_EXIT);
    status |= mMessageThread->requestExitAndWait();
    return status;
}

status_t
ControlUnit::handleMessageExit()
{
    mThreadRunning = false;
    return NO_ERROR;
}

/**
 * acquireRequestStateStruct
 *
 * acquire a free request control state structure.
 * Since this structure contains also a capture settings item that are also
 * stored in a pool we need to acquire one of those as well.
 *
 */
status_t
ControlUnit::acquireRequestStateStruct(std::shared_ptr<RequestCtrlState>& state)
{
    status_t status = NO_ERROR;
    status = mRequestStatePool.acquireItem(state);
    if (status != NO_ERROR) {
        LOGE("Failed to acquire free request state struct - BUG");
        /*
         * This should not happen since AAL is holding clients to send more
         * requests than we can take
         */
        return UNKNOWN_ERROR;
    }

    status = mCaptureUnitSettingsPool.acquireItem(state->captureSettings);
    if (status != NO_ERROR) {
        LOGE("Failed to acquire free CapU settings  struct - BUG");
        /*
         * This should not happen since AAL is holding clients to send more
         * requests than we can take
         */
        return UNKNOWN_ERROR;
    }

    // set a unique ID for the settings
    state->captureSettings->settingsIdentifier = systemTime();

    status = mProcUnitSettingsPool.acquireItem(state->processingSettings);
    if (status != NO_ERROR) {
        LOGE("Failed to acquire free ProcU settings  struct - BUG");
        /*
         * This should not happen since AAL is holding clients to send more
         * requests than we can take
         */
        return UNKNOWN_ERROR;
    }
    return OK;
}

/**
 * processRequest
 *
 * Acquire the control structure to keep the state of the request in the control
 * unit and send the message to be handled in the internal message thread.
 */
status_t
ControlUnit::processRequest(Camera3Request* request,
                            std::shared_ptr<GraphConfig> graphConfig)
{
    Message msg;
    status_t status = NO_ERROR;
    std::shared_ptr<RequestCtrlState> state;

    status = acquireRequestStateStruct(state);
    if (CC_UNLIKELY(status != OK) || CC_UNLIKELY(state.get() == nullptr)) {
        return status;  // error log already done in the helper method
    }

    state->init(request, graphConfig);

    msg.id = MESSAGE_ID_NEW_REQUEST;
    msg.state = state;
    status = mMessageQueue.send(&msg);
    return status;
}

status_t
ControlUnit::handleNewRequest(Message &msg)
{
    HAL_TRACE_CALL(CAM_GLBL_DBG_HIGH);

    status_t status = NO_ERROR;
    std::shared_ptr<RequestCtrlState> reqState = msg.state;

    /**
     * PHASE 1: Process the settings
     * In this phase we analyze the request's metadata settings and convert them
     * into either:
     *  - input parameters for 3A algorithms
     *  - parameters used for SoC sensors
     *  - Capture Unit settings
     *  - Processing Unit settings
     */
    const CameraMetadata *reqSettings = reqState->request->getSettings();

    if (reqSettings == nullptr) {
        LOGE("no settings in request - BUG");
        return UNKNOWN_ERROR;
    }

    status = mSettingsProcessor->processRequestSettings(*reqSettings, *reqState);
    if (status != NO_ERROR) {
        LOGE("Could not process all settings, reporting request as invalid");
    }

    status = processRequestForCapture(reqState);
    if (CC_UNLIKELY(status != OK)) {
        LOGE("Failed to process req %d for capture [%d]",
        reqState->request->getId(), status);
        // TODO: handle error !
    }

    return status;
}

/**
 * processRequestForCapture
 *
 * Run 3A algorithms and send the results to capture unit for capture
 *
 * This is the second phase in the request processing flow.
 *
 * The request settings have been processed in the first phase
 *
 * If this step is successful the request will be moved to the
 * mWaitingForCapture waiting for the pixel buffers.
 */
status_t
ControlUnit::processRequestForCapture(std::shared_ptr<RequestCtrlState> &reqState)
{
    status_t status = NO_ERROR;
    if (CC_UNLIKELY(reqState.get() == nullptr)) {
        LOGE("Invalid parameters passed- request not captured - BUG");
        return BAD_VALUE;
    }
    reqState->request->dumpSetting();

    if (CC_UNLIKELY(reqState->captureSettings.get() == nullptr)) {
        LOGE("capture Settings not given - BUG");
        return BAD_VALUE;
    }

    /* Write the dump flag into capture settings, so that the PAL dump can be
     * done all the way down at PgParamAdaptor. For the time being, only dump
     * during jpeg captures.
     */
    reqState->processingSettings->dump =
            LogHelper::isDumpTypeEnable(CAMERA_DUMP_RAW) &&
            reqState->request->getBufferCountOfFormat(HAL_PIXEL_FORMAT_BLOB) > 0;
    // dump the PAL run from ISA also
    reqState->captureSettings->dump = reqState->processingSettings->dump;

    int reqId = reqState->request->getId();

    /**
     * Move the request to the vector mWaitingForCapture
     */
    mWaitingForCapture.insert(std::make_pair(reqId, reqState));

    mLatestRequestId = reqId;

    int jpegBufCount = reqState->request->getBufferCountOfFormat(HAL_PIXEL_FORMAT_BLOB);
    int implDefinedBufCount = reqState->request->getBufferCountOfFormat(HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED);
    int yuv888BufCount = reqState->request->getBufferCountOfFormat(HAL_PIXEL_FORMAT_YCbCr_420_888);
    LOGD("@%s jpegs:%d impl defined:%d yuv888:%d inputbufs:%d req id %d",
         __FUNCTION__,
         jpegBufCount,
         implDefinedBufCount,
         yuv888BufCount,
         reqState->request->getNumberInputBufs(),
         reqState->request->getId());
    if (jpegBufCount > 0) {
        // NOTE: Makernote should be get after isp_bxt_run()
        // NOTE: makernote.data deleted in JpegEncodeTask::handleMakernote()
        /* TODO */
        /* const unsigned mknSize = MAKERNOTE_SECTION1_SIZE + MAKERNOTE_SECTION2_SIZE; */
        /* MakernoteData mkn = {nullptr, mknSize}; */
        /* mkn.data = new char[mknSize]; */
        /* m3aWrapper->getMakerNote(ia_mkn_trg_section_2, mkn); */

        /* reqState->captureSettings->makernote = mkn; */

    } else {
        // No JPEG buffers in request. Reset MKN info, just in case.
        reqState->captureSettings->makernote.data = nullptr;
        reqState->captureSettings->makernote.size = 0;
    }

    if (mCtrlLoop) {
        const CameraMetadata *settings = reqState->request->getSettings();
        rkisp_cl_frame_metadata_s frame_metas;
        frame_metas.metas = settings->getAndLock();
        frame_metas.id = reqId;
        status = mCtrlLoop->setFrameParams(&frame_metas);
        if (status != OK)
            LOGE("CtrlLoop setFrameParams error");

        status = settings->unlock(frame_metas.metas);
        if (status != OK) {
            LOGE("unlock frame frame_metas failed");
            return UNKNOWN_ERROR;
        }
    }
    /*TODO, needn't this anymore ? zyc*/
    status = completeProcessing(reqState);
    if (status != OK)
        LOGE("Cannot complete the buffer processing - fix the bug!");

    return status;
}

status_t ControlUnit::fillMetadata(std::shared_ptr<RequestCtrlState> &reqState)
{
    /**
     * Apparently we need to have this tags in the results
     */
    const CameraMetadata* settings = reqState->request->getSettings();
    CameraMetadata* ctrlUnitResult = reqState->ctrlUnitResult;

    if (CC_UNLIKELY(settings == nullptr)) {
        LOGE("no settings in request - BUG");
        return UNKNOWN_ERROR;
    }

    camera_metadata_ro_entry entry;
    entry = settings->find(ANDROID_CONTROL_MODE);
    if (entry.count == 1) {
        ctrlUnitResult->update(ANDROID_CONTROL_MODE, entry.data.u8, entry.count);
    }
    //# ANDROID_METADATA_Dynamic android.control.videoStabilizationMode copied
    entry = settings->find(ANDROID_CONTROL_VIDEO_STABILIZATION_MODE);
    if (entry.count == 1) {
        ctrlUnitResult->update(ANDROID_CONTROL_VIDEO_STABILIZATION_MODE, entry.data.u8, entry.count);
    }
    //# ANDROID_METADATA_Dynamic android.lens.opticalStabilizationMode copied
    entry = settings->find(ANDROID_LENS_OPTICAL_STABILIZATION_MODE);
    if (entry.count == 1) {
        ctrlUnitResult->update(ANDROID_LENS_OPTICAL_STABILIZATION_MODE, entry.data.u8, entry.count);
    }
    //# ANDROID_METADATA_Dynamic android.control.effectMode done
    entry = settings->find(ANDROID_CONTROL_EFFECT_MODE);
    if (entry.count == 1) {
        ctrlUnitResult->update(ANDROID_CONTROL_EFFECT_MODE, entry.data.u8, entry.count);
    }
    //# ANDROID_METADATA_Dynamic android.noiseReduction.mode done
    entry = settings->find(ANDROID_NOISE_REDUCTION_MODE);
    if (entry.count == 1) {
        ctrlUnitResult->update(ANDROID_NOISE_REDUCTION_MODE, entry.data.u8, entry.count);
    }
    //# ANDROID_METADATA_Dynamic android.edge.mode done
    entry = settings->find(ANDROID_EDGE_MODE);
    if (entry.count == 1) {
        ctrlUnitResult->update(ANDROID_EDGE_MODE, entry.data.u8, entry.count);
    }

    /**
     * We don't have AF, so just update metadata now
     */
    // return 0.0f for the fixed-focus
    if (!mLensSupported) {
        float focusDistance = 0.0f;
        reqState->ctrlUnitResult->update(ANDROID_LENS_FOCUS_DISTANCE,
                                         &focusDistance, 1);
        // framework says it can't be off mode for zsl,
        // so we report EDOF for fixed focus
        // TODO: need to judge if the request is ZSL ?
        uint8_t afMode = ANDROID_CONTROL_AF_MODE_EDOF;
        ctrlUnitResult->update(ANDROID_CONTROL_AF_MODE, &afMode, 1);
        uint8_t afTrigger = ANDROID_CONTROL_AF_TRIGGER_IDLE;
        ctrlUnitResult->update(ANDROID_CONTROL_AF_TRIGGER, &afTrigger, 1);

        uint8_t afState = ANDROID_CONTROL_AF_STATE_INACTIVE;
        ctrlUnitResult->update(ANDROID_CONTROL_AF_STATE, &afState, 1);
    }

    bool flash_available = false;
    uint8_t flash_mode = ANDROID_FLASH_MODE_OFF;
    mSettingsProcessor->getStaticMetadataCache().getFlashInfoAvailable(flash_available);
    if (!flash_available)
        reqState->ctrlUnitResult->update(ANDROID_FLASH_MODE,
                                         &flash_mode, 1);

    mMetadata->writeJpegMetadata(*reqState);
    uint8_t pipelineDepth;
    mSettingsProcessor->getStaticMetadataCache().getPipelineDepth(pipelineDepth);
    //# ANDROID_METADATA_Dynamic android.request.pipelineDepth done
    reqState->ctrlUnitResult->update(ANDROID_REQUEST_PIPELINE_DEPTH,
                                     &pipelineDepth, 1);
    // for soc camera
    if (!mCtrlLoop) {
        uint8_t awbMode = ANDROID_CONTROL_AWB_MODE_AUTO;
        ctrlUnitResult->update(ANDROID_CONTROL_AWB_MODE, &awbMode, 1);
        uint8_t awbState = ANDROID_CONTROL_AWB_STATE_CONVERGED;
        ctrlUnitResult->update(ANDROID_CONTROL_AWB_STATE, &awbState, 1);
        uint8_t aeMode = ANDROID_CONTROL_AE_MODE_ON;
        ctrlUnitResult->update(ANDROID_CONTROL_AE_MODE, &aeMode, 1);
        uint8_t aeState = ANDROID_CONTROL_AE_STATE_CONVERGED;
        ctrlUnitResult->update(ANDROID_CONTROL_AE_STATE, &aeState, 1);
        reqState->mClMetaReceived = true;
    }
    return OK;
}

status_t
ControlUnit::handleNewRequestDone(Message &msg)
{
    status_t status = OK;
    std::shared_ptr<RequestCtrlState> reqState = nullptr;
    int reqId = msg.requestId;

    std::map<int, std::shared_ptr<RequestCtrlState>>::iterator it =
                                    mWaitingForCapture.find(reqId);
    if (it == mWaitingForCapture.end()) {
        LOGE("Unexpected request done event received for request %d - Fix the bug", reqId);
        return UNKNOWN_ERROR;
    }

    reqState = it->second;
    if (CC_UNLIKELY(reqState.get() == nullptr || reqState->request == nullptr)) {
        LOGE("No valid state or settings for request Id = %d"
             "- Fix the bug!", reqId);
        return UNKNOWN_ERROR;
    }

    reqState->mImgProcessDone = true;
    Camera3Request* request = reqState->request;
    //if (!reqState->mClMetaReceived)
        //return OK;

    request->mCallback->metadataDone(request, request->getError() ? -1 : CONTROL_UNIT_PARTIAL_RESULT);
    /*
     * Remove the request from Q once we have received all pixel data buffers
     * we expect from ISA. Query the graph config for that.
     */
    mWaitingForCapture.erase(reqId);
    return status;
}

/**
 * completeProcessing
 *
 * Forward the pixel buffer to the Processing Unit to complete the processing.
 * If all the buffers from Capture Unit have arrived then:
 * - it updates the metadata
 * - it removes the request from the vector mWaitingForCapture.
 *
 * The metadata update is now transferred to the ProcessingUnit.
 * This is done only on arrival of the last pixel data buffer. ControlUnit still
 * keeps the state, so it is responsible for triggering the update.
 */
status_t
ControlUnit::completeProcessing(std::shared_ptr<RequestCtrlState> &reqState)
{
    HAL_TRACE_CALL(CAM_GLBL_DBG_HIGH);
    int reqId = reqState->request->getId();

    if (CC_LIKELY((reqState->request != nullptr) &&
                  (reqState->captureSettings.get() != nullptr))) {

        /* TODO: cleanup
         * This struct copy from state is only needed for JPEG creation.
         * Ideally we should directly write inside members of processingSettings
         * whatever settings are needed for Processing Unit.
         * This should be moved to any of the processXXXSettings.
         */

        fillMetadata(reqState);
        mImguUnit->completeRequest(reqState->processingSettings, true);
    } else {
        LOGE("request or captureSetting is nullptr - Fix the bug!");
        return UNKNOWN_ERROR;
    }

    return NO_ERROR;
}

status_t
ControlUnit::handleNewShutter(Message &msg)
{
    HAL_TRACE_CALL(CAM_GLBL_DBG_HIGH);
    std::shared_ptr<RequestCtrlState> reqState = nullptr;
    int reqId = msg.data.shutter.requestId;

    //check whether this reqId has been shutter done
    if (reqId <= mShutterDoneReqId)
        return OK;

    std::map<int, std::shared_ptr<RequestCtrlState>>::iterator it =
                                    mWaitingForCapture.find(reqId);
    if (it == mWaitingForCapture.end()) {
        LOGE("Unexpected shutter event received for request %d - Fix the bug", reqId);
        return UNKNOWN_ERROR;
    }

    reqState = it->second;
    if (CC_UNLIKELY(reqState.get() == nullptr || reqState->captureSettings.get() == nullptr)) {
        LOGE("No valid state or settings for request Id = %d"
             "- Fix the bug!", reqId);
        return UNKNOWN_ERROR;
    }

    /* flash state - hack, should know from frame whether it fired */
    const CameraMetadata* metaData = reqState->request->getSettings();
    if (metaData == nullptr) {
        LOGE("Metadata should not be nullptr. Fix the bug!");
        return UNKNOWN_ERROR;
    }

    uint8_t flashState = ANDROID_FLASH_STATE_UNAVAILABLE;

    //# ANDROID_METADATA_Dynamic android.flash.state done
    reqState->ctrlUnitResult->update(ANDROID_FLASH_STATE, &flashState, 1);
    int64_t ts = msg.data.shutter.tv_sec * 1000000000; // seconds to nanoseconds
    ts += msg.data.shutter.tv_usec * 1000; // microseconds to nanoseconds

    //# ANDROID_METADATA_Dynamic android.sensor.timestamp done
    reqState->ctrlUnitResult->update(ANDROID_SENSOR_TIMESTAMP, &ts, 1);
    reqState->request->mCallback->shutterDone(reqState->request, ts);
    reqState->shutterDone = true;
    reqState->captureSettings->timestamp = ts;
    mShutterDoneReqId = reqId;

    return NO_ERROR;
}

status_t
ControlUnit::flush(void)
{
    HAL_TRACE_CALL(CAM_GLBL_DBG_HIGH);
    Message msg;
    msg.id = MESSAGE_ID_FLUSH;
    mMessageQueue.remove(MESSAGE_ID_NEW_REQUEST);
    mMessageQueue.remove(MESSAGE_ID_NEW_SHUTTER);
    mMessageQueue.remove(MESSAGE_ID_NEW_REQUEST_DONE);
    return mMessageQueue.send(&msg, MESSAGE_ID_FLUSH);
}

status_t
ControlUnit::handleMessageFlush(void)
{
    HAL_TRACE_CALL(CAM_GLBL_DBG_HIGH);
    status_t status = NO_ERROR;
    if (mCtrlLoop)
        status = mCtrlLoop->stop();
    if (CC_UNLIKELY(status != OK)) {
        LOGE("Failed to stop 3a control loop!");
    }
    mImguUnit->flush();

    mWaitingForCapture.clear();
    mSettingsHistory.clear();

    return NO_ERROR;
}

void
ControlUnit::messageThreadLoop()
{
    LOGD("@%s - Start", __FUNCTION__);

    mThreadRunning = true;
    while (mThreadRunning) {
        status_t status = NO_ERROR;

        Message msg;
        mMessageQueue.receive(&msg);

        PERFORMANCE_HAL_ATRACE_PARAM1("msg", msg.id);
        LOGD("@%s, receive message id:%d", __FUNCTION__, msg.id);
        switch (msg.id) {
        case MESSAGE_ID_EXIT:
            status = handleMessageExit();
            break;
        case MESSAGE_ID_NEW_REQUEST:
            status = handleNewRequest(msg);
            break;
        case MESSAGE_ID_NEW_SHUTTER:
            status = handleNewShutter(msg);
            break;
        case MESSAGE_ID_NEW_REQUEST_DONE:
            status = handleNewRequestDone(msg);
            break;
        case MESSAGE_ID_METADATA_RECEIVED:
            status = handleMetadataReceived(msg);
            break;
        case MESSAGE_ID_FLUSH:
            status = handleMessageFlush();
            break;
        default:
            LOGE("ERROR Unknown message %d", msg.id);
            status = BAD_VALUE;
            break;
        }
        if (status != NO_ERROR)
            LOGE("error %d in handling message: %d", status,
                    static_cast<int>(msg.id));
        LOGD("@%s, finish message id:%d", __FUNCTION__, msg.id);
        mMessageQueue.reply(msg.id, status);
    }

    LOGD("%s: Exit", __FUNCTION__);
}

bool
ControlUnit::notifyCaptureEvent(CaptureMessage *captureMsg)
{
    HAL_TRACE_CALL(CAM_GLBL_DBG_HIGH);

    if (captureMsg == nullptr) {
        return false;
    }

    if (captureMsg->id == CAPTURE_MESSAGE_ID_ERROR) {
        // handle capture error
        return true;
    }

    Message msg;
    switch (captureMsg->data.event.type) {
        case CAPTURE_EVENT_SHUTTER:
            msg.id = MESSAGE_ID_NEW_SHUTTER;
            msg.data.shutter.requestId = captureMsg->data.event.reqId;
            msg.data.shutter.tv_sec = captureMsg->data.event.timestamp.tv_sec;
            msg.data.shutter.tv_usec = captureMsg->data.event.timestamp.tv_usec;
            mMessageQueue.send(&msg, MESSAGE_ID_NEW_SHUTTER);
            break;
        case CAPTURE_EVENT_NEW_SOF:
            mSofSequence = captureMsg->data.event.sequence;
            LOGD("sof event sequence = %u", mSofSequence);
            break;
        case CAPTURE_REQUEST_DONE:
            msg.id = MESSAGE_ID_NEW_REQUEST_DONE;
            msg.requestId = captureMsg->data.event.reqId;
            mMessageQueue.send(&msg, MESSAGE_ID_NEW_REQUEST_DONE);
            break;
        default:
            LOGW("Unsupported Capture event ");
            break;
    }

    return true;
}

status_t
ControlUnit::metadataReceived(int id, const camera_metadata_t *metas) {
    Message msg;
    status_t status = NO_ERROR;

    msg.id = MESSAGE_ID_METADATA_RECEIVED;
    msg.requestId = id;
    msg.metas = metas;
    status = mMessageQueue.send(&msg);
    return status;
}

status_t
ControlUnit::handleMetadataReceived(Message &msg) {
    status_t status = OK;
    std::shared_ptr<RequestCtrlState> reqState = nullptr;
    int reqId = msg.requestId;

    std::map<int, std::shared_ptr<RequestCtrlState>>::iterator it =
                                    mWaitingForCapture.find(reqId);
    if (it == mWaitingForCapture.end()) {
        LOGE("Unexpected request done event received for request %d - Fix the bug", reqId);
        return UNKNOWN_ERROR;
    }

    reqState = it->second;
    if (CC_UNLIKELY(reqState.get() == nullptr || reqState->request == nullptr)) {
        LOGE("No valid state or request for request Id = %d"
             "- Fix the bug!", reqId);
        return UNKNOWN_ERROR;
    }

    reqState->ctrlUnitResult->append(msg.metas);
    reqState->mClMetaReceived = true;

    if (!reqState->mImgProcessDone)
        return OK;

    Camera3Request* request = reqState->request;
    request->mCallback->metadataDone(request, request->getError() ? -1 : CONTROL_UNIT_PARTIAL_RESULT);
    mWaitingForCapture.erase(reqId);

    return status;
}

/**
 * Static callback forwarding methods from CL to instance
 */
void ControlUnit::sMetadatCb(const struct cl_result_callback_ops* ops,
                             struct rkisp_cl_frame_metadata_s *result) {
    LOGI("@%s %d: frame %d result meta received", __FUNCTION__, __LINE__, result->id);
    ControlUnit *ctl = const_cast<ControlUnit*>(static_cast<const ControlUnit*>(ops));

    ctl->metadataReceived(result->id, result->metas);
}

} // namespace camera2
} // namespace android

