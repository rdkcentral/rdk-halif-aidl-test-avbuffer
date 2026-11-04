
/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2025 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "vts_avbuffer_parse_hfp.h"
#include "vts_avbuffer.h"
#include "avbufferhelper.h"

#include "com/rdk/hal/audiodecoder/BpAudioDecoderManager.h"
#include "com/rdk/hal/videodecoder/IVideoDecoderManager.h"

#include <chrono>
#include <atomic>
#include <mutex>
#include <thread>

#define MAX_DECODER_COUNT 5

#define AUDIODECODER_MANAGER_GETAUDIODECODERIDS(decoderIds) \
    do { \
        ::android::binder::Status status; \
        UT_LOG_INFO("Calling getAudioDecoderIds(OUT:decoderIds:[])"); \
        status = sAudioDecoderManager->getAudioDecoderIds(&decoderIds); \
        UT_LOG_INFO("Result getAudioDecoderIds(OUT:decoderIds:[size: %d]), status:[isOK:%s, exception:%s]", \
                                decoderIds.size(), UT_Control_GetMapString(bool_mapTable, status.isOk()), \
                                UT_Control_GetMapString(statusException_mapTable, status.exceptionCode())); \
        UT_ASSERT_TRUE(status.isOk()); \
        UT_ASSERT_FALSE(decoderIds.empty()); \
        UT_ASSERT_EQUAL(status.exceptionCode(), Status::EX_NONE); \
    } while (0)

#define VIDEODECODER_MANAGER_GETVIDEODECODERIDS(decoderIds) \
    do { \
        ::android::binder::Status status; \
        UT_LOG_INFO("Calling getVideoDecoderIds(OUT:decoderIds:[])"); \
        status = sVideoDecoderManager->getVideoDecoderIds(&decoderIds); \
        UT_LOG_INFO("Result getVideoDecoderIds(OUT:decoderIds:[size: %d]), status:[isOK:%s, exception:%s]", \
                                decoderIds.size(), UT_Control_GetMapString(bool_mapTable, status.isOk()), \
                                UT_Control_GetMapString(statusException_mapTable, status.exceptionCode())); \
        UT_ASSERT_TRUE(status.isOk()); \
        UT_ASSERT_FALSE(decoderIds.empty()); \
        UT_ASSERT_EQUAL(status.exceptionCode(), Status::EX_NONE); \
    } while (0)

#define DATA_PATH "avbuffer.sample_data"
#define SHA1_PATH "avbuffer.sha1"

#define AUDIO_TEST 0x01
#define VIDEO_TEST 0x02

#define SECUREHEAP_TEST 0x01
#define ALLOCATE_TEST 0x02
#define NOTIFY_TEST 0x04
#define TRIM_TEST 0x08
#define READWRITE_TEST 0x10
#define SHA1_TEST 0x20

/* Status::Exception */
const static ut_control_keyStringMapping_t statusException_mapTable[] =
{
    {"EX_NONE",                  (int32_t) Status::EX_NONE},
    {"EX_SECURITY",              (int32_t) Status::EX_SECURITY},
    {"EX_BAD_PARCELABLE",        (int32_t) Status::EX_BAD_PARCELABLE},
    {"EX_ILLEGAL_ARGUMENT",      (int32_t) Status::EX_ILLEGAL_ARGUMENT},
    {"EX_NULL_POINTER",          (int32_t) Status::EX_NULL_POINTER},
    {"EX_ILLEGAL_STATE",         (int32_t) Status::EX_ILLEGAL_STATE},
    {"EX_NETWORK_MAIN_THREAD",   (int32_t) Status::EX_NETWORK_MAIN_THREAD},
    {"EX_UNSUPPORTED_OPERATION", (int32_t) Status::EX_UNSUPPORTED_OPERATION},
    {"EX_SERVICE_SPECIFIC",      (int32_t) Status::EX_SERVICE_SPECIFIC},
    {"EX_PARCELABLE",            (int32_t) Status::EX_PARCELABLE},
    {"EX_HAS_REPLY_HEADER",      (int32_t) Status::EX_HAS_REPLY_HEADER},
    {"EX_TRANSACTION_FAILED",    (int32_t) Status::EX_TRANSACTION_FAILED},
    {NULL, -1}
};

/* bool */
const static ut_control_keyStringMapping_t bool_mapTable[] =
{
    {"false", (int32_t) false},
    {"true",  (int32_t) true},
    {NULL, -1}
};

