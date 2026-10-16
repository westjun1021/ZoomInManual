// Camera.h
// 궤도 카메라: 목표점(target) 주위를 yaw(좌우)·pitch(위아래) 각도로 돌고, 거리(distance)로 다가간다.
// 2주차: 1주차 GLRenderer 안에 있던 카메라를 따로 뺐다.
// 3주차: 커서 쪽 줌(ZoomAt)과 부드러운 움직임. 줌·초기화는 '가려는 값(goal)'만 바꾸고,
//        Update 가 매 프레임 지금 값을 goal 쪽으로 조금씩 옮긴다(약 0.2초에 95%).
// 4주차: FlyTo — 더블클릭한 부품이 화면을 채우는 거리로 이동(보고서: 거리 = r / sin(θ/2) × 1.2).
// 5주차: FlyTo·Reset 은 더 천천히(시간 상수 0.2초 → 약 0.6초에 95%, 플랜의 '0.6초 동안 이동'), 휠 줌은 그대로 빠르게.
// 순수 GLM 코드라 MFC·OpenGL 과 상관없다.
#pragma once
#include <glm/glm.hpp>

class Camera
{
public:
    // 반지름 radius 인 모델 전체가 화면에 들어오도록 거리를 맞춘다(보고서: 거리 = r / sin(θ/2) × 1.2).
    void Fit(const glm::vec3& center, float radius);
    void Reset();                                   // 처음 시점(yaw 30°, pitch 20°, Fit 거리)으로 부드럽게 돌아간다

    void Orbit(float dxPixels, float dyPixels);     // 마우스 드래그 이동량(픽셀). 바로 반영된다
    void Zoom(int wheelDelta);                      // 목표점 쪽으로 줌(한 칸 = ±120, 거리 × 0.85)
    // pivot(커서 아래 점)이 화면에서 제자리에 머물도록 다가간다. 물러날 때는 Zoom 과 같다.
    void ZoomAt(int wheelDelta, const glm::vec3& pivot);
    // 가운데 center, 반지름 radius 인 부품이 화면을 채우도록 보는 방향은 그대로 두고 다가간다.
    void FlyTo(const glm::vec3& center, float radius);

    void Update(float dtSeconds);                   // 매 프레임: 지금 값 → 가려는 값
    void Snap();                                    // 애니메이션 없이 바로 가려는 값으로

    glm::mat4 View() const;
    glm::mat4 Projection(float aspect) const;
    glm::vec3 Eye() const;
    glm::vec3 Target() const { return m_now.target; }
    float FovYDegrees() const { return m_fovY; }
    float Distance() const { return m_now.distance; }
    float FitDistance() const { return m_fitDistance; }   // 처음 거리(시맨틱 줌의 차량 기준)

private:
    struct Pose
    {
        glm::vec3 target{ 0.0f };
        float yaw = 30.0f;
        float pitch = 20.0f;
        float distance = 6.0f;
    };

    float ZoomedDistance(int wheelDelta) const;
    void PullTargetHome(float newDistance);         // 물러날 때 목표점을 모델 가운데로 되돌린다

    Pose m_now;                   // 지금 화면에 쓰는 값
    Pose m_goal;                  // 가려는 값
    float m_smoothSeconds = 0.07f;   // 따라가는 빠르기(시간 상수). 휠 0.07초, 이동·초기화 0.2초
    glm::vec3 m_fitCenter{ 0.0f };
    float m_radius = 1.0f;        // 모델 크기(줌 범위·near/far 계산용)
    float m_fitDistance = 6.0f;
    float m_fovY = 45.0f;         // 보고서 3장의 시야각 θ
};
