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

/** @brief
 * AVBuffer.cpp
 *
 * Contains the implementation of the AVBuffer interfaces.
 */

#include "AVBuffer.h"
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <cstring>
#include <binder/IServiceManager.h>
#include "com/rdk/hal/videodecoder/IVideoDecoderManager.h"
#include "com/rdk/hal/audiodecoder/IAudioDecoderManager.h"

using namespace com::rdk::hal;
using namespace com::rdk::hal::avbuffer;
using namespace com::rdk::hal::videodecoder;
using namespace com::rdk::hal::audiodecoder;
using namespace ::android::binder;

namespace com {
    namespace rdk {
        namespace hal {
            namespace avbuffer {

                /* IAVBuffer: a secure pool can only be created for a configured (opened) decoder,
                 * otherwise EX_ILLEGAL_STATE. The decoder services are queried without blocking
                 * (checkService); an unreachable decoder is treated as not configured. */
                static bool isVideoDecoderConfigured(const IVideoDecoder::Id& videoDecoderId)
                {
                    ::android::sp<IVideoDecoderManager> manager = ::android::interface_cast<IVideoDecoderManager>(
                        ::android::defaultServiceManager()->checkService(::android::String16(IVideoDecoderManager::serviceName().c_str())));
                    ::android::sp<IVideoDecoder> decoder;
                    videodecoder::State state = videodecoder::State::UNKNOWN;
                    if (manager == nullptr || !manager->getVideoDecoder(videoDecoderId, &decoder).isOk() ||
                        decoder == nullptr || !decoder->getState(&state).isOk())
                    {
                        return false;
                    }
                    return state != videodecoder::State::UNKNOWN && state != videodecoder::State::CLOSED;
                }

                static bool isAudioDecoderConfigured(const IAudioDecoder::Id& audioDecoderId)
                {
                    if (audioDecoderId.value == IAudioDecoder::Id::UNDEFINED)
                    {
                        return false; /* no decoder to configure */
                    }
                    ::android::sp<IAudioDecoderManager> manager = ::android::interface_cast<IAudioDecoderManager>(
                        ::android::defaultServiceManager()->checkService(::android::String16(IAudioDecoderManager::serviceName().c_str())));
                    ::android::sp<IAudioDecoder> decoder;
                    audiodecoder::State state = audiodecoder::State::UNKNOWN;
                    if (manager == nullptr || !manager->getAudioDecoder(audioDecoderId, &decoder).isOk() ||
                        decoder == nullptr || !decoder->getState(&state).isOk())
                    {
                        return false;
                    }
                    return state != audiodecoder::State::UNKNOWN && state != audiodecoder::State::CLOSED;
                }

                // Pool.aidl has no INVALID_POOL constant; poolHandleCounter starts at 1,
                // so 0 is our own sentinel for "no pool".
                static constexpr uint8_t POOL_INVALID_HANDLE = 0;

                // Buffer memory is a POSIX shm segment named deterministically from the
                // handle, so any process can shm_open() it directly - alloc() only returns
                // an int64_t handle, not an fd. IAVBufferHelper's real implementation uses
                // the same naming scheme.
                static std::string shmNameForHandle(int64_t handle)
                {
                    return "/vts_avbuf_" + std::to_string(handle);
                }

                std::atomic<uint8_t> AVBuffer::poolHandleCounter(1);
                std::atomic<int64_t> AVBuffer::bufferHandleCounter(1);
                int32_t g_supported_audiodecoder_id[NUM_AUDIO_DECODER];
                int32_t g_supported_videodecoder_id[NUM_VIDEO_DECODER];

                AVBuffer::AVBuffer()
                {
                    secureHeapSize = SECURE_HEAP_SIZE;
                    nonSecureHeapSize = NON_SECURE_HEAP_SIZE;
                    totalSecureBytesUsed = 0;
                    totalNonSecureBytesUsed = 0;

                    for (int32_t i = 0;i<NUM_AUDIO_DECODER;i++)
                    {
                        g_supported_audiodecoder_id[i] = i;
                    }
                    for (int32_t i = 0;i<NUM_VIDEO_DECODER;i++)
                    {
                        g_supported_videodecoder_id[i] = i;
                    }
                    printf("[SERVICE]AVBuffer service initialized\n");
                }

