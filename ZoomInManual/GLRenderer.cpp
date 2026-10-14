// GLRenderer.cpp
#include "pch.h"

// Windows 헤더의 min/max 매크로가 std::min/std::max, GLM과 부딪히지 않게 이 파일에서만 지운다.
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <glad/glad.h>
#include "GLRenderer.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
    // 셰이더: 정점마다 색을 받아 그대로 칠한다.
    const char* kVertexShader = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
uniform mat4 uMVP;
out vec3 vColor;
void main()
{
    vColor = aColor;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

    const char* kFragmentShader = R"(#version 330 core
in vec3 vColor;
out vec4 FragColor;
void main()
{
    FragColor = vec4(vColor, 1.0);
}
)";

    struct Vertex
    {
        float x, y, z;
        float r, g, b;
    };

    float Clamp(float v, float lo, float hi)
    {
        return std::min(std::max(v, lo), hi);
    }

    GLuint CompileShader(GLenum type, const char* source, std::string* log)
    {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);

        GLint ok = GL_FALSE;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok)
        {
            char buffer[1024] = {};
            glGetShaderInfoLog(shader, sizeof(buffer), nullptr, buffer);
            if (log) *log += std::string("Shader compile error: ") + buffer;
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    }

    GLuint LinkProgram(GLuint vs, GLuint fs, std::string* log)
    {
        GLuint program = glCreateProgram();
        glAttachShader(program, vs);
        glAttachShader(program, fs);
        glLinkProgram(program);

        GLint ok = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &ok);
        if (!ok)
        {
            char buffer[1024] = {};
            glGetProgramInfoLog(program, sizeof(buffer), nullptr, buffer);
            if (log) *log += std::string("Program link error: ") + buffer;
            glDeleteProgram(program);
            return 0;
        }
        return program;
    }

    // 한 면 = 삼각형 2개. 꼭짓점 순서는 바깥에서 볼 때 반시계 방향.
    void AddFace(std::vector<Vertex>& out, const float c[4][3], float r, float g, float b)
    {
        const int order[6] = { 0, 1, 2, 0, 2, 3 };
        for (int i : order)
            out.push_back({ c[i][0], c[i][1], c[i][2], r, g, b });
    }

    // GL_ENC 코드의 color[] 표에서 앞의 6색(빨강·초록·파랑·노랑·자홍·청록)을 면마다 썼다.
    std::vector<Vertex> MakeCube()
    {
        std::vector<Vertex> v;
        const float front[4][3]  = { {-1,-1, 1}, { 1,-1, 1}, { 1, 1, 1}, {-1, 1, 1} };
        const float back[4][3]   = { { 1,-1,-1}, {-1,-1,-1}, {-1, 1,-1}, { 1, 1,-1} };
        const float right[4][3]  = { { 1,-1, 1}, { 1,-1,-1}, { 1, 1,-1}, { 1, 1, 1} };
        const float left[4][3]   = { {-1,-1,-1}, {-1,-1, 1}, {-1, 1, 1}, {-1, 1,-1} };
        const float top[4][3]    = { {-1, 1, 1}, { 1, 1, 1}, { 1, 1,-1}, {-1, 1,-1} };
        const float bottom[4][3] = { {-1,-1,-1}, { 1,-1,-1}, { 1,-1, 1}, {-1,-1, 1} };
        AddFace(v, front,  1.0f, 0.0f, 0.0f);   // +Z 빨강
        AddFace(v, back,   0.0f, 1.0f, 0.0f);   // -Z 초록
        AddFace(v, right,  0.0f, 0.0f, 1.0f);   // +X 파랑
        AddFace(v, left,   1.0f, 1.0f, 0.0f);   // -X 노랑
        AddFace(v, top,    1.0f, 0.0f, 1.0f);   // +Y 자홍
        AddFace(v, bottom, 0.0f, 1.0f, 1.0f);   // -Y 청록
        return v;
    }

    // 바닥 격자(큐브 바로 아래). X축 선은 붉게, Z축 선은 푸르게.
    std::vector<Vertex> MakeGrid()
    {
        std::vector<Vertex> v;
        const float y = -1.01f;
        const int half = 5;
        for (int i = -half; i <= half; ++i)
        {
            const float t = static_cast<float>(i);
            const bool center = (i == 0);
            // X 방향 선
            const float rx = center ? 0.85f : 0.35f, gx = center ? 0.30f : 0.35f, bx = center ? 0.30f : 0.40f;
            v.push_back({ -static_cast<float>(half), y, t, rx, gx, bx });
            v.push_back({  static_cast<float>(half), y, t, rx, gx, bx });
            // Z 방향 선
            const float rz = center ? 0.30f : 0.35f, gz = center ? 0.40f : 0.35f, bz = center ? 0.90f : 0.40f;
            v.push_back({ t, y, -static_cast<float>(half), rz, gz, bz });
            v.push_back({ t, y,  static_cast<float>(half), rz, gz, bz });
        }
        return v;
    }
}

