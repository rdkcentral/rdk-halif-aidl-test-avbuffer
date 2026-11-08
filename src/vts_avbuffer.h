/**
 *  If not stated otherwise in this file or this component's LICENSE
 *  file the following copyright and licenses apply:
 *
 *  Copyright 2025 RDK Management
 *
 *  Licensed under the Apache License, Version 2.0 (the License);
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an AS IS BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

/**
 * @addtogroup HPK Hardware Porting Kit
 * @{
 * @par The Hardware Porting Kit
 * HPK is the next evolution of the well-defined Hardware Abstraction Layer
 * (HAL), but augmented with more comprehensive documentation and test suites
 * that OEM or SOC vendors can use to self-certify their ports before taking
 * them to RDKM for validation or to an operator for final integration and
 * deployment. The Hardware Porting Kit effectively enables an OEM and/or SOC
 * vendor to self-certify their own Video Accelerator devices, with minimal RDKM
 * assistance.
 *
 */

/**
 * @addtogroup AV_BUFFER AV Buffer Module
 * @{
 */

/**
 * @addtogroup AV_BUFFER_HALTEST AV Buffer HAL Tests
 * @{
 */

/**
 * @defgroup AV_BUFFER_HALTEST_HEADER AV Buffer HAL Tests helper functions Header File
 * @{
 * @parblock
 *
 * ### Common functions/macros required for the AV Buffer module :
 *
 * Common functions/macros required for the AV Buffer module .
 *
 * **Pre-Conditions:**  None @n
 * **Dependencies:** None @n
 *
 * @endparblock
 *
 */

/**
 * @file vts_avbuffer.h
 *
 */

#ifndef __VTS_AVBUFFER_H__
#define __VTS_AVBUFFER_H__

#include <stdio.h>
#include <vector>
#include <optional>
#include <chrono>
#include <thread>
#include <stdio.h>
#include <mutex>
#include <cstring>
#include <inttypes.h>

#include <ut_kvp_profile.h>
#include <ut.h>
#include <ut_log.h>
#include <ut_control_plane.h>
#include <gtest/gtest.h>
#include <binder/IServiceManager.h>
#include <binder/ProcessState.h>
#include <binder/IPCThreadState.h>
#include <binder/Status.h>
#include <utils/String16.h>

#include "com/rdk/hal/avbuffer/IAVBuffer.h"
#include "com/rdk/hal/avbuffer/BnAVBufferSpaceListener.h"
#include "com/rdk/hal/videodecoder/IVideoDecoderManager.h"
#include "com/rdk/hal/videodecoder/BnVideoDecoderControllerListener.h"
#include "com/rdk/hal/audiodecoder/IAudioDecoderManager.h"
#include "com/rdk/hal/audiodecoder/BnAudioDecoderControllerListener.h"

#define MAX_WAIT_MS 10
#define MAX_FINAL_WAIT_MS 5000

extern void *gAvBufferTestConfiguration;

using android::sp;
using android::String16;
using android::defaultServiceManager;
using android::binder::Status;

using namespace com::rdk::hal;
using namespace com::rdk::hal::avbuffer;
using namespace com::rdk::hal::videodecoder;
using namespace com::rdk::hal::audiodecoder;

extern sp<IAVBuffer> gAVBufferManager;

/*
 * Secure pools need a configured (opened) decoder, otherwise createVideoPool()/createAudioPool()
 * return EX_ILLEGAL_STATE (IAVBuffer). The macros below open a decoder in secure mode with its first
 * supported codec before a secure pool is created, and close it after the pool is destroyed.
 * If the decoder does not support secure mode (capabilities), the controller is left null so the caller can
 * skip. If secure mode is supported but open() fails, the test fails (setup failures are not hidden).
 */

/* Minimal decoder controller listeners: the decoder is only opened, no frames are decoded */
class AVBufferVideoDecoderListener : public videodecoder::BnVideoDecoderControllerListener
{
public:
    Status onFrameOutput(int64_t, int64_t, const std::optional<videodecoder::FrameMetadata>&) override { return Status::ok(); }
    Status onEndOfStream() override { return Status::ok(); }
    Status onUserDataOutput(int64_t, const std::vector<uint8_t>&) override { return Status::ok(); }
    Status onDecodeBufferAvailable() override { return Status::ok(); }
};

class AVBufferAudioDecoderListener : public audiodecoder::BnAudioDecoderControllerListener
{
public:
    Status onFrameOutput(int64_t, int64_t, const std::optional<audiodecoder::FrameMetadata>&) override { return Status::ok(); }
    Status onEndOfStream() override { return Status::ok(); }
    Status onDecodeBufferAvailable() override { return Status::ok(); }
};

#define AVBUFFER_DECODER_READY_TIMEOUT_MS 2000

