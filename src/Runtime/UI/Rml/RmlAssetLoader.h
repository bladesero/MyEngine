#pragma once

#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/SystemInterface.h>

#include <istream>
#include <memory>
#include <string>

class RmlAssetLoader final : public Rml::FileInterface, public Rml::SystemInterface {
public:
    Rml::FileHandle Open(const Rml::String& path) override;
    void Close(Rml::FileHandle file) override;
    size_t Read(void* buffer, size_t size, Rml::FileHandle file) override;
    bool Seek(Rml::FileHandle file, long offset, int origin) override;
    size_t Tell(Rml::FileHandle file) override;
    bool LoadFile(const Rml::String& path, Rml::String& outData) override;

    double GetElapsedTime() override;
    void JoinPath(Rml::String& translatedPath,
                  const Rml::String& documentPath,
                  const Rml::String& path) override;
    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override;

private:
    std::string Resolve(const std::string& path) const;
};
