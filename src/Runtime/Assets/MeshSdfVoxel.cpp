#include "Assets/MeshSdfVoxel.h"

#include "Assets/MeshAsset.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>

namespace {
constexpr float kEpsilon = 1.0e-6f;
constexpr const char* kBase64Alphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void SetError(std::string* error, std::string message)
{
    if (error) *error = std::move(message);
}

uint32_t LinearIndex(uint32_t resolution, uint32_t x, uint32_t y, uint32_t z)
{
    return (z * resolution * resolution) + (y * resolution) + x;
}

float Dot(const Vec3& a, const Vec3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 Cross(const Vec3& a, const Vec3& b)
{
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

float LengthSq(const Vec3& v)
{
    return Dot(v, v);
}

Vec3 MinVec(const Vec3& a, const Vec3& b)
{
    return {
        (std::min)(a.x, b.x),
        (std::min)(a.y, b.y),
        (std::min)(a.z, b.z)
    };
}

Vec3 MaxVec(const Vec3& a, const Vec3& b)
{
    return {
        (std::max)(a.x, b.x),
        (std::max)(a.y, b.y),
        (std::max)(a.z, b.z)
    };
}

float DistancePointTriangleSq(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c)
{
    const Vec3 ab = b - a;
    const Vec3 ac = c - a;
    const Vec3 ap = p - a;
    const float d1 = Dot(ab, ap);
    const float d2 = Dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) return LengthSq(p - a);

    const Vec3 bp = p - b;
    const float d3 = Dot(ab, bp);
    const float d4 = Dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) return LengthSq(p - b);

    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        const float v = d1 / (d1 - d3);
        return LengthSq(p - (a + ab * v));
    }

    const Vec3 cp = p - c;
    const float d5 = Dot(ab, cp);
    const float d6 = Dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) return LengthSq(p - c);

    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        const float w = d2 / (d2 - d6);
        return LengthSq(p - (a + ac * w));
    }

    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        const Vec3 bc = c - b;
        const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return LengthSq(p - (b + bc * w));
    }

    const Vec3 n = Cross(ab, ac);
    const float nLenSq = LengthSq(n);
    if (nLenSq <= kEpsilon) return LengthSq(p - a);
    const float distance = Dot(p - a, n) / std::sqrt(nLenSq);
    return distance * distance;
}

bool RayIntersectsTriangleX(const Vec3& origin, const Vec3& a, const Vec3& b, const Vec3& c)
{
    const Vec3 direction{1.0f, 0.0f, 0.0f};
    const Vec3 edge1 = b - a;
    const Vec3 edge2 = c - a;
    const Vec3 h = Cross(direction, edge2);
    const float det = Dot(edge1, h);
    if (std::fabs(det) < kEpsilon) return false;
    const float invDet = 1.0f / det;
    const Vec3 s = origin - a;
    const float u = invDet * Dot(s, h);
    if (u < 0.0f || u > 1.0f) return false;
    const Vec3 q = Cross(s, edge1);
    const float v = invDet * Dot(direction, q);
    if (v < 0.0f || u + v > 1.0f) return false;
    const float t = invDet * Dot(edge2, q);
    return t > kEpsilon;
}

std::string Base64Encode(const std::vector<uint8_t>& bytes)
{
    std::string output;
    output.reserve(((bytes.size() + 2) / 3) * 4);
    for (size_t i = 0; i < bytes.size(); i += 3) {
        const uint32_t a = bytes[i];
        const uint32_t b = i + 1 < bytes.size() ? bytes[i + 1] : 0;
        const uint32_t c = i + 2 < bytes.size() ? bytes[i + 2] : 0;
        const uint32_t triple = (a << 16) | (b << 8) | c;
        output.push_back(kBase64Alphabet[(triple >> 18) & 0x3f]);
        output.push_back(kBase64Alphabet[(triple >> 12) & 0x3f]);
        output.push_back(i + 1 < bytes.size() ? kBase64Alphabet[(triple >> 6) & 0x3f] : '=');
        output.push_back(i + 2 < bytes.size() ? kBase64Alphabet[triple & 0x3f] : '=');
    }
    return output;
}