#define AVBUFFER_OPEN_SECURE_VIDEODECODER(videoDecoderId, videoDecoder, videoDecoderController) \
    do { \
        videoDecoderController = nullptr; \
        sp<IVideoDecoderManager> _manager = android::interface_cast<IVideoDecoderManager>( \
            defaultServiceManager()->getService(String16(IVideoDecoderManager::serviceName().c_str()))); \
        videodecoder::Capabilities _caps; \
        if (_manager == nullptr || !_manager->getVideoDecoder(videoDecoderId, &videoDecoder).isOk() || \
            videoDecoder == nullptr || !videoDecoder->getCapabilities(&_caps).isOk() || \
            !_caps.supportsSecure || _caps.supportedCodecs.empty()) \
        { \
            UT_LOG_INFO("Video decoder [%d] cannot be opened in secure mode", (videoDecoderId).value); \
            break; \
        } \
        UT_LOG_INFO("Calling open(IN:videoDecoderId:[%d], secure:[true])", (videoDecoderId).value); \
        Status _status = videoDecoder->open(_caps.supportedCodecs[0].codec, true, \
                                            new AVBufferVideoDecoderListener(), &videoDecoderController); \
        UT_LOG_INFO("Result open(IN:videoDecoderId:[%d]), status:[isOK:%s, exception:%s]", (videoDecoderId).value, \
                    UT_Control_GetMapString(bool_mapTable, _status.isOk()), \
                    UT_Control_GetMapString(statusException_mapTable, _status.exceptionCode())); \
        /* Secure mode and the codec are supported (capabilities), so open() must succeed */ \
        UT_ASSERT_TRUE(_status.isOk()); \
        UT_ASSERT_TRUE(videoDecoderController != nullptr); \
        videodecoder::State _state = videodecoder::State::UNKNOWN; \
        for (int _t = 0; videoDecoderController != nullptr && _t < AVBUFFER_DECODER_READY_TIMEOUT_MS / 10; ++_t) \
        { \
            if (videoDecoder->getState(&_state).isOk() && _state == videodecoder::State::READY) break; \
            std::this_thread::sleep_for(std::chrono::milliseconds(10)); \
        } \
        if (videoDecoderController != nullptr) { UT_ASSERT_EQUAL(_state, videodecoder::State::READY); } \
    } while (0)

#define AVBUFFER_CLOSE_VIDEODECODER(videoDecoder, videoDecoderController) \
    do { \
        bool _closed = false; \
        Status _status = videoDecoder->close(videoDecoderController, &_closed); \
        UT_LOG_INFO("Result close(video decoder), closed:[%s], status:[isOK:%s, exception:%s]", \
                    UT_Control_GetMapString(bool_mapTable, _closed), \
                    UT_Control_GetMapString(bool_mapTable, _status.isOk()), \
                    UT_Control_GetMapString(statusException_mapTable, _status.exceptionCode())); \
        UT_ASSERT_TRUE(_status.isOk()); \
        UT_ASSERT_TRUE(_closed); \
        videoDecoderController = nullptr; \
    } while (0)

#define AVBUFFER_OPEN_SECURE_AUDIODECODER(audioDecoderId, audioDecoder, audioDecoderController) \
    do { \
        audioDecoderController = nullptr; \
        sp<IAudioDecoderManager> _manager = android::interface_cast<IAudioDecoderManager>( \
            defaultServiceManager()->getService(String16(IAudioDecoderManager::serviceName().c_str()))); \
        audiodecoder::Capabilities _caps; \
        if ((audioDecoderId).value == IAudioDecoder::Id::UNDEFINED || _manager == nullptr || \
            !_manager->getAudioDecoder(audioDecoderId, &audioDecoder).isOk() || audioDecoder == nullptr || \
            !audioDecoder->getCapabilities(&_caps).isOk() || !_caps.supportsSecure || _caps.supportedCodecs.empty()) \
        { \
            UT_LOG_INFO("Audio decoder [%d] cannot be opened in secure mode", (audioDecoderId).value); \
            break; \
        } \
        UT_LOG_INFO("Calling open(IN:audioDecoderId:[%d], secure:[true])", (audioDecoderId).value); \
        Status _status = audioDecoder->open(_caps.supportedCodecs[0], true, \
                                            new AVBufferAudioDecoderListener(), &audioDecoderController); \
        UT_LOG_INFO("Result open(IN:audioDecoderId:[%d]), status:[isOK:%s, exception:%s]", (audioDecoderId).value, \
                    UT_Control_GetMapString(bool_mapTable, _status.isOk()), \
                    UT_Control_GetMapString(statusException_mapTable, _status.exceptionCode())); \
        /* Secure mode and the codec are supported (capabilities), so open() must succeed */ \
        UT_ASSERT_TRUE(_status.isOk()); \
        UT_ASSERT_TRUE(audioDecoderController != nullptr); \
        audiodecoder::State _state = audiodecoder::State::UNKNOWN; \
        for (int _t = 0; audioDecoderController != nullptr && _t < AVBUFFER_DECODER_READY_TIMEOUT_MS / 10; ++_t) \
        { \
            if (audioDecoder->getState(&_state).isOk() && _state == audiodecoder::State::READY) break; \
            std::this_thread::sleep_for(std::chrono::milliseconds(10)); \
        } \
        if (audioDecoderController != nullptr) { UT_ASSERT_EQUAL(_state, audiodecoder::State::READY); } \
    } while (0)

