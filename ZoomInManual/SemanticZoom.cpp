// SemanticZoom.cpp
#include "pch.h"

// Windows 헤더의 min/max 매크로가 std::min/std::max, GLM 과 부딪히지 않게 이 파일에서만 지운다.
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include "SemanticZoom.h"
#include "Model.h"

#include <algorithm>
#include <cmath>

void SemanticZoom::Build(const Model& model)
{
    m_parts.clear();
    m_views.clear();
    m_focus.clear();

    const std::vector<Model::Node>& nodes = model.Nodes();
    m_slotOfNode.assign(nodes.size(), -1);
    for (int i = 0; i < static_cast<int>(nodes.size()); ++i)
    {
        const Model::Node& node = nodes[i];
        if (!node.isPart || !node.hasBounds)
            continue;
        Part part;
        part.node = i;
        part.level = node.level;
        part.center = (node.boundsMin + node.boundsMax) * 0.5f;          // 경계 상자의 가운데
        part.radius = glm::length(node.boundsMax - node.boundsMin) * 0.5f;   // 상자 대각선의 절반 = 경계구 반지름
        const int parentPart = node.parent >= 0 ? nodes[node.parent].part : -1;
        part.parentSlot = parentPart >= 0 ? m_slotOfNode[parentPart] : -1;
        m_slotOfNode[i] = static_cast<int>(m_parts.size());
        m_parts.push_back(part);
    }
    for (int slot = 0; slot < static_cast<int>(m_parts.size()); ++slot)
    {
        const int parentSlot = m_parts[slot].parentSlot;
        if (parentSlot >= 0)
        {
            m_parts[parentSlot].hasChildParts = true;
            m_parts[parentSlot].childSlots.push_back(slot);
        }
    }
    for (int owner : model.ShellOwners())
    {
        if (owner >= 0 && owner < static_cast<int>(m_slotOfNode.size()) && m_slotOfNode[owner] >= 0)
            m_parts[m_slotOfNode[owner]].hasShell = true;
    }

    m_views.resize(m_parts.size());
    for (size_t slot = 0; slot < m_parts.size(); ++slot)
    {
        m_views[slot].node = m_parts[slot].node;
        m_views[slot].radius = m_parts[slot].radius;
        m_views[slot].hasShell = m_parts[slot].hasShell;
    }
}

void SemanticZoom::Update(const glm::vec3& eye, const glm::vec3& lookAt, float viewportHeight, float fovYDegrees, float fitDistance)
{
    const float tanHalf = std::tan(fovYDegrees * 0.5f * 3.14159265f / 180.0f);
    const float H = std::max(viewportHeight, 1.0f);
    const Settings& s = m_settings;

    for (size_t slot = 0; slot < m_parts.size(); ++slot)
    {
        Part& part = m_parts[slot];
        PartView& view = m_views[slot];

        const float d = std::max(glm::length(eye - part.center), 1e-4f);
        view.distance = d;
        view.screenPx = part.radius * H / (2.0f * d * tanHalf);
        view.openDistance = (part.level == 0)
            ? fitDistance * s.rootOpenRatio
            : part.radius * H / (2.0f * s.openPx * tanHalf);

        // 펼침 ↔ 접힘(히스테리시스). r_px ≥ 300 ⇔ d ≤ d_open,  r_px < 240 ⇔ d > d_open / 0.8
        if (!part.rawOpen && d <= view.openDistance)
            part.rawOpen = true;
        else if (part.rawOpen && d > view.openDistance / s.closeRatio)
            part.rawOpen = false;

        const bool parentOpen = part.parentSlot < 0 || m_views[part.parentSlot].open;
        view.open = part.rawOpen && parentOpen && (part.hasChildParts || part.hasShell);

        // 겉껍데기: d_fade(= 1.5·d_open) 에서 1, d_open 에서 α_min, 그 사이는 직선
        if (part.hasShell)
        {
            const float fadeStart = view.openDistance * s.fadeRatio;
            float t = (d - view.openDistance) / std::max(fadeStart - view.openDistance, 1e-6f);
            t = std::min(std::max(t, 0.0f), 1.0f);
            view.alpha = s.alphaMin + (1.0f - s.alphaMin) * t;
        }
        else
        {
            view.alpha = 1.0f;
        }
    }

    // 펼쳐진 사슬: 맨 위 부품(차량)부터, 펼쳐진 자식 중 카메라 목표점에 가장 가까운 것을 따라 내려간다.
    // 가깝다 = |목표점 − 가운데| ÷ 반지름 (1보다 작으면 목표점이 그 부품의 경계구 안에 있다).
    // 화면에 가장 크게 보이는 것으로 고르면 카메라 바로 앞의 바퀴가 뽑히는 문제가 있었다.
    m_focus.clear();
    int slot = -1;
    for (size_t i = 0; i < m_parts.size(); ++i)
    {
        if (m_parts[i].parentSlot < 0)
        {
            slot = static_cast<int>(i);
            break;
        }
    }
    while (slot >= 0 && m_views[slot].open)
    {
        m_focus.push_back(m_parts[slot].node);
        int best = -1;
        float bestScore = 0.0f;
        for (int child : m_parts[slot].childSlots)
        {
            if (!m_views[child].open)
                continue;
            const Part& c = m_parts[child];
            const float score = glm::length(lookAt - c.center) / std::max(c.radius, 1e-4f);
            if (score > 1.25f)   // 목표점이 그 부품 근처가 아니면(카메라 옆을 스쳐 펼쳐진 바퀴 등) 건너뛴다
                continue;
            if (best < 0 || score < bestScore)
            {
                best = child;
                bestScore = score;
            }
        }
        slot = best;
    }
}

void SemanticZoom::ApplyTo(Model& model, float forcedAlpha) const
{
    for (const PartView& view : m_views)
    {
        if (view.hasShell)
            model.SetShellAlpha(view.node, std::min(view.alpha, forcedAlpha));
    }
}

const SemanticZoom::PartView* SemanticZoom::Find(int node) const
{
    if (node < 0 || node >= static_cast<int>(m_slotOfNode.size()) || m_slotOfNode[node] < 0)
        return nullptr;
    return &m_views[m_slotOfNode[node]];
}
