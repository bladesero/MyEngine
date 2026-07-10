#include "Navigation/NavAgentComponent.h"

#include "Navigation/NavigationWorld.h"
#include "Physics/CharacterControllerComponent.h"
#include "Scene/Actor.h"
#include "Scene/Scene.h"

#include <algorithm>

bool NavAgentComponent::SetDestination(const Vec3& destination){Actor* owner=GetOwner();Scene* scene=owner?owner->GetScene():nullptr;if(!scene)return false;std::vector<Vec3> path;if(!scene->GetNavigationWorld().FindPath(owner->GetWorldPosition(),destination,path))return false;m_Destination=destination;m_Path=std::move(path);m_PathIndex=m_Path.size()>1?1:0;m_Reached=false;m_PathRevision=scene->GetNavigationWorld().GetRevision();return true;}
void NavAgentComponent::Stop(){m_Path.clear();m_PathIndex=0;m_Reached=true;if(Actor* owner=GetOwner())if(auto* controller=owner->GetComponent<CharacterControllerComponent>())controller->Move(Vec3::Zero());}
void NavAgentComponent::SetSpeed(float value){m_Speed=std::max(0.0f,value);}void NavAgentComponent::SetStoppingDistance(float value){m_StoppingDistance=std::max(0.0f,value);}
void NavAgentComponent::OnUpdate(float dt){Actor* owner=GetOwner();Scene* scene=owner?owner->GetScene():nullptr;if(!owner||!scene||!HasPath())return;if(m_PathRevision!=scene->GetNavigationWorld().GetRevision()&&!SetDestination(m_Destination)){Stop();return;}Vec3 delta=m_Path[m_PathIndex]-owner->GetWorldPosition();delta.y=0;float distance=delta.Length();if(distance<=m_StoppingDistance){++m_PathIndex;if(!HasPath()){Stop();return;}delta=m_Path[m_PathIndex]-owner->GetWorldPosition();delta.y=0;distance=delta.Length();}if(distance<=0.0001f)return;Vec3 velocity=delta.Normalized()*m_Speed;if(auto* controller=owner->GetComponent<CharacterControllerComponent>())controller->Move(velocity);else owner->GetTransform().position+=velocity*std::min(dt,distance/std::max(0.001f,m_Speed));}
void NavAgentComponent::Serialize(nlohmann::json& data)const{data={{"speed",m_Speed},{"stoppingDistance",m_StoppingDistance}};}void NavAgentComponent::Deserialize(const nlohmann::json& data){SetSpeed(data.value("speed",3.0f));SetStoppingDistance(data.value("stoppingDistance",0.15f));Stop();}
