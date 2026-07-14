#include "Renderer/ShaderCacheService.h"

ShaderCacheService& ShaderCacheService::Get() {
    static ShaderCacheService service;
    return service;
}

void ShaderCacheService::SetResolver(Resolver resolver) {
    m_Resolver = std::move(resolver);
}

void ShaderCacheService::ClearResolver() {
    m_Resolver = {};
}

ShaderCacheResult ShaderCacheService::EnsureShaderArtifact(const ShaderCacheRequest& request) const {
    if (!m_Resolver) {
        return {false, false, {}, "shader cache service is not configured"};
    }
    return m_Resolver(request);
}