#define AVBUFFER_CLOSE_AUDIODECODER(audioDecoder, audioDecoderController) \
    do { \
        bool _closed = false; \
        Status _status = audioDecoder->close(audioDecoderController, &_closed); \
        UT_LOG_INFO("Result close(audio decoder), closed:[%s], status:[isOK:%s, exception:%s]", \
                    UT_Control_GetMapString(bool_mapTable, _closed), \
                    UT_Control_GetMapString(bool_mapTable, _status.isOk()), \
                    UT_Control_GetMapString(statusException_mapTable, _status.exceptionCode())); \
        UT_ASSERT_TRUE(_status.isOk()); \
        UT_ASSERT_TRUE(_closed); \
        audioDecoderController = nullptr; \
    } while (0)


#define AVBUFFER_GETMETRICS(avBufferManager, secureHeap, heapMetrics) \
    do { \
        UT_LOG_INFO("Calling getHeapMetrics(IN:secureHeap:[%s], OUT:heapMetrics[])", \
                    UT_Control_GetMapString(bool_mapTable, secureHeap)); \
        Status status = avBufferManager->getHeapMetrics(secureHeap, &heapMetrics); \
        UT_LOG_INFO("Result getHeapMetrics(IN:secureHeap:[%s], OUT:heapMetrics[secure:%s, bytesUsed:%d, bytesTotal:%d]), status:[isOK:%s, exception:%s]", \
                    UT_Control_GetMapString(bool_mapTable, secureHeap), \
                    UT_Control_GetMapString(bool_mapTable, heapMetrics.secure), \
                    heapMetrics.bytesUsed, \
                    heapMetrics.bytesTotal, \
                    UT_Control_GetMapString(bool_mapTable, status.isOk()), \
                    UT_Control_GetMapString(statusException_mapTable, status.exceptionCode())); \
        UT_ASSERT_TRUE(status.isOk()); \
        UT_ASSERT_EQUAL(status.exceptionCode(), Status::EX_NONE); \
    } while (0)

#define AVBUFFER_CREATE_VIDEOPOOL_SUCCESS(avBufferManager, secureHeap, videoDecoderId, listener, videoPool) \
    do { \
        UT_LOG_INFO("Calling createVideoPool(IN:secureHeap:[%d], IN:videoDecoderId:[%d], IN:listener:[%p], OUT:videoPool)", \
                    secureHeap, videoDecoderId.value, listener.get()); \
        Status status = avBufferManager->createVideoPool(secureHeap, videoDecoderId, listener, &videoPool); \
        UT_LOG_INFO("Result createVideoPool(IN:secureHeap:[%d], IN:videoDecoderId:[%d], IN:listener:[%p], OUT:videoPool.handle:[%d]), status:[isOK:%s, exception:%s]", \
                    secureHeap, videoDecoderId.value, listener.get(), videoPool.handle, \
                    UT_Control_GetMapString(bool_mapTable, status.isOk()), UT_Control_GetMapString(statusException_mapTable, status.exceptionCode())); \
        UT_ASSERT_TRUE(status.isOk()); \
        UT_ASSERT_TRUE(videoPool.handle >= 0); \
        UT_ASSERT_EQUAL(status.exceptionCode(), Status::EX_NONE); \
    } while (0)

#define AVBUFFER_CREATE_VIDEOPOOL_FAILURE(avBufferManager, secureHeap, videoDecoderId, listener, videoPool, exception) \
    do { \
        UT_LOG_INFO("Calling createVideoPool(IN:secureHeap:[%d], IN:videoDecoderId:[%d], IN:listener:[%p], OUT:videoPool)", \
                    secureHeap, videoDecoderId.value, listener.get()); \
        Status status = avBufferManager->createVideoPool(secureHeap, videoDecoderId, listener, &videoPool); \
        UT_LOG_INFO("Result createVideoPool(IN:secureHeap:[%d], IN:videoDecoderId:[%d], IN:listener:[%p], OUT:videoPool.handle:[%d]), status:[isOK:%s, exception:%s]", \
                    secureHeap, videoDecoderId.value, listener.get(), videoPool.handle, \
                    UT_Control_GetMapString(bool_mapTable, status.isOk()), UT_Control_GetMapString(statusException_mapTable, status.exceptionCode())); \
        UT_ASSERT_FALSE(status.isOk()); \
        UT_ASSERT_EQUAL(status.exceptionCode(), exception); \
    } while (0)

