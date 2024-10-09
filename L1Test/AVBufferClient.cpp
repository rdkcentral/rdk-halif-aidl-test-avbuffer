
/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2024 RDK Management
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

 /**
* @file AVBufferClient.cpp
* @page AVBufferClient hal Level 1 Tests
*
* ## Module's Role
* This module includes Level 1 functional tests (success and failure scenarios).
* This is to ensure that the AVBufferClient hal APIs meet the requirements across all vendors.
*
*/

#include <gtest/gtest.h>
#include <binder/IServiceManager.h>
#include <binder/ProcessState.h>
#include <binder/IPCThreadState.h>
#include <binder/Status.h>
#include <utils/String16.h>
#include <stdio.h>
#include <vector>
#include <optional>

#include "com/rdk/hal/avbuffer/IAVBuffer.h"
#include "com/rdk/hal/avbuffer/BnAVBufferSpaceListener.h"

using android::sp;
using android::String16;
using android::defaultServiceManager;
using android::binder::Status;

using namespace com::rdk::hal;
using namespace com::rdk::hal::avbuffer;
using namespace com::rdk::hal::videodecoder;
using namespace com::rdk::hal::audiodecoder;

sp<IAVBuffer> service;
sp<IAVBuffer> getService() {
    service = android::interface_cast<IAVBuffer>(
            defaultServiceManager()->getService(String16("AVBuffer")));
    if (service == nullptr) {
        printf("Service not available\n");
    }
    return service;
}

bool isValidPool(const Pool& pool) {
    return pool.handle != Pool::INVALID;
}

class AVBufferSpaceListener : public BnAVBufferSpaceListener {
public:

    AVBufferSpaceListener() {};
    virtual ~AVBufferSpaceListener() = default;

    Status onSpaceAvailable() override {
        return ::android::binder::Status::ok();
    }
};

// Suite for pool management tests
class L1AVBufferPoolManagementTestSuite : public ::testing::Test {
protected:
    void SetUp() override {
        bool secureHeap = true;
        ::com::rdk::hal::videodecoder::IVideoDecoder::Id videoDecoderId;
        videoDecoderId.value = 0;
        sp<IAVBufferSpaceListener> listener = new AVBufferSpaceListener();
        Status status = service->createVideoPool(secureHeap, videoDecoderId, listener, &mVideoPool);
        if (!status.isOk()) {
            FAIL() << "Failed to create video pool";
        }
        ASSERT_NE(mVideoPool.handle, Pool::INVALID);
    }

    void TearDown() override {
        if (mVideoPool.handle != ::com::rdk::hal::avbuffer::Pool::INVALID) {
            Status status = service->destroyPool(mVideoPool);
            ASSERT_TRUE(status.isOk());
        }
    }
    ::com::rdk::hal::avbuffer::Pool mVideoPool;
};

// Suite for buffer management tests
class L1AVBufferManagementTestSuite : public L1AVBufferPoolManagementTestSuite {
protected:
    void SetUp() override {
        L1AVBufferPoolManagementTestSuite::SetUp();
        Status status = service->alloc(mVideoPool, 1024, &mBufferHandle);
        if (!status.isOk()) {
            FAIL() << "Failed to allocate buffer";
        }
        ASSERT_NE(mBufferHandle, IAVBuffer::INVALID_HANDLE);
    }

    void TearDown() override {
        if (mBufferHandle != IAVBuffer::INVALID_HANDLE) {
            Status status = service->free(mBufferHandle);
            ASSERT_TRUE(status.isOk());
        }
        L1AVBufferPoolManagementTestSuite::TearDown();
    }
    int64_t mBufferHandle;
};

/**
* @brief Test to verify the retrieval of heap metrics for secure heap
*
* This test calls the getHeapMetrics() API with secureHeap set to true and checks if
* the returned heapMetrics indicates it is secure and the metrics for bytes used and total 
* bytes are within valid ranges.@n
*
* **Test Group ID:** Basic: 01@n
* **Test Case ID:** 001@n
* **Priority:** High@n
* @n
* **Pre-Conditions:** None@n
* **Dependencies:** None@n
* **User Interaction:** None
* @n
* **Test Procedure:**@n
* | Variation / Step | Description | Test Data | Expected Result | Notes |
* | :----: | --------- | ---------- |-------------- | ----- |
* | 01 | Invoke getHeapMetrics() with secureHeap set to true | secureHeap = true | status.isOk() and heapMetrics.secure is true, heapMetrics.bytesUsed >= 0, heapMetrics.bytesTotal > 0 | Should be successful |
*/
TEST(L1AVBufferTestSuite,test_l1_avbuffer_positive_1_getHeapMetrics) 
{
    bool secureHeap = true;
    ::com::rdk::hal::avbuffer::HeapMetrics heapMetrics;
    ::android::binder::Status status = service->getHeapMetrics(secureHeap, &heapMetrics);

    EXPECT_TRUE(status.isOk());
    EXPECT_TRUE(heapMetrics.secure);
    EXPECT_GE(heapMetrics.bytesUsed, 0);
    EXPECT_GT(heapMetrics.bytesTotal, 0);
}

/**
* @brief Test the getHeapMetrics function in L1 AV Buffer Test Suite with positive scenario 2
*
* This test aims to verify that the getHeapMetrics function correctly provides the heap metrics when the secureHeap parameter is set to false. It confirms that the function executes without errors, and the returned metrics are valid.
*
* **Test Group ID:** Basic: 01@n
* **Test Case ID:** 002@n
* **Priority:** High@n
* @n
* **Pre-Conditions:** None@n
* **Dependencies:** None@n
* **User Interaction:** None
* @n
* **Test Procedure:**@n
* | Variation / Step | Description | Test Data | Expected Result | Notes |
* | :----: | --------- | ---------- |-------------- | ----- |
* | 01 | Initialize secureHeap variable and declare heapMetrics object | secureHeap = false | secureHeap is initialized to false, heapMetrics object is declared | Initialization should be successful |
* | 02 | Call the getHeapMetrics function with valid values | secureHeap = false, heapMetrics = valid| The function call returns a status and heapMetrics object | The call should be successful without errors |
* | 03 | Check the status of getHeapMetrics | N/A | status.isOk() should be true | Status should be ok |
* | 04 | Validate the secure property of heapMetrics | N/A | heapMetrics.secure should be false | Heap metrics secure property should be false |
* | 05 | Validate the bytesUsed property of heapMetrics | N/A | heapMetrics.bytesUsed >= 0 | bytesUsed should be non-negative |
* | 06 | Validate the bytesTotal property of heapMetrics | N/A | heapMetrics.bytesTotal > 0 | bytesTotal should be positive |
*/
TEST(L1AVBufferTestSuite,test_l1_avbuffer_positive_2_getHeapMetrics) 
{
    bool secureHeap = false;
    ::com::rdk::hal::avbuffer::HeapMetrics heapMetrics;
    ::android::binder::Status status = service->getHeapMetrics(secureHeap, &heapMetrics);

    EXPECT_TRUE(status.isOk());
    EXPECT_FALSE(heapMetrics.secure);
    EXPECT_GE(heapMetrics.bytesUsed, 0);
    EXPECT_GT(heapMetrics.bytesTotal, 0);
}

