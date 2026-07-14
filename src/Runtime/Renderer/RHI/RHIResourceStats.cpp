#include "Renderer/RHI/RHIResourceStats.h"
#include "Renderer/RHI/GpuResource.h"
#include "Renderer/RHI/GpuBuffer.h"
#include "Renderer/RHI/GpuTexture.h"

#include <algorithm>
#include <mutex>

namespace {
std::mutex g_Mutex;RHIResourceStats g_Stats;
void Refresh(){g_Stats.liveResourceBytes=g_Stats.liveBufferBytes+g_Stats.liveTextureBytes;
    g_Stats.peakResourceBytes=std::max(g_Stats.peakResourceBytes,g_Stats.liveResourceBytes);
    g_Stats.peakBuffers=std::max(g_Stats.peakBuffers,g_Stats.liveBuffers);
    g_Stats.peakTextures=std::max(g_Stats.peakTextures,g_Stats.liveTextures);
    g_Stats.peakDescriptors=std::max(g_Stats.peakDescriptors,g_Stats.liveDescriptors);
    g_Stats.peakNativeDescriptorSlots=std::max(g_Stats.peakNativeDescriptorSlots,
                                               g_Stats.liveNativeDescriptorSlots);}
uint64_t Sub(uint64_t value,uint64_t amount){return value>=amount?value-amount:0;}
uint64_t& NativeSlots(RHINativeDescriptorKind kind){
    switch(kind){case RHINativeDescriptorKind::Resource:return g_Stats.liveNativeResourceSlots;
    case RHINativeDescriptorKind::Sampler:return g_Stats.liveNativeSamplerSlots;
    case RHINativeDescriptorKind::RenderTarget:return g_Stats.liveNativeRenderTargetSlots;
    default:return g_Stats.liveNativeDepthStencilSlots;}}
}

RHIResourceStats RHIResourceStatsProvider::GetStats(){std::lock_guard<std::mutex> l(g_Mutex);return g_Stats;}
void RHIResourceStatsProvider::ResetPeaksAndTotals(){std::lock_guard<std::mutex> l(g_Mutex);
    g_Stats.peakResourceBytes=g_Stats.liveResourceBytes;g_Stats.peakBuffers=g_Stats.liveBuffers;
    g_Stats.peakTextures=g_Stats.liveTextures;g_Stats.peakDescriptors=g_Stats.liveDescriptors;
    g_Stats.peakNativeDescriptorSlots=g_Stats.liveNativeDescriptorSlots;
    g_Stats.createdResourceBytes=0;g_Stats.releasedResourceBytes=0;
    g_Stats.failedNativeDescriptorAllocations=0;}
void RHIResourceStatsProvider::AddBuffer(uint64_t b){std::lock_guard<std::mutex> l(g_Mutex);++g_Stats.liveBuffers;g_Stats.liveBufferBytes+=b;g_Stats.createdResourceBytes+=b;Refresh();}
void RHIResourceStatsProvider::AddTexture(uint64_t b){std::lock_guard<std::mutex> l(g_Mutex);++g_Stats.liveTextures;g_Stats.liveTextureBytes+=b;g_Stats.createdResourceBytes+=b;Refresh();}
void RHIResourceStatsProvider::AddDescriptors(uint32_t n){std::lock_guard<std::mutex> l(g_Mutex);g_Stats.liveDescriptors+=n;Refresh();}
void RHIResourceStatsProvider::AddNativeDescriptorSlots(RHINativeDescriptorKind kind,uint32_t n){std::lock_guard<std::mutex> l(g_Mutex);g_Stats.liveNativeDescriptorSlots+=n;NativeSlots(kind)+=n;Refresh();}
void RHIResourceStatsProvider::RecordNativeDescriptorAllocationFailure(RHINativeDescriptorKind){std::lock_guard<std::mutex> l(g_Mutex);++g_Stats.failedNativeDescriptorAllocations;}
void RHIResourceStatsProvider::ReleaseBuffer(uint64_t b){std::lock_guard<std::mutex> l(g_Mutex);g_Stats.liveBuffers=Sub(g_Stats.liveBuffers,1);g_Stats.liveBufferBytes=Sub(g_Stats.liveBufferBytes,b);g_Stats.releasedResourceBytes+=b;Refresh();}
void RHIResourceStatsProvider::ReleaseTexture(uint64_t b){std::lock_guard<std::mutex> l(g_Mutex);g_Stats.liveTextures=Sub(g_Stats.liveTextures,1);g_Stats.liveTextureBytes=Sub(g_Stats.liveTextureBytes,b);g_Stats.releasedResourceBytes+=b;Refresh();}
void RHIResourceStatsProvider::ReleaseDescriptors(uint32_t n){std::lock_guard<std::mutex> l(g_Mutex);g_Stats.liveDescriptors=Sub(g_Stats.liveDescriptors,n);Refresh();}
void RHIResourceStatsProvider::ReleaseNativeDescriptorSlots(RHINativeDescriptorKind kind,uint32_t n){std::lock_guard<std::mutex> l(g_Mutex);g_Stats.liveNativeDescriptorSlots=Sub(g_Stats.liveNativeDescriptorSlots,n);auto& slots=NativeSlots(kind);slots=Sub(slots,n);Refresh();}

