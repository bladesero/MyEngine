#include "Renderer/ParticleSystemComponent.h"

#include "Assets/AssetManager.h"
#include "Camera/Camera.h"
#include "Scene/Actor.h"

#include <algorithm>
#include <cmath>

void ParticleSystemComponent::EnsureResources()
{
    if (!m_RenderMesh) { m_RenderMesh=std::make_shared<MeshAsset>("__runtime__/particles");m_RenderMesh->SetName("Particles"); }
    if (!m_Material) { auto material=MaterialAsset::CreateDefault("Particle");material->SetBlendMode(BlendMode::Transparent);material->SetTwoSided(true);material->SetParam("BaseColor",MaterialParam::FromColor(Vec3::One(),1.0f));m_Material=AssetManager::Get().Register(material); }
}
void ParticleSystemComponent::SetAssetPath(const std::string& path){m_AssetPath=path;if(path.empty())return;auto asset=AssetManager::Get().Load<ParticleAsset>(path);if(asset)m_Settings=asset->GetSettings();}
void ParticleSystemComponent::OnBeginPlay(){EnsureResources();if(m_Settings.playOnStart)Play();}
void ParticleSystemComponent::OnEndPlay(){Stop(true);}
void ParticleSystemComponent::OnAnimationEvent(const AnimationEventData& event){if(event.name=="Particle.Play")Play();else if(event.name=="Particle.Stop")Stop();else if(event.name=="Particle.Burst"){uint32_t count=m_Settings.burst;try{if(!event.payload.empty())count=static_cast<uint32_t>(std::stoul(event.payload));}catch(...){ }Emit(count);}}
bool ParticleSystemComponent::Play(){EnsureResources();m_Playing=true;m_EmitterAge=0.0f;m_EmissionAccumulator=0.0f;if(m_Settings.burst>0)Emit(m_Settings.burst);return true;}
void ParticleSystemComponent::Stop(bool clear){m_Playing=false;m_EmissionAccumulator=0.0f;m_EmitterAge=0.0f;if(clear)m_Particles.clear();}
void ParticleSystemComponent::Emit(uint32_t count)
{
    EnsureResources();std::uniform_real_distribution<float> unit(-1.0f,1.0f);
    count=std::min<uint32_t>(count,m_Settings.maxParticles-static_cast<uint32_t>(std::min<size_t>(m_Particles.size(),m_Settings.maxParticles)));
    for(uint32_t i=0;i<count;++i){Vec3 direction(unit(m_Random),std::abs(unit(m_Random)),unit(m_Random));if(direction.LengthSq()<0.001f)direction=Vec3::Up();direction=direction.Normalized();m_Particles.push_back({Vec3::Zero(),direction*m_Settings.startSpeed,0.0f,std::max(0.01f,m_Settings.lifetime),unit(m_Random)*kPi});}
}
void ParticleSystemComponent::OnUpdate(float dt)
{
    dt=std::max(0.0f,dt);if(m_Playing){m_EmitterAge+=dt;const float emitTime=m_Settings.loop?dt:std::min(dt,std::max(0.0f,m_Settings.lifetime-(m_EmitterAge-dt)));if(emitTime>0.0f&&m_Settings.rate>0.0f){m_EmissionAccumulator+=emitTime*m_Settings.rate;const uint32_t count=static_cast<uint32_t>(m_EmissionAccumulator);if(count){Emit(count);m_EmissionAccumulator-=count;}}if(!m_Settings.loop&&m_EmitterAge>=m_Settings.lifetime)m_Playing=false;}
    for(auto& particle:m_Particles){particle.age+=dt;particle.position+=particle.velocity*dt;}
    m_Particles.erase(std::remove_if(m_Particles.begin(),m_Particles.end(),[](const Particle& particle){return particle.age>=particle.lifetime;}),m_Particles.end());
}
MeshAsset* ParticleSystemComponent::BuildBillboardMesh(const Camera& camera)
{
    EnsureResources();std::vector<MeshVertex> vertices;std::vector<uint32_t> indices;vertices.reserve(m_Particles.size()*4);indices.reserve(m_Particles.size()*6);
    const Vec3 right=camera.GetRight(),up=camera.GetCamUp();
    for(const Particle& particle:m_Particles){const float t=std::clamp(particle.age/particle.lifetime,0.0f,1.0f);const float half=(m_Settings.startSize+(m_Settings.endSize-m_Settings.startSize)*t)*0.5f;const Vec3 color=Vec3::Lerp(m_Settings.startColor,m_Settings.endColor,t);const float alpha=m_Settings.startAlpha+(m_Settings.endAlpha-m_Settings.startAlpha)*t;const Vec4 vertexColor(color.x,color.y,color.z,alpha);const Vec3 r=right*half,u=up*half;const uint32_t base=static_cast<uint32_t>(vertices.size());vertices.push_back({particle.position-r-u});vertices.back().u=0;vertices.back().v=1;vertices.back().color=vertexColor;vertices.push_back({particle.position-r+u});vertices.back().u=0;vertices.back().v=0;vertices.back().color=vertexColor;vertices.push_back({particle.position+r+u});vertices.back().u=1;vertices.back().v=0;vertices.back().color=vertexColor;vertices.push_back({particle.position+r-u});vertices.back().u=1;vertices.back().v=1;vertices.back().color=vertexColor;indices.insert(indices.end(),{base,base+1,base+2,base,base+2,base+3});}
    if(m_Material)m_Material->SetParam("BaseColor",MaterialParam::FromColor(Vec3::One(),1.0f));
    std::vector<SubMesh> submeshes;if(!indices.empty())submeshes.push_back({0,static_cast<uint32_t>(indices.size()),0,0,"Particles",{}});m_RenderMesh->SetGeometry(std::move(vertices),std::move(indices),std::move(submeshes));return m_RenderMesh.get();
}
void ParticleSystemComponent::Serialize(nlohmann::json& data) const {data={{"asset",m_AssetPath},{"maxParticles",m_Settings.maxParticles},{"rate",m_Settings.rate},{"burst",m_Settings.burst},{"lifetime",m_Settings.lifetime},{"startSpeed",m_Settings.startSpeed},{"startSize",m_Settings.startSize},{"endSize",m_Settings.endSize},{"startColor",{m_Settings.startColor.x,m_Settings.startColor.y,m_Settings.startColor.z}},{"endColor",{m_Settings.endColor.x,m_Settings.endColor.y,m_Settings.endColor.z}},{"startAlpha",m_Settings.startAlpha},{"endAlpha",m_Settings.endAlpha},{"loop",m_Settings.loop},{"playOnStart",m_Settings.playOnStart}};}
void ParticleSystemComponent::Deserialize(const nlohmann::json& data){m_Settings.maxParticles=std::max(1u,data.value("maxParticles",256u));m_Settings.rate=std::max(0.0f,data.value("rate",20.0f));m_Settings.burst=data.value("burst",0u);m_Settings.lifetime=std::max(0.01f,data.value("lifetime",1.5f));m_Settings.startSpeed=data.value("startSpeed",1.0f);m_Settings.startSize=std::max(0.0f,data.value("startSize",0.2f));m_Settings.endSize=std::max(0.0f,data.value("endSize",0.0f));m_Settings.startAlpha=std::clamp(data.value("startAlpha",1.0f),0.0f,1.0f);m_Settings.endAlpha=std::clamp(data.value("endAlpha",0.0f),0.0f,1.0f);m_Settings.loop=data.value("loop",true);m_Settings.playOnStart=data.value("playOnStart",true);const std::string asset=data.value("asset",std::string{});if(!asset.empty())SetAssetPath(asset);Stop(true);}