/**
* @brief Test the behavior of createAudioPool API during the creation of an audio pool with valid secure heap and audioDecoderId value
*
* This test verifies the creation of an audio pool using a secure heap in an audio buffer service. It ensures that the audio pool is created successfully and the handle received is valid. Additionally, the test validates the status returned by the service method. Finally, the created pool is destroyed to clean up.@n
*
* **Test Group ID:** Basic: 01@n
* **Test Case ID:** 003@n
* **Priority:** High@n
* @n
* **Pre-Conditions:** None@n
* **Dependencies:** None@n
* **User Interaction:** None
* @n
* **Test Procedure:**@n
* | Variation / Step | Description | Test Data | Expected Result | Notes |
* | :----: | --------- | ---------- |-------------- | ----- |
* | 01 | Invoke createAudioPool with valid values | secureHeap = true, audioDecoderId.value = 0 | Status should be OK | Should be successful |
* | 02 | Validate output pool handle | outputPool.handle | Should be valid and not equal to Pool::INVALID | Should be successful |
* | 03 | Destroy created audio pool | outputPool | Status should be OK | Clean up step |
*/
TEST(L1AVBufferTestSuite, test_l1_avbuffer_positive_1_createAudioPool) 
{
    bool secureHeap = true;
    ::com::rdk::hal::audiodecoder::IAudioDecoder::Id audioDecoderId;
    audioDecoderId.value = 0; // Assuming 0 is a valid decoder ID
    sp<IAVBufferSpaceListener> listener = new AVBufferSpaceListener();
    ::com::rdk::hal::avbuffer::Pool outputPool;
    ::android::binder::Status status = service->createAudioPool(secureHeap, audioDecoderId, listener, &outputPool);

    EXPECT_TRUE(status.isOk());
    EXPECT_NE(outputPool.handle, Pool::INVALID);
    //cleaning up
    service->destroyPool(outputPool);
}

/**
* @brief Test to verify behavior createAudioPool API during the creation of audio pool
*
* This test verifies that the audio pool is successfully created using a 
* specified audio decoder without secure heap.@n
*
* **Test Group ID:** Basic: 01@n
* **Test Case ID:** 004@n
* **Priority:** High@n
* @n
* **Pre-Conditions:** None@n
* **Dependencies:** None@n
* **User Interaction:** None
* @n
* **Test Procedure:**@n
* | Variation / Step | Description | Test Data | Expected Result | Notes |
* |:----------------:|-----------|--------------|---------|--------------|
* | 01 | Set secureHeap to false | secureHeap = false | | Should be successful|
* | 02 | Initialize audioDecoderId  | audioDecoderId.value = 0 | | Should be successful|
* | 03 | Instantiate listener | listener = new AVBufferSpaceListener() |  | Should be successful|
* | 04 | Declare outputPool | | outputPool = valid |Should be successful|
* | 05 | Call createAudioPool| secureHeap, audioDecoderId, listener, &outputPool | status should be Ok, outputPool.handle should not be INVALID | Validation of success |
* | 06 | Cleanup by destroying pool | outputPool|Pool destroyed without errors| Should be successful|
*/
TEST(L1AVBufferTestSuite, test_l1_avbuffer_positive_2_createAudioPool) 
{
    bool secureHeap = false;
    ::com::rdk::hal::audiodecoder::IAudioDecoder::Id audioDecoderId;
    audioDecoderId.value = 0; // Assuming 1 is a valid decoder ID
    sp<IAVBufferSpaceListener> listener = new AVBufferSpaceListener();
    ::com::rdk::hal::avbuffer::Pool outputPool;
    ::android::binder::Status status = service->createAudioPool(secureHeap, audioDecoderId, listener, &outputPool);

    EXPECT_TRUE(status.isOk());
    EXPECT_NE(outputPool.handle, Pool::INVALID);
    //cleaning up
    service->destroyPool(outputPool);
}

/**
* @brief Verifies that createAudioPool rejects invalid audio decoder IDs 
*
* Tests if the createAudioPool API correctly handles an invalid audio decoder ID by rejecting it with an appropriate exception code@n
*
* **Test Group ID:** Basic: 01@n
* **Test Case ID:** 005@n
* **Priority:** High@n
* @n
* **Pre-Conditions:** None@n
* **Dependencies:** None@n
* **User Interaction:** None
* @n
* **Test Procedure:**@n
* | Variation / Step | Description | Test Data | Expected Result | Notes |
* | :----: | --------- | ---------- |-------------- | ----- |
* | 01 | Set up test variables and input data | secureHeap = true, audioDecoderId.value = -2, listener = new AVBufferSpaceListener() | Variables set | Should be successful |
* | 02 | Invoke createAudioPool API | secureHeap = true, audioDecoderId = {value = -2}, listener = new AVBufferSpaceListener(), &outputPool | API is invoked | Should Fail|
* | 03 | Check for expected exception | status.exceptionCode() | ::android::binder::Status::EX_ILLEGAL_ARGUMENT | Exception matches expected result |
* | 04 | Check for invalid pool handle | outputPool.handle | Pool::INVALID | Pool handle should be marked invalid |
*/
TEST(L1AVBufferTestSuite, test_l1_avbuffer_negative_1_createAudioPool) 
{
    bool secureHeap = true;
    ::com::rdk::hal::audiodecoder::IAudioDecoder::Id audioDecoderId;
    audioDecoderId.value = -2; // Invalid decoder ID
    sp<IAVBufferSpaceListener> listener = new AVBufferSpaceListener();
    ::com::rdk::hal::avbuffer::Pool outputPool;
    ::android::binder::Status status = service->createAudioPool(secureHeap, audioDecoderId, listener, &outputPool);

    EXPECT_EQ(status.exceptionCode(), ::android::binder::Status::EX_ILLEGAL_ARGUMENT);
    EXPECT_EQ(outputPool.handle, Pool::INVALID);
}

/**
* @brief Test for the negative case of creating an audio pool with an undefined audio decoder ID.
*
* This test is designed to verify the behavior of the createAudioPool function when an undefined audio decoder ID is used.
* The expected outcome is that the function should return an illegal argument exception code, and the pool handle should be invalid.@n
*
* **Test Group ID:** Basic: 01@n
* **Test Case ID:** 006@n
* **Priority:** High@n
* @n
* **Pre-Conditions:** None@n
* **Dependencies:** None@n
* **User Interaction:** None
* @n
* **Test Procedure:**@n
* | Variation / Step | Description | Test Data |Expected Result |Notes |
* | :----: | --------- | ---------- |-------------- | ----- |
* | 01 | Invoke createAudioPool with undefined audioDecoderId | secureHeap = false, audioDecoderId = UNDEFINED, listener = AVBufferSpaceListener, outputPool = valid buffer | status.exceptionCode() = EX_ILLEGAL_ARGUMENT, outputPool.handle = INVALID | Should Fail |
*/
TEST(L1AVBufferTestSuite, test_l1_avbuffer_negative_2_createAudioPool) 
{
    bool secureHeap = false;
    ::com::rdk::hal::audiodecoder::IAudioDecoder::Id audioDecoderId;
    audioDecoderId.value = ::com::rdk::hal::audiodecoder::IAudioDecoder::Id::UNDEFINED; // Undefined decoder ID
    sp<IAVBufferSpaceListener> listener = new AVBufferSpaceListener();
    ::com::rdk::hal::avbuffer::Pool outputPool;
    ::android::binder::Status status = service->createAudioPool(secureHeap, audioDecoderId, listener, &outputPool);

    EXPECT_EQ(status.exceptionCode(), ::android::binder::Status::EX_ILLEGAL_ARGUMENT);
    EXPECT_EQ(outputPool.handle, Pool::INVALID);
}