#define AVBUFFER_CREATE_AUDIOPOOL_SUCCESS(avBufferManager, secureHeap, audioDecoderId, listener, audioPool) \
    do { \
        UT_LOG_INFO("Calling createAudioPool(IN:secureHeap:[%d], IN:audioDecoderId:[%d], IN:listener:[%p], OUT:audioPool)", \
                    secureHeap, audioDecoderId.value, listener.get()); \
        Status status = avBufferManager->createAudioPool(secureHeap, audioDecoderId, listener, &audioPool); \
        UT_LOG_INFO("Result createAudioPool(IN:secureHeap:[%d], IN:audioDecoderId:[%d], IN:listener:[%p], OUT:audioPool.handle:[%d]), status:[isOK:%s, exception:%s]", \
                    secureHeap, audioDecoderId.value, listener.get(), audioPool.handle, \
                    UT_Control_GetMapString(bool_mapTable, status.isOk()), UT_Control_GetMapString(statusException_mapTable, status.exceptionCode())); \
        UT_ASSERT_TRUE(status.isOk()); \
        UT_ASSERT_TRUE(audioPool.handle >= 0); \
        UT_ASSERT_EQUAL(status.exceptionCode(), Status::EX_NONE); \
    } while (0)

#define AVBUFFER_CREATE_AUDIOPOOL_FAILURE(avBufferManager, secureHeap, audioDecoderId, listener, audioPool, exception) \
    do { \
        UT_LOG_INFO("Calling createAudioPool(IN:secureHeap:[%d], IN:audioDecoderId:[%d], IN:listener:[%p], OUT:audioPool)", \
                    secureHeap, audioDecoderId.value, listener.get()); \
        Status status = avBufferManager->createAudioPool(secureHeap, audioDecoderId, listener, &audioPool); \
        UT_LOG_INFO("Result createAudioPool(IN:secureHeap:[%d], IN:audioDecoderId:[%d], IN:listener:[%p], OUT:audioPool.handle:[%d]), status:[isOK:%s, exception:%s]", \
                    secureHeap, audioDecoderId.value, listener.get(), audioPool.handle, \
                    UT_Control_GetMapString(bool_mapTable, status.isOk()), UT_Control_GetMapString(statusException_mapTable, status.exceptionCode())); \
        UT_ASSERT_FALSE(status.isOk()); \
        UT_ASSERT_EQUAL(status.exceptionCode(), exception); \
    } while (0)

#define AVBUFFER_DESTROY_POOL_SUCCESS(avBufferManager, pool) \
    do { \
        bool _aidl_return = false; \
        Status status; \
        UT_LOG_INFO("Calling destroyPool(IN:poolHandle:[%d], OUT:_aidl_return:[])", pool.handle); \
        status = avBufferManager->destroyPool(pool, &_aidl_return); \
        UT_LOG_INFO("Result destroyPool with (IN:poolHandle:[%d], OUT:_aidl_return:[%s]), status:[isOK:%s, exception:%s]", \
                    pool.handle, UT_Control_GetMapString(bool_mapTable, _aidl_return), \
                    UT_Control_GetMapString(bool_mapTable, status.isOk()), \
                    UT_Control_GetMapString(statusException_mapTable, status.exceptionCode())); \
        UT_ASSERT_TRUE(status.isOk()); \
        UT_ASSERT_TRUE(_aidl_return); \
        UT_ASSERT_EQUAL(status.exceptionCode(), Status::EX_NONE); \
    } while (0)

#define AVBUFFER_DESTROY_POOL_FAILURE(avBufferManager, pool, exception) \
    do { \
        bool _aidl_return = false; \
        Status status; \
        UT_LOG_INFO("Calling destroyPool(IN:poolHandle:[%d], OUT:_aidl_return:[])", pool.handle); \
        status = avBufferManager->destroyPool(pool, &_aidl_return); \
        UT_LOG_INFO("Result destroyPool with (IN:poolHandle:[%d], OUT:_aidl_return:[%s]), status:[isOK:%s, exception:%s]", \
                    pool.handle, UT_Control_GetMapString(bool_mapTable, _aidl_return), \
                    UT_Control_GetMapString(bool_mapTable, status.isOk()), \
                    UT_Control_GetMapString(statusException_mapTable, status.exceptionCode())); \
        UT_ASSERT_EQUAL(status.exceptionCode(), exception); \
        if(status.exceptionCode() == Status::EX_NONE) \
        { \
            UT_ASSERT_TRUE(status.isOk()); \
            UT_ASSERT_FALSE(_aidl_return); \
        } \
        else { \
            UT_ASSERT_FALSE(status.isOk()); \
        } \
    } while (0)