int Base64Value(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

bool Base64Decode(std::string_view text, std::vector<uint8_t>& bytes)
{
    bytes.clear();
    std::array<int, 4> quartet{};
    int count = 0;
    for (char c : text) {
        if (c == '\r' || c == '\n' || c == '\t' || c == ' ') continue;
        quartet[count++] = c == '=' ? -2 : Base64Value(c);
        if (quartet[count - 1] == -1) return false;
        if (count != 4) continue;
        const uint32_t a = static_cast<uint32_t>(quartet[0]);
        const uint32_t b = static_cast<uint32_t>(quartet[1]);
        const uint32_t c2 = quartet[2] >= 0 ? static_cast<uint32_t>(quartet[2]) : 0;
        const uint32_t d = quartet[3] >= 0 ? static_cast<uint32_t>(quartet[3]) : 0;
        const uint32_t triple = (a << 18) | (b << 12) | (c2 << 6) | d;
        bytes.push_back(static_cast<uint8_t>((triple >> 16) & 0xff));
        if (quartet[2] != -2) bytes.push_back(static_cast<uint8_t>((triple >> 8) & 0xff));
        if (quartet[3] != -2) bytes.push_back(static_cast<uint8_t>(triple & 0xff));
        count = 0;
    }
    return count == 0;
}

struct TriangleInfo {
    std::array<Vec3, 3> v;
    Vec3 min;
    Vec3 max;
};

uint32_t ClampGridCell(float value, float minValue, float extent, uint32_t resolution)
{
    if (extent <= kEpsilon || resolution == 0) return 0;
    const float normalized = (value - minValue) / extent;
    const int cell = static_cast<int>(std::floor(normalized * static_cast<float>(resolution)));
    return static_cast<uint32_t>((std::max)(0, (std::min)(cell, static_cast<int>(resolution - 1))));
}

uint32_t GridIndex3(uint32_t resolution, uint32_t x, uint32_t y, uint32_t z)
{
    return (z * resolution * resolution) + (y * resolution) + x;
}

uint32_t GridIndex2(uint32_t resolution, uint32_t y, uint32_t z)
{
    return z * resolution + y;
}

std::vector<std::vector<uint32_t>> BuildDistanceGrid(
    const std::vector<TriangleInfo>& triangles,
    const MeshSdfVoxelData& data,
    uint32_t gridResolution)
{
    std::vector<std::vector<uint32_t>> grid(
        static_cast<size_t>(gridResolution) * gridResolution * gridResolution);
    const Vec3 extent = data.bounds.max - data.bounds.min;
    for (uint32_t i = 0; i < triangles.size(); ++i) {
        const TriangleInfo& tri = triangles[i];
        const uint32_t minX = ClampGridCell(
            tri.min.x - data.cellSize, data.bounds.min.x, extent.x, gridResolution);
        const uint32_t minY = ClampGridCell(
            tri.min.y - data.cellSize, data.bounds.min.y, extent.y, gridResolution);
        const uint32_t minZ = ClampGridCell(
            tri.min.z - data.cellSize, data.bounds.min.z, extent.z, gridResolution);
        const uint32_t maxX = ClampGridCell(
            tri.max.x + data.cellSize, data.bounds.min.x, extent.x, gridResolution);
        const uint32_t maxY = ClampGridCell(
            tri.max.y + data.cellSize, data.bounds.min.y, extent.y, gridResolution);
        const uint32_t maxZ = ClampGridCell(
            tri.max.z + data.cellSize, data.bounds.min.z, extent.z, gridResolution);
        for (uint32_t z = minZ; z <= maxZ; ++z)
            for (uint32_t y = minY; y <= maxY; ++y)
                for (uint32_t x = minX; x <= maxX; ++x)
                    grid[GridIndex3(gridResolution, x, y, z)].push_back(i);
    }
    return grid;
}

std::vector<std::vector<uint32_t>> BuildRayGrid(
    const std::vector<TriangleInfo>& triangles,
    const MeshSdfVoxelData& data,
    uint32_t gridResolution)
{
    std::vector<std::vector<uint32_t>> grid(
        static_cast<size_t>(gridResolution) * gridResolution);
    const Vec3 extent = data.bounds.max - data.bounds.min;
    for (uint32_t i = 0; i < triangles.size(); ++i) {
        const TriangleInfo& tri = triangles[i];
        const uint32_t minY = ClampGridCell(
            tri.min.y - data.cellSize, data.bounds.min.y, extent.y, gridResolution);
        const uint32_t minZ = ClampGridCell(
            tri.min.z - data.cellSize, data.bounds.min.z, extent.z, gridResolution);
        const uint32_t maxY = ClampGridCell(
            tri.max.y + data.cellSize, data.bounds.min.y, extent.y, gridResolution);
        const uint32_t maxZ = ClampGridCell(
            tri.max.z + data.cellSize, data.bounds.min.z, extent.z, gridResolution);
        for (uint32_t z = minZ; z <= maxZ; ++z)
            for (uint32_t y = minY; y <= maxY; ++y)
                grid[GridIndex2(gridResolution, y, z)].push_back(i);
    }
    return grid;
}

std::vector<uint8_t> EncodeInt16LE(const std::vector<int16_t>& values)
{
    std::vector<uint8_t> bytes(values.size() * 2);
    for (size_t i = 0; i < values.size(); ++i) {
        const uint16_t v = static_cast<uint16_t>(values[i]);
        bytes[i * 2 + 0] = static_cast<uint8_t>(v & 0xff);
        bytes[i * 2 + 1] = static_cast<uint8_t>((v >> 8) & 0xff);
    }
    return bytes;
}

bool DecodeInt16LE(const std::vector<uint8_t>& bytes, std::vector<int16_t>& values)
{
    if (bytes.size() % 2 != 0) return false;
    values.resize(bytes.size() / 2);
    for (size_t i = 0; i < values.size(); ++i) {
        const uint16_t v = static_cast<uint16_t>(bytes[i * 2]) |
            (static_cast<uint16_t>(bytes[i * 2 + 1]) << 8);
        values[i] = static_cast<int16_t>(v);
    }
    return true;
}

std::string Attribute(const std::string& text, const std::string& name)
{
    const std::string pattern = name + "=\"";
    const size_t begin = text.find(pattern);
    if (begin == std::string::npos) return {};
    const size_t valueBegin = begin + pattern.size();
    const size_t end = text.find('"', valueBegin);
    if (end == std::string::npos) return {};
    return text.substr(valueBegin, end - valueBegin);
}

std::string ElementText(const std::string& text, const std::string& name)
{
    const std::string close = "</" + name + ">";
    const std::string openPrefix = "<" + name;
    const size_t begin = text.find(openPrefix);
    if (begin == std::string::npos) return {};
    const size_t openEnd = text.find('>', begin + openPrefix.size());
    if (openEnd == std::string::npos) return {};
    const size_t valueBegin = openEnd + 1;
    const size_t end = text.find(close, valueBegin);
    if (end == std::string::npos) return {};
    return text.substr(valueBegin, end - valueBegin);
}

bool ParseFloat(const std::string& text, float& value)
{
    try {
        size_t consumed = 0;
        value = std::stof(text, &consumed);
        return consumed == text.size();
    } catch (...) {
        return false;
    }
}

bool ParseUint(const std::string& text, uint32_t& value)
{
    try {
        size_t consumed = 0;
        const unsigned long parsed = std::stoul(text, &consumed);
        value = static_cast<uint32_t>(parsed);
        return consumed == text.size();
    } catch (...) {
        return false;
    }
}
}