/**
* @brief Test case for creating a video pool with positive parameters
*
* This test checks the createVideoPool functionality in a positive scenario where secureHeap is true and a valid videoDecoderId is given@n
*
* **Test Group ID:** Basic: 01@n
* **Test Case ID:** 007@n
* **Priority:** High@n
* @n
* **Pre-Conditions:** None@n
* **Dependencies:** None@n
* **User Interaction:** None
* @n
* **Test Procedure:**@n
* | Variation / Step | Description | Test Data |Expected Result |Notes |
* | :----: | --------- | ---------- |-------------- | ----- |
* | 01 | Setup listener for AV buffer | listener = new AVBufferSpaceListener() | Listener should be correctly created |  |
* | 02 | Setup secureHeap and videoDecoderId | secureHeap = true, videoDecoderId = 0 | secureHeap and videoDecoderId should be set |  |
* | 03 | Invoke createVideoPool API with valid values |secureHeap = true, videoDecoderId = 0, listener = valid, outputVideoPool = valid buffer | Pool should be created successfully | Should be Successful|
* | 04 | Validate creation status | N/A | status.isOk() should be true | Ensure function returns success |
* | 05 | Validate created pool | N/A | isValidPool(outputVideoPool) should be true | Pool should be valid |
* | 06 | Clean up created pool | N/A | service->destroyPool(outputVideoPool) should execute without errors | Clean-up must be successful |
*/
TEST(L1AVBufferTestSuite, test_l1_avbuffer_positive_1_createVideoPool) 
{
    bool secureHeap = true;
    ::com::rdk::hal::videodecoder::IVideoDecoder::Id videoDecoderId;
    videoDecoderId.value = 0;
    sp<IAVBufferSpaceListener> listener = new AVBufferSpaceListener();
    ::com::rdk::hal::avbuffer::Pool outputVideoPool;
    Status status = service->createVideoPool(secureHeap, videoDecoderId, listener, &outputVideoPool);

    EXPECT_TRUE(status.isOk()) << "Failed to create video pool";
    EXPECT_TRUE(isValidPool(outputVideoPool)) << "Created pool is not valid";
    //cleaning up
    service->destroyPool(outputVideoPool);
}

/**
* @brief Verifies the behavior of createVideoPool API when it is invoked with valid inputs.
*
* This test checks if the video pool can be successfully created with given 
* inputs and ensures that the created pool is valid. It also cleans up by 
* destroying the created pool at the end.
*
* **Test Group ID:** Basic: 01@n
* **Test Case ID:** 008@n
* **Priority:** High@n
* @n
* **Pre-Conditions:** None@n
* **Dependencies:** None@n
* **User Interaction:** None
* @n
* **Test Procedure:**@n
* | Variation / Step | Description | Test Data | Expected Result | Notes |
* | :----: | --------- | ---------- |-------------- | ----- |
* | 01 | Initialize variables and setup | secureHeap = false, videoDecoderId.value = 0, listener = new AVBufferSpaceListener() | Variables initialized | Should be successful |
* | 02 | Invoke the createVideoPool with valid inputs | secureHeap = false, videoDecoderId = 0, listener valid, outputVideoPool = valid | Status should be OK | Successfully created video pool |
* | 03 | Verify created video pool is valid | outputVideoPool | Pool should be valid | Created pool is valid |
* | 04 | Destroy created video pool | outputVideoPool | Pool should be destroyed | Successfully cleaned up |
*/
TEST(L1AVBufferTestSuite, test_l1_avbuffer_positive_2_createVideoPool) 
{
    bool secureHeap = false;
    ::com::rdk::hal::videodecoder::IVideoDecoder::Id videoDecoderId;
    videoDecoderId.value = 0;
    sp<IAVBufferSpaceListener> listener = new AVBufferSpaceListener();
    ::com::rdk::hal::avbuffer::Pool outputVideoPool;
    Status status = service->createVideoPool(secureHeap, videoDecoderId, listener, &outputVideoPool);

    EXPECT_TRUE(status.isOk()) << "Failed to create video pool";
    EXPECT_TRUE(isValidPool(outputVideoPool)) << "Created pool is not valid";
    //cleaning up
    service->destroyPool(outputVideoPool);
}

/**
* @brief Test the creation of a video pool with an undefined video decoder ID.
*
* This test verifies the behavior of the `createVideoPool` function when called with an undefined video decoder ID. 
* The function is expected to return an illegal argument exception and the created video pool should be invalid.@n
*
* **Test Group ID:** Basic: 01@n
* **Test Case ID:** 009@n
* **Priority:** High@n
* @n
* **Pre-Conditions:** None@n
* **Dependencies:** None@n
* **User Interaction:** None
* @n
* **Test Procedure:**@n
* | Variation / Step | Description | Test Data | Expected Result | Notes |
* | :----: | --------- | ---------- |-------------- | ----- |
* | 01 | Attempt to create a video pool with an undefined decoder ID | secureHeap = true, videoDecoderId = UNDEFINED, listener = new AVBufferSpaceListener() | Status exception code should be EX_ILLEGAL_ARGUMENT, outputVideoPool should be invalid | Should Fail |
*/
TEST(L1AVBufferTestSuite, test_l1_avbuffer_negative_1_createVideoPool) 
{
    bool secureHeap = true;
    ::com::rdk::hal::videodecoder::IVideoDecoder::Id videoDecoderId;
    videoDecoderId.value = ::com::rdk::hal::videodecoder::IVideoDecoder::Id::UNDEFINED; // Undefined decoder ID
    sp<IAVBufferSpaceListener> listener = new AVBufferSpaceListener();
    ::com::rdk::hal::avbuffer::Pool outputVideoPool;
    Status status = service->createVideoPool(secureHeap, videoDecoderId, listener, &outputVideoPool);

    EXPECT_EQ(status.exceptionCode(), ::android::binder::Status::EX_ILLEGAL_ARGUMENT);
    EXPECT_FALSE(isValidPool(outputVideoPool));
}

/**
* @brief Test the destroy functionality with an invalid pool handle
*
* This test validates that the destroyPool function properly handles the case where an invalid pool handle is passed. The expectation is that an illegal argument error is returned, demonstrating the robustness of the destroyPool function in handling erroneous inputs.@n
*
* **Test Group ID:** Basic: 01@n
* **Test Case ID:** 010@n
* **Priority:** High@n
* @n
* **Pre-Conditions:** None@n
* **Dependencies:** None@n
* **User Interaction:** None
* @n
* **Test Procedure:**@n
* | Variation / Step | Description | Test Data |Expected Result |Notes |
* | :----: | --------- | ---------- |-------------- | ----- |
* | 01 | Create a pool handle and set it to invalid | poolHandle.handle = Pool::INVALID | | Should be successful |
* | 02 | Call destroyPool with invalid handle | poolHandle.handle = Pool::INVALID | EX_ILLEGAL_ARGUMENT | Should Fail |
* | 03 | Validate the error code | status.serviceSpecificErrorCode() | Should match EX_ILLEGAL_ARGUMENT | Should be successful |
*/
TEST(L1AVBufferTestSuite, test_l1_avbuffer_negative_destroyPool) 
{
    ::com::rdk::hal::avbuffer::Pool poolHandle;
    poolHandle.handle = ::com::rdk::hal::avbuffer::Pool::INVALID;
    Status status = service->destroyPool(poolHandle);

    EXPECT_EQ(::android::binder::Status::EX_ILLEGAL_ARGUMENT, status.serviceSpecificErrorCode());
}