#define AVBUFFER_GETPOOL_METRICS_SUCCESS(avBufferManager, pool, poolMetrics) \
    do { \
        UT_LOG_INFO("Calling getPoolMetrics(IN:poolHandle:[%d], OUT:PoolMetrics[])", pool.handle); \
        Status status = avBufferManager->getPoolMetrics(pool, &poolMetrics); \
        UT_LOG_INFO("Result getPoolMetrics(IN:poolHandle:[%d], OUT:PoolMetrics[bytesUsed:%d, bytesTotal:%d]), status:[isOK:%s, exception:%s]", \
                    pool.handle, poolMetrics.bytesUsed, poolMetrics.bytesTotal, \
                    UT_Control_GetMapString(bool_mapTable, status.isOk()), UT_Control_GetMapString(statusException_mapTable, status.exceptionCode())); \
        UT_ASSERT_TRUE(status.isOk()); \
        UT_ASSERT_EQUAL(status.exceptionCode(), Status::EX_NONE); \
    } while (0)

#define AVBUFFER_GETPOOL_METRICS_FAILURE(avBufferManager, pool, poolMetrics, exception) \
    do { \
        UT_LOG_INFO("Calling getPoolMetrics(IN:poolHandle:[%d], OUT:PoolMetrics[])", pool.handle); \
        Status status = avBufferManager->getPoolMetrics(pool, &poolMetrics); \
        UT_LOG_INFO("Result getPoolMetrics(IN:poolHandle:[%d], OUT:PoolMetrics[bytesUsed:%d, bytesTotal:%d]), status:[isOK:%s, exception:%s]", \
                    pool.handle, poolMetrics.bytesUsed, poolMetrics.bytesTotal, \
                    UT_Control_GetMapString(bool_mapTable, status.isOk()), UT_Control_GetMapString(statusException_mapTable, status.exceptionCode())); \
        UT_ASSERT_FALSE(status.isOk()); \
        UT_ASSERT_EQUAL(status.exceptionCode(), exception); \
    } while (0)

#define AVBUFFER_GETALLPOOL_METRICS_SUCCESS(avBufferManager, secureHeap, poolMetrics) \
    do { \
        UT_LOG_INFO("Calling getAllPoolMetrics(IN:secureHeap:[%s], OUT:poolMetrics[])", UT_Control_GetMapString(bool_mapTable, secureHeap)); \
        Status status = avBufferManager->getAllPoolMetrics(secureHeap, &poolMetrics); \
        UT_LOG_INFO("Result getAllPoolMetrics(IN:secureHeap:[%s], OUT:poolMetrics[%zu]), status:[isOK:%s, exception:%s]", \
            UT_Control_GetMapString(bool_mapTable, secureHeap), poolMetrics.size(), UT_Control_GetMapString(bool_mapTable, status.isOk()), \
                     UT_Control_GetMapString(statusException_mapTable, status.exceptionCode())); \
        UT_ASSERT_TRUE(status.isOk()); \
        UT_ASSERT_EQUAL(status.exceptionCode(), Status::EX_NONE); \
    } while (0)

#define AVBUFFER_ALLOC_SUCCESS(avBufferManager, pool, size, bufferHandle) \
    do { \
        bool isValidResult = false; \
        UT_LOG_INFO("Calling alloc(IN:poolHandle:[%d], IN:size:[%d], OUT:bufferHandle)", pool.handle, size); \
        Status status = avBufferManager->alloc(pool, size, &bufferHandle); \
        UT_LOG_INFO("Result alloc(IN:poolHandle:[%d], IN:size:[%d], OUT:bufferHandle:[0x%016" PRIx64 "]), status:[isOK:%s, exception:%s]", \
                    pool.handle, size, bufferHandle, UT_Control_GetMapString(bool_mapTable, status.isOk()), \
                    UT_Control_GetMapString(statusException_mapTable, status.exceptionCode())); \
        UT_ASSERT_TRUE(status.isOk()); \
        AVBUFFER_ISVALID_SUCCESS(avBufferManager, bufferHandle, isValidResult); \
        UT_ASSERT_TRUE(isValidResult); \
        UT_ASSERT_EQUAL(status.exceptionCode(), Status::EX_NONE); \
    } while (0)

#define AVBUFFER_ALLOC_FAILURE(avBufferManager, pool, size, bufferHandle, exception) \
    do { \
        bool isValidResult = false; \
        UT_LOG_INFO("Calling alloc(IN:poolHandle:[%d], IN:size:[%d], OUT:bufferHandle)", pool.handle, size); \
        Status status = avBufferManager->alloc(pool, size, &bufferHandle); \
        UT_LOG_INFO("Result alloc(IN:poolHandle:[%d], IN:size:[%d], OUT:bufferHandle:[0x%016" PRIx64 "]), status:[isOK:%s, exception:%s]", \
                    pool.handle, size, bufferHandle, UT_Control_GetMapString(bool_mapTable, status.isOk()), \
                    UT_Control_GetMapString(statusException_mapTable, status.exceptionCode())); \
        UT_ASSERT_EQUAL(status.exceptionCode(), exception); \
        if(status.exceptionCode() == Status::EX_NONE){ \
            UT_ASSERT_TRUE(status.isOk()); \
            AVBUFFER_ISVALID_SUCCESS(avBufferManager, bufferHandle, isValidResult); \
            UT_ASSERT_FALSE(isValidResult); \
        } \
        else { \
            UT_ASSERT_FALSE(status.isOk()); \
        } \
    } while (0)

