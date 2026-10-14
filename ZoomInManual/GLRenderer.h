// GLRenderer.h
// OpenGL 3.3 Core로 장면을 그리는 클래스. 창(MFC)과 상관없는 순수 OpenGL 코드다.
// 1주차: 색 큐브 + 바닥 격자 + 궤도 카메라(드래그 회전, 휠 줌).
// 2주차부터 여기에 GLB 모델 로딩(Assimp)을 붙인다.
#pragma once
#include <string>

class GLRenderer
{
public:
    // OpenGL 컨텍스트가 현재 스레드에 연결되고 gladLoadGL()이 끝난 뒤에 호출한다.
    bool Init(std::string* log = nullptr);
    void Shutdown();

    void Resize(int width, int height);
    void Render();

    // 카메라 조작 (GL_ENC 코드의 마우스 회전·z/x 확대를 옮겨 온 것)
    void Orbit(float dxPixels, float dyPixels);   // 마우스 드래그 이동량(픽셀)
    void Zoom(int wheelDelta);                    // WM_MOUSEWHEEL 값(한 칸 = ±120)
    void ResetView();

    void ToggleWireframe() { m_wireframe = !m_wireframe; }
    std::string GlInfo() const;                   // 예: "OpenGL 4.6.0 ... (Core) / GPU 이름"

private:
    unsigned int m_program = 0;
    unsigned int m_vao = 0;
    unsigned int m_vbo = 0;
    int m_locMvp = -1;
    int m_cubeVertexCount = 0;
    int m_gridVertexCount = 0;

    int m_width = 1;
    int m_height = 1;

    // 궤도 카메라: 원점을 중심으로 yaw(좌우)·pitch(위아래) 각도와 거리
    float m_yaw = 30.0f;
    float m_pitch = 20.0f;
    float m_distance = 6.0f;
    bool  m_wireframe = false;
};
