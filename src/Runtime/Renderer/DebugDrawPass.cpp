#include "Renderer/DebugDrawPass.h"

#include "Core/Logger.h"
#include "Renderer/EngineShaderCatalog.h"
#include "Renderer/RHI/GpuBindGroup.h"
#include "Renderer/RHI/GpuPipeline.h"
#include "Renderer/RHI/VertexLayout.h"
#include "Renderer/ShaderManager.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

namespace {

constexpr size_t kInstancesPerDraw = 64;
constexpr uint32_t kCircleSegments = 32;

struct DebugDrawVertex {
    Vec3 position;
    Vec2 auxiliary;
};

struct DebugDrawInstance {
    Mat4 transform = Mat4::Identity();
    Color color{};
    Vec4 shapeParameters{0.0f, 0.0f, 0.0f, 0.0f};
};

struct DebugDrawConstants {
    float viewProjection[16]{};
    float world[kInstancesPerDraw][16]{};
    float color[kInstancesPerDraw][4]{};
    float shapeParameters[kInstancesPerDraw][4]{};
};

struct GeometryGpu {
    std::shared_ptr<GpuBuffer> vertexBuffer;
    std::shared_ptr<GpuBuffer> indexBuffer;
    uint32_t indexCount = 0;

    bool IsValid() const { return vertexBuffer && indexBuffer && indexCount > 0; }
};

struct PreparedGroup {
    GeometryGpu geometry;
    DebugDrawDepthMode depthMode = DebugDrawDepthMode::Test;
    std::vector<DebugDrawInstance> instances;
};

void AddSegment(std::vector<DebugDrawVertex>& vertices, std::vector<uint32_t>& indices, const Vec3& a, float auxiliaryA,
                const Vec3& b, float auxiliaryB) {
    const uint32_t base = static_cast<uint32_t>(vertices.size());
    vertices.push_back({a, {auxiliaryA, 0.0f}});
    vertices.push_back({b, {auxiliaryB, 0.0f}});
    indices.push_back(base);
    indices.push_back(base + 1u);
}

std::vector<uint32_t> BuildLineIndices(size_t vertexCount, const std::vector<uint32_t>& triangleIndices) {
    std::unordered_set<uint64_t> uniqueEdges;
    const auto addEdge = [&](uint32_t a, uint32_t b) {
        if (a >= vertexCount || b >= vertexCount)
            return false;
        const uint32_t lo = (std::min)(a, b);
        const uint32_t hi = (std::max)(a, b);
        uniqueEdges.insert((static_cast<uint64_t>(lo) << 32u) | hi);
        return true;
    };

    if (!triangleIndices.empty()) {
        if (triangleIndices.size() % 3u != 0u)
            return {};
        for (size_t i = 0; i < triangleIndices.size(); i += 3u) {
            const uint32_t a = triangleIndices[i + 0u];
            const uint32_t b = triangleIndices[i + 1u];
            const uint32_t c = triangleIndices[i + 2u];
            if (!addEdge(a, b) || !addEdge(b, c) || !addEdge(c, a))
                return {};
        }
    } else {
        if (vertexCount % 3u != 0u)
            return {};
        for (uint32_t i = 0; i < vertexCount; i += 3u) {
            addEdge(i, i + 1u);
            addEdge(i + 1u, i + 2u);
            addEdge(i + 2u, i);
        }
    }

    std::vector<uint64_t> sortedEdges(uniqueEdges.begin(), uniqueEdges.end());
    std::sort(sortedEdges.begin(), sortedEdges.end());
    std::vector<uint32_t> result;
    result.reserve(sortedEdges.size() * 2u);
    for (uint64_t edge : sortedEdges) {
        result.push_back(static_cast<uint32_t>(edge >> 32u));
        result.push_back(static_cast<uint32_t>(edge));
    }
    return result;
}

} // namespace

struct DebugDrawPass::Impl {
    struct AssetGeometryEntry {
        std::weak_ptr<MeshAsset> asset;
        uint64_t version = 0;
        GeometryGpu geometry;
    };

    struct ProceduralGeometryEntry {
        std::weak_ptr<const DebugDrawMesh> mesh;
        GeometryGpu geometry;
    };