#define AVBUFFER_FREE_SUCCESS(avBufferManager, bufferHandle) \
    do { \
        bool _isValidHandle = false; \
        AVBUFFER_ISVALID_SUCCESS(avBufferManager, bufferHandle, _isValidHandle); \
        if(_isValidHandle) \
        { \
            bool _aidl_return = false; \
            UT_LOG_INFO("Calling free(IN:bufferHandle:[0x%016" PRIx64 "], OUT:_aidl_return)", static_cast<uint64_t>(bufferHandle)); \
            Status status = avBufferManager->free(bufferHandle, &_aidl_return); \
            UT_LOG_INFO("Result free(IN:bufferHandle:[0x%016" PRIx64 "], OUT:_aidl_return:[%s]), status:[isOK:%s, exception:%s]", \
                        static_cast<uint64_t>(bufferHandle), UT_Control_GetMapString(bool_mapTable, _aidl_return), \
                        UT_Control_GetMapString(bool_mapTable, status.isOk()), \
                        UT_Control_GetMapString(statusException_mapTable, status.exceptionCode())); \
            UT_ASSERT_TRUE(status.isOk()); \
            UT_ASSERT_EQUAL(status.exceptionCode(), Status::EX_NONE); \
            UT_ASSERT_TRUE(_aidl_return); \
        } \
        else \
        { \
            UT_LOG_WARNING("AVBUFFER_FREE_SUCCESS: bufferHandle [0x%016" PRIx64 "] is invalid, skipping free", static_cast<uint64_t>(bufferHandle)); \
        } \
    } while (0)

#define AVBUFFER_FREE_FAILURE(avBufferManager, bufferHandle, exception) \
    do { \
        bool _aidl_return = false; \
        UT_LOG_INFO("Calling free(IN:bufferHandle:[0x%016" PRIx64 "], OUT:_aidl_return)", static_cast<uint64_t>(bufferHandle)); \
        Status status = avBufferManager->free(bufferHandle, &_aidl_return); \
        UT_LOG_INFO("Result free(IN:bufferHandle:[0x%016" PRIx64 "], OUT:_aidl_return:[%s]), status:[isOK:%s, exception:%s]", \
                    static_cast<uint64_t>(bufferHandle), UT_Control_GetMapString(bool_mapTable, _aidl_return), \
                    UT_Control_GetMapString(bool_mapTable, status.isOk()), \
                    UT_Control_GetMapString(statusException_mapTable, status.exceptionCode())); \
        UT_ASSERT_EQUAL(status.exceptionCode(), exception); \
        if(status.exceptionCode() == Status::EX_NONE){ \
            UT_ASSERT_TRUE(status.isOk()); \
            UT_ASSERT_FALSE(_aidl_return); \
        } \
        else { \
            UT_ASSERT_FALSE(status.isOk()); \
        } \
    } while (0)

    #define AVBUFFER_NOTIFYSPACE_SUCCESS(avBufferManager, pool, size) \
    do { \
        bool _aidl_return = false; \
        UT_LOG_INFO("Calling notifyWhenSpaceAvailable(IN:poolHandle:[%d], IN:size:[%d], OUT:_aidl_return)", pool.handle, size); \
        Status status = avBufferManager->notifyWhenSpaceAvailable(pool, size, &_aidl_return); \
        UT_LOG_INFO("Result notifyWhenSpaceAvailable(IN:poolHandle:[%d], IN:size:[%d], OUT:_aidl_return:[%s]), status:[isOK:%s, exception:%s]", \
                    pool.handle, size, UT_Control_GetMapString(bool_mapTable, _aidl_return), \
                    UT_Control_GetMapString(bool_mapTable, status.isOk()), \
                    UT_Control_GetMapString(statusException_mapTable, status.exceptionCode())); \
        UT_ASSERT_TRUE(status.isOk()); \
        UT_ASSERT_EQUAL(status.exceptionCode(), Status::EX_NONE); \
        UT_ASSERT_TRUE(_aidl_return); \
    } while (0)

#define AVBUFFER_NOTIFYSPACE_FAILURE(avBufferManager, pool, size, exception) \
    do { \
        bool _aidl_return = false; \
        UT_LOG_INFO("Calling notifyWhenSpaceAvailable(IN:poolHandle:[%d], IN:size:[%d], OUT:_aidl_return)", pool.handle, size); \
        Status status = avBufferManager->notifyWhenSpaceAvailable(pool, size, &_aidl_return); \
        UT_LOG_INFO("Result notifyWhenSpaceAvailable(IN:poolHandle:[%d], IN:size:[%d], OUT:_aidl_return:[%s]), status:[isOK:%s, exception:%s]", \
                    pool.handle, size, UT_Control_GetMapString(bool_mapTable, _aidl_return), \
                    UT_Control_GetMapString(bool_mapTable, status.isOk()), \
                    UT_Control_GetMapString(statusException_mapTable, status.exceptionCode())); \
        UT_ASSERT_EQUAL(status.exceptionCode(), exception); \
        if(status.exceptionCode() == Status::EX_NONE){ \
            UT_ASSERT_TRUE(status.isOk()); \
            UT_ASSERT_FALSE(_aidl_return); \
        } \
        else { \
            UT_ASSERT_FALSE(status.isOk()); \
        } \
    } while (0)