                AVBuffer::~AVBuffer()
                {
                    printf("[SERVICE]AVBuffer service destroyed\n");
                }

                // Caller must hold poolMutex.
                uint8_t AVBuffer::allocatePoolHandle()
                {
                    if (!freePoolHandles.empty())
                    {
                        auto it = freePoolHandles.begin();
                        uint8_t handle = *it;
                        freePoolHandles.erase(it);
                        return handle;
                    }
                    // Pool.handle is a signed int8_t (1-127 valid); minting past 127 would
                    // silently become negative once assigned to the AIDL parcelable,
                    // violating the "successful pool handles are non-negative" contract
                    // (see AVBUFFER_CREATE_*POOL_SUCCESS's handle >= 0 assertions). Signal
                    // exhaustion via POOL_INVALID_HANDLE instead of minting past the range -
                    // callers must fail pool creation when they see this.
                    if (poolHandleCounter.load(std::memory_order_relaxed) > 127)
                    {
                        return POOL_INVALID_HANDLE;
                    }
                    return poolHandleCounter.fetch_add(1);
                }

                Status AVBuffer::getHeapMetrics(bool secureHeap, HeapMetrics* _aidl_return)
                {
                    // Same lock used by create*Pool()/destroyPool() writers.
                    std::lock_guard<std::mutex> lock(poolMutex);
                    _aidl_return->secure = secureHeap;

                    if (secureHeap)
                    {
                        _aidl_return->bytesUsed = this->totalSecureBytesUsed;
                        _aidl_return->bytesTotal = this->secureHeapSize;
                    }
                    else
                    {
                        _aidl_return->bytesUsed = this->totalNonSecureBytesUsed;
                        _aidl_return->bytesTotal = this->nonSecureHeapSize;
                    }
                    return Status::ok();
                }

                Status AVBuffer::createVideoPool(bool secureHeap, const IVideoDecoder::Id& videoDecoderId, const ::android::sp<IAVBufferSpaceListener>& listener, Pool* _aidl_return)
                {
                    // Query the decoder before taking the pool lock (binder call)
                    const bool decoderConfigured = secureHeap && isVideoDecoderConfigured(videoDecoderId);
                    std::lock_guard<std::mutex> lock(poolMutex);
                    int32_t found = 0;
                    _aidl_return->handle = POOL_INVALID_HANDLE;

                    // Check if decoder ID is invalid
                    if (videoDecoderId.value == IVideoDecoder::Id::UNDEFINED)
                    {
                        return Status::fromExceptionCode(Status::EX_ILLEGAL_ARGUMENT);
                    }
                    for(int32_t i = 0; i < NUM_VIDEO_DECODER; i++)
                    {
                        if(g_supported_videodecoder_id[i] == videoDecoderId.value)
                        {
                            found = 1;
                            break;
                        }
                    }
                    if(!found)
                    {
                        return Status::fromExceptionCode(Status::EX_ILLEGAL_ARGUMENT);
                    }
                    if (secureHeap && !decoderConfigured)
                    {
                        return Status::fromExceptionCode(Status::EX_ILLEGAL_STATE);
                    }
                    // Check for available memory and create pool
                    if (secureHeap)
                    {
                        if ((this->secureHeapSize - this->totalSecureBytesUsed) < VIDEO_POOL_SIZE)
                        {
                            return Status::fromExceptionCode(Status::EX_SERVICE_SPECIFIC, "HALError::OUT_OF_MEMORY");
                        }
                        this->totalSecureBytesUsed += VIDEO_POOL_SIZE;
                    }
                    else
                    {
                        if ((this->nonSecureHeapSize - this->totalNonSecureBytesUsed) < VIDEO_POOL_SIZE)
                        {
                            return Status::fromExceptionCode(Status::EX_SERVICE_SPECIFIC, "HALError::OUT_OF_MEMORY");
                        }
                        this->totalNonSecureBytesUsed += VIDEO_POOL_SIZE;
                    }
                    uint8_t handle = allocatePoolHandle();
                    if (handle == POOL_INVALID_HANDLE)
                    {
                        // No valid handle available (1-127 exhausted) - roll back the heap
                        // accounting reserved above rather than leaking it on this failure.
                        if (secureHeap)
                        {
                            this->totalSecureBytesUsed -= VIDEO_POOL_SIZE;
                        }
                        else
                        {
                            this->totalNonSecureBytesUsed -= VIDEO_POOL_SIZE;
                        }
                        return Status::fromExceptionCode(Status::EX_SERVICE_SPECIFIC, "HALError::OUT_OF_MEMORY");
                    }
                    Pool pool;
                    pool.handle = handle;
                    *_aidl_return = pool;

                    // Save PoolMetrics and secure flag in internal map
                    PoolMetrics metrics;
                    metrics.poolHandle = pool;
                    metrics.bytesUsed = 0;
                    metrics.bytesTotal = VIDEO_POOL_SIZE;

                    InternalPoolInfo poolInfo;
                    poolInfo.metrics = metrics;
                    poolInfo.isSecure = secureHeap;

                    poolMetricsMap[handle] = poolInfo;

                    std::cout << "[SERVICE] Created " << (secureHeap ? "secure" : "non-secure")
                              << " video pool with handle: " << static_cast<int>(handle) << std::endl;

                    this->spaceListener = listener;
                    return Status::ok();
                }