    explicit Impl(IRHIDevice* inDevice) : device(inDevice) {}

    IRHIDevice* device = nullptr;
    std::shared_ptr<ShaderHandle> shaderHandle;
    uint64_t shaderVersion = 0;
    std::unordered_map<uint64_t, std::shared_ptr<GpuGraphicsPipeline>> pipelines;
    std::array<GeometryGpu, 4> builtins;
    std::unordered_map<AssetID, AssetGeometryEntry> assetGeometry;
    std::unordered_map<const DebugDrawMesh*, ProceduralGeometryEntry> proceduralGeometry;
    std::vector<PreparedGroup> groups;
    RHIFormat colorFormat = RHIFormat::Unknown;
    RHIFormat depthFormat = RHIFormat::Unknown;
    size_t preparedCommandCount = 0;
    size_t preparedDrawCount = 0;
    bool loggedBindingFailure = false;

    GpuShader* GetShader() {
        if (!device)
            return nullptr;
        if (!shaderHandle) {
            const VertexElement layout[] = {
                {"POSITION", 0, VertexFormat::Float3, offsetof(DebugDrawVertex, position)},
                {"TEXCOORD", 0, VertexFormat::Float2, offsetof(DebugDrawVertex, auxiliary)},
            };
            shaderHandle = ShaderManager::Get().GetOrCreate(EngineShaders::kDebugDraw, layout, 2);
        }
        if (shaderHandle && shaderHandle->version != shaderVersion) {
            shaderVersion = shaderHandle->version;
            pipelines.clear();
            loggedBindingFailure = false;
        }
        return shaderHandle ? shaderHandle->shader.get() : nullptr;
    }

    GpuGraphicsPipeline* GetPipeline(DebugDrawDepthMode mode) {
        const uint64_t key = static_cast<uint64_t>(colorFormat) | (static_cast<uint64_t>(depthFormat) << 8u) |
                             (static_cast<uint64_t>(mode) << 16u);
        if (const auto found = pipelines.find(key); found != pipelines.end())
            return found->second.get();
        GpuShader* shader = GetShader();
        if (!shader || !device)
            return nullptr;
        GraphicsPipelineDesc desc;
        desc.shader = shaderHandle->shader;
        desc.colorFormats = {colorFormat};
        desc.depthFormat = depthFormat;
        desc.topology = RHIPrimitiveTopology::LineList;
        desc.rasterizer.cullMode = RHICullMode::None;
        desc.depthStencil.depthTestEnable = mode == DebugDrawDepthMode::Test;
        desc.depthStencil.depthWriteEnable = false;
        desc.depthStencil.depthCompareOp = RHICompareOp::LessEqual;
        desc.blend.attachments[0].blendEnable = true;
        auto pipeline = device->CreateGraphicsPipeline(desc);
        if (!pipeline)
            return nullptr;
        GpuGraphicsPipeline* result = pipeline.get();
        pipelines.emplace(key, std::move(pipeline));
        return result;
    }

    GeometryGpu CreateGeometry(const std::vector<DebugDrawVertex>& vertices,
                               const std::vector<uint32_t>& indices) const {
        GeometryGpu result;
        if (!device || vertices.empty() || indices.empty())
            return result;
        result.vertexBuffer = device->CreateVertexBuffer(
            vertices.data(), static_cast<uint32_t>(vertices.size() * sizeof(vertices[0])), sizeof(DebugDrawVertex));
        result.indexBuffer =
            device->CreateIndexBuffer(indices.data(), static_cast<uint32_t>(indices.size() * sizeof(indices[0])));
        result.indexCount = static_cast<uint32_t>(indices.size());
        if (!result.vertexBuffer || !result.indexBuffer)
            return {};
        return result;
    }

    GeometryGpu CreateGeometry(const std::vector<Vec3>& positions, const std::vector<uint32_t>& indices) const {
        std::vector<DebugDrawVertex> vertices;
        vertices.reserve(positions.size());
        for (const Vec3& position : positions)
            vertices.push_back({position, {0.0f, 0.0f}});
        return CreateGeometry(vertices, indices);
    }