#define AVBUFFER_TRIMSIZE_SUCCESS(avBufferManager, bufferHandle, newSize) \
    do { \
        bool _isValidHandle = false; \
        AVBUFFER_ISVALID_SUCCESS(avBufferManager, bufferHandle, _isValidHandle); \
        UT_ASSERT_TRUE(_isValidHandle); \
        bool _aidl_return = false; \
        UT_LOG_INFO("Calling trimSize with video buffer handle(IN:bufferHandle:[0x%016" PRIx64 "], IN:newSize:[%d], OUT:_aidl_return)", static_cast<uint64_t>(bufferHandle), newSize); \
        Status status = avBufferManager->trimSize(bufferHandle, newSize, &_aidl_return); \
        UT_LOG_INFO("Result trimSize(IN:bufferHandle:[0x%016" PRIx64 "], IN:newSize:[%d], OUT:_aidl_return:[%s]), status:[isOK:%s, exception:%s]", \
                    static_cast<uint64_t>(bufferHandle), newSize, UT_Control_GetMapString(bool_mapTable, _aidl_return), \
                    UT_Control_GetMapString(bool_mapTable, status.isOk()), UT_Control_GetMapString(statusException_mapTable, status.exceptionCode())); \
        UT_ASSERT_TRUE(status.isOk()); \
        UT_ASSERT_EQUAL(status.exceptionCode(), Status::EX_NONE); \
        UT_ASSERT_TRUE(_aidl_return); \
    } while (0)

#define AVBUFFER_TRIMSIZE_FAILURE(avBufferManager, bufferHandle, newSize, exception) \
    do { \
        bool _aidl_return = false; \
        UT_LOG_INFO("Calling trimSize with video buffer handle(IN:bufferHandle:[0x%016" PRIx64 "], IN:newSize:[%d], OUT:_aidl_return)", static_cast<uint64_t>(bufferHandle), newSize); \
        Status status = avBufferManager->trimSize(bufferHandle, newSize, &_aidl_return); \
        UT_LOG_INFO("Result trimSize(IN:bufferHandle:[0x%016" PRIx64 "], IN:newSize:[%d], OUT:_aidl_return:[%s]), status:[isOK:%s, exception:%s]", \
                    static_cast<uint64_t>(bufferHandle), newSize, UT_Control_GetMapString(bool_mapTable, _aidl_return), \
                    UT_Control_GetMapString(bool_mapTable, status.isOk()), UT_Control_GetMapString(statusException_mapTable, status.exceptionCode())); \
        UT_ASSERT_EQUAL(status.exceptionCode(), exception); \
        if(status.exceptionCode() == Status::EX_NONE){ \
            UT_ASSERT_TRUE(status.isOk()); \
            UT_ASSERT_FALSE(_aidl_return); \
        } \
        else { \
            UT_ASSERT_FALSE(status.isOk()); \
        } \
    } while (0)

#define AVBUFFER_ISVALID_SUCCESS(avBufferManager, bufferHandle, isValidResult) \
    do { \
        UT_LOG_INFO("Calling isValid(IN:bufferHandle:[0X%016" PRIx64 "], OUT:_aidl_return[])", static_cast<uint64_t>(bufferHandle)); \
        Status status = avBufferManager->isValid(bufferHandle, &isValidResult); \
        UT_LOG_INFO("Result isValid(IN:bufferHandle:[0X%016" PRIx64 "], OUT:_aidl_return:[%s]), status:[isOK:%s, exception:%s]", \
                    static_cast<uint64_t>(bufferHandle), UT_Control_GetMapString(bool_mapTable, isValidResult), \
                    UT_Control_GetMapString(bool_mapTable, status.isOk()), \
                    UT_Control_GetMapString(statusException_mapTable, status.exceptionCode())); \
        UT_ASSERT_TRUE(status.isOk()); \
        UT_ASSERT_EQUAL(status.exceptionCode(), Status::EX_NONE); \
    } while (0)

#define AVBUFFER_GETALLOCLIST_SUCCESS(avBufferManager, poolHandle, allocList) \
    do { \
        UT_LOG_INFO("Calling getAllocList with video pool handle (IN:poolHandle:[%d], OUT:allocList[])", poolHandle.handle); \
        Status status = avBufferManager->getAllocList(poolHandle, &allocList); \
        UT_LOG_INFO("Result getAllocList(IN:poolHandle:[%d], OUT:allocList[size:%zu]), status:[isOK:%s, exception:%s]", \
                    poolHandle.handle, allocList.size(), \
                    UT_Control_GetMapString(bool_mapTable, status.isOk()), \
                    UT_Control_GetMapString(statusException_mapTable, status.exceptionCode())); \
        UT_ASSERT_TRUE(status.isOk()); \
        UT_ASSERT_EQUAL(status.exceptionCode(), Status::EX_NONE); \
    } while (0)