/**
* @brief Test to verify the successful destruction of a video buffer pool
*
* This test checks the functionality of the destroyPool method to ensure that it correctly destroys a previously created video buffer pool when provided with a valid pool handle.@n
*
* **Test Group ID:** Basic: 01@n
* **Test Case ID:** 011@n
* **Priority:** High@n
* @n
* **Pre-Conditions:** None@n
* **Dependencies:** None@n
* **User Interaction:** None
* @n
* **Test Procedure:**@n
* | Variation / Step | Description | Test Data | Expected Result | Notes |
* |:----------------:|-------------|-----------|-----------------|-------|
* | 01 | Setup initial conditions and invoke createVideoPool with valid values| secureHeap = false, videoDecoderId.value = 0, listener = new AVBufferSpaceListener(), poolHandle = &poolHandle | Pool should be created successfully | Should be successful |
* | 02 | Call destroyPool with valid pool handle | poolHandle = poolHandle | Pool should be destroyed successfully | Should be successful |
*/
TEST(L1AVBufferTestSuite, test_l1_avbuffer_positive_destroyPool) 
{
    bool secureHeap = false;
    ::com::rdk::hal::videodecoder::IVideoDecoder::Id videoDecoderId;
    videoDecoderId.value = 0;
    sp<IAVBufferSpaceListener> listener = new AVBufferSpaceListener();
    ::com::rdk::hal::avbuffer::Pool poolHandle;
    Status status = service->createVideoPool(secureHeap, videoDecoderId, listener, &poolHandle);
    // Call the destroyPool method with the valid pool handle
    status = service->destroyPool(poolHandle);

    EXPECT_TRUE(status.isOk());
}

/**
* @brief Verifies retrieval of pool metrics for a valid video buffer pool
*
* This test checks whether the getPoolMetrics function successfully retrieves metrics for a created video buffer pool by evaluating the correctness of the returned metrics.
*
* **Test Group ID:** Basic: 01@n
* **Test Case ID:** 012@n
* **Priority:** High@n
* @n
* **Pre-Conditions:** L1AVBufferPoolManagementTestSuite@n
* **Dependencies:** None@n
* **User Interaction:** None
* @n
* **Test Procedure:**@n
* | Variation / Step | Description | Test Data | Expected Result | Notes |
* | :----: | --------- | ---------- |-------------- | ----- |
* | 01 | Setup pre-requisite, create video pool | secureHeap = true, videoDecoderId.value = 0, listener = AVBufferSpaceListener() | Video pool created successfully | Should be successful |
* | 02 | Retrieve metrics for pool | mVideoPool = valid, poolMetrics = valid  | Status isOk() = true, metrics retrieved correctly | Should be successful |
* | 03 | Verify pool handle matches | poolMetrics.poolHandle.handle = mVideoPool.handle | poolHandle matches | Should be successful |
* | 04 | Verify bytes used is populated | poolMetrics.bytesUsed | bytesUsed >= 0 | Should be successful |
* | 05 | Verify total bytes is populated | poolMetrics.bytesTotal | bytesTotal > 0 | Should be successful |
* | 06 | Tear down post-requisite, destroy video pool | None | Video pool destroyed successfully | Should be successful |
*/
TEST_F(L1AVBufferPoolManagementTestSuite, test_l1_avbuffer_positive_1_getPoolMetrics) 
{
    ::com::rdk::hal::avbuffer::PoolMetrics poolMetrics;
    Status status = service->getPoolMetrics(mVideoPool, &poolMetrics);

    EXPECT_EQ(status.isOk(), true);
    EXPECT_EQ(poolMetrics.poolHandle.handle, mVideoPool.handle);
    EXPECT_GE(poolMetrics.bytesUsed, 0);  // Ensure bytesUsed is populated
    EXPECT_GT(poolMetrics.bytesTotal, 0);  // Ensure bytesTotal is populated
}

/**
* @brief Test for handling invalid pool metrics
*
* This test checks if the function appropriately handles an invalid pool handle condition by invoking getPoolMetrics with an invalid pool handle and verifying the correct exception is raised.
*
* **Test Group ID:** Basic: 01@n
* **Test Case ID:** 013@n
* **Priority:** High@n
* @n
* **Pre-Conditions:** None@n
* **Dependencies:** None@n
* **User Interaction:** None
* @n
* **Test Procedure:**@n
* | Variation / Step | Description | Test Data | Expected Result |Notes |
* | :----: | --------- | ---------- |-------------- | ----- |
* | 01 | Initialize pool handle with INVALID and call getPoolMetrics | poolHandle.handle = INVALID | Exception code should be EX_ILLEGAL_ARGUMENT | Should Fail |
*/
TEST(L1AVBufferTestSuite, test_l1_avbuffer_negative_1_getPoolMetrics) 
{
    ::com::rdk::hal::avbuffer::Pool poolHandle;
    poolHandle.handle = ::com::rdk::hal::avbuffer::Pool::INVALID;
    ::com::rdk::hal::avbuffer::PoolMetrics poolMetrics;
    Status status = service->getPoolMetrics(poolHandle, &poolMetrics);

    EXPECT_EQ(status.exceptionCode(), ::android::binder::Status::EX_ILLEGAL_ARGUMENT);
}

/**
* @brief Tests the retrieval of all AV buffer pool metrics
*
* Verifies that the method `getAllPoolMetrics` correctly retrieves metrics for all buffer pools when a secure heap is used@n
*
* **Test Group ID:** Basic: 01@n
* **Test Case ID:** 014@n
* **Priority:** High@n
* @n
* **Pre-Conditions:** None@n
* **Dependencies:** None@n
* **User Interaction:** None
* @n
* **Test Procedure:**@n
* | Variation / Step | Description | Test Data | Expected Result |Notes |
* | :----: | --------- | ---------- |-------------- | ----- |
* | 01 | Initialize secure heap flag | secureHeap = true | Initial setup completed | Should be successful |
* | 02 | Invoke getAllPoolMetrics with secureHeap and empty poolList | secureHeap = true, poolList = empty | Call executes without error | Should be successful |
* | 03 | Check status is OK | status object from getAllPoolMetrics call | status.isOk() should be true | Should be successful |
* | 04 | Verify each poolMetrics in poolList | Each element of poolList | poolMetrics.poolHandle.handle != Pool::INVALID and poolMetrics.bytesUsed, poolMetrics.bytesTotal >= 0 | Should be successful |
* | 05 | Validate total bytes against used bytes | Each element of poolList | poolMetrics.bytesTotal >= poolMetrics.bytesUsed | Should be successful |
*/
TEST(L1AVBufferTestSuite, test_l1_avbuffer_positive_1_getAllPoolMetrics) 
{
    bool secureHeap = true;
    std::vector<::com::rdk::hal::avbuffer::PoolMetrics> poolList;
    ::android::binder::Status status = service->getAllPoolMetrics(secureHeap, &poolList);

    ASSERT_TRUE(status.isOk());
    for (const auto& poolMetrics : poolList) 
    {
        ASSERT_NE(poolMetrics.poolHandle.handle, Pool::INVALID);
        ASSERT_GE(poolMetrics.bytesUsed, 0);
        ASSERT_GE(poolMetrics.bytesTotal, 0);
        ASSERT_GE(poolMetrics.bytesTotal, poolMetrics.bytesUsed);
    }
}

