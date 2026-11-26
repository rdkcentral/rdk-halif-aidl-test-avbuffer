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

/** @brief
 * AVBufferHelperImpl.cpp
 *
 * Real IAVBufferHelper implementation. Buffer handles allocated via
 * IAVBuffer::alloc() (a separate binder service process) are backed by a
 * POSIX shared-memory segment named deterministically from the handle
 * ("/vts_avbuf_<handle>" — see AVBuffer::shmNameForHandle() in
 * ../src/AVBuffer.cpp, which must use the exact same naming scheme).
 * IAVBuffer.alloc() only returns a plain int64_t handle (not a
 * ParcelFileDescriptor), so this process independently shm_open()s the same
 * name rather than receiving an fd over Binder.
 */

#include "avbufferhelper.h"

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <mutex>
#include <unordered_map>

namespace
{
    struct MappedRegion
    {
        void*    ptr;
        uint32_t size;
    };

    std::mutex gMapMutex;
    std::unordered_map<uint64_t, MappedRegion> gMappedRegions;

    std::string shmNameForHandle(uint64_t handle)
    {
        return "/vts_avbuf_" + std::to_string(handle);
    }
}

class AVBufferHelperImpl final : public IAVBufferHelper
{
public:
    void* mapHandle(uint64_t handle, uint32_t* size) override
    {
        if (!size)
        {
            return nullptr;
        }
        *size = 0;

        {
            std::lock_guard<std::mutex> lock(gMapMutex);
            auto it = gMappedRegions.find(handle);
            if (it != gMappedRegions.end())
            {
                // Already mapped: return the existing mapping instead of mmap'ing
                // again and leaking the first one - unmapHandle() only ever
                // tracks the most recent ptr per handle.
                *size = it->second.size;
                return it->second.ptr;
            }
        }

        std::string name = shmNameForHandle(handle);
        int fd = shm_open(name.c_str(), O_RDWR, 0666);
        if (fd < 0)
        {
            return nullptr;
        }

        struct stat st;
        if (fstat(fd, &st) != 0 || st.st_size <= 0)
        {
            close(fd);
            return nullptr;
        }

        void* ptr = mmap(nullptr, static_cast<size_t>(st.st_size), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        close(fd); // fd not needed once mmap'd; the mapping stays valid until munmap
        if (ptr == MAP_FAILED)
        {
            return nullptr;
        }

        {
            std::lock_guard<std::mutex> lock(gMapMutex);
            gMappedRegions[handle] = MappedRegion{ptr, static_cast<uint32_t>(st.st_size)};
        }

        *size = static_cast<uint32_t>(st.st_size);
        return ptr;
    }

    bool unmapHandle(uint64_t handle) override
    {
        std::lock_guard<std::mutex> lock(gMapMutex);
        auto it = gMappedRegions.find(handle);
        if (it == gMappedRegions.end())
        {
            return false;
        }
        munmap(it->second.ptr, it->second.size);
        gMappedRegions.erase(it);
        return true;
    }

    uint32_t getAllocationSize(uint64_t handle) override
    {
        std::string name = shmNameForHandle(handle);
        int fd = shm_open(name.c_str(), O_RDONLY, 0666);
        if (fd < 0)
        {
            return 0;
        }
        struct stat st;
        uint32_t size = 0;
        if (fstat(fd, &st) == 0 && st.st_size > 0)
        {
            size = static_cast<uint32_t>(st.st_size);
        }
        close(fd);
        return size;
    }

    bool writeSecureHandle(uint64_t handle, void* data, uint32_t size) override
    {
        if (!data)
        {
            return false;
        }
        uint32_t mappedSize = 0;
        void* ptr = mapHandle(handle, &mappedSize);
        if (!ptr || mappedSize < size)
        {
            if (ptr)
            {
                unmapHandle(handle);
            }
            return false;
        }
        memcpy(ptr, data, size);
        unmapHandle(handle);
        return true;
    }

    bool copySecureHandleWithMap(uint64_t handleTo, uint64_t handleFrom, CopyMap map, uint32_t mapSize) override
    {
        (void)mapSize; // interface passes a single CopyMap by value despite the count parameter
        uint32_t sizeTo = 0, sizeFrom = 0;
        void* ptrTo = mapHandle(handleTo, &sizeTo);
        void* ptrFrom = mapHandle(handleFrom, &sizeFrom);

        bool ok = false;
        if (ptrTo && ptrFrom &&
            static_cast<uint64_t>(map.src_offset) + map.size <= sizeFrom &&
            static_cast<uint64_t>(map.dst_offset) + map.size <= sizeTo)
        {
            memcpy(static_cast<uint8_t*>(ptrTo) + map.dst_offset,
                   static_cast<uint8_t*>(ptrFrom) + map.src_offset,
                   map.size);
            ok = true;
        }

        if (ptrTo)
        {
            unmapHandle(handleTo);
        }
        if (ptrFrom)
        {
            unmapHandle(handleFrom);
        }
        return ok;
    }
};

// AV Buffer 0.2.1.0: reference-counted singleton. getAVBufferHelperInstance() acquires a
// reference, releaseAVBufferHelperInstance() drops it and the instance is destroyed (and any
// regions still mapped are unmapped) when the count reaches zero.
namespace
{
    std::mutex gInstanceMutex;
    AVBufferHelperImpl* gInstance = nullptr;
    unsigned int gRefCount = 0;

    void unmapAllRegions()
    {
        std::lock_guard<std::mutex> lock(gMapMutex);
        for (auto& entry : gMappedRegions)
        {
            munmap(entry.second.ptr, entry.second.size);
        }
        gMappedRegions.clear();
    }
}

IAVBufferHelper* getAVBufferHelperInstance()
{
    std::lock_guard<std::mutex> lock(gInstanceMutex);
    if (gInstance == nullptr)
    {
        gInstance = new AVBufferHelperImpl();
    }
    ++gRefCount;
    return gInstance;
}

void releaseAVBufferHelperInstance(IAVBufferHelper* instance)
{
    std::lock_guard<std::mutex> lock(gInstanceMutex);
    if (instance == nullptr || instance != gInstance || gRefCount == 0)
    {
        fprintf(stderr, "releaseAVBufferHelperInstance: invalid or unbalanced release ignored\n");
        return;
    }
    if (--gRefCount == 0)
    {
        unmapAllRegions();
        delete gInstance;
        gInstance = nullptr;
    }
}
