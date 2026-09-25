// © 2026 NVIDIA Corporation

#include <unistd.h>

namespace nri {

PipelineCacheMetal::PipelineCacheMetal(DeviceMetal& device) : m_Device(device) {
}

PipelineCacheMetal::~PipelineCacheMetal() {
    if (m_Archive)
        m_Archive->release();

    if (m_Path[0])
        unlink(m_Path);
}

Result PipelineCacheMetal::Create(const PipelineCacheDesc& desc) {
    // Metal's binary archive API requires a file URL, including for serialization.
    const size_t length = confstr(_CS_DARWIN_USER_TEMP_DIR, m_Path, sizeof(m_Path));

    if (!length || length + sizeof("nri-cache-XXXXXX") > sizeof(m_Path))
        return Result::FAILURE;

    strcat(m_Path, "nri-cache-XXXXXX");
    const int fd = mkstemp(m_Path);

    if (fd == -1) {
        m_Path[0] = 0;

        return Result::FAILURE;
    }

    FILE* file = fdopen(fd, "wb");

    if (!file) {
        close(fd);

        return Result::FAILURE;
    }

    const bool written = !desc.data || fwrite(desc.data, 1, desc.size, file) == desc.size;
    const int closed = fclose(file);

    if (!written || closed)
        return Result::FAILURE;

    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
    MTL::BinaryArchiveDescriptor* archiveDesc = MTL::BinaryArchiveDescriptor::alloc()->init();

    if (desc.data && desc.size)
        archiveDesc->setUrl(NS::URL::fileURLWithPath(NS::String::string(m_Path, NS::UTF8StringEncoding)));

    NS::Error* error = nullptr;
    m_Archive = m_Device.GetNativeObject()->newBinaryArchive(archiveDesc, &error);
    archiveDesc->release();
    pool->release();

    return m_Archive ? Result::SUCCESS : (desc.data && desc.size ? Result::OUT_OF_DATE : Result::FAILURE);
}

Result PipelineCacheMetal::GetData(void* dst, uint64_t& size) const {
    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
    NS::Error* error = nullptr;
    const bool serialized = m_Archive->serializeToURL(NS::URL::fileURLWithPath(NS::String::string(m_Path, NS::UTF8StringEncoding)), &error);
    pool->release();

    if (!serialized)
        return Result::FAILURE;

    FILE* file = fopen(m_Path, "rb");

    if (!file)
        return Result::FAILURE;

    if (fseek(file, 0, SEEK_END)) {
        fclose(file);

        return Result::FAILURE;
    }

    const long length = ftell(file);
    rewind(file);
    Result result = Result::SUCCESS;

    if (length < 0)
        result = Result::FAILURE;
    else {
        if (dst) {
            if (size < (uint64_t)length)
                result = Result::OUT_OF_MEMORY;
            else if (fread(dst, 1, length, file) != (size_t)length)
                result = Result::FAILURE;
        }

        size = (uint64_t)length;
    }

    fclose(file);

    return result;
}

DeviceMetal& PipelineCacheMetal::GetDevice() const {
    return m_Device;
}

MTL::BinaryArchive* PipelineCacheMetal::GetNativeObject() const {
    return m_Archive;
}

void PipelineCacheMetal::SetDebugName(const char* name) {
    m_Archive->setLabel(NS::String::string(name, NS::UTF8StringEncoding));
}

} // namespace nri