/**
* @brief Tests the retrieval of all pool metrics from the service using getAllPoolMetrics API.
*
* This test case verifies that the service can successfully return pool metrics when queried. It ensures that the metrics 
* values make logical sense.@n
*
* **Test Group ID:** Basic: 01@n
* **Test Case ID:** 015@n
* **Priority:** High@n
* @n
* **Pre-Conditions:** None@n
* **Dependencies:** None@n
* **User Interaction:** None
* @n
* **Test Procedure:**@n
* | Variation / Step | Description | Test Data |Expected Result |Notes |
* | :----: | --------- | ---------- |-------------- | ----- |
* | 01 | Initialize variables, secureHeap is set to false, and inovke the getAllPoolMetrics API with valid values  | secureHeap = false | Status OK | Should be successful |
* | 02 | Validate the pool handle is valid | None | poolMetrics.poolHandle.handle != Pool::INVALID | Should be successful |
* | 03 | Ensure bytesUsed is non-negative | None | poolMetrics.bytesUsed >= 0 | Should be successful |
* | 04 | Ensure bytesTotal is non-negative | None | poolMetrics.bytesTotal >= 0 | Should be successful |
* | 05 | Check total bytes are greater than or equal to used bytes | None | poolMetrics.bytesTotal >= poolMetrics.bytesUsed | Should be successful |
*/
TEST(L1AVBufferTestSuite, test_l1_avbuffer_positive_2_getAllPoolMetrics) 
{
    bool secureHeap = false;
    std::vector<::com::rdk::hal::avbuffer::PoolMetrics> _aidl_return;
    ::android::binder::Status status = service->getAllPoolMetrics(secureHeap, &_aidl_return);

    ASSERT_TRUE(status.isOk());
    for (const auto& poolMetrics : _aidl_return) 
    {
        ASSERT_NE(poolMetrics.poolHandle.handle, Pool::INVALID);
        ASSERT_GE(poolMetrics.bytesUsed, 0);
        ASSERT_GE(poolMetrics.bytesTotal, 0);
        ASSERT_GE(poolMetrics.bytesTotal, poolMetrics.bytesUsed);
    }
}

/**
* @brief Test positive scenario of allocating buffer in the video pool
*
* This test verifies buffer allocation in the video pool with a valid size input.@n
*
* **Test Group ID:** Basic: 01@n
* **Test Case ID:** 016@n
* **Priority:** High@n
* @n
* **Pre-Conditions:** 
* L1AVBufferPoolManagementTestSuite@n
* **Dependencies:** 
* None@n
* **User Interaction:** None
* @n
* **Test Procedure:**@n
* | Variation / Step | Description | Test Data | Expected Result | Notes |
* | :--------------: | ----------- | --------- | --------------- | ----- |
* | 01 | Setup function creating video pool | secureHeap = true, videoDecoderId.value = 0,  listener = new AVBufferSpaceListener() | Successful creation of video pool | Setup function |
* | 02 | Allocating buffer in the created video pool with valid size | mVideoPool = valid, size = 1024, _aidl_return = valid | Allocation status is OK, _aidl_return >= 0 | Should be successful |
* | 03 | Teardown function destroying video pool | None | Successful destruction of video pool | Teardown function |
*/
TEST_F(L1AVBufferPoolManagementTestSuite, test_l1_avbuffer_positive_1_alloc) 
{
    int32_t size = 1024; // Valid size
    int64_t _aidl_return;
    ::android::binder::Status status = service->alloc(mVideoPool, size, &_aidl_return);

    ASSERT_TRUE(status.isOk());
    ASSERT_GE(_aidl_return, 0);
}

/**
* @brief This test ensures that the allocation function fails with an invalid pool handle.
*
* The objective of this test is to verify that the allocation function correctly handles an invalid pool handle@n
*
* **Test Group ID:** Basic: 01@n
* **Test Case ID:** 017@n
* **Priority:** High@n
* @n
* **Pre-Conditions:** None@n
* **Dependencies:** None@n
* **User Interaction:** None
* @n
* **Test Procedure:**@n
* | Variation / Step | Description | Test Data | Expected Result | Notes |
* | :----: | --------- | ---------- |-------------- | ----- |
* | 01 | Initializing Pool with invalid handle | pool.handle = Pool::INVALID, size = 1024 | Pool handle is invalid | Should be successful |
* | 02 | Allocating memory with invalid pool handle | Allocating with invalid pool handle and size 1024 | Status should not be ok, exception code should be EX_ILLEGAL_ARGUMENT | Should Fail |
*/
TEST(L1AVBufferTestSuite, test_l1_avbuffer_negative_1_alloc) 
{
    ::com::rdk::hal::avbuffer::Pool pool;
    pool.handle = ::com::rdk::hal::avbuffer::Pool::INVALID; // Invalid handle
    int32_t size = 1024; // Valid size
    int64_t _aidl_return;
    ::android::binder::Status status = service->alloc(pool, size, &_aidl_return);

    ASSERT_FALSE(status.isOk());
    ASSERT_EQ(status.exceptionCode(),  ::android::binder::Status::EX_ILLEGAL_ARGUMENT);
}

/**
* @brief Test for negative allocation in AV buffer pool
*
* This test verifies the behavior of the allocation function in the AV buffer pool when given an invalid size input.@n
*
* **Test Group ID:** Basic (for L1): 01@n
* **Test Case ID:** 018@n
* **Priority:** High@n
* @n
* **Pre-Conditions:** L1AVBufferPoolManagementTestSuite@n
* **Dependencies:** None@n
* **User Interaction:** None
* @n
* **Test Procedure:**@n
* | Variation / Step | Description | Test Data | Expected Result | Notes |
* | :----: | --------- | ---------- |-------------- | ----- |
* | 01 | Set up the test environment | bool secureHeap = true, videoDecoderId.value = 0, listener = new AVBufferSpaceListener() | Video pool should be created | Should be successful |
* | 02 | Check that the video pool handle is valid | None | Video pool handle should not be invalid | Should be successful |
* | 03 | Execute alloc function with invalid size | size = 0 | Status should return not OK and exception code should be EX_ILLEGAL_ARGUMENT | Should Fail |
* | 04 | Tear down the test environment | None | Video pool should be destroyed | Should be successful |
*/
TEST_F(L1AVBufferPoolManagementTestSuite, test_l1_avbuffer_negative_2_alloc) 
{
    int32_t size = 0; // Invalid size
    int64_t _aidl_return;
    ::android::binder::Status status = service->alloc(mVideoPool, size, &_aidl_return);

    ASSERT_FALSE(status.isOk());
    ASSERT_EQ(status.exceptionCode(),  ::android::binder::Status::EX_ILLEGAL_ARGUMENT);
}

/**
* @brief Validates allocation with negative size
*
* This test validates the behavior of the allocation function when provided with a negative buffer size. This checks whether the function correctly handles invalid arguments and returns appropriate error codes.
*
* **Test Group ID:** Basic: 01@n
* **Test Case ID:** 019@n
* **Priority:** High@n
* @n
* **Pre-Conditions:** L1AVBufferPoolManagementTestSuite@n
* **Dependencies:** None@n
* **User Interaction:** None
* @n
* **Test Procedure:**@n
* | Variation / Step | Description | Test Data | Expected Result | Notes |
* | :----: | --------- | ---------- |-------------- | ----- |
* | 01 | Set up the test environment by creating the video pool | secureHeap=true, videoDecoderId=0, listener=AVBufferSpaceListener() | Should successfully create video pool | Should be successful |
* | 02 | Attempt to allocate a buffer with a negative size | mVideoPool = valid, size=-1024, _aidl_return = valid  | EX_ILLEGAL_ARGUMENT | Should Fail |
* | 03 | Tear down the test environment by destroying the pool | mVideoPool | Should successfully destroy the video pool | Should be successful |
*/
TEST_F(L1AVBufferPoolManagementTestSuite, test_l1_avbuffer_negative_3_alloc) 
{
    int32_t size = -1024; // Invalid size
    int64_t _aidl_return;
    ::android::binder::Status status = service->alloc(mVideoPool, size, &_aidl_return);

    ASSERT_FALSE(status.isOk());
    ASSERT_EQ(status.exceptionCode(),  ::android::binder::Status::EX_ILLEGAL_ARGUMENT);
}

