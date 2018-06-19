
/*
 * Copyright (C) 2014-2017 Intel Corporation
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

#define LOG_TAG "RkIspcl"

#include <rkisp_control_loop.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include "LogHelper.h"
#include <camera/CameraMetadata.h>

enum cl_state_e {
	CL_STATE_INITED,
	CL_STATE_PREPARED,
	CL_STATE_STARTED,
};

struct cl_context_s {
	//TODO : add mutext to protect it
	enum cl_state_e state;
};

int rkisp_cl_init(void** cl_ctx, const char* tuning_file_path)
{
    LOGI("@%s %d: enter", __FUNCTION__, __LINE__);
	struct cl_context_s* ctx = NULL;

	if (!cl_ctx || !tuning_file_path) {
		printf("%s: error\n", __func__);
		goto err_ret;
	}
	ctx = (struct cl_context_s*)malloc(sizeof(struct cl_context_s));
	if (!ctx) {
		printf("%s: alloc cl context error!\n", __func__);
		goto err_ret;
	}

	ctx->state = CL_STATE_INITED;
	*cl_ctx = (void*)ctx;

	//TODO: parse tuning file
	printf("%s: tuning file path %s\n", __func__, tuning_file_path);

	return 0;

err_ret:
	return -1;
}

int rkisp_cl_prepare(void* cl_ctx,
                     const struct rkisp_cl_prepare_params_s* prepare_params)
{
	const char* path = NULL;
	int fd = -1;
	struct cl_context_s* ctx = (struct cl_context_s*)cl_ctx;
    LOGI("@%s %d: enter", __FUNCTION__, __LINE__);

	if (!ctx || ctx->state != CL_STATE_INITED || !prepare_params) {
		printf("%s: error\n", __func__);
		goto err_ret;
	}

	// isp sd node
	path = prepare_params->isp_sd_node_path;
	if (path) {
		printf("%s:isp sd node path %s\n", __func__, path);
		//try to open and close
		fd = open(path, O_RDWR);
		if (fd < 0)
			printf("%s: failed open %s error %s\n", __func__, path, strerror(errno));
		//else
		//	close(fd);
	}

	// isp params video node
	path = prepare_params->isp_vd_params_path;
	if (path) {
		printf("%s:isp params node path %s\n", __func__, path);
		//try to open and close
		fd = open(path, O_RDWR);
		if (fd < 0)
			printf("%s: failed open %s\n", __func__, path);
		else
			close(fd);
	}
	
	// isp stats video node
	path = prepare_params->isp_vd_stats_path;
	if (path) {
		printf("%s:isp stats node path %s\n", __func__, path);
		//try to open and close
		fd = open(path, O_RDWR);
		if (fd < 0)
			printf("%s: failed open %s\n", __func__, path);
		else
			close(fd);
	}
	
	// isp stats video node
	path = prepare_params->sensor_sd_node_path;
	if (path) {
		printf("%s:isp sensor node path %s\n", __func__, path);
		//try to open and close
		fd = open(path, O_RDWR);
		if (fd < 0)
			printf("%s: failed open %s\n", __func__, path);
		else
			close(fd);
	}

	ctx->state = CL_STATE_PREPARED;
	return 0;

err_ret:
	return -1;
}

int rkisp_cl_start(void* cl_ctx)
{
    LOGI("@%s %d: enter", __FUNCTION__, __LINE__);
	struct cl_context_s* ctx = (struct cl_context_s*)cl_ctx;

	if (!ctx || ctx->state != CL_STATE_PREPARED) {
		printf("%s: error\n", __func__);
		goto err_ret;
	}

	ctx->state = CL_STATE_STARTED;

	//TODO: start the control loop

	return 0;

err_ret:
	return -1;
}

int rkisp_cl_set_frame_params(const void* cl_ctx, const int request_frame_id,
                              const struct rkisp_cl_frame_metadata_s* frame_params)
{
    LOGI("@%s %d: enter", __FUNCTION__, __LINE__);
	struct cl_context_s* ctx = (struct cl_context_s*)cl_ctx;

    android::CameraMetadata metas;
    static int count = 0;
    if (count < 5)
    {
        metas = frame_params->metas;
        std::string fileName("/data/dump/");
        fileName += "settings_" + std::to_string(request_frame_id);
        LOGI("@%s %d: filename:%s", __FUNCTION__, __LINE__, fileName.data());
        int fd = open(fileName.data(), O_RDWR | O_CREAT, 0666);
        if (fd != -1) {
            metas.dump(fd, 2);
        } else {
            LOGE("@%s %d:open failed, errmsg: %s ", __FUNCTION__, __LINE__, strerror(errno));
        }
        close(fd);
    }
    count++;



	if (!ctx) {
		printf("%s: error\n", __func__);
		goto err_ret;
	}

	if (request_frame_id <= 0 && frame_params) {
		printf("%s: set the initial params\n", __func__);
	} else if (!frame_params) {
		printf("%s: use the last frame's params for frame request id %d\n",
				__func__, request_frame_id);
	} else {
		printf("%s: set new frame params for reuqest id %d\n",
				__func__, request_frame_id);	
	}
	
	return 0;

err_ret:
	return -1;
}

int rkisp_cl_get_frame_metas(const void* cl_ctx, const int result_frame_id,
                             struct rkisp_cl_frame_metadata_s* result_metas)
{
    LOGI("@%s %d: enter", __FUNCTION__, __LINE__);
	struct cl_context_s* ctx = (struct cl_context_s*)cl_ctx;

    android::CameraMetadata metas;
    static int count = 0;
    if (count < 5)
    {
        metas = result_metas->metas;
        std::string fileName("/data/dump/");
        fileName += "results_" + std::to_string(result_frame_id);
        LOGI("@%s %d: filename:%s", __FUNCTION__, __LINE__, fileName.data());
        int fd = open(fileName.data(), O_RDWR | O_CREAT, 0666);
        if (fd != -1) {
            metas.dump(fd, 2);
        } else {
            LOGE("@%s %d:open failed, errmsg: %s ", __FUNCTION__, __LINE__, strerror(errno));
        }
        close(fd);
    }
    count++;

	if (!ctx || result_metas) {
		printf("%s: error\n", __func__);
		goto err_ret;
	}

	if (result_frame_id <= 0)
		printf("%s: get the default settings\n", __func__);

	return 0;

err_ret:
	return -1;
}

/*
 * Stop the current control loop.
 * Args:
 *    |cl_ctx|: current CL context
 * Returns:
 *    -EINVAL: failed
 *    0      : success
 */
int rkisp_cl_stop(void* cl_ctx)
{
	struct cl_context_s* ctx = (struct cl_context_s*)cl_ctx;

	if (!ctx || ctx->state != CL_STATE_STARTED) {
		printf("%s: error\n", __func__);
		goto err_ret;
	}

	ctx->state = CL_STATE_PREPARED;

	return 0;

err_ret:
	return -1;
}

void rkisp_cl_deinit(void* cl_ctx)
{
	struct cl_context_s* ctx = (struct cl_context_s*)cl_ctx;

	if (!ctx ||
		ctx->state != CL_STATE_INITED ||
		ctx->state != CL_STATE_PREPARED) {
		printf("%s: error\n", __func__);
	    return;	
	}

	free(ctx);
}