bool MeshSdfVoxelData::Valid() const
{
    const size_t expected = static_cast<size_t>(resolution) * resolution * resolution;
    return resolution > 0 && sdf.size() == expected && voxels.size() == ((expected + 7) / 8);
}

bool MeshSdfVoxelData::IsVoxelOccupied(uint32_t x, uint32_t y, uint32_t z) const
{
    if (x >= resolution || y >= resolution || z >= resolution) return false;
    const uint32_t index = LinearIndex(resolution, x, y, z);
    const uint32_t byteIndex = index / 8;
    const uint32_t bitIndex = index % 8;
    return byteIndex < voxels.size() && (voxels[byteIndex] & (1u << bitIndex)) != 0;
}

MeshSdfVoxelBakeResult MeshSdfVoxelBaker::BakeMedium(const MeshAsset& mesh)
{
    MeshSdfVoxelBakeResult result;
    const auto& vertices = mesh.GetVertices();
    const auto& indices = mesh.GetIndices();
    if (vertices.empty() || indices.size() < 3) {
        result.error = "mesh has no triangle geometry";
        return result;
    }

    const AABB bounds = mesh.GetAABB();
    Vec3 extent = bounds.max - bounds.min;
    const float maxExtent = (std::max)({extent.x, extent.y, extent.z});
    if (maxExtent <= kEpsilon) {
        result.error = "mesh bounds are degenerate";
        return result;
    }

    constexpr uint32_t resolution = MeshSdfVoxelData::kMediumResolution;
    const size_t voxelCount = static_cast<size_t>(resolution) * resolution * resolution;
    const float padding = maxExtent * 0.02f;
    result.data.resolution = resolution;
    result.data.bounds.min = bounds.min - Vec3{padding, padding, padding};
    result.data.bounds.max = bounds.max + Vec3{padding, padding, padding};
    extent = result.data.bounds.max - result.data.bounds.min;
    result.data.cellSize = (std::max)({extent.x, extent.y, extent.z}) /
        static_cast<float>(resolution);

    std::vector<TriangleInfo> triangles;
    triangles.reserve(indices.size() / 3);
    size_t degenerateTriangles = 0;
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        const uint32_t ia = indices[i + 0];
        const uint32_t ib = indices[i + 1];
        const uint32_t ic = indices[i + 2];
        if (ia >= vertices.size() || ib >= vertices.size() || ic >= vertices.size()) continue;
        const Vec3 a = vertices[ia].position;
        const Vec3 b = vertices[ib].position;
        const Vec3 c = vertices[ic].position;
        if (LengthSq(Cross(b - a, c - a)) <= kEpsilon) {
            ++degenerateTriangles;
            continue;
        }
        TriangleInfo triangle;
        triangle.v = {a, b, c};
        triangle.min = MinVec(MinVec(a, b), c);
        triangle.max = MaxVec(MaxVec(a, b), c);
        triangles.push_back(triangle);
    }
    if (triangles.empty()) {
        result.error = "mesh contains only degenerate triangles";
        return result;
    }
    if (degenerateTriangles > 0) {
        result.warnings.push_back(
            "ignored " + std::to_string(degenerateTriangles) + " degenerate triangle(s)");
    }

    std::vector<float> distances(voxelCount, 0.0f);
    float maxAbsDistance = 0.0f;
    result.data.voxels.assign((voxelCount + 7) / 8, 0);
    constexpr uint32_t distanceGridResolution = 16;
    constexpr uint32_t rayGridResolution = 64;
    const auto distanceGrid =
        BuildDistanceGrid(triangles, result.data, distanceGridResolution);
    const auto rayGrid = BuildRayGrid(triangles, result.data, rayGridResolution);
    std::vector<uint32_t> candidates;
    std::vector<uint32_t> candidateMarks(triangles.size(), 0);
    uint32_t candidateTag = 1;
    const Vec3 bakedExtent = result.data.bounds.max - result.data.bounds.min;

    for (uint32_t z = 0; z < resolution; ++z) {
        for (uint32_t y = 0; y < resolution; ++y) {
            for (uint32_t x = 0; x < resolution; ++x) {
                const Vec3 p{
                    result.data.bounds.min.x + (static_cast<float>(x) + 0.5f) * result.data.cellSize,
                    result.data.bounds.min.y + (static_cast<float>(y) + 0.5f) * result.data.cellSize,
                    result.data.bounds.min.z + (static_cast<float>(z) + 0.5f) * result.data.cellSize
                };

                float minDistanceSq = std::numeric_limits<float>::max();
                uint32_t hits = 0;
                const Vec3 rayOrigin = p + Vec3{0.0f, result.data.cellSize * 0.013f,
                                                result.data.cellSize * 0.007f};
                candidates.clear();
                if (++candidateTag == 0) {
                    std::fill(candidateMarks.begin(), candidateMarks.end(), 0);
                    candidateTag = 1;
                }
                const uint32_t centerX = ClampGridCell(
                    p.x, result.data.bounds.min.x, bakedExtent.x, distanceGridResolution);
                const uint32_t centerY = ClampGridCell(
                    p.y, result.data.bounds.min.y, bakedExtent.y, distanceGridResolution);
                const uint32_t centerZ = ClampGridCell(
                    p.z, result.data.bounds.min.z, bakedExtent.z, distanceGridResolution);
                for (uint32_t radius = 0; radius < distanceGridResolution && candidates.empty();
                     ++radius) {
                    const int minX = (std::max)(0, static_cast<int>(centerX) - static_cast<int>(radius));
                    const int minY = (std::max)(0, static_cast<int>(centerY) - static_cast<int>(radius));
                    const int minZ = (std::max)(0, static_cast<int>(centerZ) - static_cast<int>(radius));
                    const int maxX = (std::min)(static_cast<int>(distanceGridResolution - 1),
                        static_cast<int>(centerX) + static_cast<int>(radius));
                    const int maxY = (std::min)(static_cast<int>(distanceGridResolution - 1),
                        static_cast<int>(centerY) + static_cast<int>(radius));
                    const int maxZ = (std::min)(static_cast<int>(distanceGridResolution - 1),
                        static_cast<int>(centerZ) + static_cast<int>(radius));
                    for (int gz = minZ; gz <= maxZ; ++gz)
                        for (int gy = minY; gy <= maxY; ++gy)
                            for (int gx = minX; gx <= maxX; ++gx) {
                                if (radius > 0 &&
                                    gx > minX && gx < maxX &&
                                    gy > minY && gy < maxY &&
                                    gz > minZ && gz < maxZ) {
                                    continue;
                                }
                                const auto& bin = distanceGrid[GridIndex3(
                                    distanceGridResolution,
                                    static_cast<uint32_t>(gx),
                                    static_cast<uint32_t>(gy),
                                    static_cast<uint32_t>(gz))];
                                for (uint32_t triangleIndex : bin) {
                                    if (candidateMarks[triangleIndex] == candidateTag) continue;
                                    candidateMarks[triangleIndex] = candidateTag;
                                    candidates.push_back(triangleIndex);
                                }
                            }
                }
                if (candidates.empty()) {
                    candidates.reserve(triangles.size());
                    for (uint32_t triangleIndex = 0; triangleIndex < triangles.size();
                         ++triangleIndex) {
                        candidates.push_back(triangleIndex);
                    }
                }
                for (uint32_t triangleIndex : candidates) {
                    const TriangleInfo& tri = triangles[triangleIndex];
                    minDistanceSq = (std::min)(
                        minDistanceSq,
                        DistancePointTriangleSq(p, tri.v[0], tri.v[1], tri.v[2]));
                }
                const uint32_t rayY = ClampGridCell(
                    rayOrigin.y, result.data.bounds.min.y, bakedExtent.y, rayGridResolution);
                const uint32_t rayZ = ClampGridCell(
                    rayOrigin.z, result.data.bounds.min.z, bakedExtent.z, rayGridResolution);
                const auto& rayCandidates = rayGrid[GridIndex2(rayGridResolution, rayY, rayZ)];
                for (uint32_t triangleIndex : rayCandidates) {
                    const TriangleInfo& tri = triangles[triangleIndex];
                    if (tri.max.x <= rayOrigin.x ||
                        rayOrigin.y < tri.min.y || rayOrigin.y > tri.max.y ||
                        rayOrigin.z < tri.min.z || rayOrigin.z > tri.max.z) {
                        continue;
                    }
                    if (RayIntersectsTriangleX(rayOrigin, tri.v[0], tri.v[1], tri.v[2])) {
                        ++hits;
                    }
                }
                const bool inside = (hits % 2) == 1;
                const float signedDistance = std::sqrt(minDistanceSq) * (inside ? -1.0f : 1.0f);
                const uint32_t index = LinearIndex(resolution, x, y, z);
                distances[index] = signedDistance;
                maxAbsDistance = (std::max)(maxAbsDistance, std::fabs(signedDistance));
                if (signedDistance <= 0.0f) {
                    result.data.voxels[index / 8] |= static_cast<uint8_t>(1u << (index % 8));
                }
            }
        }
    }

    result.data.sdfScale = maxAbsDistance > 0.0f ? maxAbsDistance / 32767.0f : 1.0f;
    result.data.sdf.resize(voxelCount);
    for (size_t i = 0; i < voxelCount; ++i) {
        const float normalized = distances[i] / result.data.sdfScale;
        const int quantized = static_cast<int>(std::round(normalized));
        result.data.sdf[i] = static_cast<int16_t>(
            (std::max)(-32767, (std::min)(32767, quantized)));
    }
    result.succeeded = result.data.Valid();
    if (!result.succeeded) result.error = "generated SDF/voxel payload is invalid";
    return result;
}