/**
* @brief Test to check allocation of excessively large memory in the buffer pool.
*
* This test verifies that attempting to allocate an excessively large memory block@n
* from a video pool fails as expected with appropriate error status.
*
* **Test Group ID:** Basic: 01@n
* **Test Case ID:** 020@n
* **Priority:** High@n
* @n
* **Pre-Conditions:** L1AVBufferPoolManagementTestSuite@n
* **Dependencies:** None@n
* **User Interaction:** None
* @n
* **Test Procedure:**@n
* | Variation / Step | Description | Test Data | Expected Result | Notes |
* | :--------------: | ----------- | --------- | --------------- | ----- |
* | 01 | Setup Initial Conditions | secureHeap = true, videoDecoderId.value = 0, listener = new AVBufferSpaceListener() | mVideoPool is created successfully | Should be successful |
* | 02 | Initiate excessive memory allocation request | mVideoPool = valid, size = 99999999999, _aidl_return = valid  | status not ok and EX_SERVICE_SPECIFIC| Should Fail |
* | 03 | Destroy the pool | mVideoPool.handle != Pool::INVALID | Pool destroyed successfully | Should be successful |
*/
TEST_F(L1AVBufferPoolManagementTestSuite, test_l1_avbuffer_negative_4_alloc) 
{
    int32_t size = 99999999999; // Size exceeds available memory
    int64_t _aidl_return;
    ::android::binder::Status status = service->alloc(mVideoPool, size, &_aidl_return);

    ASSERT_FALSE(status.isOk());
    ASSERT_EQ(status.exceptionCode(),  ::android::binder::Status::EX_SERVICE_SPECIFIC);
}

/**
* @brief Test the `notifyWhenSpaceAvailable` function with an invalid handle
*
* Tests the scenario where the `notifyWhenSpaceAvailable` function is called using an invalid pool handle@n
*
* **Test Group ID:** Basic: 01@n
* **Test Case ID:** 021@n
* **Priority:** High@n
* @n
* **Pre-Conditions:** None@n
* **Dependencies:** None@n
* **User Interaction:** None
* @n
* **Test Procedure:**@n
* | Variation / Step | Description | Test Data |Expected Result |Notes |
* | :----: | --------- | ---------- |-------------- | ----- |
* | 01 | Initial pool setup and invalid handle assignment | pool.handle = INVALID |  |  |
* | 02 | Call notifyWhenSpaceAvailable with invalid handle | size = 100 | status.isOk() should return false | Should be unsuccessful |
*/
TEST(L1AVBufferTestSuite, test_l1_avbuffer_negative_1_notifyWhenSpaceAvailable) 
{
    ::com::rdk::hal::avbuffer::Pool pool;
    pool.handle = ::com::rdk::hal::avbuffer::Pool::INVALID; // Invalid handle
    int32_t size = 100;
    ::android::binder::Status status = service->notifyWhenSpaceAvailable(pool, size);

    ASSERT_FALSE(status.isOk());
}

/**
* @brief Test for notifyWhenSpaceAvailable function in L1AVBufferPoolManagementTestSuite.
*
* The test checks the behavior of the function notifyWhenSpaceAvailable when passing a zero size parameter. It is expected 
* that the function will not succeed, validating error handling in the API for invalid input size.
*
* **Test Group ID:** Basic: 01@n
* **Test Case ID:** 022@n
* **Priority:** High@n
* @n
* **Pre-Conditions:** L1AVBufferPoolManagementTestSuite@n
* **Dependencies:** None@n
* **User Interaction:** None
* @n
* **Test Procedure:**@n
* | Variation / Step | Description | Test Data |Expected Result |Notes |
* | :----: | --------- | ---------- |-------------- | ----- |
* | 01 | Setup video pool | secureHeap = true, videoDecoderId = 0, listener = AVBufferSpaceListener() | Video pool created successfully | Should be successful |
* | 02 | Call notifyWhenSpaceAvailable with zero size | mVideoPool, size = 0, _aidl_return = valid | notifyWhenSpaceAvailable should not be OK | Should fail |
* | 03 | Destroy video pool | mVideoPool | Video pool destroyed successfully | Should be successful |
*/
TEST_F(L1AVBufferPoolManagementTestSuite, test_l1_avbuffer_negative_2_notifyWhenSpaceAvailable) 
{
    int32_t size = 0;
    ::android::binder::Status status = service->notifyWhenSpaceAvailable(mVideoPool, size);

    ASSERT_FALSE(status.isOk());
}

/**
* @brief Test to validate trimming of buffer size.
*
* This test checks whether the trimSize API can successfully trim the buffer size as requested.
*
* **Test Group ID:** Basic: 01@n
* **Test Case ID:** 023@n
* **Priority:** High@n
* @n
* **Pre-Conditions:** L1AVBufferManagementTestSuite, L1AVBufferPoolManagementTestSuite@n
* **Dependencies:** None@n
* **User Interaction:** None
* @n
* **Test Procedure:**@n
* | Variation / Step | Description | Test Data |Expected Result |Notes |
* | :----: | --------- | ---------- |-------------- | ----- |
* | 01 | SetUp function from L1AVBufferPoolManagementTestSuite | secureHeap = true, videoDecoderId.value = 0, listener = new AVBufferSpaceListener() | mVideoPool should be created successfully | Should be successful |
* | 02 | Allocating buffer in SetUp | mVideoPool = valid, size = 1024, mBufferHandle =valid | mBufferHandle should be valid and not equal to IAVBuffer::INVALID_HANDLE | Should be successful |
* | 03 | Trimming buffer size | mBufferHandle = valid, newSize = 1024 | status.isOk() should be true | Should be successful |
* | 04 | Freeing the buffer in TearDown | mBufferHandle | status.isOk() should be true | Should be successful |
* | 05 | Destroying video pool in TearDown | mVideoPool.handle | status.isOk() should be true | Should be successful |
*/
TEST_F(L1AVBufferManagementTestSuite, test_l1_avbuffer_positive_1_trimSize) 
{
    int32_t newSize = 1024;
    ::android::binder::Status status = service->trimSize(mBufferHandle, newSize);

    EXPECT_TRUE(status.isOk());
}

/**
* @brief Test for trimSize functionality with invalid newSize 
*
* This test attempts to trim the size of a buffer in the buffer pool with an invalid new size of zero,
* ensuring that the service correctly handles illegal arguments.@n
*
* **Test Group ID:** Basic: 01@n
* **Test Case ID:** 024@n
* **Priority:** High@n
* @n
* **Pre-Conditions:** L1AVBufferManagementTestSuite, L1AVBufferPoolManagementTestSuite@n
* **Dependencies:** None@n
* **User Interaction:** None
* @n
* **Test Procedure:**@n
* | Variation / Step | Description       | Test Data                 | Expected Result                                              | Notes               |
* |:----------------:|-------------------|---------------------------|--------------------------------------------------------------|---------------------|
* | 01 | Setup initial state by creating video pool  | None | Video pool should be created successfully without errors | Setup function from prerequisite |
* | 02 | Allocate buffer from video pool | pool size: 1024 | Buffer should be allocated successfully without errors  | Part of SetUp() |
* | 03 | Call trimSize with newSize = 0 (Invalid new size) | mBufferHandle = allocated handle, newSize = 0 | status.exceptionCode should be EX_ILLEGAL_ARGUMENT | Should Fail |
* | 04 | Free the allocated buffer | mBufferHandle = allocated handle  | Buffer should be freed successfully without errors | Part of TearDown() |
* | 05 | Destroy video pool | None | Video pool should be destroyed successfully without errors | TearDown function from prerequisite |

*/
TEST_F(L1AVBufferManagementTestSuite, test_l1_avbuffer_negative_1_trimSize) 
{
    int32_t newSize = 0; //invalid new size
    ::android::binder::Status status = service->trimSize(mBufferHandle, newSize);

    ASSERT_EQ(status.exceptionCode(),  ::android::binder::Status::EX_ILLEGAL_ARGUMENT);
}

