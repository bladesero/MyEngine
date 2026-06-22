#include "UI/Rml/RmlAssetLoader.h"

#include "Assets/AssetManager.h"
#include "Core/Logger.h"
#include "Core/EngineTime.h"

#include <filesystem>
#include <sstream>

std::string RmlAssetLoader::Resolve(const std::string& path) const
{
    return AssetManager::Get().ResolvePath(path);
}

Rml::FileHandle RmlAssetLoader::Open(const Rml::String& path)
{
    auto stream = std::make_unique<std::ifstream>(
        Resolve(path), std::ios::binary);
    if (!*stream) return 0;
    return reinterpret_cast<Rml::FileHandle>(stream.release());
}

void RmlAssetLoader::Close(Rml::FileHandle file)
{
    delete reinterpret_cast<std::ifstream*>(file);
}

size_t RmlAssetLoader::Read(void* buffer, size_t size, Rml::FileHandle file)
{
    auto* stream = reinterpret_cast<std::ifstream*>(file);
    if (!stream || !buffer || size == 0) return 0;
    stream->read(reinterpret_cast<char*>(buffer), static_cast<std::streamsize>(size));
    return static_cast<size_t>(stream->gcount());
}

bool RmlAssetLoader::Seek(Rml::FileHandle file, long offset, int origin)
{
    auto* stream = reinterpret_cast<std::ifstream*>(file);
    if (!stream) return false;
    std::ios_base::seekdir direction = std::ios::beg;
    if (origin == SEEK_CUR) direction = std::ios::cur;
    else if (origin == SEEK_END) direction = std::ios::end;
    stream->clear();
    stream->seekg(offset, direction);
    return static_cast<bool>(*stream);
}

size_t RmlAssetLoader::Tell(Rml::FileHandle file)
{
    auto* stream = reinterpret_cast<std::ifstream*>(file);
    if (!stream) return 0;
    return static_cast<size_t>(stream->tellg());
}

bool RmlAssetLoader::LoadFile(const Rml::String& path, Rml::String& outData)
{
    std::ifstream input(Resolve(path), std::ios::binary);
    if (!input) return false;
    std::ostringstream ss;
    ss << input.rdbuf();
    outData = ss.str();
    return true;
}

double RmlAssetLoader::GetElapsedTime()
{
    return Time::TotalSeconds();
}

void RmlAssetLoader::JoinPath(Rml::String& translatedPath,
                              const Rml::String& documentPath,
                              const Rml::String& path)
{
    std::filesystem::path input(path);
    if (input.is_absolute() || path.rfind("Content/", 0) == 0 ||
        path.rfind("__builtin__/", 0) == 0) {
        translatedPath = path;
        return;
    }
    translatedPath = (std::filesystem::path(documentPath).parent_path() / input).generic_string();
}

bool RmlAssetLoader::LogMessage(Rml::Log::Type type, const Rml::String& message)
{
    switch (type) {
    case Rml::Log::LT_ERROR: Logger::Error("[RmlUi] ", message); break;
    case Rml::Log::LT_WARNING: Logger::Warn("[RmlUi] ", message); break;
    default: Logger::Info("[RmlUi] ", message); break;
    }
    return true;
}