bool GLRenderer::Init(std::string* log)
{
    GLuint vs = CompileShader(GL_VERTEX_SHADER, kVertexShader, log);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kFragmentShader, log);
    if (!vs || !fs)
    {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return false;
    }
    m_program = LinkProgram(vs, fs, log);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!m_program)
        return false;
    m_locMvp = glGetUniformLocation(m_program, "uMVP");

    // 큐브와 격자를 버퍼 하나에 이어 붙인다: [큐브 36개][격자 선]
    std::vector<Vertex> vertices = MakeCube();
    m_cubeVertexCount = static_cast<int>(vertices.size());
    std::vector<Vertex> grid = MakeGrid();
    m_gridVertexCount = static_cast<int>(grid.size());
    vertices.insert(vertices.end(), grid.begin(), grid.end());

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)),
                 vertices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(3 * sizeof(float)));
    glBindVertexArray(0);

    glEnable(GL_DEPTH_TEST);

    const GLenum err = glGetError();
    if (err != GL_NO_ERROR)
    {
        if (log) *log += "OpenGL error during Init: " + std::to_string(err);
        return false;
    }
    return true;
}

void GLRenderer::Shutdown()
{
    if (m_vbo)     glDeleteBuffers(1, &m_vbo);
    if (m_vao)     glDeleteVertexArrays(1, &m_vao);
    if (m_program) glDeleteProgram(m_program);
    m_vbo = m_vao = m_program = 0;
}

void GLRenderer::Resize(int width, int height)
{
    m_width = std::max(width, 1);
    m_height = std::max(height, 1);
}

void GLRenderer::Render()
{
    glViewport(0, 0, m_width, m_height);
    glClearColor(0.12f, 0.12f, 0.15f, 1.0f);   // Texture_Mapping 코드와 같은 배경색
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    // 투영: 보고서 3장의 시야각 θ = 45°
    const float aspect = static_cast<float>(m_width) / static_cast<float>(m_height);
    const glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspect, 0.05f, 200.0f);

    // 궤도 카메라: 원점 주위를 도는 눈 위치
    const float yaw = glm::radians(m_yaw);
    const float pitch = glm::radians(m_pitch);
    const glm::vec3 eye = m_distance * glm::vec3(std::cos(pitch) * std::sin(yaw),
                                                 std::sin(pitch),
                                                 std::cos(pitch) * std::cos(yaw));
    const glm::mat4 view = glm::lookAt(eye, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 mvp = projection * view;   // 모델 행렬은 아직 단위 행렬

    glUseProgram(m_program);
    glUniformMatrix4fv(m_locMvp, 1, GL_FALSE, glm::value_ptr(mvp));
    glBindVertexArray(m_vao);

    glPolygonMode(GL_FRONT_AND_BACK, m_wireframe ? GL_LINE : GL_FILL);
    glDrawArrays(GL_TRIANGLES, 0, m_cubeVertexCount);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    glDrawArrays(GL_LINES, m_cubeVertexCount, m_gridVertexCount);

    glBindVertexArray(0);
    glUseProgram(0);
}

void GLRenderer::Orbit(float dxPixels, float dyPixels)
{
    // 오른쪽으로 끌면 물체가 오른쪽으로 돌고, 아래로 끌면 위에서 내려다보게 된다.
    m_yaw -= dxPixels * 0.4f;
    m_pitch = Clamp(m_pitch + dyPixels * 0.4f, -89.0f, 89.0f);
}

void GLRenderer::Zoom(int wheelDelta)
{
    // 휠 한 칸(120)마다 거리 × 0.85 → 가까워질수록 천천히 다가간다.
    const float steps = static_cast<float>(wheelDelta) / 120.0f;
    m_distance = Clamp(m_distance * std::pow(0.85f, steps), 1.5f, 50.0f);
}

void GLRenderer::ResetView()
{
    m_yaw = 30.0f;
    m_pitch = 20.0f;
    m_distance = 6.0f;
}

std::string GLRenderer::GlInfo() const
{
    auto text = [](GLenum name) {
        const GLubyte* s = glGetString(name);
        return s ? std::string(reinterpret_cast<const char*>(s)) : std::string("?");
    };
    GLint mask = 0;
    glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &mask);
    const char* profile = (mask & GL_CONTEXT_CORE_PROFILE_BIT) ? "Core" : "Compatibility";
    return "OpenGL " + text(GL_VERSION) + " (" + profile + ") / " + text(GL_RENDERER);
}