/**
* @brief Test case to verify handling of invalid trim size(Size larger than BufferHandle)
*
* This test ensures that attempting to trim an AV buffer to a size larger than the
* buffer's allocated size results in the appropriate error handling. 
*
* **Test Group ID:** Basic: 01@n
* **Test Case ID:** 025@n
* **Priority:** High@n
* @n
* **Pre-Conditions:** L1AVBufferManagementTestSuite, L1AVBufferPoolManagementTestSuite@n
* **Dependencies:** None@n
* **User Interaction:** None
* @n
* **Test Procedure:**@n
* | Variation / Step | Description | Test Data |Expected Result |Notes |
* | :----: | --------- | ---------- |-------------- | ----- |
* | 01 | Initiate and set up the video buffer pool | secureHeap=true, videoDecoderId=0, listener=<AVBufferSpaceListener> | Should successfully create video pool |  |
* | 02 | Allocate buffer from the video pool | pool=mVideoPool, size=1024 | Should successfully allocate buffer handle |  |
* | 03 | Call trimSize invalid new size | bufferHandle=mBufferHandle, newSize=2048 | Should return exception code ::android::binder::Status::EX_ILLEGAL_ARGUMENT | Should Fail |
* | 04 | Free the allocated buffer handle | bufferHandle=mBufferHandle | Should successfully free buffer |  |
* | 05 | Tear down and clean up the video buffer pool | pool=mVideoPool.handle | Should successfully destroy video pool |  |
*/
TEST_F(L1AVBufferManagementTestSuite, test_l1_avbuffer_negative_2_trimSize) 
{
    int32_t newSize = 2048; //invalid size. Size larger than BufferHandle created by alloc (1024)
    ::android::binder::Status status = service->trimSize(mBufferHandle, newSize);

    ASSERT_EQ(status.exceptionCode(),  ::android::binder::Status::EX_ILLEGAL_ARGUMENT);
}

/**
* @brief Test to check the positive scenario of freeing a buffer in L1AVBuffer.
*
* This test verifies the successful allocation and subsequent freeing of a buffer from the video pool.@n
*
* **Test Group ID:** Basic: 01@n
* **Test Case ID:** 026@n
* **Priority:** High@n
* @n
* **Pre-Conditions:** L1AVBufferPoolManagementTestSuite@n
* **Dependencies:** None@n
* **User Interaction:** None@n
* @n
* **Test Procedure:**@n
* | Variation / Step | Description | Test Data | Expected Result | Notes |
* | :----: | --------- | ---------- |-------------- | ----- |
* | 01 | Set up the test environment and create video pool | secureHeap = true, videoDecoderId.value = 0, listener = AVBufferSpaceListener() | Video pool created successfully | None |
* | 02 | Invoke alloc API with valid values | mVideoPool = valid, size = 1024, bufferHandle = valid | Buffer allocated successfully and status should be Ok | None |
*/
TEST_F(L1AVBufferPoolManagementTestSuite, test_l1_avbuffer_positive_1_free) 
{
    int64_t bufferHandle ;
    Status status = service->alloc(mVideoPool, 1024, &bufferHandle);
    if (!status.isOk()) 
    {
        FAIL() << "Failed to allocate buffer as a prerequisite to free()";
    }
    ASSERT_NE(bufferHandle, IAVBuffer::INVALID_HANDLE);
    status = service->free(bufferHandle);

    EXPECT_TRUE(status.isOk());
}

/**
* @brief Tests the 'free' method with an invalid buffer handle
*
* This test checks the 'free' method of the service using an invalid buffer handle to ensure
* that the correct error code is returned when freeing a non-existent or invalid buffer handle.@n
*
* **Test Group ID:** Basic: 01@n
* **Test Case ID:** 027@n
* **Priority:** High@n
* @n
* **Pre-Conditions:** None@n
* **Dependencies:** None@n
* **User Interaction:** None
* @n
* **Test Procedure:**@n
* | Variation / Step | Description | Test Data | Expected Result | Notes |
* | :----: | --------- | ---------- |-------------- | ----- |
* | 01 | Call 'free' with invalid buffer handle | bufferHandle = -1 | Ensure 'EX_ILLEGAL_ARGUMENT' is returned | Should Fail |
*/
TEST(L1AVBufferTestSuite, test_l1_avbuffer_negative_1_free) 
{
    int64_t bufferHandle = -1; // Invalid buffer handle
    Status status = service->free(bufferHandle);

    EXPECT_EQ(status.exceptionCode(),  ::android::binder::Status::EX_ILLEGAL_ARGUMENT);
}

/**
* @brief Test freeing an invalid buffer handle
*
* This test checks the behavior of the free API when attempting to free an large buffer handle@n
*
* **Test Group ID:** Basic: 01@n
* **Test Case ID:** 028@n
* **Priority:** High@n
* @n
* **Pre-Conditions:** None@n
* **Dependencies:** None@n
* **User Interaction:** None
* @n
* **Test Procedure:**@n
* | Variation / Step | Description | Test Data |Expected Result |Notes |
* | :----: | --------- | ---------- |-------------- | ----- |
* | 01 | Attempt to free an invalid buffer handle | bufferHandle = 9223372036854775807 | EX_ILLEGAL_ARGUMENT | Should Fail |
*/
TEST(L1AVBufferTestSuite, test_l1_avbuffer_negative_2_free) 
{
    int64_t bufferHandle = 9223372036854775807; // Unreasonably large number
    Status status = service->free(bufferHandle);

    EXPECT_EQ(status.exceptionCode(),  ::android::binder::Status::EX_ILLEGAL_ARGUMENT);
}

/**
* @brief Test to validate the allocation and validation of a buffer in a video pool
*
* This test validates the proper allocation of a buffer and ensures that the allocated buffer is valid in the video pool@n
*
* **Test Group ID:** Basic: 01@n
* **Test Case ID:** 029@n
* **Priority:** High@n
* @n
* **Pre-Conditions:** L1AVBufferPoolManagementTestSuite@n
* **Dependencies:** None@n
* **User Interaction:** None
* @n
* **Test Procedure:**@n
* | Variation / Step | Description | Test Data |Expected Result |Notes |
* | :----: | --------- | ---------- |-------------- | ----- |
* | 01 | Setup video pool | secureHeap = true, videoDecoderId.value = 0, listener = new AVBufferSpaceListener() | Successfully created video pool | Should be successful |
* | 02 | Allocate buffer from video pool | mVideoPool = valid, size = 1024, bufferHandle = valid | Successfully allocated buffer | Should be successful |
* | 03 | Verify buffer validity | bufferHandle | expectedValidity = true | Should be successful |
* | 04 | Destroy video pool | mVideoPool | Successfully destroyed video pool | Should be successful |
*/
TEST_F(L1AVBufferPoolManagementTestSuite, test_l1_avbuffer_positive_1_IsValid)
{
    int64_t bufferHandle ;
    bool isValid;
    bool expectedValidity = true;
    Status status = service->alloc(mVideoPool, 1024, &bufferHandle);
    if (!status.isOk()) 
    {
        FAIL() << "Failed to allocate buffer";
    }
    ASSERT_NE(bufferHandle, IAVBuffer::INVALID_HANDLE);
    status = service->isValid(bufferHandle, &isValid);

    EXPECT_TRUE(status.isOk());
    EXPECT_EQ(isValid, expectedValidity);
}

