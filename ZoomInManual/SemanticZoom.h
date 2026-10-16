// SemanticZoom.h
// 3주차: 시맨틱 줌. 매 프레임 부품마다 경계구의 화면 반지름 r_px 를 계산한다(보고서 3장).
//
//   r_px   = r · H / (2 · d · tan(θ/2))        r = 경계구 반지름, d = 카메라 ~ 경계구 가운데, H = 3D 뷰 높이(px), θ = 45°
//   d_open = r · H / (2 · 300 · tan(θ/2))      r_px 가 300px 가 되는 거리
//
//   - 펼침: r_px ≥ 300px (d ≤ d_open),  접힘: r_px < 240px (d > d_open / 0.8). 그 사이는 이전 상태 유지 → 깜빡이지 않음
//   - 겉껍데기 불투명도: d 가 1.5·d_open → d_open 으로 다가오는 동안 1 → 0.15
//   - 부모가 접혀 있으면 자식은 펼쳐지지 않는다(차량 → 배터리팩 → 모듈 순서)
//
// 예외 하나: 차량(L0)은 보고서의 L0~L2 위에 우리가 얹은 단계라, 차 전체가 보이는 처음 화면에서 이미
// r_px 가 300px 근처다(3D 뷰 높이 700px 에서 약 270px). 그대로 쓰면 시작부터 차체가 반쯤 투명하므로
// 차량만 d_open = 처음 거리 × 0.6 으로 둔다(휠 3~4칸에 펼침). 나머지 부품은 모두 같은 공식.
//
// 순수 C++ + GLM 코드(MFC·OpenGL 과 상관없음).
#pragma once
#include <glm/glm.hpp>

#include <vector>

class Model;

class SemanticZoom
{
public:
    struct Settings
    {
        float openPx = 300.0f;        // T_open
        float closeRatio = 0.8f;      // 접힘 = 0.8 × T_open = 240px
        float fadeRatio = 1.5f;       // d_fade = 1.5 × d_open
        float alphaMin = 0.15f;       // α_min
        float rootOpenRatio = 0.6f;   // 차량(L0)만: d_open = 처음 거리 × 0.6
    };

    struct PartView                   // 부품 하나의 이번 프레임 값
    {
        int node = -1;                // Model 노드 번호
        float radius = 0.0f;          // r (m)
        float distance = 0.0f;        // d (m)
        float screenPx = 0.0f;        // r_px
        float openDistance = 0.0f;    // d_open (m)
        float alpha = 1.0f;           // 겉껍데기 불투명도(겉껍데기가 없으면 1)
        bool open = false;            // 펼침(부모가 모두 펼쳐져 있을 때만 true)
        bool hasShell = false;
    };

    void Build(const Model& model);   // 모델을 읽은 뒤 한 번
    // 매 프레임: eye = 카메라 위치, lookAt = 카메라 목표점(커서 줌이 커서 쪽으로 옮긴다),
    //            viewportHeight = H(px), fitDistance = 처음 거리(차량 기준)
    void Update(const glm::vec3& eye, const glm::vec3& lookAt, float viewportHeight, float fovYDegrees, float fitDistance);
    // 겉껍데기 투명도를 모델에 넣는다. forcedAlpha < 1 이면(S 키) 모든 겉껍데기를 그 값 이하로.
    void ApplyTo(Model& model, float forcedAlpha) const;

    // 펼쳐진 부품 사슬: [차량, 배터리팩, 모듈 …]. 같은 단계에서 여러 개가 펼쳐져 있으면
    // 카메라 목표점(지금 들여다보는 곳)에 가장 가까운 것을 따라간다.
    const std::vector<int>& FocusPath() const { return m_focus; }
    const std::vector<PartView>& Parts() const { return m_views; }
    const PartView* Find(int node) const;
    const Settings& GetSettings() const { return m_settings; }

private:
    struct Part
    {
        int node = -1;
        int parentSlot = -1;          // 부모 부품의 번호(m_parts 안), 없으면 -1
        int level = 0;
        glm::vec3 center{ 0.0f };
        float radius = 0.0f;
        bool hasShell = false;
        bool hasChildParts = false;   // 자식 부품이 있어야 '펼칠' 것이 있다
        bool rawOpen = false;         // 자기 r_px 만 본 상태(히스테리시스 기억)
        std::vector<int> childSlots;
    };

    std::vector<Part> m_parts;        // 부모가 자식보다 앞(Model 노드 순서)
    std::vector<PartView> m_views;
    std::vector<int> m_slotOfNode;    // Model 노드 번호 → m_parts 번호(부품이 아니면 -1)
    std::vector<int> m_focus;
    Settings m_settings;
};
