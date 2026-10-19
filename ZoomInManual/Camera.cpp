// Camera.cpp
#include "pch.h"

// Windows 헤더의 min/max 매크로가 std::min/std::max, GLM 과 부딪히지 않게 이 파일에서만 지운다.
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include "Camera.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace
{
    constexpr float kZoomSmooth = 0.07f;   // 휠 줌: 0.2초면 95% 도착
    constexpr float kFlySmooth = 0.20f;    // 부품으로 이동·처음 시점: 0.6초면 95% 도착

    float Clamp(float v, float lo, float hi)
    {
        return std::min(std::max(v, lo), hi);
    }
}

void Camera::Fit(const glm::vec3& center, float radius)
{
    m_fitCenter = center;
    m_radius = std::max(radius, 0.01f);
    m_fitDistance = m_radius / std::sin(glm::radians(m_fovY) * 0.5f) * 1.2f;
    Reset();
    Snap();
}

void Camera::Reset()
{
    m_smoothSeconds = kFlySmooth;
    m_goal.target = m_fitCenter;
    m_goal.distance = m_fitDistance;
    m_goal.pitch = 20.0f;
    // 여러 바퀴 돌린 뒤에도 가까운 쪽으로 돌아오게 30° + 360° × n 중 지금과 가장 가까운 값을 고른다.
    m_goal.yaw = 30.0f + 360.0f * std::round((m_now.yaw - 30.0f) / 360.0f);
}

void Camera::Orbit(float dxPixels, float dyPixels)
{
    // 오른쪽으로 끌면 물체가 오른쪽으로 돌고, 아래로 끌면 위에서 내려다보게 된다(1주차와 같음).
    m_now.yaw -= dxPixels * 0.4f;
    m_now.pitch = Clamp(m_now.pitch + dyPixels * 0.4f, -89.0f, 89.0f);
    m_goal.yaw = m_now.yaw;       // 드래그는 손을 바로 따라오게
    m_goal.pitch = m_now.pitch;
}

float Camera::ZoomedDistance(int wheelDelta) const
{
    // 휠 한 칸마다 거리 × 0.85. 모델 반지름의 5% 까지 다가가고, 처음 거리의 4배까지 물러난다.
    const float steps = static_cast<float>(wheelDelta) / 120.0f;
    return Clamp(m_goal.distance * std::pow(0.85f, steps), m_radius * 0.05f, m_fitDistance * 4.0f);
}

void Camera::PullTargetHome(float newDistance)
{
    // 처음 거리까지 물러나는 동안 목표점이 모델 가운데로 조금씩 돌아온다(다 물러나면 처음 구도).
    const float span = m_fitDistance - m_goal.distance;
    const float t = span > 1e-4f ? Clamp((newDistance - m_goal.distance) / span, 0.0f, 1.0f) : 1.0f;
    m_goal.target += (m_fitCenter - m_goal.target) * t;
}

void Camera::Zoom(int wheelDelta)
{
    m_smoothSeconds = kZoomSmooth;
    const float newDistance = ZoomedDistance(wheelDelta);
    if (newDistance > m_goal.distance)
        PullTargetHome(newDistance);
    m_goal.distance = newDistance;
}

void Camera::ZoomAt(int wheelDelta, const glm::vec3& pivot)
{
    m_smoothSeconds = kZoomSmooth;
    const float newDistance = ZoomedDistance(wheelDelta);
    if (newDistance >= m_goal.distance || m_goal.distance <= 0.0f)
    {
        Zoom(wheelDelta);
        return;
    }
    // 눈과 목표점을 pivot 쪽으로 같은 비율 s 만큼 당기면 보는 방향은 그대로이고 pivot 은 화면에서 안 움직인다.
    const float s = newDistance / m_goal.distance;
    m_goal.target = pivot + (m_goal.target - pivot) * s;
    m_goal.distance = newDistance;
}

void Camera::FlyTo(const glm::vec3& center, float radius)
{
    m_smoothSeconds = kFlySmooth;
    m_goal.target = center;
    m_goal.distance = Clamp(std::max(radius, 0.001f) / std::sin(glm::radians(m_fovY) * 0.5f) * 1.2f,
                            m_radius * 0.05f, m_fitDistance * 4.0f);
}

void Camera::FlyToDistance(const glm::vec3& center, float distance)
{
    m_smoothSeconds = kFlySmooth;
    m_goal.target = center;
    m_goal.distance = Clamp(distance, m_radius * 0.05f, m_fitDistance * 4.0f);
}

void Camera::Update(float dtSeconds)
{
    // 목표점과 거리를 같은 비율로 따라가게 해야 커서 아래 점이 움직이는 동안에도 제자리에 있다.
    const float k = 1.0f - std::exp(-std::max(dtSeconds, 0.0f) / m_smoothSeconds);
    m_now.target += (m_goal.target - m_now.target) * k;
    m_now.distance += (m_goal.distance - m_now.distance) * k;
    m_now.yaw += (m_goal.yaw - m_now.yaw) * k;
    m_now.pitch += (m_goal.pitch - m_now.pitch) * k;
}

void Camera::Snap()
{
    m_now = m_goal;
}

glm::vec3 Camera::Eye() const
{
    const float yaw = glm::radians(m_now.yaw);
    const float pitch = glm::radians(m_now.pitch);
    const glm::vec3 offset(std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw));
    return m_now.target + offset * m_now.distance;
}

glm::mat4 Camera::View() const
{
    return glm::lookAt(Eye(), m_now.target, glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 Camera::Projection(float aspect) const
{
    // 가까이 갈수록 near 를 줄여서 부품 속까지 잘리지 않게 본다.
    const float nearPlane = std::max(m_now.distance * 0.01f, 0.001f);
    const float farPlane = m_now.distance + m_radius * 4.0f;
    return glm::perspective(glm::radians(m_fovY), aspect, nearPlane, farPlane);
}