    GeometryGpu BuildBuiltin(DebugDrawGeometryKind kind) const {
        std::vector<DebugDrawVertex> vertices;
        std::vector<uint32_t> indices;
        if (kind == DebugDrawGeometryKind::Line) {
            AddSegment(vertices, indices, Vec3::Zero(), 0.0f, Vec3::Right(), 0.0f);
        } else if (kind == DebugDrawGeometryKind::Box) {
            const std::array<Vec3, 8> corners = {
                Vec3{-1, -1, -1}, Vec3{1, -1, -1}, Vec3{1, 1, -1}, Vec3{-1, 1, -1},
                Vec3{-1, -1, 1},  Vec3{1, -1, 1},  Vec3{1, 1, 1},  Vec3{-1, 1, 1},
            };
            const uint32_t edges[][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
                                         {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
            for (const auto& edge : edges)
                AddSegment(vertices, indices, corners[edge[0]], 0.0f, corners[edge[1]], 0.0f);
        } else if (kind == DebugDrawGeometryKind::Sphere) {
            for (uint32_t circle = 0; circle < 3; ++circle) {
                for (uint32_t segment = 0; segment < kCircleSegments; ++segment) {
                    const float a0 = kTwoPi * static_cast<float>(segment) / static_cast<float>(kCircleSegments);
                    const float a1 = kTwoPi * static_cast<float>(segment + 1u) / static_cast<float>(kCircleSegments);
                    const float c0 = std::cos(a0), s0 = std::sin(a0);
                    const float c1 = std::cos(a1), s1 = std::sin(a1);
                    const Vec3 p0 = circle == 0 ? Vec3{c0, s0, 0} : circle == 1 ? Vec3{c0, 0, s0} : Vec3{0, c0, s0};
                    const Vec3 p1 = circle == 0 ? Vec3{c1, s1, 0} : circle == 1 ? Vec3{c1, 0, s1} : Vec3{0, c1, s1};
                    AddSegment(vertices, indices, p0, 0.0f, p1, 0.0f);
                }
            }
        } else if (kind == DebugDrawGeometryKind::Capsule) {
            for (float sign : {-1.0f, 1.0f}) {
                for (uint32_t segment = 0; segment < kCircleSegments; ++segment) {
                    const float a0 = kTwoPi * static_cast<float>(segment) / static_cast<float>(kCircleSegments);
                    const float a1 = kTwoPi * static_cast<float>(segment + 1u) / static_cast<float>(kCircleSegments);
                    AddSegment(vertices, indices, {std::cos(a0), sign, std::sin(a0)}, sign,
                               {std::cos(a1), sign, std::sin(a1)}, sign);
                }
            }
            const Vec3 cardinal[] = {Vec3::Right(), -Vec3::Right(), Vec3::Forward(), -Vec3::Forward()};
            for (const Vec3& point : cardinal)
                AddSegment(vertices, indices, {point.x, 1.0f, point.z}, 1.0f, {point.x, -1.0f, point.z}, -1.0f);
            for (uint32_t plane = 0; plane < 2; ++plane) {
                for (float sign : {-1.0f, 1.0f}) {
                    for (uint32_t segment = 0; segment < kCircleSegments / 2u; ++segment) {
                        const float a0 = kPi * static_cast<float>(segment) / static_cast<float>(kCircleSegments / 2u);
                        const float a1 =
                            kPi * static_cast<float>(segment + 1u) / static_cast<float>(kCircleSegments / 2u);
                        const float y0 = sign + sign * std::sin(a0);
                        const float y1 = sign + sign * std::sin(a1);
                        const Vec3 p0 = plane == 0 ? Vec3{std::cos(a0), y0, 0.0f} : Vec3{0.0f, y0, std::cos(a0)};
                        const Vec3 p1 = plane == 0 ? Vec3{std::cos(a1), y1, 0.0f} : Vec3{0.0f, y1, std::cos(a1)};
                        AddSegment(vertices, indices, p0, sign, p1, sign);
                    }
                }
            }
        }
        return CreateGeometry(vertices, indices);
    }

    GeometryGpu GetBuiltin(DebugDrawGeometryKind kind) {
        const size_t index = static_cast<size_t>(kind);
        if (index >= builtins.size())
            return {};
        if (!builtins[index].IsValid())
            builtins[index] = BuildBuiltin(kind);
        return builtins[index];
    }

    GeometryGpu GetAssetGeometry(const MeshHandle& handle) {
        if (!handle.IsValid())
            return {};
        const std::shared_ptr<MeshAsset>& mesh = handle.Shared();
        auto found = assetGeometry.find(mesh->GetID());
        if (found != assetGeometry.end() && found->second.asset.lock() == mesh &&
            found->second.version == mesh->GetVersion() && found->second.geometry.IsValid()) {
            return found->second.geometry;
        }
        std::vector<Vec3> positions;
        positions.reserve(mesh->GetVertices().size());
        for (const MeshVertex& vertex : mesh->GetVertices())
            positions.push_back(vertex.position);
        const std::vector<uint32_t> lineIndices = BuildLineIndices(positions.size(), mesh->GetIndices());
        GeometryGpu geometry = CreateGeometry(positions, lineIndices);
        if (geometry.IsValid())
            assetGeometry[mesh->GetID()] = {mesh, mesh->GetVersion(), geometry};
        return geometry;
    }

    GeometryGpu GetProceduralGeometry(const DebugDrawMeshHandle& handle) {
        if (!handle || !handle->IsValid())
            return {};
        auto found = proceduralGeometry.find(handle.get());
        if (found != proceduralGeometry.end() && found->second.geometry.IsValid())
            return found->second.geometry;
        GeometryGpu geometry = CreateGeometry(handle->GetPositions(), handle->GetLineIndices());
        if (geometry.IsValid())
            proceduralGeometry[handle.get()] = {handle, geometry};
        return geometry;
    }

    void PruneCaches() {
        for (auto it = assetGeometry.begin(); it != assetGeometry.end();) {
            if (it->second.asset.expired())
                it = assetGeometry.erase(it);
            else
                ++it;
        }
        for (auto it = proceduralGeometry.begin(); it != proceduralGeometry.end();) {
            if (it->second.mesh.expired())
                it = proceduralGeometry.erase(it);
            else
                ++it;
        }
    }

    GeometryGpu ResolveGeometry(const DebugDrawCommand& command) {
        switch (command.geometry) {
        case DebugDrawGeometryKind::Line:
        case DebugDrawGeometryKind::Box:
        case DebugDrawGeometryKind::Sphere:
        case DebugDrawGeometryKind::Capsule:
            return GetBuiltin(command.geometry);
        case DebugDrawGeometryKind::MeshAsset:
            return GetAssetGeometry(command.mesh);
        case DebugDrawGeometryKind::ProceduralMesh:
            return GetProceduralGeometry(command.proceduralMesh);
        }
        return {};
    }
};

DebugDrawPass::DebugDrawPass(IRHIDevice* device) : RenderPass(device), m_Impl(std::make_unique<Impl>(device)) {
}
DebugDrawPass::~DebugDrawPass() = default;

void DebugDrawPass::Execute(GpuCommandList&, const Scene&, const Camera&) {
}

bool DebugDrawPass::Prepare(const std::vector<DebugDrawCommand>& commands, RHIFormat colorFormat, RHIFormat depthFormat,
                            DebugDrawViewMask viewMask) {
    m_Impl->groups.clear();
    m_Impl->preparedCommandCount = 0;
    m_Impl->preparedDrawCount = 0;
    m_Impl->colorFormat = colorFormat;
    m_Impl->depthFormat = depthFormat;
    m_Impl->PruneCaches();
    if (commands.empty() || !m_Impl->GetShader())
        return false;

    struct GroupKey {
        const GpuBuffer* vertexBuffer = nullptr;
        DebugDrawDepthMode depthMode = DebugDrawDepthMode::Test;
        bool operator==(const GroupKey& other) const {
            return vertexBuffer == other.vertexBuffer && depthMode == other.depthMode;
        }
    };
    struct GroupKeyHash {
        size_t operator()(const GroupKey& key) const {
            return std::hash<const void*>{}(key.vertexBuffer) ^ (static_cast<size_t>(key.depthMode) << 1u);
        }
    };
    std::unordered_map<GroupKey, size_t, GroupKeyHash> groupLookup;
    for (const DebugDrawCommand& command : commands) {
        if (!DebugDrawViewMatches(command.viewMask, viewMask))
            continue;
        GeometryGpu geometry = m_Impl->ResolveGeometry(command);
        if (!geometry.IsValid())
            continue;
        const GroupKey key{geometry.vertexBuffer.get(), command.depthMode};
        auto found = groupLookup.find(key);
        if (found == groupLookup.end()) {
            const size_t index = m_Impl->groups.size();
            m_Impl->groups.push_back({geometry, command.depthMode, {}});
            found = groupLookup.emplace(key, index).first;
        }
        m_Impl->groups[found->second].instances.push_back({command.transform, command.color, command.shapeParameters});
        ++m_Impl->preparedCommandCount;
    }

    for (const PreparedGroup& group : m_Impl->groups) {
        if (!m_Impl->GetPipeline(group.depthMode)) {
            m_Impl->groups.clear();
            m_Impl->preparedCommandCount = 0;
            return false;
        }
        m_Impl->preparedDrawCount += (group.instances.size() + kInstancesPerDraw - 1u) / kInstancesPerDraw;
    }
    return !m_Impl->groups.empty();
}

void DebugDrawPass::ExecutePrepared(GpuCommandList& commands, const Camera& camera) {
    if (m_Impl->groups.empty() || !m_Impl->shaderHandle || !m_Impl->shaderHandle->shader)
        return;
    const Mat4 viewProjection = camera.GetViewProj();
    for (const PreparedGroup& group : m_Impl->groups) {
        GpuGraphicsPipeline* pipeline = m_Impl->GetPipeline(group.depthMode);
        if (!pipeline)
            continue;
        commands.SetGraphicsPipeline(pipeline);
        commands.BindVertexBuffer(group.geometry.vertexBuffer.get());
        commands.BindIndexBuffer(group.geometry.indexBuffer.get());
        for (size_t first = 0; first < group.instances.size(); first += kInstancesPerDraw) {
            const size_t count = (std::min)(kInstancesPerDraw, group.instances.size() - first);
            DebugDrawConstants constants{};
            std::memcpy(constants.viewProjection, viewProjection.Data(), sizeof(constants.viewProjection));
            for (size_t i = 0; i < count; ++i) {
                const DebugDrawInstance& instance = group.instances[first + i];
                std::memcpy(constants.world[i], instance.transform.Data(), sizeof(constants.world[i]));
                const Vec4 color = instance.color.ToVec4();
                constants.color[i][0] = color.x;
                constants.color[i][1] = color.y;
                constants.color[i][2] = color.z;
                constants.color[i][3] = color.w;
                constants.shapeParameters[i][0] = instance.shapeParameters.x;
                constants.shapeParameters[i][1] = instance.shapeParameters.y;
                constants.shapeParameters[i][2] = instance.shapeParameters.z;
                constants.shapeParameters[i][3] = instance.shapeParameters.w;
            }
            auto bindings = m_Impl->device->CreateBindGroup(m_Impl->shaderHandle->shader);
            std::string error;
            if (!bindings || !bindings->SetConstants("DebugDrawConstants", &constants, sizeof(constants)) ||
                !bindings->Validate(&error)) {
                if (!m_Impl->loggedBindingFailure) {
                    Logger::Warn("[DebugDrawPass] Failed to bind draw constants", error.empty() ? std::string{} : ": ",
                                 error);
                    m_Impl->loggedBindingFailure = true;
                }
                continue;
            }
            commands.SetBindGroup(0, bindings.get());
            commands.DrawIndexedInstanced(group.geometry.indexCount, static_cast<uint32_t>(count), 0, 0);
        }
    }
}

size_t DebugDrawPass::GetPreparedCommandCount() const {
    return m_Impl->preparedCommandCount;
}

size_t DebugDrawPass::GetPreparedDrawCount() const {
    return m_Impl->preparedDrawCount;
}