bool MeshSdfVoxelXml::Save(const std::filesystem::path& path,
                           const MeshSdfVoxelData& data,
                           std::string* error)
{
    if (!data.Valid()) {
        SetError(error, "SDF/voxel data is invalid");
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        SetError(error, "failed to create SDF/voxel directory: " + ec.message());
        return false;
    }

    const std::filesystem::path temporary = path.string() + ".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
        SetError(error, "failed to create SDF/voxel XML");
        return false;
    }

    output << "<MeshSdfVoxel version=\"1\" resolution=\"" << data.resolution
           << "\" cellSize=\"" << data.cellSize
           << "\" sdfScale=\"" << data.sdfScale << "\">\n";
    output << "  <Bounds minX=\"" << data.bounds.min.x
           << "\" minY=\"" << data.bounds.min.y
           << "\" minZ=\"" << data.bounds.min.z
           << "\" maxX=\"" << data.bounds.max.x
           << "\" maxY=\"" << data.bounds.max.y
           << "\" maxZ=\"" << data.bounds.max.z << "\"/>\n";
    output << "  <Sdf encoding=\"base64-int16-le\">"
           << Base64Encode(EncodeInt16LE(data.sdf)) << "</Sdf>\n";
    output << "  <Voxels encoding=\"base64-bitset-lsb\">"
           << Base64Encode(data.voxels) << "</Voxels>\n";
    output << "</MeshSdfVoxel>\n";
    output.close();
    if (!output) {
        std::filesystem::remove(temporary, ec);
        SetError(error, "failed writing SDF/voxel XML");
        return false;
    }
    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        std::filesystem::remove(temporary, ec);
        SetError(error, "failed to install SDF/voxel XML: " + ec.message());
        return false;
    }
    return true;
}

