// PartSearch.cpp
#include "pch.h"
#include "PartSearch.h"

#include <algorithm>

namespace
{
    // "  ModuLe   03 " → "module 03" (같은 검색어인지 비교할 때만 쓴다. 한글은 그대로)
    std::string Normalize(const std::string& query)
    {
        std::string out;
        bool space = false;
        for (char c : query)
        {
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            {
                space = !out.empty();
                continue;
            }
            if (space)
                out.push_back(' ');
            space = false;
            out.push_back((c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c);
        }
        return out;
    }
}

void PartSearch::Reset()
{
    m_key.clear();
    m_matches.clear();
    m_index = -1;
}

int PartSearch::Next(const Model& model, const PartDB& db, const std::string& query)
{
    const std::string key = Normalize(query);
    if (key.empty())
    {
        Reset();
        return -1;
    }

    if (key != m_key || m_matches.empty())
    {
        // 새 검색어: 모든 부품(품번 빈 객체)을 훑어 맞는 것을 모은다
        m_key = key;
        m_matches.clear();
        m_index = -1;
        const std::vector<Model::Node>& nodes = model.Nodes();
        for (int i = 0; i < static_cast<int>(nodes.size()); ++i)
        {
            if (nodes[i].isPart && db.Matches(nodes[i].name, key))
                m_matches.push_back(i);
        }
        // 큰 부품 먼저(단계 L0 → L3), 같은 단계는 모델 순서 그대로
        std::stable_sort(m_matches.begin(), m_matches.end(),
                         [&nodes](int a, int b) { return nodes[a].level < nodes[b].level; });
    }
    if (m_matches.empty())
        return -1;

    m_index = (m_index + 1) % static_cast<int>(m_matches.size());
    return m_matches[m_index];
}
