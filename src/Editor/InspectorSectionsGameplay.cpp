#include "Editor/InspectorSectionShared.h"

namespace {
class GameplayInspectorSection final : public ActorInspectorSection {
public:
    const char* GetID() const override { return "gameplay"; }
    int GetOrder() const override { return 240; }

    void Draw(EditorContext& context) override {
        Actor* actor = SelectedActor(context);
        if (!actor)
            return;

        auto* health = actor->GetComponent<HealthComponent>();
        auto* hitbox = actor->GetComponent<HitboxComponent>();
        auto* hurtbox = actor->GetComponent<HurtboxComponent>();
        auto* interaction = actor->GetComponent<InteractionComponent>();
        auto* particles = actor->GetComponent<ParticleSystemComponent>();
        auto* listener = actor->GetComponent<AudioListenerComponent>();
        auto* agent = actor->GetComponent<NavAgentComponent>();
        auto* enemy = actor->GetComponent<EnemyAIComponent>();
        auto* feedback = actor->GetComponent<GameplayFeedbackComponent>();
        if (!health && !hitbox && !hurtbox && !interaction && !particles && !listener && !agent && !enemy &&
            !feedback) {
            return;
        }

        ImGui::Separator();
        ImGui::PushID("GameplayComponents");
        if (!SectionHeaderWithIcon(context, EditorIcons::Physics, "Gameplay")) {
            ImGui::PopID();
            return;
        }

        if (health) {
            ImGui::TextUnformatted("Health");
            float maxHealth = health->GetMaxHealth();
            float value = health->GetHealth();
            if (ImGui::DragFloat("Max Health", &maxHealth, 1.0f, 0.01f, 100000.0f)) {
                CommitComponentEdit(context, *actor, *health, "maxHealth",
                                    [&] { health->SetMaxHealth(maxHealth, true); });
            }
            if (ImGui::DragFloat("Current Health", &value, 1.0f, 0.0f, 100000.0f)) {
                CommitComponentEdit(context, *actor, *health, "health", [&] { health->SetHealth(value); });
            }
            if (EditorWidgets::IconButton("RemoveHealth", "X", "Remove Health")) {
                RemoveComponentByType(context, *actor, "Health");
            }
        }

        if (hitbox) {
            ImGui::TextUnformatted("Hitbox");
            float damage = hitbox->GetDamage();
            float radius = hitbox->GetRadius();
            uint32_t team = hitbox->GetTeam();
            if (ImGui::DragFloat("Damage", &damage, 0.5f, 0.0f, 10000.0f)) {
                CommitComponentEdit(context, *actor, *hitbox, "damage", [&] { hitbox->SetDamage(damage); });
            }
            if (ImGui::DragFloat("Hit Radius", &radius, 0.02f, 0.01f, 100.0f)) {
                CommitComponentEdit(context, *actor, *hitbox, "radius", [&] { hitbox->SetRadius(radius); });
            }
            if (ImGui::InputScalar("Hit Team", ImGuiDataType_U32, &team)) {
                CommitComponentEdit(context, *actor, *hitbox, "team", [&] { hitbox->SetTeam(team); });
            }
        }

        if (hurtbox) {
            ImGui::TextUnformatted("Hurtbox");
            uint32_t team = hurtbox->GetTeam();
            float multiplier = hurtbox->GetDamageMultiplier();
            if (ImGui::InputScalar("Hurt Team", ImGuiDataType_U32, &team)) {
                CommitComponentEdit(context, *actor, *hurtbox, "team", [&] { hurtbox->SetTeam(team); });
            }
            if (ImGui::DragFloat("Damage Multiplier", &multiplier, 0.05f, 0.0f, 10.0f)) {
                CommitComponentEdit(context, *actor, *hurtbox, "damageMultiplier",
                                    [&] { hurtbox->SetDamageMultiplier(multiplier); });
            }
        }

        if (interaction) {
            ImGui::TextUnformatted("Interaction");
            float range = interaction->GetRange();
            bool single = interaction->IsSingleUse();
            bool destroyOnUse = interaction->GetDestroyOnUse();
            if (ImGui::DragFloat("Interaction Range", &range, 0.05f, 0.0f, 100.0f)) {
                CommitComponentEdit(context, *actor, *interaction, "range", [&] { interaction->SetRange(range); });
            }
            if (ImGui::Checkbox("Single Use", &single)) {
                CommitComponentEdit(context, *actor, *interaction, "singleUse",
                                    [&] { interaction->SetSingleUse(single); });
            }
            if (ImGui::Checkbox("Destroy On Use", &destroyOnUse)) {
                CommitComponentEdit(context, *actor, *interaction, "destroyOnUse",
                                    [&] { interaction->SetDestroyOnUse(destroyOnUse); });
            }
            ImGui::Text("Prompt: %s", interaction->GetPrompt().c_str());
        }

        if (particles) {
            ImGui::TextUnformatted("Particle System");
            auto settings = particles->GetSettings();
            bool changed = ImGui::InputScalar("Max Particles", ImGuiDataType_U32, &settings.maxParticles);
            changed |= ImGui::DragFloat("Emission Rate", &settings.rate, 0.5f, 0.0f, 10000.0f);
            changed |= ImGui::DragFloat("Lifetime", &settings.lifetime, 0.05f, 0.01f, 100.0f);
            changed |= ImGui::DragFloat("Start Size", &settings.startSize, 0.01f, 0.0f, 100.0f);
            if (changed) {
                CommitComponentEdit(context, *actor, *particles, "emitter",
                                    [&] { particles->GetSettings() = settings; });
            }
            ImGui::Text("Alive: %zu", particles->GetAliveCount());
        }

        if (listener) {
            bool primary = listener->IsPrimary();
            if (ImGui::Checkbox("Primary Audio Listener", &primary)) {
                CommitComponentEdit(context, *actor, *listener, "primary", [&] { listener->SetPrimary(primary); });
            }
        }

        if (agent) {
            float speed = agent->GetSpeed();
            float stop = agent->GetStoppingDistance();
            if (ImGui::DragFloat("Agent Speed", &speed, 0.1f, 0.0f, 100.0f)) {
                CommitComponentEdit(context, *actor, *agent, "speed", [&] { agent->SetSpeed(speed); });
            }
            if (ImGui::DragFloat("Stopping Distance", &stop, 0.01f, 0.0f, 10.0f)) {
                CommitComponentEdit(context, *actor, *agent, "stoppingDistance",
                                    [&] { agent->SetStoppingDistance(stop); });
            }
            ImGui::Text("Path: %s", agent->HasPath() ? "Active" : (agent->ReachedDestination() ? "Reached" : "None"));
        }

        if (enemy) {
            float detection = enemy->GetDetectionRange();
            float fieldOfView = enemy->GetFieldOfViewDegrees();
            float attackRange = enemy->GetAttackRange();
            float damage = enemy->GetAttackDamage();
            if (ImGui::DragFloat("Detection Range", &detection, 0.1f, 0.0f, 1000.0f)) {
                CommitComponentEdit(context, *actor, *enemy, "detectionRange",
                                    [&] { enemy->SetDetectionRange(detection); });
            }
            if (ImGui::DragFloat("Field Of View", &fieldOfView, 1.0f, 1.0f, 360.0f)) {
                CommitComponentEdit(context, *actor, *enemy, "fieldOfView",
                                    [&] { enemy->SetFieldOfViewDegrees(fieldOfView); });
            }
            if (ImGui::DragFloat("Attack Range", &attackRange, 0.05f, 0.0f, 100.0f)) {
                CommitComponentEdit(context, *actor, *enemy, "attackRange",
                                    [&] { enemy->SetAttackRange(attackRange); });
            }
            if (ImGui::DragFloat("Attack Damage", &damage, 0.5f, 0.0f, 10000.0f)) {
                CommitComponentEdit(context, *actor, *enemy, "attackDamage", [&] { enemy->SetAttackDamage(damage); });
            }
            ImGui::Text("State: %d", static_cast<int>(enemy->GetState()));
        }

        if (feedback) {
            ImGui::Text("Feedback: %s", feedback->IsActive() ? "Active" : "Idle");
        }
        ImGui::PopID();
    }
};

} // namespace

void RegisterGameplayInspectorSections(std::vector<std::unique_ptr<EditorInspectorSection>>& sections) {
    sections.push_back(std::make_unique<GameplayInspectorSection>());
}