#define AVBUFFER_GETALLOCLIST_FAILURE(avBufferManager, poolHandle, allocList, exception) \
    do { \
        UT_LOG_INFO("Calling getAllocList with video pool handle (IN:poolHandle:[%d], OUT:allocList[])", poolHandle.handle); \
        Status status = avBufferManager->getAllocList(poolHandle, &allocList); \
        UT_LOG_INFO("Result getAllocList(IN:poolHandle:[%d], OUT:allocList[size:%zu]), status:[isOK:%s, exception:%s]", \
                     poolHandle.handle, allocList.size(), \
                     UT_Control_GetMapString(bool_mapTable, status.isOk()), \
                     UT_Control_GetMapString(statusException_mapTable, status.exceptionCode())); \
        UT_ASSERT_FALSE(status.isOk()); \
        UT_ASSERT_EQUAL(status.exceptionCode(), exception); \
    } while (0)

#define AVBUFFER_CALCULATESHA_SUCCESS(avBufferManager, poolHandle, sha1Result) \
    do { \
        bool _isValidHandle = false; \
        AVBUFFER_ISVALID_SUCCESS(avBufferManager, poolHandle, _isValidHandle); \
        UT_ASSERT_TRUE(_isValidHandle); \
        UT_LOG_INFO("Calling calculateSHA1 with video buffer handle (IN:bufferHandle:[0x%016" PRIx64 "], OUT:sha1Result[])", static_cast<uint64_t>(poolHandle)); \
        Status status = avBufferManager->calculateSHA1(poolHandle, &sha1Result); \
        UT_LOG_INFO("Result calculateSHA1(IN:bufferHandle:[0x%016" PRIx64 "], OUT:sha1Result[size:%zu]), status:[isOK:%s, exception:%s]", \
                    static_cast<uint64_t>(poolHandle), sha1Result.size(), UT_Control_GetMapString(bool_mapTable, status.isOk()), \
                    UT_Control_GetMapString(statusException_mapTable, status.exceptionCode())); \
        if (status.isOk()) { \
            UT_ASSERT_TRUE(status.isOk()); \
            UT_ASSERT_EQUAL(status.exceptionCode(), Status::EX_NONE); \
            UT_ASSERT_FALSE(sha1Result.empty()); \
        } else { \
            UT_ASSERT_EQUAL(status.exceptionCode(), Status::EX_UNSUPPORTED_OPERATION); \
            UT_ASSERT_TRUE(sha1Result.empty()); \
        } \
    } while (0)

#define AVBUFFER_CALCULATESHA_FAILURE(avBufferManager, poolHandle, sha1Result, exception) \
    do { \
        UT_LOG_INFO("Calling calculateSHA1 with video buffer handle (IN:bufferHandle:[0x%016" PRIx64 "], OUT:sha1Result[])", static_cast<uint64_t>(poolHandle)); \
        Status status = avBufferManager->calculateSHA1(poolHandle, &sha1Result); \
        UT_LOG_INFO("Result calculateSHA1(IN:bufferHandle:[0x%016" PRIx64 "], OUT:sha1Result[size:%zu]), status:[isOK:%s, exception:%s]", \
                    static_cast<uint64_t>(poolHandle), sha1Result.size(), UT_Control_GetMapString(bool_mapTable, status.isOk()), \
                    UT_Control_GetMapString(statusException_mapTable, status.exceptionCode())); \
        if(status.exceptionCode() != Status::EX_UNSUPPORTED_OPERATION) { \
            UT_ASSERT_FALSE(status.isOk()); \
            UT_ASSERT_EQUAL(status.exceptionCode(), exception); \
        } \
    } while (0)

    #define AVBUFFER_WAIT_FOR_SPACEAVAILABLE(avBufferManager, listener, isSpaceAvailable) \
    int32_t sleep_time = 0; \
    do{\
        do \
        { \
            UT_LOG_INFO("WAIT Started"); \
            std::this_thread::sleep_for(std::chrono::milliseconds(MAX_WAIT_MS)); \
            sleep_time += MAX_WAIT_MS; \
            /* Check the state of the decoder */ \
            isSpaceAvailable = listener->getIsSpaceAvailable(sleep_time); \
            UT_LOG_INFO("WAIT loop"); \
        } while (sleep_time < MAX_FINAL_WAIT_MS && isSpaceAvailable != true);\
    }while(0);



#endif //__VTS_AVBUFFER_H__

/** @} */ // End of AV_BUFFER_HALTEST_HEADER
/** @} */ // End of AV_BUFFER_HALTEST
/** @} */ // End of AV_BUFFER
/** @} */ // End of HPK