uint64_t EstimateRHITextureBytes(const RHITextureDesc& d){
    uint64_t total=0;uint32_t w=std::max(1u,d.width),h=std::max(1u,d.height);
    for(uint32_t mip=0;mip<std::max(1u,d.mipLevels);++mip){
        uint64_t bytes=0;
        if(d.format==RHIFormat::BC1UNorm)bytes=((w+3)/4)*((h+3)/4)*8;
        else if(d.format==RHIFormat::BC3UNorm)bytes=((w+3)/4)*((h+3)/4)*16;
        else{uint32_t bpp=4;switch(d.format){case RHIFormat::R8UInt:case RHIFormat::R8UNorm:bpp=1;break;
            case RHIFormat::R16UInt:bpp=2;break;case RHIFormat::RG16Float:bpp=4;break;
            case RHIFormat::RGBA16Float:case RHIFormat::RG32Float:bpp=8;break;
            case RHIFormat::RGB32Float:bpp=12;break;case RHIFormat::RGBA32Float:bpp=16;break;default:break;}
            bytes=static_cast<uint64_t>(w)*h*bpp;}
        total+=bytes*std::max(1u,d.arrayLayers)*std::max(1u,d.sampleCount);w=std::max(1u,w/2);h=std::max(1u,h/2);
    }return total;
}
void CommitRHIResourceAccounting(const std::shared_ptr<GpuBuffer>& r){if(r)r->CommitAccounting(GpuResourceAccountingClass::Buffer,r->desc.size);}
void CommitRHIResourceAccounting(const std::shared_ptr<GpuTexture>& r){if(r)r->CommitAccounting(GpuResourceAccountingClass::Texture,EstimateRHITextureBytes(r->desc));}

GpuResource::~GpuResource(){
    if(m_AccountingClass==GpuResourceAccountingClass::Buffer)RHIResourceStatsProvider::ReleaseBuffer(m_AccountingBytes);
    else if(m_AccountingClass==GpuResourceAccountingClass::Texture)RHIResourceStatsProvider::ReleaseTexture(m_AccountingBytes);
    else if(m_AccountingClass==GpuResourceAccountingClass::Descriptor)RHIResourceStatsProvider::ReleaseDescriptors(m_AccountingDescriptors);
}
void GpuResource::CommitAccounting(GpuResourceAccountingClass c,uint64_t b,uint32_t d){
    if(m_AccountingClass!=GpuResourceAccountingClass::None)return;m_AccountingClass=c;m_AccountingBytes=b;m_AccountingDescriptors=d;
    if(c==GpuResourceAccountingClass::Buffer)RHIResourceStatsProvider::AddBuffer(b);
    else if(c==GpuResourceAccountingClass::Texture)RHIResourceStatsProvider::AddTexture(b);
    else if(c==GpuResourceAccountingClass::Descriptor)RHIResourceStatsProvider::AddDescriptors(d);
}
