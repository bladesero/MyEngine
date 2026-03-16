#pragma once

#include "Scene/Actor.h"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>

// ==========================================================================
// Scene  –  Actor 集合管理
//
//  - 创建 / 销毁 Actor
//  - 按 ID / 名字查找
//  - 遍历所有 Actor（Update）
//  - 延迟销毁：标记 → 帧末批量清除
// ==========================================================================

class Scene {
public:
    explicit Scene(std::string name = "Scene");
    ~Scene() = default;

    // 禁止拷贝
    Scene(const Scene&)            = delete;
    Scene& operator=(const Scene&) = delete;

    // -----------------------------------------------------------------------
    // 基础属性
    // -----------------------------------------------------------------------
    const std::string& GetName() const { return m_Name; }
    void               SetName(const std::string& name) { m_Name = name; }

    // -----------------------------------------------------------------------
    // Actor 创建
    // -----------------------------------------------------------------------

    // 创建 Actor 并加入场景，返回裸指针（所有权由 Scene 持有）
    Actor* CreateActor(const std::string& name = "Actor");

    // 创建 Actor 并立即设置父节点
    Actor* CreateActor(const std::string& name, Actor* parent);

    // 反序列化专用：以指定 ID 创建 Actor（保持原始 ID）
    Actor* CreateActorWithID(const std::string& name, uint64_t id);

    // 反序列化专用：重置 ID 计数器
    void SetNextID(uint64_t nextID) { m_NextID = nextID; }

    // 清除场景所有内容
    void Clear();

    // -----------------------------------------------------------------------
    // Actor 销毁
    // -----------------------------------------------------------------------

    // 立即销毁（从场景移除，并递归销毁所有子节点）
    void DestroyActor(Actor* actor);

    // 延迟销毁：帧末在 FlushPendingDestroy() 调用时批量清除
    void DestroyActorDeferred(Actor* actor);

    // 处理所有延迟销毁队列（通常在帧末调用）
    void FlushPendingDestroy();

    // -----------------------------------------------------------------------
    // 查找
    // -----------------------------------------------------------------------
    Actor* FindByID(uint64_t id) const;
    Actor* FindByName(const std::string& name) const;

    // 返回所有根节点 Actor（无父节点）
    std::vector<Actor*> GetRootActors() const;

    // 返回场景所有 Actor（扁平列表）
    const std::vector<std::unique_ptr<Actor>>& GetAllActors() const { return m_Actors; }

    // 获取 Actor 数量
    size_t ActorCount() const { return m_Actors.size(); }

    // -----------------------------------------------------------------------
    // 遍历
    // -----------------------------------------------------------------------
    void ForEach(const std::function<void(Actor&)>& fn) const;

    // -----------------------------------------------------------------------
    // 生命周期（由外部驱动，如 Layer）
    // -----------------------------------------------------------------------
    void OnUpdate(float deltaSeconds);

private:
    // 真正销毁 Actor 及其全部子树
    void DestroyActorInternal(Actor* actor);

    std::string m_Name;
    uint64_t    m_NextID = 1;

    std::vector<std::unique_ptr<Actor>>         m_Actors;
    std::unordered_map<uint64_t, Actor*>        m_IDMap;

    // 待销毁队列（延迟删除）
    std::vector<Actor*> m_PendingDestroy;
};