/**
* @brief Tests the behavior of the API isValid, when it is invoked with invalid buffer handle
*
* This test checks the behavior, when an invalid buffer handle is passed to the isValid function.
* It ensures that the function correctly identifies the buffer as invalid and returns the expected status.
*
* **Test Group ID:** Basic: 01 @n
* **Test Case ID:** 030 @n
* **Priority:** High @n
* @n
* **Pre-Conditions:** None @n
* **Dependencies:** None @n
* **User Interaction:** None
* @n
* **Test Procedure:**@n
* | Variation / Step | Description | Test Data |Expected Result |Notes |
* | :----: | --------- | ---------- |-------------- | ----- |
* | 01 | Initialize variables and set any pre-conditions | expectedValidity = false, bufferHandle = -1 | Variables should be set correctly | Should be successful |
* | 02 | Call the isValid function with invalid buffer handle | bufferHandle = -1, isValid | status should not be OK, isValid should be false | Should Fail |
* | 03 | Validate the results with expected values | expectedValidity = false | status is not OK, isValid is false | Should be successful |
*/
TEST(L1AVBufferTestSuite, test_l1_avbuffer_negative_1_IsValid) 
{
    bool expectedValidity = false;
    bool isValid;
    int64_t bufferHandle = -1; // Invalid buffer handle
    Status status = service->isValid(bufferHandle, &isValid);

    ASSERT_FALSE(status.isOk());
    EXPECT_EQ(isValid, expectedValidity);
}

/**
* @brief Verify the allocation list retrieval functionality
*
* This test checks if the allocation list can be successfully retrieved from the video buffer pool and verifies if the returned list is not empty.@n
*
* **Test Group ID:** Basic: 01@n
* **Test Case ID:** 031@n
* **Priority:** High@n
* @n
* **Pre-Conditions:** L1AVBufferPoolManagementTestSuite@n
* **Dependencies:** None@n
* **User Interaction:** None
* @n
* **Test Procedure:**@n
* | Variation / Step | Description | Test Data | Expected Result | Notes |
* | :----: | --------- | ---------- | ----------- | ----- |
* | 01 | Setup the test environment | secureHeap = true, videoDecoderId.value = 0, listener = AVBufferSpaceListener() | Video pool is created successfully | Must not fail |
* | 02 | Call getAllocList on the video pool | mVideoPool = valid, _aidl_return = valid | status.isOk() should return true | Should Successful |
* | 03 | Verify the allocation list is not empty |  | !_aidl_return.empty() should return true | Allocation list should be non-empty |
* | 04 | Teardown the test environment |  | Video pool is destroyed successfully | Must not fail |
*/
TEST_F(L1AVBufferPoolManagementTestSuite, test_l1_avbuffer_positive_1_getAllocList) 
{
    std::vector<int64_t> _aidl_return;
    ::android::binder::Status status = service->getAllocList(mVideoPool, &_aidl_return);

    EXPECT_TRUE(status.isOk());
    EXPECT_FALSE(_aidl_return.empty());
}

/**
* @brief Test L1 AVBuffer to handle invalid pool handle and return proper error code
*
* This test verifies that the getAllocList correctly returns the error code when an invalid pool handle is provided. This is important for ensuring the robustness and reliability of the getAllocList API's error handling mechanisms.
*
* **Test Group ID:** Basic: 01@n
* **Test Case ID:** 032@n
* **Priority:** High@n
* @n
* **Pre-Conditions:** None@n
* **Dependencies:** None@n
* **User Interaction:** None
* @n
* **Test Procedure:**@n
* | Variation / Step | Description | Test Data |Expected Result |Notes |
* | :----: | --------- | ---------- |-------------- | ----- |
* | 01 | Initialize the pool handle with INVALID value | Pool handle = INVALID | Pool handle initialized with INVALID | |
* | 02 | Call getAllocList with invalid pool handle | poolHandle = INVALID, _aidl_return | Return status should be EX_ILLEGAL_ARGUMENT, _aidl_return should be empty |Should Fail |
*/
TEST(L1AVBufferTestSuite, test_l1_avbuffer_negative_1_getAllocList) 
{
    ::com::rdk::hal::avbuffer::Pool poolHandle;
    poolHandle.handle = ::com::rdk::hal::avbuffer::Pool::INVALID;
    std::vector<int64_t> _aidl_return;
    ::android::binder::Status status = service->getAllocList(poolHandle, &_aidl_return);

    EXPECT_EQ(status.serviceSpecificErrorCode(), ::android::binder::Status::EX_ILLEGAL_ARGUMENT);
    EXPECT_TRUE(_aidl_return.empty());
}

/**
* @brief Test calculation of SHA1 hash on an allocated buffer
*
* This test verifies that the service can calculate the SHA1 hash of an allocated buffer properly, and that the resulting hash is not empty@n
*
* **Test Group ID:** Basic: 01@n
* **Test Case ID:** 033@n
* **Priority:** High@n
* @n
* **Pre-Conditions:** 
* L1AVBufferManagementTestSuite@n
* L1AVBufferPoolManagementTestSuite@n
* **Dependencies:** None@n
* **User Interaction:** None
* @n
* **Test Procedure:**@n
* | Variation / Step | Description | Test Data | Expected Result | Notes |
* | :----: | --------- | ---------- |-------------- | ----- |
* | 01 | Initial Setup for L1AVBufferPoolManagementTestSuite | secureHeap = true, videoDecoderId.value = 0 | Video pool created successfully | Should be successful |
* | 02 | Allocate buffer in SetUp() | mVideoPool, size = 1024, mBufferHandle = valid | Buffer allocated successfully | mBufferHandle should not be INVALID_HANDLE |
* | 03 | Calculate SHA1 hash of allocated buffer | mBufferHandle | Status should be OK, hash result should not be empty | Should be successful |
* | 04 | Free the allocated buffer in TearDown() | mBufferHandle | Buffer freed successfully | Should be successful |
* | 05 | Destroy video pool in TearDown() | mVideoPool | Video pool destroyed successfully | Should be successful |
*/
TEST_F(L1AVBufferManagementTestSuite, test_l1_avbuffer_positive_1_calculateSHA1) 
{
    std::vector<uint8_t> resultBuffer;
    Status status = service->calculateSHA1(mBufferHandle, &resultBuffer);
    EXPECT_TRUE(status.isOk());
    ASSERT_FALSE(resultBuffer.empty());
}

/**
* @brief Test for calculating SHA1 with invalid buffer handle
*
* This test checks the functionality when an invalid buffer handle is passed to the calculateSHA1 function. The expected behavior is that the function should recognize the invalid handle and return appropriate error status and an empty result buffer.@n
*
* **Test Group ID:** Basic: 01@n
* **Test Case ID:** 034@n
* **Priority:** High@n
* @n
* **Pre-Conditions:** None@n
* **Dependencies:** None@n
* **User Interaction:** None
* @n
* **Test Procedure:**@n
* | Variation / Step | Description | Test Data | Expected Result | Notes |
* | :----: | --------- | ---------- |-------------- | ----- |
* | 01 | Setup test suite and initialize variables | bufferHandle = -1 | Variables are initialized correctly | Should be successful |
* | 02 | Call calculateSHA1 function with invalid bufferHandle | bufferHandle = -1 | resultBuffer should be empty, status should indicate illegal argument error | Should Fail |
* | 03 | Verify that resultBuffer is empty and status code is as expected | N/A | resultBuffer.empty() should be true, status.serviceSpecificErrorCode() should be EX_ILLEGAL_ARGUMENT | Should be successful |
*/
TEST(L1AVBufferTestSuite, test_l1_avbuffer_negative_1_calculateSHA1) 
{
    int64_t bufferHandle = -1; //invalid bufferHandle
    std::vector<uint8_t> resultBuffer;
    Status status = service->calculateSHA1(bufferHandle, &resultBuffer);

    ASSERT_TRUE(resultBuffer.empty());
    EXPECT_EQ(status.serviceSpecificErrorCode(), ::android::binder::Status::EX_ILLEGAL_ARGUMENT);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    getService();
    return RUN_ALL_TESTS();
}
