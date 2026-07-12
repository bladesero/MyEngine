#include "Scene/WorldFrameScheduler.h"
#include "Scene/Scene.h"
#include "Core/Logger.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <queue>
#include <unordered_set>

namespace { void Error(std::string* out,const std::string& value){if(out)*out=value;} }

const char* WorldPhaseName(WorldPhase phase) {
    static constexpr const char* names[] = {"WorldFrameBegin","PreUpdate","FixedPrePhysics","FixedPhysics","FixedPostPhysics","Update","LateUpdate","RenderExtract","WorldFrameEnd"};
    const size_t index=static_cast<size_t>(phase);return index<kWorldPhaseCount?names[index]:"Unknown";
}

WorldFrameScheduler::WorldFrameScheduler(bool registerBuiltins, bool freezeAfterRegistration) {
    if (!registerBuiltins) return;
    RegisterSystem({"Navigation",WorldPhase::PreUpdate,0,{}, {},false,[](WorldTickContext& c){c.scene.GetNavigationWorld().Update(c.deltaSeconds);}});
    RegisterSystem({"Components.FixedUpdate",WorldPhase::FixedPrePhysics,0,{}, {},false,[](WorldTickContext& c){c.scene.ForEach([&](Actor& a){a.FixedUpdate(c.deltaSeconds);});}});
    RegisterSystem({"Physics",WorldPhase::FixedPhysics,0,{}, {},false,[](WorldTickContext& c){c.scene.GetPhysicsWorld().StepFixed(c.scene,c.deltaSeconds);}});
    RegisterSystem({"Components.Update",WorldPhase::Update,0,{}, {},false,[](WorldTickContext& c){c.scene.ForEach([&](Actor& a){a.Update(c.deltaSeconds);});}});
    RegisterSystem({"Components.LateUpdate",WorldPhase::LateUpdate,0,{}, {},false,[](WorldTickContext& c){c.scene.ForEach([&](Actor& a){a.LateUpdate(c.deltaSeconds);});}});
    if (freezeAfterRegistration) Freeze();
}
bool WorldFrameScheduler::RegisterSystem(WorldSystemDescriptor d,std::string* error){if(m_Frozen||m_Executing){Error(error,"scheduler registration is closed");return false;}if(d.stableName.empty()||!d.execute){Error(error,"system requires name and callback");return false;}for(const auto& s:m_Systems)if(s.stableName==d.stableName){Error(error,"duplicate system: "+d.stableName);return false;}m_Systems.push_back(std::move(d));return true;}
bool WorldFrameScheduler::Freeze(std::string* error){
    std::unordered_map<std::string,size_t> names;for(size_t i=0;i<m_Systems.size();++i)names[m_Systems[i].stableName]=i;
    for(int p=static_cast<int>(WorldPhase::WorldFrameBegin);p<=static_cast<int>(WorldPhase::WorldFrameEnd);++p){WorldPhase phase=static_cast<WorldPhase>(p);std::vector<size_t> nodes;for(size_t i=0;i<m_Systems.size();++i)if(m_Systems[i].phase==phase)nodes.push_back(i);std::unordered_map<size_t,std::vector<size_t>> edges;std::unordered_map<size_t,int> indegree;for(size_t n:nodes)indegree[n]=0;
        for(size_t n:nodes){const auto add=[&](const std::string& dep,bool after){auto it=names.find(dep);if(it==names.end()){Error(error,"unknown dependency: "+dep);return false;}size_t other=it->second;if(m_Systems[other].phase!=phase){Error(error,"cross-phase dependency: "+dep);return false;}size_t from=after?other:n,to=after?n:other;edges[from].push_back(to);++indegree[to];return true;};for(const auto& dep:m_Systems[n].runsAfter)if(!add(dep,true))return false;for(const auto& dep:m_Systems[n].runsBefore)if(!add(dep,false))return false;}
        auto cmp=[&](size_t a,size_t b){const auto& x=m_Systems[a];const auto& y=m_Systems[b];return x.order!=y.order?x.order>y.order:x.stableName>y.stableName;};std::priority_queue<size_t,std::vector<size_t>,decltype(cmp)> ready(cmp);for(auto [n,d]:indegree)if(d==0)ready.push(n);auto& order=m_Order[phase];while(!ready.empty()){size_t n=ready.top();ready.pop();order.push_back(n);for(size_t to:edges[n])if(--indegree[to]==0)ready.push(to);}if(order.size()!=nodes.size()){Error(error,"scheduler dependency cycle");return false;}}
    m_Frozen=true;return true;
}
bool WorldFrameScheduler::ExecutePhase(WorldPhase phase,Scene& scene,float delta,bool paused){const auto phaseStart=std::chrono::steady_clock::now();auto finish=[&]{m_Stats.phaseMilliseconds[static_cast<size_t>(phase)]+=std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now()-phaseStart).count();};auto it=m_Order.find(phase);if(it==m_Order.end()){finish();return true;}for(size_t index:it->second){auto& system=m_Systems[index];if(paused&&!system.runWhenPaused&&phase!=WorldPhase::RenderExtract)continue;WorldTickContext context{scene,delta,m_FrameIndex,m_FixedTickIndex};try{system.execute(context);}catch(const std::exception& e){Logger::Error("[WorldScheduler] phase=",WorldPhaseName(phase)," system=",system.stableName," failed: ",e.what());finish();return false;}scene.FlushCommands();}finish();return true;}
void WorldFrameScheduler::Tick(Scene& scene,float unscaledDelta,bool forceStep){m_Executing=true;++m_FrameIndex;m_Stats={};const bool paused=scene.GetState()==SceneState::Paused;const float frameDelta=std::max(unscaledDelta,0.0f)*scene.GetTimeScale();const float accumulatorDelta=std::min(frameDelta,0.25f);ExecutePhase(WorldPhase::WorldFrameBegin,scene,frameDelta,paused);ExecutePhase(WorldPhase::PreUpdate,scene,frameDelta,paused);if(forceStep)m_Accumulator=std::max(m_Accumulator,m_FixedDelta);else if(!paused)m_Accumulator+=accumulatorDelta;uint32_t ticks=0;while(m_Accumulator>=m_FixedDelta&&ticks<4){++m_FixedTickIndex;if(!ExecutePhase(WorldPhase::FixedPrePhysics,scene,m_FixedDelta,false)||!ExecutePhase(WorldPhase::FixedPhysics,scene,m_FixedDelta,false)||!ExecutePhase(WorldPhase::FixedPostPhysics,scene,m_FixedDelta,false))break;m_Accumulator-=m_FixedDelta;++ticks;}m_Stats.fixedTicks=ticks;if(m_Accumulator>=m_FixedDelta){m_Stats.droppedFixedTicks=static_cast<uint32_t>(m_Accumulator/m_FixedDelta);m_Accumulator=std::fmod(m_Accumulator,m_FixedDelta);}if(!paused||forceStep){ExecutePhase(WorldPhase::Update,scene,frameDelta,false);ExecutePhase(WorldPhase::LateUpdate,scene,frameDelta,false);}ExecutePhase(WorldPhase::RenderExtract,scene,frameDelta,paused);ExecutePhase(WorldPhase::WorldFrameEnd,scene,frameDelta,paused);m_Executing=false;}
void WorldFrameScheduler::Reset(){m_Accumulator=0;m_FrameIndex=0;m_FixedTickIndex=0;m_Stats={};}