class AVBufferSpaceListener : public BnAVBufferSpaceListener
{
private:
    std::atomic<bool> isSpaceAvailable{false};

public:

    AVBufferSpaceListener() {};
    virtual ~AVBufferSpaceListener() = default;

    ::android::binder::Status onSpaceAvailable() override
    {
        isSpaceAvailable.store(true, std::memory_order_release);
        return ::android::binder::Status::ok();
    }

    bool waitForSpaceAvailable(std::chrono::milliseconds timeout)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (!isSpaceAvailable.load(std::memory_order_acquire))
        {
            if (std::chrono::steady_clock::now() >= deadline)
            {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return true;
    }
};

static sp<IAudioDecoderManager> sAudioDecoderManager;
static std::vector<IAudioDecoder::Id> sAudioDecoderIds;
static std::vector<IVideoDecoder::Id> sVideoDecoderIds;
static std::vector<sp<AVBufferSpaceListener>> sSpaceListeners;
static std::vector<int64_t> sBufferHandles;
static std::vector<Pool> sPoolsCreated;
static sp<IVideoDecoderManager> sVideoDecoderManager;
static IAVBufferHelper* sAVbufferHelper;
static std::mutex sResourceMutex;

UT_status_t vts_audiodecoder_getService()
{
    sAudioDecoderManager = android::interface_cast<IAudioDecoderManager>(
            defaultServiceManager()->getService(String16(IAudioDecoderManager::serviceName().c_str())));
    if (sAudioDecoderManager == NULL)
    {
        UT_LOG_ERROR("Failed to get AudioDecoderManager instance");
        return UT_STATUS_FAILURE;
    }
    return UT_STATUS_OK;
}

UT_status_t vts_videodecoder_getService()
{
    sVideoDecoderManager =
        android::interface_cast<IVideoDecoderManager>(
            android::defaultServiceManager()->getService(
                android::String16(IVideoDecoderManager::serviceName().c_str())
            )
        );
    if (sVideoDecoderManager == NULL)
    {
        UT_LOG_ERROR("Failed to get VideoDecoderManager instance");
        return UT_STATUS_FAILURE;
    }
    return UT_STATUS_OK;
}


class VTS_L3_AVBuffer : public virtual UTCore

{
    protected:
        static void SetUpTestSuite()
        {
            if(gAVBufferManager == NULL)
            {
                UT_FAIL_FATAL("Failed to get AVBufferManager instance");
            }
            UT_ASSERT_EQUAL(vts_audiodecoder_getService(),UT_STATUS_OK);
            AUDIODECODER_MANAGER_GETAUDIODECODERIDS(sAudioDecoderIds);
            UT_ASSERT_EQUAL(vts_videodecoder_getService(),UT_STATUS_OK);
            VIDEODECODER_MANAGER_GETVIDEODECODERIDS(sVideoDecoderIds);
            sAVbufferHelper = getAVBufferHelperInstance();
            if(sAVbufferHelper == NULL)
            {
                UT_FAIL_FATAL("Failed to get AVBufferHelper instance");
            }
        }

        static void TearDownTestSuite()
        {
            // AV Buffer 0.2.1.0: the helper is a reference-counted singleton owned by the vendor library.
            // Release the reference acquired in SetUpTestSuite(); never delete the instance directly.
            if (sAVbufferHelper != NULL)
            {
                releaseAVBufferHelperInstance(sAVbufferHelper);
                sAVbufferHelper = NULL;
            }
        }