                Status AVBuffer::createAudioPool(bool secureHeap, const IAudioDecoder::IAudioDecoder::Id &audioDecoderId, const ::android::sp<IAVBufferSpaceListener> &listener, Pool *_aidl_return)
                {
                    // Query the decoder before taking the pool lock (binder call)
                    const bool decoderConfigured = secureHeap && isAudioDecoderConfigured(audioDecoderId);
                    std::lock_guard<std::mutex> lock(poolMutex);
                    int32_t found = 0;
                    _aidl_return->handle = POOL_INVALID_HANDLE;

                    if (audioDecoderId.value != IAudioDecoder::Id::UNDEFINED)
                    {
                        for(int32_t i = 0; i < NUM_AUDIO_DECODER; i++)
                        {
                            if(g_supported_audiodecoder_id[i] == audioDecoderId.value)
                            {
                                found = 1;
                                break;
                            }
                        }
                        if(!found)
                        {
                            return Status::fromExceptionCode(Status::EX_ILLEGAL_ARGUMENT);
                        }
                    }

                    if (secureHeap && !decoderConfigured)
                    {
                        return Status::fromExceptionCode(Status::EX_ILLEGAL_STATE);
                    }
                    // Check for available memory and create pool
                    if (secureHeap)
                    {
                        if ((this->secureHeapSize - this->totalSecureBytesUsed) < AUDIO_POOL_SIZE)
                        {
                            return Status::fromExceptionCode(Status::EX_SERVICE_SPECIFIC, "HALError::OUT_OF_MEMORY");
                        }
                        this->totalSecureBytesUsed += AUDIO_POOL_SIZE;
                    }
                    else
                    {
                        if ((this->nonSecureHeapSize - this->totalNonSecureBytesUsed) < AUDIO_POOL_SIZE)
                        {
                            return Status::fromExceptionCode(Status::EX_SERVICE_SPECIFIC, "HALError::OUT_OF_MEMORY");
                        }
                        this->totalNonSecureBytesUsed += AUDIO_POOL_SIZE;
                    }
                    uint8_t handle = allocatePoolHandle();
                    if (handle == POOL_INVALID_HANDLE)
                    {
                        // No valid handle available (1-127 exhausted) - roll back the heap
                        // accounting reserved above rather than leaking it on this failure.
                        if (secureHeap)
                        {
                            this->totalSecureBytesUsed -= AUDIO_POOL_SIZE;
                        }
                        else
                        {
                            this->totalNonSecureBytesUsed -= AUDIO_POOL_SIZE;
                        }
                        return Status::fromExceptionCode(Status::EX_SERVICE_SPECIFIC, "HALError::OUT_OF_MEMORY");
                    }
                    Pool pool;
                    pool.handle = handle;
                    *_aidl_return = pool;

                    // Save PoolMetrics and secure flag in internal map
                    PoolMetrics metrics;
                    metrics.poolHandle = pool;
                    metrics.bytesUsed = 0;
                    metrics.bytesTotal = AUDIO_POOL_SIZE;

                    InternalPoolInfo poolInfo;
                    poolInfo.metrics = metrics;
                    poolInfo.isSecure = secureHeap;

                    poolMetricsMap[handle] = poolInfo;

                    std::cout << "[SERVICE]Created " << (secureHeap ? "secure" : "non-secure")
                              << " audio pool with handle: " << static_cast<int>(handle) << std::endl;

                    this->spaceListener = listener;
                    return Status::ok();
                }

