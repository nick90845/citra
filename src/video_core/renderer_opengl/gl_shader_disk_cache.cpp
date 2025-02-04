// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cstring>
#include <fmt/format.h>
#include "common/assert.h"
#include "common/common_paths.h"
#include "common/common_types.h"
#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/version.h"
#include "common/zstd_compression.h"
#include "core/core.h"
#include "core/hle/kernel/process.h"
#include "core/settings.h"
#include "video_core/renderer_opengl/gl_shader_disk_cache.h"

namespace OpenGL {

enum class TransferableEntryKind : u32 {
    Raw,
};

enum class PrecompiledEntryKind : u32 {
    Decompiled,
    Dump,
};

ShaderDiskCacheRaw::ShaderDiskCacheRaw(u64 unique_identifier, ProgramType program_type,
                                       RawShaderConfig config, ProgramCode program_code)
    : unique_identifier{unique_identifier}, program_type{program_type}, config{config},
      program_code{std::move(program_code)} {}

ShaderDiskCacheRaw::ShaderDiskCacheRaw() = default;

ShaderDiskCacheRaw::~ShaderDiskCacheRaw() = default;

bool ShaderDiskCacheRaw::Load(FileUtil::IOFile& file) {
    if (file.ReadBytes(&unique_identifier, sizeof(u64)) != sizeof(u64) ||
        file.ReadBytes(&program_type, sizeof(u32)) != sizeof(u32)) {
        return false;
    }

    u64 reg_array_len;
    if (file.ReadBytes(&reg_array_len, sizeof(u64)) != sizeof(u64)) {
        return false;
    }

    if (file.ReadArray(config.reg_array.data(), reg_array_len) != reg_array_len) {
        return false;
    }

    // Read in type specific configuration
    if (program_type == ProgramType::VS) {
        u64 code_len;
        if (file.ReadBytes(&code_len, sizeof(u64)) != sizeof(u64)) {
            return false;
        }
        program_code.resize(code_len);
        if (file.ReadArray(program_code.data(), code_len) != code_len) {
            return false;
        }
    }

    return true;
}

bool ShaderDiskCacheRaw::Save(FileUtil::IOFile& file) const {
    if (file.WriteObject(unique_identifier) != 1 ||
        file.WriteObject(static_cast<u32>(program_type)) != 1) {
        return false;
    }

    // Just for future proofing, save the sizes of the array to the file
    const std::size_t reg_array_len = Pica::Regs::NUM_REGS;
    if (file.WriteObject(static_cast<u64>(reg_array_len)) != 1) {
        return false;
    }
    if (file.WriteArray(config.reg_array.data(), reg_array_len) != reg_array_len) {
        return false;
    }

    if (program_type == ProgramType::VS) {
        const std::size_t code_len = program_code.size();
        if (file.WriteObject(static_cast<u64>(code_len)) != 1) {
            return false;
        }
        if (file.WriteArray(program_code.data(), code_len) != code_len) {
            return false;
        }
    }
    return true;
}

ShaderDiskCache::ShaderDiskCache(bool separable) : separable{separable} {}

ShaderDiskCache::~ShaderDiskCache() = default;

std::optional<std::vector<ShaderDiskCacheRaw>> ShaderDiskCache::LoadTransferable() {
    const bool has_title_id = GetProgramID() != 0;
    if (!Settings::values.use_disk_shader_cache || !has_title_id) {
        return std::nullopt;
    }
    tried_to_load = true;

    FileUtil::IOFile file(GetTransferablePath(), "rb");
    if (!file.IsOpen()) {
        LOG_INFO(Render_OpenGL, "No transferable shader cache found for game with title id={}",
                 GetTitleID());
        return std::nullopt;
    }

    u16 version;
    if (file.ReadBytes(&version, sizeof(version)) != sizeof(version)) {
        LOG_ERROR(Render_OpenGL,
                  "Failed to get transferable cache version for title id={} - skipping",
                  GetTitleID());
        return std::nullopt;
    }
    if (version != Version::shader_cache) {
        LOG_INFO(Render_OpenGL, "Different transferable cache version - removing");
        file.Close();
        InvalidateAll();
        return std::nullopt;
    }

    // Load the shaders
    std::vector<ShaderDiskCacheRaw> raws;
    while (file.Tell() < file.GetSize()) {
        TransferableEntryKind kind;
        if (file.ReadBytes(&kind, sizeof(u32)) != sizeof(u32)) {
            LOG_ERROR(Render_OpenGL, "Failed to read transferable file - skipping");
            return std::nullopt;
        }

        switch (kind) {
        case TransferableEntryKind::Raw: {
            ShaderDiskCacheRaw entry;
            if (!entry.Load(file)) {
                LOG_ERROR(Render_OpenGL, "Failed to load transferable raw entry - skipping");
                return std::nullopt;
            }
            transferable.insert({entry.GetUniqueIdentifier(), {}});
            raws.push_back(std::move(entry));
            break;
        }
        default:
            LOG_ERROR(Render_OpenGL, "Unknown transferable shader cache entry kind={} - skipping",
                      static_cast<u32>(kind));
            return std::nullopt;
        }
    }

    LOG_INFO(Render_OpenGL, "Loaded {} raws", raws.size());

    return raws;
}

std::pair<std::unordered_map<u64, ShaderDiskCacheDecompiled>, ShaderDumpsMap>
ShaderDiskCache::LoadPrecompiled() {
    if (!IsUsable())
        return {};

    FileUtil::IOFile file(GetPrecompiledPath(), "rb");
    if (!file.IsOpen()) {
        LOG_INFO(Render_OpenGL, "No precompiled shader cache found for game with title id={}",
                 GetTitleID());
        return {};
    }

    const auto result = LoadPrecompiledFile(file);
    if (!result) {
        LOG_ERROR(Render_OpenGL,
                  "Failed to load precompiled cache for game with title id={} - removing",
                  GetTitleID());
        file.Close();
        InvalidatePrecompiled();
        return {};
    }
    return *result;
}

std::optional<std::pair<std::unordered_map<u64, ShaderDiskCacheDecompiled>, ShaderDumpsMap>>
ShaderDiskCache::LoadPrecompiledFile(FileUtil::IOFile& file) {
    u16 version;
    if (file.ReadBytes(&version, sizeof(version)) != sizeof(version)) {
        LOG_ERROR(Render_OpenGL, "Could not read version from precompiled shader cache");
        return std::nullopt;
    }
    if (version != Version::shader_cache) {
        LOG_ERROR(Render_OpenGL, "Different shader cache version");
        return std::nullopt;
    }

    // Read compressed file from disk and decompress to virtual precompiled cache file
    std::vector<u8> compressed(file.GetSize() - sizeof(version));
    file.ReadBytes(compressed.data(), compressed.size());
    const std::vector<u8> decompressed = Common::Compression::DecompressDataZSTD(compressed);
    SaveArrayToPrecompiled(decompressed.data(), decompressed.size());
    decompressed_precompiled_cache_offset = 0;

    std::unordered_map<u64, ShaderDiskCacheDecompiled> decompiled;
    ShaderDumpsMap dumps;
    while (decompressed_precompiled_cache_offset < decompressed_precompiled_cache.size()) {
        PrecompiledEntryKind kind;
        if (!LoadObjectFromPrecompiled(kind)) {
            return std::nullopt;
        }

        switch (kind) {
        case PrecompiledEntryKind::Decompiled: {
            u64 unique_identifier;
            if (!LoadObjectFromPrecompiled(unique_identifier)) {
                return std::nullopt;
            }

            auto entry = LoadDecompiledEntry();
            if (!entry) {
                return std::nullopt;
            }
            decompiled.insert({unique_identifier, std::move(*entry)});
            break;
        }
        case PrecompiledEntryKind::Dump: {
            u64 unique_identifier;
            if (!LoadObjectFromPrecompiled(unique_identifier)) {
                return std::nullopt;
            }

            ShaderDiskCacheDump dump;
            if (!LoadObjectFromPrecompiled(dump.binary_format)) {
                return std::nullopt;
            }

            u32 binary_length;
            if (!LoadObjectFromPrecompiled(binary_length)) {
                return std::nullopt;
            }

            dump.binary.resize(binary_length);
            if (!LoadArrayFromPrecompiled(dump.binary.data(), dump.binary.size())) {
                return std::nullopt;
            }

            dumps.insert({unique_identifier, dump});
            break;
        }
        default:
            return std::nullopt;
        }
    }
    LOG_INFO(Render_OpenGL, "Loaded {} decompiled", decompiled.size());
    LOG_INFO(Render_OpenGL, "Loaded {} dumps", dumps.size());
    return {{decompiled, dumps}};
}

std::optional<ShaderDiskCacheDecompiled> ShaderDiskCache::LoadDecompiledEntry() {
    u32 code_size;
    if (!LoadObjectFromPrecompiled(code_size)) {
        return std::nullopt;
    }

    std::string code(code_size, '\0');
    if (!LoadArrayFromPrecompiled(code.data(), code.size())) {
        return std::nullopt;
    }

    ShaderDiskCacheDecompiled entry;
    entry.code = std::move(code);

    return entry;
}

bool ShaderDiskCache::SaveDecompiledFile(u64 unique_identifier,
                                         const ShaderDecompiler::ProgramResult& code) {
    if (!SaveObjectToPrecompiled(static_cast<u32>(PrecompiledEntryKind::Decompiled)) ||
        !SaveObjectToPrecompiled(unique_identifier) ||
        !SaveObjectToPrecompiled(static_cast<u32>(code.size())) ||
        !SaveArrayToPrecompiled(code.data(), code.size())) {
        return false;
    }

    return true;
}

void ShaderDiskCache::InvalidateAll() {
    if (!FileUtil::Delete(GetTransferablePath())) {
        LOG_ERROR(Render_OpenGL, "Failed to invalidate transferable file={}",
                  GetTransferablePath());
    }
    InvalidatePrecompiled();
}

void ShaderDiskCache::InvalidatePrecompiled() {
    // Clear virtaul precompiled cache file
    decompressed_precompiled_cache.resize(0);

    if (!FileUtil::Delete(GetPrecompiledPath())) {
        LOG_ERROR(Render_OpenGL, "Failed to invalidate precompiled file={}", GetPrecompiledPath());
    }
}

void ShaderDiskCache::SaveRaw(const ShaderDiskCacheRaw& entry) {
    if (!IsUsable())
        return;

    const u64 id = entry.GetUniqueIdentifier();
    if (transferable.find(id) != transferable.end()) {
        // The shader already exists
        return;
    }

    FileUtil::IOFile file = AppendTransferableFile();
    if (!file.IsOpen())
        return;
    if (file.WriteObject(TransferableEntryKind::Raw) != 1 || !entry.Save(file)) {
        LOG_ERROR(Render_OpenGL, "Failed to save raw transferable cache entry - removing");
        file.Close();
        InvalidateAll();
        return;
    }
    transferable.insert({id, entry});
}

void ShaderDiskCache::SaveDecompiled(u64 unique_identifier,
                                     const ShaderDecompiler::ProgramResult& code) {
    if (!IsUsable())
        return;

    if (!SaveDecompiledFile(unique_identifier, code)) {
        LOG_ERROR(Render_OpenGL,
                  "Failed to save decompiled entry to the precompiled file - removing");
        InvalidatePrecompiled();
    }
}

void ShaderDiskCache::SaveDump(u64 unique_identifier, GLuint program) {
    if (!IsUsable())
        return;

    GLint binary_length;
    glGetProgramiv(program, GL_PROGRAM_BINARY_LENGTH, &binary_length);

    GLenum binary_format;
    std::vector<u8> binary(binary_length);
    glGetProgramBinary(program, binary_length, nullptr, &binary_format, binary.data());

    if (!SaveObjectToPrecompiled(static_cast<u32>(PrecompiledEntryKind::Dump)) ||
        !SaveObjectToPrecompiled(unique_identifier) ||
        !SaveObjectToPrecompiled(static_cast<u32>(binary_format)) ||
        !SaveObjectToPrecompiled(static_cast<u32>(binary_length)) ||
        !SaveArrayToPrecompiled(binary.data(), binary.size())) {
        LOG_ERROR(Render_OpenGL, "Failed to save binary program file in shader={:016x} - removing",
                  unique_identifier);
        InvalidatePrecompiled();
        return;
    }
}

bool ShaderDiskCache::IsUsable() const {
    return tried_to_load && Settings::values.use_disk_shader_cache;
}

FileUtil::IOFile ShaderDiskCache::AppendTransferableFile() {
    if (!EnsureDirectories()) {
        return {};
    }

    const std::string transferable_path = GetTransferablePath();
    const bool existed = FileUtil::Exists(transferable_path);

    FileUtil::IOFile file(transferable_path, "ab");
    if (!file.IsOpen()) {
        LOG_ERROR(Render_OpenGL, "Failed to open transferable cache in path={}", transferable_path);
        return {};
    }
    if (!existed || file.GetSize() == 0) {
        // If the file didn't exist, write its major version
        if (file.WriteBytes(&Version::shader_cache, sizeof(Version::shader_cache)) !=
            sizeof(Version::shader_cache)) {
            LOG_ERROR(Render_OpenGL, "Failed to write transferable cache major version in path={}",
                      transferable_path);
            return {};
        }
    }
    return file;
}

void ShaderDiskCache::SaveVirtualPrecompiledFile() {
    decompressed_precompiled_cache_offset = 0;
    const std::vector<u8>& compressed = Common::Compression::CompressDataZSTDDefault(
        decompressed_precompiled_cache.data(), decompressed_precompiled_cache.size());

    const std::string precompiled_path = GetPrecompiledPath();
    FileUtil::IOFile file(precompiled_path, "wb");

    if (!file.IsOpen()) {
        LOG_ERROR(Render_OpenGL, "Failed to open precompiled cache in path={}", precompiled_path);
        return;
    }
    if (file.WriteBytes(&Version::shader_cache, sizeof(Version::shader_cache)) !=
        sizeof(Version::shader_cache)) {
        LOG_ERROR(Render_OpenGL, "Failed to write precompiled cache major version");
        return;
    }
    if (file.WriteBytes(compressed.data(), compressed.size()) != compressed.size()) {
        LOG_ERROR(Render_OpenGL, "Failed to write compressed precompiled cache in path={}",
                  precompiled_path);
        return;
    }
}

bool ShaderDiskCache::EnsureDirectories() const {
    const auto CreateDir = [](const std::string& dir) {
        if (!FileUtil::CreateDir(dir)) {
            LOG_ERROR(Render_OpenGL, "Failed to create directory={}", dir);
            return false;
        }
        return true;
    };

    return CreateDir(FileUtil::GetUserPath(FileUtil::UserPath::ShaderDir)) &&
           CreateDir(GetBaseDir()) && CreateDir(GetTransferableDir()) &&
           CreateDir(GetPrecompiledDir());
}

std::string ShaderDiskCache::GetTransferablePath() {
    return FileUtil::SanitizePath(GetTransferableDir() + DIR_SEP_CHR + GetTitleID() + ".bin");
}

std::string ShaderDiskCache::GetPrecompiledPath() {
    return FileUtil::SanitizePath(GetPrecompiledDir() + DIR_SEP_CHR + GetTitleID() + ".bin");
}

std::string ShaderDiskCache::GetTransferableDir() const {
    return GetBaseDir() + DIR_SEP "transferable";
}

std::string ShaderDiskCache::GetPrecompiledDir() const {
    return GetBaseDir() + DIR_SEP "precompiled";
}

std::string ShaderDiskCache::GetBaseDir() const {
    return FileUtil::GetUserPath(FileUtil::UserPath::ShaderDir) + DIR_SEP "opengl";
}

u64 ShaderDiskCache::GetProgramID() {
    // Skip games without title id
    if (program_id != 0) {
        return program_id;
    }
    if (Core::System::GetInstance().GetAppLoader().ReadProgramId(program_id) !=
        Loader::ResultStatus::Success) {
        return 0;
    }
    return program_id;
}

std::string ShaderDiskCache::GetTitleID() {
    if (!title_id.empty()) {
        return title_id;
    }
    title_id = fmt::format("{:016X}", GetProgramID());
    return title_id;
}

} // namespace OpenGL