        static void checkMetrics(Pool pool,int32_t sizeFree,int32_t sizeUsed)
        {
            PoolMetrics poolMetrics;
            AVBUFFER_GETPOOL_METRICS_SUCCESS(gAVBufferManager,pool,poolMetrics);
            UT_ASSERT_GREATER_EQUAL(poolMetrics.bytesUsed, 0);
            UT_ASSERT_LESS_EQUAL(poolMetrics.bytesUsed, poolMetrics.bytesTotal);
            UT_ASSERT_EQUAL(poolMetrics.bytesUsed, sizeUsed);
            UT_ASSERT_EQUAL(poolMetrics.bytesTotal, sizeUsed+sizeFree);
        }
    public:
        void AvBufferTest(IAudioDecoder::Id _audioDecoderId,IVideoDecoder::Id _videoDecoderId, uint8_t testConfig, uint8_t testSelection)
        {
            Pool pool;
            PoolMetrics poolMetrics;
            bool isValidResult = false;
            bool isSpaceAvailable = false;
            bool secureHeap = false;
            int64_t bufferHandle = -1;
            void* mapBuffer =  NULL;
            uint32_t mapSize = 0;
            int32_t sizeUsed = 0;
            int32_t sizeFree = 0;
            uint8_t standard_data[256] = {0};
            uint8_t read_data[256] = {0};
            uint8_t standard_data_sha1[20] = {0};
            std::vector<uint8_t> sha1Result;
            sp<AVBufferSpaceListener> listener = new AVBufferSpaceListener();
            {
                std::lock_guard<std::mutex> lock(sResourceMutex);
                sSpaceListeners.emplace_back(listener);
            }
            ut_kvp_instance_t* kvpTestInstance = (ut_kvp_instance_t*)gAvBufferTestConfiguration;

            if(testConfig & SECUREHEAP_TEST)
            {
                secureHeap = true;
            }

            if (secureHeap && !gAVBuffer_hfp.supportSecure)
            {
                UT_IGNORE_TEST() << "secureHeapBytes is not defined or is zero in the HFP";
            }

            if(testSelection == AUDIO_TEST){
                AVBUFFER_CREATE_AUDIOPOOL_SUCCESS(gAVBufferManager, secureHeap, _audioDecoderId, listener, pool);
            }
            else{
                AVBUFFER_CREATE_VIDEOPOOL_SUCCESS(gAVBufferManager, secureHeap, _videoDecoderId, listener, pool);
            }
            {
                std::lock_guard<std::mutex> lock(sResourceMutex);
                sPoolsCreated.emplace_back(pool);
            }
            AVBUFFER_GETPOOL_METRICS_SUCCESS(gAVBufferManager, pool, poolMetrics);
            UT_ASSERT_GREATER_EQUAL(poolMetrics.bytesUsed, 0);
            UT_ASSERT_LESS_EQUAL(poolMetrics.bytesUsed, poolMetrics.bytesTotal);
            if (testConfig & ALLOCATE_TEST)
            {
                AVBUFFER_ALLOC_SUCCESS(gAVBufferManager, pool, (poolMetrics.bytesTotal), bufferHandle);
                AVBUFFER_ISVALID_SUCCESS(gAVBufferManager, bufferHandle, isValidResult);
                if(!isValidResult)
                {
                    UT_LOG_ERROR("No Valid Handle Available bufferHandle [0x%016" PRIx64 "]. Cannot Proceed to next step.",bufferHandle);
                }
                UT_ASSERT_TRUE_FATAL(isValidResult);
                {
                    std::lock_guard<std::mutex> lock(sResourceMutex);
                    sBufferHandles.push_back(bufferHandle);
                }
                sizeUsed = poolMetrics.bytesTotal;
                sizeFree = 0;
                checkMetrics(pool, sizeFree, sizeUsed);
            }
            if (testConfig & NOTIFY_TEST)
            {
                AVBUFFER_NOTIFYSPACE_SUCCESS(gAVBufferManager, pool, (poolMetrics.bytesTotal) / 2);
            }
            if ((testConfig & TRIM_TEST) || (testConfig & NOTIFY_TEST))
            {
                AVBUFFER_TRIMSIZE_SUCCESS(gAVBufferManager, bufferHandle, (sizeUsed) / 2);
                sizeFree = sizeUsed / 2;
                sizeUsed = sizeUsed / 2;
                checkMetrics(pool, sizeFree, sizeUsed);
            }


            if(testConfig & NOTIFY_TEST)
            {
                isSpaceAvailable = listener->waitForSpaceAvailable(std::chrono::seconds(2));
                UT_ASSERT_TRUE(isSpaceAvailable);
            }
            if(testConfig & READWRITE_TEST )
            {
                for(int i=0; i< ut_kvp_getListCount(kvpTestInstance,DATA_PATH); i++)
                {
                    string hexpath = string(DATA_PATH) +"."+ to_string(i);
                    standard_data[i] = (uint8_t)ut_kvp_getUInt8Field(kvpTestInstance,hexpath.c_str());
                }
                AVBUFFER_TRIMSIZE_SUCCESS(gAVBufferManager,bufferHandle,sizeof(standard_data));
                if(secureHeap){
                    if(!sAVbufferHelper->writeSecureHandle(bufferHandle, standard_data, sizeof(standard_data)))
                    {
                        UT_LOG_ERROR("Failed to copy the data to secure buffer");
                    }
                }
                else
                {
                    mapBuffer = sAVbufferHelper->mapHandle(bufferHandle, &mapSize);
                    if((mapBuffer != NULL) && (mapSize >= sizeof(standard_data))){
                        memcpy(mapBuffer, standard_data, sizeof(standard_data));
                        sAVbufferHelper->unmapHandle(bufferHandle);
                    }else {
                        UT_LOG_ERROR("Failed to copy the data to buffer");
                    }
                    mapBuffer = sAVbufferHelper->mapHandle(bufferHandle, &mapSize);
                    if((mapBuffer != NULL) && (mapSize >= sizeof(standard_data))){
                        memcpy(read_data,(void *)mapBuffer,sizeof(read_data));
                        sAVbufferHelper->unmapHandle(bufferHandle);
                    }else {
                        UT_LOG_ERROR("Failed to copy the data from buffer");
                    }
                    UT_ASSERT_TRUE((memcmp(standard_data,read_data,sizeof(standard_data)) == 0));
                }

            }
            if(testConfig & SHA1_TEST)
            {
                for(int i = 0; i<ut_kvp_getListCount(kvpTestInstance,SHA1_PATH);i++)
                {
                    string sha1Path = string(SHA1_PATH) + "." + to_string(i);
                    standard_data_sha1[i] = (uint8_t)ut_kvp_getUInt8Field(kvpTestInstance,sha1Path.c_str());
                }
                AVBUFFER_CALCULATESHA_SUCCESS(gAVBufferManager,bufferHandle,sha1Result);
                if(!sha1Result.empty()){
                    for(int i = 0; i< 20; i++)
                    {
                        UT_ASSERT_EQUAL(sha1Result[i], standard_data_sha1[i]);
                    }
                }
            }
        }
        void destroyPools(uint8_t testConfig)
        {
            std::lock_guard<std::mutex> lock(sResourceMutex);
            if(ALLOCATE_TEST & testConfig){
                for(const auto& bufferHandle : sBufferHandles){
                    AVBUFFER_FREE_SUCCESS(gAVBufferManager,bufferHandle);
                }
                sBufferHandles.clear();
            }
            for(const auto& pool : sPoolsCreated){
                AVBUFFER_DESTROY_POOL_SUCCESS(gAVBufferManager,pool);
            }
            sPoolsCreated.clear();
            sSpaceListeners.clear();
        }