                Status AVBuffer::destroyPool(const Pool& poolHandle, bool* _aidl_return)
                {
                    std::lock_guard<std::mutex> lock(poolMutex);
                    if (poolHandle.handle == POOL_INVALID_HANDLE)
                    {
                        *_aidl_return = false;
                        return Status::ok();
                    }
                    auto it = poolMetricsMap.find(poolHandle.handle);
                    if (it == poolMetricsMap.end())
                    {
                        *_aidl_return = false;
                        return Status::ok();
                    }

                    const InternalPoolInfo& poolInfo = it->second;

                    // Check if the pool is empty
                    if (poolInfo.metrics.bytesUsed > 0)
                    {
                        *_aidl_return = false;
                        return Status::fromExceptionCode(Status::EX_SERVICE_SPECIFIC, "HALError::NOT_EMPTY");
                    }

                    if (poolInfo.isSecure)
                    {
                        this->totalSecureBytesUsed -= poolInfo.metrics.bytesTotal;
                    }
                    else
                    {
                        this->totalNonSecureBytesUsed -= poolInfo.metrics.bytesTotal;
                    }

                    // Erase pool info from map and return the handle to the free list for reuse
                    poolMetricsMap.erase(it);
                    freePoolHandles.insert(poolHandle.handle);
                    *_aidl_return = true;
                    return Status::ok();
                }

                Status AVBuffer::getPoolMetrics(const Pool& poolHandle, PoolMetrics* _aidl_return)
                {
                    std::lock_guard<std::mutex> lock(poolMutex);
                    if (poolHandle.handle == POOL_INVALID_HANDLE)
                    {
                        return Status::fromExceptionCode(Status::EX_ILLEGAL_ARGUMENT);
                    }

                    auto it = poolMetricsMap.find(poolHandle.handle);
                    if (it == poolMetricsMap.end())
                    {
                        return Status::fromExceptionCode(Status::EX_ILLEGAL_ARGUMENT);
                    }

                    const InternalPoolInfo& poolInfo = it->second;
                    *_aidl_return = poolInfo.metrics;
                    return Status::ok();
                }

                Status AVBuffer::getAllPoolMetrics(bool secureHeap, ::std::vector<PoolMetrics>* _aidl_return)
                {
                    std::lock_guard<std::mutex> lock(poolMutex);
                    _aidl_return->clear();
                    for (const auto& [handle, info] : poolMetricsMap)
                    {
                        if (info.isSecure == secureHeap)
                        {
                            _aidl_return->push_back(info.metrics);
                        }
                    }
                    return Status::ok();
                }

