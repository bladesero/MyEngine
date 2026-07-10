#include "Gameplay/EnemyAIComponent.h"

#include "Gameplay/GameplayComponents.h"
#include "Navigation/NavAgentComponent.h"
#include "Physics/PhysicsWorld.h"
#include "Scene/Actor.h"
#include "Scene/Scene.h"

#include <algorithm>

void EnemyAIComponent::SetTarget(ActorHandle target){m_Target=target;m_TargetActorID=0;if(Actor* owner=GetOwner())if(Scene* scene=owner->GetScene())if(Actor* actor=scene->TryGetActor(target))m_TargetActorID=actor->GetID();}
void EnemyAIComponent::SetDetectionRange(float value){m_DetectionRange=std::max(0.0f,value);}void EnemyAIComponent::SetFieldOfViewDegrees(float value){m_FieldOfViewDegrees=std::clamp(value,1.0f,360.0f);}void EnemyAIComponent::SetAttackRange(float value){m_AttackRange=std::max(0.0f,value);}void EnemyAIComponent::SetAttackDamage(float value){m_AttackDamage=std::max(0.0f,value);}
void EnemyAIComponent::Stagger(float duration){if(m_State!=EnemyState::Dead){ChangeState(EnemyState::Stagger);m_StateTimer=std::max(0.0f,duration);}}
void EnemyAIComponent::ChangeState(EnemyState state){if(m_State==state)return;m_State=state;m_StateTimer=0.0f;if((state==EnemyState::Idle||state==EnemyState::Attack||state==EnemyState::Stagger||state==EnemyState::Dead)&&GetOwner())if(auto* agent=GetOwner()->GetComponent<NavAgentComponent>())agent->Stop();}
bool EnemyAIComponent::CanSeeTarget()const{Actor* owner=GetOwner();Scene* scene=owner?owner->GetScene():nullptr;Actor* target=scene?scene->TryGetActor(m_Target):nullptr;if(!owner||!target)return false;Vec3 delta=target->GetWorldPosition()-owner->GetWorldPosition();float distance=delta.Length();if(distance>m_DetectionRange)return false;if(distance>1e-5f&&m_FieldOfViewDegrees<359.9f){const Vec3 forward=owner->GetWorldMatrix().TransformDir(Vec3::Forward()).Normalized();const float minimumDot=std::cos(m_FieldOfViewDegrees*0.5f*kDeg2Rad);if(forward.Dot(delta/distance)<minimumDot)return false;}RaycastHit hit;if(scene->GetPhysicsWorld().Raycast(*scene,Ray{owner->GetWorldPosition()+Vec3(0,1,0),delta.Normalized()},distance,0xffffffffu,hit))return hit.actor==target;return true;}
void EnemyAIComponent::OnUpdate(float dt)
{
    Actor* owner=GetOwner();Scene* scene=owner?owner->GetScene():nullptr;if(!owner||!scene)return;if(!scene->TryGetActor(m_Target)&&m_TargetActorID)m_Target=scene->GetHandle(m_TargetActorID);Actor* target=scene->TryGetActor(m_Target);auto* health=owner->GetComponent<HealthComponent>();auto* agent=owner->GetComponent<NavAgentComponent>();m_AttackTimer=std::max(0.0f,m_AttackTimer-dt);m_StateTimer=std::max(0.0f,m_StateTimer-dt);
    if(health&&health->IsDead()){ChangeState(EnemyState::Dead);return;}if(m_State==EnemyState::Stagger){if(m_StateTimer<=0)ChangeState(target?EnemyState::Chase:EnemyState::Idle);return;}if(m_State==EnemyState::Dead)return;
    if(!target){for(const auto& sound:scene->GetNavigationWorld().QuerySounds(owner->GetWorldPosition()))if(scene->TryGetActor(sound.source)){SetTarget(sound.source);target=scene->TryGetActor(m_Target);break;}}
    const float distance=target?(target->GetWorldPosition()-owner->GetWorldPosition()).Length():1e9f;
    if(target&&distance<=m_AttackRange){ChangeState(EnemyState::Attack);if(m_AttackTimer<=0){if(auto* targetHealth=target->GetComponent<HealthComponent>())targetHealth->ApplyDamage({owner->GetHandle(),target->GetHandle(),m_AttackDamage,target->GetWorldPosition(),(target->GetWorldPosition()-owner->GetWorldPosition()).Normalized()});m_AttackTimer=m_AttackCooldown;}return;}
    if(target&&CanSeeTarget()){ChangeState(EnemyState::Chase);if(agent&&(!agent->HasPath()||(agent->GetDestination()-target->GetWorldPosition()).LengthSq()>0.25f))agent->SetDestination(target->GetWorldPosition());return;}
    if(!m_PatrolPoints.empty()){ChangeState(EnemyState::Patrol);if(agent&&(!agent->HasPath()||agent->ReachedDestination())){agent->SetDestination(m_PatrolPoints[m_PatrolIndex]);m_PatrolIndex=(m_PatrolIndex+1)%m_PatrolPoints.size();}}else ChangeState(EnemyState::Idle);
}
void EnemyAIComponent::Serialize(nlohmann::json& data)const{data={{"targetActorId",m_TargetActorID},{"detectionRange",m_DetectionRange},{"fieldOfView",m_FieldOfViewDegrees},{"attackRange",m_AttackRange},{"attackDamage",m_AttackDamage},{"attackCooldown",m_AttackCooldown},{"patrolPoints",nlohmann::json::array()}};for(const Vec3& p:m_PatrolPoints)data["patrolPoints"].push_back({p.x,p.y,p.z});}
void EnemyAIComponent::Deserialize(const nlohmann::json& data){m_Target={};m_TargetActorID=data.value("targetActorId",uint64_t(0));SetDetectionRange(data.value("detectionRange",10.0f));SetFieldOfViewDegrees(data.value("fieldOfView",120.0f));SetAttackRange(data.value("attackRange",1.5f));SetAttackDamage(data.value("attackDamage",10.0f));m_AttackCooldown=std::max(0.0f,data.value("attackCooldown",1.0f));m_PatrolPoints.clear();for(const auto& p:data.value("patrolPoints",nlohmann::json::array()))if(p.is_array()&&p.size()==3)m_PatrolPoints.push_back({p[0].get<float>(),p[1].get<float>(),p[2].get<float>()});m_State=EnemyState::Idle;}