        void runTest(int8_t testSelection,bool isConcurrent, uint8_t testConfig){
            IVideoDecoder::Id _videoDecoderId;
            IAudioDecoder::Id _audioDecoderId;
            _videoDecoderId.value = IVideoDecoder::Id::UNDEFINED;
            _audioDecoderId.value = IAudioDecoder::Id::UNDEFINED;
            std::vector<std::thread> threads;
            if(testSelection & AUDIO_TEST)
            {
                if(!isConcurrent)
                {
                    for(const auto& _decoderId: sAudioDecoderIds)
                    {
                        this->AvBufferTest(_decoderId,_videoDecoderId,testConfig,AUDIO_TEST);
                    }
                }
                else
                {
                    for (const auto &_decoderId : sAudioDecoderIds)
                    {
                        threads.emplace_back([this, _videoDecoderId, _decoderId, testConfig]()
                                             { this->AvBufferTest(_decoderId, _videoDecoderId, testConfig,AUDIO_TEST); });
                    }
                }
            }
            else if(testSelection & VIDEO_TEST)
            {
                if(!isConcurrent){
                    for(const auto& _decoderId: sVideoDecoderIds)
                    {
                        this->AvBufferTest(_audioDecoderId,_decoderId,testConfig,VIDEO_TEST);
                    }
                }
                else
                {
                    for(const auto& _decoderId: sVideoDecoderIds)
                    {
                        threads.emplace_back([this, _audioDecoderId,_decoderId,testConfig]() {
                            this->AvBufferTest(_audioDecoderId,_decoderId,testConfig,VIDEO_TEST);
                            });
                    }
                }
            }
            for (auto &t : threads)
            {
                if (t.joinable())
                    t.join();
            }
            this->destroyPools(testConfig);
            threads.clear();
        }
};

/**
 * @brief Test for VTS_L1_AVBuffer to verify heap metrics retrieval for both secure and non-secure heaps.
 *
 * This test checks the functionality of the getHeapMetrics API for both non-secure and secure heap scenarios.
 * It ensures that the API returns the correct heap metrics and validates the expected conditions for both types of heaps.
 *
 * **Test Group ID:** Basic: 01@n
 * **Test Case ID:** 001@n
 * **Priority:** High@n
 * @n
 * **Pre-Conditions:** None@n
 * **Dependencies:** None@n
 * **User Interaction:** None@n
 * @n
 *
 * **Test Procedure:**@n
 * | Variation / Step | Description | Test Data | Expected Result | Notes |
 * | :----: | --------- | ---------- |-------------- | ----- |
 * | 01 | Initialize non-secure heap metrics retrieval | secureHeap = false, heapMetrics = uninitialized | Status is OK, heapMetrics.secure is false, bytesUsed >= 0, bytesUsed <= bytesTotal, bytesTotal matches expected value | Should Pass |
 * | 02 | Call getHeapMetrics for non-secure heap | secureHeap = false, heapMetrics = initialized | Status is OK, heapMetrics.secure is false, bytesUsed >= 0, bytesUsed <= bytesTotal, bytesTotal matches expected value | Should Pass |
 * | 03 | Validate non-secure heap metrics | heapMetrics = populated | Assertions for status, secure, bytesUsed, and bytesTotal | Should be successful |
 * | 04 | Initialize secure heap metrics retrieval | secureHeap = value from profile, heapMetrics = uninitialized | Status is OK, heapMetrics.secure is true, bytesUsed >= 0, bytesUsed <= bytesTotal, bytesTotal matches expected value | Should Pass |
 * | 05 | Call getHeapMetrics for secure heap | secureHeap = value from profile, heapMetrics = initialized | Status is OK, heapMetrics.secure is true, bytesUsed >= 0, bytesUsed <= bytesTotal, bytesTotal matches expected value | Should Pass |
 * | 06 | Validate secure heap metrics | heapMetrics = populated | Assertions for status, secure, bytesUsed, and bytesTotal | Should be successful |
 */
UT_ADD_TEST(VTS_L3_AVBuffer, seq_nonsec_audiopool)
{
    uint8_t testConfig = 0x00;
    this->runTest(AUDIO_TEST,false,testConfig);

}

UT_ADD_TEST(VTS_L3_AVBuffer, seq_nonsec_audiopoolpcm)
{
    uint8_t testConfig = 0x00;
    IAudioDecoder::Id _audioDecoderId;
    _audioDecoderId.value = IAudioDecoder::Id::UNDEFINED;
    IVideoDecoder::Id _videoDecoderId;
    _videoDecoderId.value = IVideoDecoder::Id::UNDEFINED;

    this->AvBufferTest(_audioDecoderId,_videoDecoderId,testConfig,AUDIO_TEST);
    this->destroyPools(testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, seq_nonsec_audiopool_alloc)
{
    uint8_t testConfig = ALLOCATE_TEST;
    this->runTest(AUDIO_TEST,false,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, seq_nonsec_audiopool_trim)
{
    uint8_t testConfig = ALLOCATE_TEST | TRIM_TEST;
    this->runTest(AUDIO_TEST,false,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, seq_nonsec_audiopool_notify)
{
    uint8_t testConfig = ALLOCATE_TEST| NOTIFY_TEST;
    this->runTest(AUDIO_TEST,false,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, seq_nonsec_audiopool_readwrite)
{
    uint8_t testConfig = ALLOCATE_TEST| READWRITE_TEST;
    this->runTest(AUDIO_TEST,false,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, seq_nonsec_audiopool_sha1)
{
    uint8_t testConfig = ALLOCATE_TEST | READWRITE_TEST |SHA1_TEST;
    this->runTest(AUDIO_TEST,false,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, seq_sec_audiopool)
{
    uint8_t testConfig = SECUREHEAP_TEST;
    this->runTest(AUDIO_TEST,false,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, seq_sec_audiopoolpcm)
{
    uint8_t testConfig = SECUREHEAP_TEST;
    IAudioDecoder::Id _audioDecoderId;
    _audioDecoderId.value = IAudioDecoder::Id::UNDEFINED;
    IVideoDecoder::Id _videoDecoderId;
    _videoDecoderId.value = IVideoDecoder::Id::UNDEFINED;

    this->AvBufferTest(_audioDecoderId,_videoDecoderId,testConfig,AUDIO_TEST);
    this->destroyPools(testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, seq_sec_audiopool_alloc)
{
    uint8_t testConfig = SECUREHEAP_TEST | ALLOCATE_TEST;
    this->runTest(AUDIO_TEST,false,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, seq_sec_audiopool_trim)
{
    uint8_t testConfig = SECUREHEAP_TEST | ALLOCATE_TEST | TRIM_TEST;
    this->runTest(AUDIO_TEST,false,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, seq_sec_audiopool_notify)
{
    uint8_t testConfig = SECUREHEAP_TEST | ALLOCATE_TEST | NOTIFY_TEST;
    this->runTest(AUDIO_TEST,false,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, seq_sec_audiopool_readwrite)
{
    uint8_t testConfig = SECUREHEAP_TEST | ALLOCATE_TEST | READWRITE_TEST;
    this->runTest(AUDIO_TEST,false,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, seq_sec_audiopool_sha1)
{
    uint8_t testConfig = SECUREHEAP_TEST | ALLOCATE_TEST | READWRITE_TEST | SHA1_TEST;
    this->runTest(AUDIO_TEST,false,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, con_nonsec_audiopool)
{
    uint8_t testConfig = 0x00;
    this->runTest(AUDIO_TEST,true,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, con_nonsec_audiopool_alloc)
{
    uint8_t testConfig = ALLOCATE_TEST;
    this->runTest(AUDIO_TEST,true,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, con_nonsec_audiopool_trim)
{
    uint8_t testConfig = ALLOCATE_TEST | TRIM_TEST;
    this->runTest(AUDIO_TEST,true,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, con_nonsec_audiopool_notify)
{
    uint8_t testConfig = ALLOCATE_TEST | NOTIFY_TEST;
    this->runTest(AUDIO_TEST,true,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, con_nonsec_audiopool_readwrite)
{
    uint8_t testConfig = ALLOCATE_TEST | READWRITE_TEST;
    this->runTest(AUDIO_TEST,true,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, con_nonsec_audiopool_sha1)
{
    uint8_t testConfig = ALLOCATE_TEST | READWRITE_TEST | SHA1_TEST;
    this->runTest(AUDIO_TEST,true,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, con_sec_audiopool)
{
    uint8_t testConfig = SECUREHEAP_TEST;
    this->runTest(AUDIO_TEST,true,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, con_sec_audiopool_alloc)
{
    uint8_t testConfig = SECUREHEAP_TEST| ALLOCATE_TEST;
    this->runTest(AUDIO_TEST,true,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, con_sec_audiopool_trim)
{
    uint8_t testConfig = SECUREHEAP_TEST| ALLOCATE_TEST | TRIM_TEST;
    this->runTest(AUDIO_TEST,true,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, con_sec_audiopool_notify)
{
    uint8_t testConfig = SECUREHEAP_TEST| ALLOCATE_TEST | NOTIFY_TEST;
    this->runTest(AUDIO_TEST,true,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, con_sec_audiopool_readwrite)
{
    uint8_t testConfig = SECUREHEAP_TEST| ALLOCATE_TEST | READWRITE_TEST;
    this->runTest(AUDIO_TEST,true,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, con_sec_audiopool_sha1)
{
    uint8_t testConfig = SECUREHEAP_TEST| ALLOCATE_TEST | READWRITE_TEST | SHA1_TEST;
    this->runTest(AUDIO_TEST,true,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, seq_nonsec_videopool)
{
    uint8_t testConfig = 0x00;
    this->runTest(VIDEO_TEST,false,testConfig);
}


UT_ADD_TEST(VTS_L3_AVBuffer, seq_nonsec_videopool_alloc)
{
    uint8_t testConfig = ALLOCATE_TEST;
    this->runTest(VIDEO_TEST,false,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, seq_nonsec_videopool_trim)
{
    uint8_t testConfig = ALLOCATE_TEST | TRIM_TEST ;
    this->runTest(VIDEO_TEST,false,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, seq_nonsec_videopool_notify)
{
    uint8_t testConfig = ALLOCATE_TEST | NOTIFY_TEST;
    this->runTest(VIDEO_TEST,false,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, seq_nonsec_videopool_readwrite)
{
    uint8_t testConfig = ALLOCATE_TEST | READWRITE_TEST;
    this->runTest(VIDEO_TEST,false,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, seq_nonsec_videopool_sha1)
{
    uint8_t testConfig = ALLOCATE_TEST | READWRITE_TEST | SHA1_TEST;
    this->runTest(VIDEO_TEST,false,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, seq_sec_videopool)
{
    uint8_t testConfig = SECUREHEAP_TEST;
    this->runTest(VIDEO_TEST,false,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, seq_sec_videopool_alloc)
{
    uint8_t testConfig = SECUREHEAP_TEST | ALLOCATE_TEST;
    this->runTest(VIDEO_TEST,false,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, seq_sec_videopool_trim)
{
    uint8_t testConfig = SECUREHEAP_TEST | ALLOCATE_TEST | TRIM_TEST;
    this->runTest(VIDEO_TEST,false,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, seq_sec_videopool_notify)
{
    uint8_t testConfig = SECUREHEAP_TEST | ALLOCATE_TEST | NOTIFY_TEST;
    this->runTest(VIDEO_TEST,false,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, seq_sec_videopool_readwrite)
{
    uint8_t testConfig = SECUREHEAP_TEST | ALLOCATE_TEST | READWRITE_TEST;
    this->runTest(VIDEO_TEST,false,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, seq_sec_videopool_sha1)
{
    uint8_t testConfig = SECUREHEAP_TEST | ALLOCATE_TEST | READWRITE_TEST | SHA1_TEST;
    this->runTest(VIDEO_TEST,false,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, con_nonsec_videopool)
{
    uint8_t testConfig = 0x00;
    this->runTest(VIDEO_TEST,true,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, con_nonsec_videopool_alloc)
{
    uint8_t testConfig = ALLOCATE_TEST;
    this->runTest(VIDEO_TEST,true,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, con_nonsec_videopool_trim)
{
    uint8_t testConfig = ALLOCATE_TEST | TRIM_TEST;
    this->runTest(VIDEO_TEST,true,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, con_nonsec_videopool_notify)
{
    uint8_t testConfig = ALLOCATE_TEST | NOTIFY_TEST;
    this->runTest(VIDEO_TEST,true,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, con_nonsec_videopool_readwrite)
{
    uint8_t testConfig = ALLOCATE_TEST | READWRITE_TEST;
    this->runTest(VIDEO_TEST,true,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, con_nonsec_videopool_sha1)
{
    uint8_t testConfig = ALLOCATE_TEST | READWRITE_TEST | SHA1_TEST;
    this->runTest(VIDEO_TEST,true,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, con_sec_videopool)
{
    uint8_t testConfig = SECUREHEAP_TEST;
    this->runTest(VIDEO_TEST,true,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, con_sec_videopool_alloc)
{
    uint8_t testConfig = SECUREHEAP_TEST | ALLOCATE_TEST;
    this->runTest(VIDEO_TEST,true,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, con_sec_videopool_trim)
{
    uint8_t testConfig = SECUREHEAP_TEST | ALLOCATE_TEST | TRIM_TEST;
    this->runTest(VIDEO_TEST,true,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, con_sec_videopool_notify)
{
    uint8_t testConfig = SECUREHEAP_TEST | ALLOCATE_TEST | NOTIFY_TEST;
    this->runTest(VIDEO_TEST,true,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, con_sec_videopool_readwrite)
{
    uint8_t testConfig = SECUREHEAP_TEST | ALLOCATE_TEST | READWRITE_TEST;
    this->runTest(VIDEO_TEST,true,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, con_sec_videopool_sha1)
{
    uint8_t testConfig = SECUREHEAP_TEST | ALLOCATE_TEST | READWRITE_TEST | SHA1_TEST;
    this->runTest(VIDEO_TEST,true,testConfig);
}
UT_ADD_TEST(VTS_L3_AVBuffer, seq_nonsec_audio_videopool)
{
    uint8_t testConfig = 0x00;
    this->runTest((VIDEO_TEST|AUDIO_TEST),false,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, seq_nonsec_audio_videopool_alloc)
{
    uint8_t testConfig = ALLOCATE_TEST;
    this->runTest((VIDEO_TEST|AUDIO_TEST),false,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, seq_nonsec_audio_videopool_trim)
{
    uint8_t testConfig = ALLOCATE_TEST | TRIM_TEST;
    this->runTest((VIDEO_TEST|AUDIO_TEST),false,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, seq_nonsec_audio_videopool_notify)
{
    uint8_t testConfig = ALLOCATE_TEST | NOTIFY_TEST;
    this->runTest((VIDEO_TEST|AUDIO_TEST),false,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, seq_nonsec_audio_videopool_readwrite)
{
    uint8_t testConfig = ALLOCATE_TEST | READWRITE_TEST;
    this->runTest((VIDEO_TEST|AUDIO_TEST),false,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, seq_nonsec_audio_videopool_sha1)
{
    uint8_t testConfig = ALLOCATE_TEST | READWRITE_TEST | SHA1_TEST;
    this->runTest((VIDEO_TEST|AUDIO_TEST),false,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, seq_sec_audio_videopool)
{
    uint8_t testConfig = SECUREHEAP_TEST;
    this->runTest((VIDEO_TEST|AUDIO_TEST),false,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, seq_sec_audio_videopool_alloc)
{
    uint8_t testConfig = SECUREHEAP_TEST | ALLOCATE_TEST;
    this->runTest((VIDEO_TEST|AUDIO_TEST),false,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, seq_sec_audio_videopool_trim)
{
    uint8_t testConfig = SECUREHEAP_TEST | ALLOCATE_TEST | TRIM_TEST;
    this->runTest((VIDEO_TEST|AUDIO_TEST),false,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, seq_sec_audio_videopool_notify)
{
    uint8_t testConfig = SECUREHEAP_TEST | ALLOCATE_TEST | NOTIFY_TEST;
    this->runTest((VIDEO_TEST|AUDIO_TEST),false,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, seq_sec_audio_videopool_readwrite)
{
    uint8_t testConfig = SECUREHEAP_TEST | ALLOCATE_TEST | READWRITE_TEST;
    this->runTest((VIDEO_TEST|AUDIO_TEST),false,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, seq_sec_audio_videopool_sha1)
{
    uint8_t testConfig = SECUREHEAP_TEST | ALLOCATE_TEST | READWRITE_TEST | SHA1_TEST;
    this->runTest((VIDEO_TEST|AUDIO_TEST),false,testConfig);
}
UT_ADD_TEST(VTS_L3_AVBuffer, con_nonsec_audio_videopool)
{
    uint8_t testConfig = 0x00;
    this->runTest((VIDEO_TEST|AUDIO_TEST),true,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, con_nonsec_audio_videopool_alloc)
{
    uint8_t testConfig = ALLOCATE_TEST;
    this->runTest((VIDEO_TEST|AUDIO_TEST),true,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, con_nonsec_audio_videopool_trim)
{
    uint8_t testConfig = ALLOCATE_TEST | TRIM_TEST;
    this->runTest((VIDEO_TEST|AUDIO_TEST),true,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, con_nonsec_audio_videopool_notify)
{
    uint8_t testConfig = ALLOCATE_TEST | NOTIFY_TEST;
    this->runTest((VIDEO_TEST|AUDIO_TEST),true,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, con_nonsec_audio_videopool_readwrite)
{
    uint8_t testConfig = ALLOCATE_TEST | READWRITE_TEST;
    this->runTest((VIDEO_TEST|AUDIO_TEST),true,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, con_nonsec_audio_videopool_sha1)
{
    uint8_t testConfig = ALLOCATE_TEST | READWRITE_TEST | SHA1_TEST;
    this->runTest((VIDEO_TEST|AUDIO_TEST),true,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, con_sec_audio_videopool)
{
    uint8_t testConfig = SECUREHEAP_TEST;
    this->runTest((VIDEO_TEST|AUDIO_TEST),true,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, con_sec_audio_videopool_alloc)
{
    uint8_t testConfig = SECUREHEAP_TEST | ALLOCATE_TEST;
    this->runTest((VIDEO_TEST|AUDIO_TEST),true,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, con_sec_audio_videopool_trim)
{
    uint8_t testConfig = SECUREHEAP_TEST | ALLOCATE_TEST | TRIM_TEST;
    this->runTest((VIDEO_TEST|AUDIO_TEST),true,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, con_sec_audio_videopool_notify)
{
    uint8_t testConfig = SECUREHEAP_TEST | ALLOCATE_TEST | NOTIFY_TEST;
    this->runTest((VIDEO_TEST|AUDIO_TEST),true,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, con_sec_audio_videopool_readwrite)
{
    uint8_t testConfig = SECUREHEAP_TEST | ALLOCATE_TEST | READWRITE_TEST;
    this->runTest((VIDEO_TEST|AUDIO_TEST),true,testConfig);
}

UT_ADD_TEST(VTS_L3_AVBuffer, con_sec_audio_videopool_sha1)
{
    uint8_t testConfig = SECUREHEAP_TEST | ALLOCATE_TEST | READWRITE_TEST | SHA1_TEST;
    this->runTest((VIDEO_TEST|AUDIO_TEST),true,testConfig);
}