                Status AVBuffer::alloc(const Pool& poolHandle, int32_t size, int64_t* _aidl_return)
                {
                    *_aidl_return = POOL_INVALID_HANDLE;

                    if(size <= 0 || poolHandle.handle == POOL_INVALID_HANDLE)
                    {
                        return Status::ok();
                    }

                    std::lock_guard<std::mutex> lock(poolMutex);
                    const uint8_t handle = poolHandle.handle;

                    // Check if pool exists
                    auto metricsIt = poolMetricsMap.find(handle);
                    if (metricsIt == poolMetricsMap.end())
                    {
                        return Status::ok();
                    }

                    PoolMetrics& metrics = metricsIt->second.metrics;

                    // Check if there's enough space
                    if ((metrics.bytesTotal - metrics.bytesUsed) < size)
                    {
                        return Status::fromExceptionCode( Status::EX_SERVICE_SPECIFIC, "HALError::OUT_OF_MEMORY");
                    }
                    // Allocate buffer handle
                    int64_t bufferHandle = bufferHandleCounter.fetch_add(1);

                    // Backed by real shared memory so mappers can read/write it. No O_EXCL:
                    // a segment orphaned by a killed prior run could collide with a fresh
                    // handle and wrongly fail with EEXIST; reuse is safe since handles are
                    // never reused while this process is alive. Mode 0600: only same-UID
                    // mock/test processes ever need to open this by name, so no reason to
                    // make it world-read/write on shared/multi-user build machines.
                    // O_CLOEXEC: closes the race where a concurrent fork()+exec() on another
                    // thread could otherwise inherit this fd before our own close() below runs.
                    {
                        std::string shmName = shmNameForHandle(bufferHandle);
                        int shmFd = shm_open(shmName.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
                        if (shmFd < 0 || ftruncate(shmFd, size) != 0)
                        {
                            if (shmFd >= 0)
                            {
                                close(shmFd);
                            }
                            shm_unlink(shmName.c_str());
                            return Status::fromExceptionCode(Status::EX_SERVICE_SPECIFIC, "HALError::OUT_OF_MEMORY");
                        }
                        close(shmFd);
                    }

                    // Store buffer info
                    {
                        std::lock_guard<std::mutex> bufLock(bufferLock);
                        bufferMap[bufferHandle] = BufferInfo{
                                .poolHandle = poolHandle,
                                .size = size,
                                .valid = true
                        };
                        poolAllocations[handle].push_back(bufferHandle);
                    }
                    metrics.bytesUsed += size;
                    *_aidl_return = bufferHandle;
                    return Status::ok();
                }

                Status AVBuffer::notifyWhenSpaceAvailable(const Pool& poolHandle, int32_t size, bool* _aidl_return)
                {
                    if (size <= 0 || size > POOL_SIZE)
                    {
                        *_aidl_return = false;
                        return Status::fromExceptionCode(Status::EX_ILLEGAL_ARGUMENT);
                    }

                    long availableSpace;
                    {
                        // Lock covers only the map read; the listener callback below must run
                        // unlocked or a listener calling back into alloc()/free() would deadlock.
                        std::lock_guard<std::mutex> lock(poolMutex);
                        auto it = poolMetricsMap.find(poolHandle.handle);
                        if (it == poolMetricsMap.end())
                        {
                            *_aidl_return = false;
                            return Status::fromExceptionCode(Status::EX_ILLEGAL_ARGUMENT);
                        }
                        const auto& poolInfo = it->second;
                        availableSpace = poolInfo.metrics.bytesTotal - poolInfo.metrics.bytesUsed;
                    }

                    if (availableSpace >= size)
                    {
                        if (spaceListener != nullptr)
                        {
                            this->spaceListener->onSpaceAvailable();
                        }
                    }
                    else
                    {
                        //TBD
                        std::thread([this]() {
                            std::this_thread::sleep_for(std::chrono::milliseconds(200));  // simulate delay
                            if (spaceListener != nullptr)
                            {
                                this->spaceListener->onSpaceAvailable();
                            }
                        }).detach();
                    }

                    *_aidl_return = true;
                    return Status::ok();
                }

                Status AVBuffer::trimSize(int64_t bufferHandle, int32_t newSize, bool* _aidl_return)
                {
                    if (newSize <= 0)
                    {
                        *_aidl_return = false;
                        return Status::ok();
                    }

                    // Lock order must match alloc()'s poolMutex-then-bufferLock nesting
                    // to avoid an AB-BA deadlock against concurrent alloc()/free() calls.
                    std::lock_guard<std::mutex> poolLock(poolMutex);
                    std::lock_guard<std::mutex> lock(bufferLock);

                    auto it = bufferMap.find(bufferHandle);
                    if (it == bufferMap.end() || !it->second.valid)
                    {
                        *_aidl_return = false;
                        return Status::ok();
                    }

                    BufferInfo& info = it->second;
                    const uint8_t poolHandle = info.poolHandle.handle;

                    auto& allocList = poolAllocations[poolHandle];
                    if (allocList.empty() || allocList.back() != bufferHandle)
                    {
                        return Status::fromExceptionCode(Status::EX_ILLEGAL_STATE);
                    }

                    if (newSize > info.size)
                    {
                        *_aidl_return = false;
                        return Status::ok();
                    }

                    auto poolIt = poolMetricsMap.find(poolHandle);
                    if (poolIt != poolMetricsMap.end()) {
                        poolIt->second.metrics.bytesUsed -= (info.size - newSize);
                    }

                    info.size = newSize;

                    // Shrink the backing shm segment to match: mapHandle()/getAllocationSize()
                    // fstat() this segment directly rather than querying BufferInfo::size, so
                    // without this they'd keep reporting (and mapping) the pre-trim size,
                    // letting writers touch bytes past the trimmed boundary.
                    {
                        std::string shmName = shmNameForHandle(bufferHandle);
                        int shmFd = shm_open(shmName.c_str(), O_RDWR | O_CLOEXEC, 0600);
                        if (shmFd >= 0)
                        {
                            ftruncate(shmFd, newSize);
                            close(shmFd);
                        }
                    }

                    *_aidl_return = true;
                    return Status::ok();
                }

                Status AVBuffer::free(int64_t bufferHandle, bool* _aidl_return)
                {
                    // Must match alloc()'s poolMutex-then-bufferLock order - reversing it
                    // here previously caused an AB-BA deadlock between concurrent calls.
                    std::lock_guard <std::mutex> poolLock(poolMutex);
                    std::lock_guard <std::mutex> lock(bufferLock);
                    auto it = bufferMap.find(bufferHandle);
                    if (it == bufferMap.end() || !it->second.valid)
                    {
                        *_aidl_return = false;
                        return Status::ok();
                    }

                    BufferInfo &info = it->second;
                    const uint8_t poolHandle = info.poolHandle.handle;
                    auto poolIt = poolMetricsMap.find(poolHandle);
                    if (poolIt != poolMetricsMap.end())
                    {
                        poolIt->second.metrics.bytesUsed -= info.size;
                    }

                    // Mark invalid and remove from allocations list
                    info.valid = false;

                    auto &vec = poolAllocations[poolHandle];
                    vec.erase(std::remove(vec.begin(), vec.end(), bufferHandle), vec.end());

                    shm_unlink(shmNameForHandle(bufferHandle).c_str());

                    *_aidl_return = true;
                    return Status::ok();
                }

                Status AVBuffer::getAllocList(const Pool& poolHandle, std::vector<int64_t>* _aidl_return)
                {
                    std::lock_guard<std::mutex> lock(poolMutex);
                    if (poolMetricsMap.find(poolHandle.handle) == poolMetricsMap.end())
                    {
                        return Status::fromExceptionCode(Status::EX_ILLEGAL_ARGUMENT);
                    }
                    std::lock_guard<std::mutex> bufLock(bufferLock);
                    *_aidl_return = poolAllocations[poolHandle.handle];
                    return Status::ok();
                }

                Status AVBuffer::calculateSHA1(int64_t bufferHandle, ::std::vector<uint8_t>* _aidl_return)
                {
                    std::lock_guard<std::mutex> lock(bufferLock);
                    auto it = bufferMap.find(bufferHandle);
                    if (it == bufferMap.end() || !it->second.valid)
                    {
                        return Status::fromExceptionCode(Status::EX_ILLEGAL_ARGUMENT);
                    }
                    // SHA-1 support is not implemented in this build.
                    return Status::fromExceptionCode(Status::EX_UNSUPPORTED_OPERATION);
                }

                Status AVBuffer::isValid(int64_t bufferHandle, bool* _aidl_return)
                {
                    std::lock_guard<std::mutex> lock(bufferLock);
                    auto it = bufferMap.find(bufferHandle);
                    *_aidl_return = (it != bufferMap.end() && it->second.valid);
                    return Status::ok();
                }
            }
        }
    }
}