bool MeshSdfVoxelXml::Load(const std::filesystem::path& path,
                           MeshSdfVoxelData& data,
                           std::string* error)
{
    if (error) error->clear();
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        SetError(error, "SDF/voxel XML does not exist");
        return false;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    const std::string text = buffer.str();

    MeshSdfVoxelData parsed;
    if (!ParseUint(Attribute(text, "resolution"), parsed.resolution) ||
        !ParseFloat(Attribute(text, "cellSize"), parsed.cellSize) ||
        !ParseFloat(Attribute(text, "sdfScale"), parsed.sdfScale) ||
        !ParseFloat(Attribute(text, "minX"), parsed.bounds.min.x) ||
        !ParseFloat(Attribute(text, "minY"), parsed.bounds.min.y) ||
        !ParseFloat(Attribute(text, "minZ"), parsed.bounds.min.z) ||
        !ParseFloat(Attribute(text, "maxX"), parsed.bounds.max.x) ||
        !ParseFloat(Attribute(text, "maxY"), parsed.bounds.max.y) ||
        !ParseFloat(Attribute(text, "maxZ"), parsed.bounds.max.z)) {
        SetError(error, "SDF/voxel XML metadata is malformed");
        return false;
    }

    std::vector<uint8_t> sdfBytes;
    const std::string sdfText = ElementText(text, "Sdf");
    if (sdfText.empty()) {
        SetError(error, "SDF payload is missing");
        return false;
    }
    if (!Base64Decode(sdfText, sdfBytes) ||
        !DecodeInt16LE(sdfBytes, parsed.sdf)) {
        SetError(error, "SDF payload is malformed");
        return false;
    }
    const std::string voxelText = ElementText(text, "Voxels");
    if (voxelText.empty()) {
        SetError(error, "voxel payload is missing");
        return false;
    }
    if (!Base64Decode(voxelText, parsed.voxels)) {
        SetError(error, "voxel payload is malformed");
        return false;
    }
    if (!parsed.Valid()) {
        SetError(error, "SDF/voxel payload size does not match resolution");
        return false;
    }
    data = std::move(parsed);
    return true;
}
