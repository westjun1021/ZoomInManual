// GLRenderer.cpp
#include "pch.h"

// Windows 헤더의 min/max 매크로가 std::min/std::max, GLM 과 부딪히지 않게 이 파일에서만 지운다.
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <glad/glad.h>
#include "GLRenderer.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

namespace
{
    // 격자: 정점마다 색을 받아 그대로 칠한다(1주차 셰이더).
    // 7주차 2차: 격자는 가운데에서 멀어질수록 흐려진다(uFade). 배경 판들은 uFade.y = 0 → 그대로.
    const char* kColorVertexShader = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
uniform mat4 uMVP;
out vec3 vColor;
out vec2 vGround;   // 격자 판 위 자리(격자 가운데 = 0, 미터)
void main()
{
    vColor = aColor;
    vGround = aPos.xz;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

    const char* kColorFragmentShader = R"(#version 330 core
in vec3 vColor;
in vec2 vGround;
uniform vec2 uFade;   // 가운데에서 x 부터 흐려져 y 에서 사라진다. y <= 0 이면 흐리지 않는다
out vec4 FragColor;
void main()
{
    float a = 1.0;
    if (uFade.y > 0.0)
        a = 1.0 - smoothstep(uFade.x, uFade.y, length(vGround));
    FragColor = vec4(vColor, a);
}
)";

    // 모델: 재질 색 × 텍스처, 카메라 쪽 조명(확산 + 약한 반사광), 양면 조명, 감마 보정
    // 6주차: 인스턴싱이면(uInstanced) 행렬·법선 행렬·강조를 uniform 대신 인스턴스 속성(3~10)에서 읽는다.
    const char* kLitVertexShader = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in mat4 aInstModel;       // 3~6
layout(location = 7) in mat3 aInstNormal;      // 7~9
layout(location = 10) in vec4 aInstHighlight;
uniform mat4 uModel;
uniform mat3 uNormalMatrix;
uniform vec4 uHighlight;      // 선택 강조: rgb = 색, a = 세기(0 = 강조 없음)
uniform bool uInstanced;
uniform float uInstHighlight; // 인스턴스 강조 세기에 곱할 값
uniform mat4 uViewProj;
out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
flat out vec4 vHighlight;
void main()
{
    mat4 model = uModel;
    mat3 normalMatrix = uNormalMatrix;
    vec4 highlight = uHighlight;
    if (uInstanced)
    {
        model = aInstModel;
        normalMatrix = aInstNormal;
        highlight = vec4(aInstHighlight.rgb, aInstHighlight.a * uInstHighlight);
    }
    vec4 world = model * vec4(aPos, 1.0);
    vWorldPos = world.xyz;
    vNormal = normalMatrix * aNormal;
    vUV = aUV;
    vHighlight = highlight;
    gl_Position = uViewProj * world;
}
)";

    const char* kLitFragmentShader = R"(#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
uniform vec4 uBaseColor;      // glTF baseColorFactor (선형 색)
uniform bool uHasTexture;
uniform sampler2D uTexture;   // sRGB 텍스처 → 읽으면 선형 값
uniform vec2 uUvScale;
uniform float uAlpha;         // 겉껍데기를 흐리게 할 때 < 1
flat in vec4 vHighlight;      // 선택 강조: rgb = 색, a = 세기(0 = 강조 없음)
uniform vec3 uLightDir;       // 빛이 오는 방향(월드)
uniform vec3 uEye;
out vec4 FragColor;
void main()
{
    vec4 base = uBaseColor;
    if (uHasTexture)
        base *= texture(uTexture, vUV * uUvScale);
    float alpha = base.a * uAlpha;
    if (base.a < 0.02)
        discard;

    vec3 n = normalize(vNormal);
    if (!gl_FrontFacing)
        n = -n;                                // 뒷면도 밝게(양면 재질이 많다)
    vec3 l = normalize(uLightDir);
    vec3 v = normalize(uEye - vWorldPos);
    float diffuse = max(dot(n, l), 0.0);
    float specular = pow(max(dot(n, normalize(l + v)), 0.0), 48.0) * 0.25;
    vec3 color = base.rgb * (0.28 + 0.85 * diffuse) + vec3(specular);
    if (vHighlight.a > 0.0)
        color = mix(color, vHighlight.rgb * (0.35 + 0.75 * diffuse), vHighlight.a);   // 음영은 남기고 색만 바꾼다
    // 7주차 2차: 흐려진 겉껍데기(차체·커버)는 비스듬히 보이는 가장자리를 덜 투명하게 → 속은 비치고 윤곽은 또렷하다.
    //   많이 흐려질수록(uAlpha 가 작을수록) 윤곽을 더 살린다. 불투명한 부품(uAlpha = 1)은 그대로.
    if (uAlpha < 0.999)
    {
        float rim = pow(1.0 - clamp(dot(n, v), 0.0, 1.0), 3.0);
        float fade = 1.0 - uAlpha;
        alpha = mix(alpha, max(alpha, base.a * 0.55), rim * fade);
        color = mix(color, vec3(0.82, 0.87, 0.95), rim * fade * 0.35);
    }
    if (alpha < 0.02)
        discard;
    FragColor = vec4(pow(color, vec3(1.0 / 2.2)), alpha);
}
)";

    // 클릭 식별: 노드마다 정해진 색 하나로 칠한다(조명·텍스처·감마 없음 → 읽은 값 = 넣은 값)
    const char* kIdVertexShader = R"(#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uModel;
uniform mat4 uViewProj;
void main()
{
    gl_Position = uViewProj * uModel * vec4(aPos, 1.0);
}
)";

    const char* kIdFragmentShader = R"(#version 330 core
uniform vec3 uIdColor;
out vec4 FragColor;
void main()
{
    FragColor = vec4(uIdColor, 1.0);
}
)";

    // 성능 표시: 글자 그림(텍스처)을 화면 픽셀에 1:1 로 붙인다
    const char* kOverlayVertexShader = R"(#version 330 core
layout(location = 0) in vec2 aCorner;   // (0,0) 왼쪽 아래 ~ (1,1) 오른쪽 위
uniform vec4 uRect;                     // 화면 좌표(-1~1): 왼쪽, 아래, 오른쪽, 위
out vec2 vUV;
void main()
{
    vUV = vec2(aCorner.x, 1.0 - aCorner.y);   // 그림은 위 줄부터 들어 있다
    gl_Position = vec4(mix(uRect.xy, uRect.zw, aCorner), 0.0, 1.0);
}
)";

    const char* kOverlayFragmentShader = R"(#version 330 core
in vec2 vUV;
uniform sampler2D uImage;
out vec4 FragColor;
void main()
{
    FragColor = texture(uImage, vUV);
}
)";

    struct ColorVertex
    {
        float x, y, z;
        float r, g, b;
    };

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

    GLuint BuildProgram(const char* vsSource, const char* fsSource, std::string* log)
    {
        GLuint vs = CompileShader(GL_VERTEX_SHADER, vsSource, log);
        GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fsSource, log);
        if (!vs || !fs)
        {
            if (vs) glDeleteShader(vs);
            if (fs) glDeleteShader(fs);
            return 0;
        }
        GLuint program = glCreateProgram();
        glAttachShader(program, vs);
        glAttachShader(program, fs);
        glLinkProgram(program);
        glDeleteShader(vs);
        glDeleteShader(fs);

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

    // 부품 이미지(화면 ③): 시야각을 좁혀(30°) 원근 왜곡이 적은 '제품 사진' 느낌으로
    constexpr float kImageFovY = 30.0f;
    // 배경(7주차 어두운 화면): 가운데가 조금 밝은 어두운 회색 → 둘레는 ③ 패널 색에 가깝게(그림 가장자리가 패널에 녹아든다)
    constexpr float kBackdropCenter[3] = { 0.235f, 0.255f, 0.290f };
    constexpr float kBackdropEdge[3] = { 0.118f, 0.133f, 0.160f };
    constexpr int kBackdropSegments = 48;
    // 3D 뷰 배경: 위가 조금 밝은 그러데이션(어두운 남색 회색)
    constexpr float kViewTop[3] = { 0.150f, 0.160f, 0.190f };
    constexpr float kViewBottom[3] = { 0.075f, 0.082f, 0.098f };

    // 바닥 격자: 가로세로 10m, 0.5m 간격(모델 단위가 미터).
    // 7주차 2차: 가운데 선도 다른 선과 같은 회색(빨강·파랑 축 선은 개발용이라 뺐다), 1m 선만 조금 밝게.
    //   가장자리는 셰이더가 흐린다(uFade) → 격자 끝의 딱 끊긴 선이 안 보인다.
    constexpr float kGridHalf = 5.0f;
    std::vector<ColorVertex> MakeGrid()
    {
        std::vector<ColorVertex> v;
        const float half = kGridHalf;
        const float step = 0.5f;
        const int count = static_cast<int>(half / step);
        for (int i = -count; i <= count; ++i)
        {
            const float t = static_cast<float>(i) * step;
            const bool major = (i % 2 == 0);
            const float base = major ? 0.30f : 0.22f;
            const float blue = base + 0.04f;   // 3D 배경(남색 회색)에 맞춰 살짝 푸르게
            v.push_back({ -half, 0.0f, t, base, base, blue });
            v.push_back({  half, 0.0f, t, base, base, blue });
            v.push_back({ t, 0.0f, -half, base, base, blue });
            v.push_back({ t, 0.0f,  half, base, base, blue });
        }
        return v;
    }
}

bool GLRenderer::Init(std::string* log)
{
    m_colorProgram = BuildProgram(kColorVertexShader, kColorFragmentShader, log);
    m_litProgram = BuildProgram(kLitVertexShader, kLitFragmentShader, log);
    m_idProgram = BuildProgram(kIdVertexShader, kIdFragmentShader, log);
    m_overlayProgram = BuildProgram(kOverlayVertexShader, kOverlayFragmentShader, log);
    if (!m_colorProgram || !m_litProgram || !m_idProgram || !m_overlayProgram)
        return false;
    m_locOverlayRect = glGetUniformLocation(m_overlayProgram, "uRect");
    glUseProgram(m_overlayProgram);
    glUniform1i(glGetUniformLocation(m_overlayProgram, "uImage"), 0);
    glUseProgram(0);
    m_locIdViewProj = glGetUniformLocation(m_idProgram, "uViewProj");
    m_locIdModel = glGetUniformLocation(m_idProgram, "uModel");
    m_locIdColor = glGetUniformLocation(m_idProgram, "uIdColor");

    m_locColorMvp = glGetUniformLocation(m_colorProgram, "uMVP");
    m_locColorFade = glGetUniformLocation(m_colorProgram, "uFade");
    glUseProgram(m_colorProgram);
    glUniform2f(m_locColorFade, 0.0f, 0.0f);   // 기본: 흐리지 않는다(배경 판)
    glUseProgram(0);
    m_locViewProj = glGetUniformLocation(m_litProgram, "uViewProj");
    m_locLightDir = glGetUniformLocation(m_litProgram, "uLightDir");
    m_locEye = glGetUniformLocation(m_litProgram, "uEye");
    m_modelUniforms.model = glGetUniformLocation(m_litProgram, "uModel");
    m_modelUniforms.normalMatrix = glGetUniformLocation(m_litProgram, "uNormalMatrix");
    m_modelUniforms.baseColor = glGetUniformLocation(m_litProgram, "uBaseColor");
    m_modelUniforms.hasTexture = glGetUniformLocation(m_litProgram, "uHasTexture");
    m_modelUniforms.uvScale = glGetUniformLocation(m_litProgram, "uUvScale");
    m_modelUniforms.alpha = glGetUniformLocation(m_litProgram, "uAlpha");
    m_modelUniforms.highlight = glGetUniformLocation(m_litProgram, "uHighlight");
    m_modelUniforms.instanced = glGetUniformLocation(m_litProgram, "uInstanced");
    m_modelUniforms.instHighlight = glGetUniformLocation(m_litProgram, "uInstHighlight");
    glUseProgram(m_litProgram);
    glUniform1i(glGetUniformLocation(m_litProgram, "uTexture"), 0);
    glUniform1i(m_modelUniforms.instanced, 0);
    glUniform1f(m_modelUniforms.instHighlight, 1.0f);
    glUseProgram(0);

    // 성능 표시용 사각형(꼭짓점 4개)과 텍스처
    const float corners[8] = { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f };
    glGenVertexArrays(1, &m_overlayVao);
    glGenBuffers(1, &m_overlayVbo);
    glBindVertexArray(m_overlayVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_overlayVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(corners), corners, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), reinterpret_cast<void*>(0));
    glBindVertexArray(0);
    glGenQueries(kTimerSlots, m_timerQueries);

    const std::vector<ColorVertex> grid = MakeGrid();
    m_gridVertexCount = static_cast<int>(grid.size());
    glGenVertexArrays(1, &m_gridVao);
    glGenBuffers(1, &m_gridVbo);
    glBindVertexArray(m_gridVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_gridVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(grid.size() * sizeof(ColorVertex)), grid.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ColorVertex), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(ColorVertex), reinterpret_cast<void*>(3 * sizeof(float)));
    glBindVertexArray(0);

    // 부품 이미지 배경: 가운데 한 점(밝게) + 둘레 원(어둡게)의 부채꼴. 원은 화면 모서리까지 덮는 크기(좌표 = 화면 -1~1)
    std::vector<ColorVertex> fan;
    fan.push_back({ 0.0f, 0.1f, 0.0f, kBackdropCenter[0], kBackdropCenter[1], kBackdropCenter[2] });
    for (int i = 0; i <= kBackdropSegments; ++i)
    {
        const float a = 6.2831853f * static_cast<float>(i) / static_cast<float>(kBackdropSegments);
        fan.push_back({ 1.5f * std::cos(a), 0.1f + 1.5f * std::sin(a), 0.0f, kBackdropEdge[0], kBackdropEdge[1], kBackdropEdge[2] });
    }
    m_backdropVertices = static_cast<int>(fan.size());
    auto makeColorVao = [](const ColorVertex* vertices, size_t count, unsigned int* vao, unsigned int* vbo) {
        glGenVertexArrays(1, vao);
        glGenBuffers(1, vbo);
        glBindVertexArray(*vao);
        glBindBuffer(GL_ARRAY_BUFFER, *vbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(count * sizeof(ColorVertex)), vertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ColorVertex), reinterpret_cast<void*>(0));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(ColorVertex), reinterpret_cast<void*>(3 * sizeof(float)));
        glBindVertexArray(0);
    };
    makeColorVao(fan.data(), fan.size(), &m_backdropVao, &m_backdropVbo);

    // 3D 뷰 배경: 화면 전체 사각형, 위아래 색만 다르다
    const ColorVertex view[4] = {
        { -1.0f, -1.0f, 0.0f, kViewBottom[0], kViewBottom[1], kViewBottom[2] },
        {  1.0f, -1.0f, 0.0f, kViewBottom[0], kViewBottom[1], kViewBottom[2] },
        { -1.0f,  1.0f, 0.0f, kViewTop[0], kViewTop[1], kViewTop[2] },
        {  1.0f,  1.0f, 0.0f, kViewTop[0], kViewTop[1], kViewTop[2] },
    };
    makeColorVao(view, 4, &m_viewBackdropVao, &m_viewBackdropVbo);

    m_camera.Fit(glm::vec3(0.0f), 2.5f);   // 모델이 없을 때 기본 시점
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
    m_model.Release();
    if (m_gridVbo)      glDeleteBuffers(1, &m_gridVbo);
    if (m_gridVao)      glDeleteVertexArrays(1, &m_gridVao);
    if (m_colorProgram) glDeleteProgram(m_colorProgram);
    if (m_litProgram)   glDeleteProgram(m_litProgram);
    if (m_idProgram)    glDeleteProgram(m_idProgram);
    if (m_pickFbo)      glDeleteFramebuffers(1, &m_pickFbo);
    if (m_pickColor)    glDeleteRenderbuffers(1, &m_pickColor);
    if (m_pickDepth)    glDeleteRenderbuffers(1, &m_pickDepth);
    if (m_imageFbo)          glDeleteFramebuffers(1, &m_imageFbo);
    if (m_imageColor)        glDeleteRenderbuffers(1, &m_imageColor);
    if (m_imageDepth)        glDeleteRenderbuffers(1, &m_imageDepth);
    if (m_imageResolveFbo)   glDeleteFramebuffers(1, &m_imageResolveFbo);
    if (m_imageResolveColor) glDeleteRenderbuffers(1, &m_imageResolveColor);
    if (m_backdropVbo)       glDeleteBuffers(1, &m_backdropVbo);
    if (m_backdropVao)       glDeleteVertexArrays(1, &m_backdropVao);
    if (m_viewBackdropVbo)   glDeleteBuffers(1, &m_viewBackdropVbo);
    if (m_viewBackdropVao)   glDeleteVertexArrays(1, &m_viewBackdropVao);
    if (m_overlayProgram)    glDeleteProgram(m_overlayProgram);
    if (m_overlayVbo)        glDeleteBuffers(1, &m_overlayVbo);
    if (m_overlayVao)        glDeleteVertexArrays(1, &m_overlayVao);
    for (OverlayLayer& layer : m_layers)
    {
        if (layer.texture)
            glDeleteTextures(1, &layer.texture);
        layer = OverlayLayer();
    }
    if (m_timerQueries[0])   glDeleteQueries(kTimerSlots, m_timerQueries);
    m_overlayProgram = m_overlayVbo = m_overlayVao = 0;
    m_viewBackdropVbo = m_viewBackdropVao = 0;
    m_backdropVertices = 0;
    for (int i = 0; i < kTimerSlots; ++i)
    {
        m_timerQueries[i] = 0;
        m_timerPending[i] = false;
    }
    m_timerActive = false;
    m_gridVbo = m_gridVao = m_colorProgram = m_litProgram = m_idProgram = 0;
    m_pickFbo = m_pickColor = m_pickDepth = 0;
    m_pickWidth = m_pickHeight = 0;
    m_imageFbo = m_imageColor = m_imageDepth = m_imageResolveFbo = m_imageResolveColor = 0;
    m_backdropVbo = m_backdropVao = 0;
    m_imageWidth = m_imageHeight = m_imageSamples = 0;
}

void GLRenderer::Resize(int width, int height)
{
    m_width = std::max(width, 1);
    m_height = std::max(height, 1);
}

bool GLRenderer::LoadModel(const std::string& path, std::string* error)
{
    if (!m_model.Load(path, error))
        return false;
    m_camera.Fit(m_model.Center(), m_model.Radius());
    m_zoom.Build(m_model);
    Update(0.0f);
    return true;
}

void GLRenderer::Update(float dtSeconds)
{
    m_camera.Update(dtSeconds);
    if (m_model.Loaded())
    {
        m_zoom.Update(m_camera.Eye(), m_camera.Target(), static_cast<float>(m_height),
                      m_camera.FovYDegrees(), m_camera.FitDistance());
        m_zoom.ApplyTo(m_model, m_forcedShellAlpha);
    }
}

void GLRenderer::ToggleShells()
{
    m_forcedShellAlpha = ShellsForced() ? 1.0f : m_zoom.GetSettings().alphaMin;
    if (m_model.Loaded())
        m_zoom.ApplyTo(m_model, m_forcedShellAlpha);
}

glm::mat4 GLRenderer::ViewProj() const
{
    const float aspect = static_cast<float>(m_width) / static_cast<float>(m_height);
    return m_camera.Projection(aspect) * m_camera.View();
}

glm::vec3 GLRenderer::PickPoint(int x, int y)
{
    const glm::mat4 viewProj = ViewProj();
    const glm::mat4 inverse = glm::inverse(viewProj);

    // 목표점이 화면에서 갖는 깊이(아무것도 안 잡히면 이 깊이의 평면을 쓴다)
    const glm::vec4 targetClip = viewProj * glm::vec4(m_camera.Target(), 1.0f);
    float ndcZ = targetClip.z / targetClip.w;

    if (m_model.Loaded() && x >= 0 && y >= 0 && x < m_width && y < m_height)
    {
        // 지금 카메라로 한 번 그려서(화면에는 안 내보냄) 깊이 버퍼를 채우고 커서 아래 1픽셀을 읽는다.
        // 윈도우 y 는 위가 0, OpenGL 은 아래가 0 이라 뒤집는다.
        Render();
        float depth = 1.0f;
        glReadPixels(x, m_height - 1 - y, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
        if (depth < 0.99999f)
            ndcZ = depth * 2.0f - 1.0f;   // 모델 표면을 잡았다(격자·흐려진 겉껍데기는 깊이를 안 남긴다)
    }

    const glm::vec4 ndc((static_cast<float>(x) + 0.5f) / static_cast<float>(m_width) * 2.0f - 1.0f,
                        1.0f - (static_cast<float>(y) + 0.5f) / static_cast<float>(m_height) * 2.0f,
                        ndcZ, 1.0f);
    const glm::vec4 world = inverse * ndc;
    return glm::vec3(world) / world.w;
}

void GLRenderer::ZoomAtCursor(int x, int y, int wheelDelta)
{
    if (wheelDelta > 0)
        m_camera.ZoomAt(wheelDelta, PickPoint(x, y));   // 다가갈 때: 커서 아래 점 쪽으로
    else
        m_camera.Zoom(wheelDelta);                      // 물러날 때: 처음 구도 쪽으로
}

bool GLRenderer::EnsurePickTarget()
{
    if (m_pickFbo && m_pickWidth == m_width && m_pickHeight == m_height)
        return true;
    if (!m_pickFbo)
    {
        glGenFramebuffers(1, &m_pickFbo);
        glGenRenderbuffers(1, &m_pickColor);
        glGenRenderbuffers(1, &m_pickDepth);
    }
    GLint previous = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);
    glBindFramebuffer(GL_FRAMEBUFFER, m_pickFbo);
    glBindRenderbuffer(GL_RENDERBUFFER, m_pickColor);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, m_width, m_height);   // sRGB 아님 → 넣은 값 그대로 읽힌다
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, m_pickColor);
    glBindRenderbuffer(GL_RENDERBUFFER, m_pickDepth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, m_width, m_height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_pickDepth);
    const bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous));
    m_pickWidth = ok ? m_width : 0;
    m_pickHeight = ok ? m_height : 0;
    return ok;
}

int GLRenderer::PickNode(int x, int y)
{
    if (!m_model.Loaded() || x < 0 || y < 0 || x >= m_width || y >= m_height || !EnsurePickTarget())
        return -1;

    // 클릭할 때만 화면 밖 FBO 에 노드 번호 색으로 한 번 더 그리고 커서 아래 1픽셀을 읽는다.
    GLint previous = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);
    glBindFramebuffer(GL_FRAMEBUFFER, m_pickFbo);
    glViewport(0, 0, m_width, m_height);
    glDisable(GL_BLEND);
    glDisable(GL_DITHER);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    // 읽을 곳은 1픽셀뿐이라 그 픽셀만 칠하게 막는다(색칠 작업이 거의 없어져 빠르다).
    glEnable(GL_SCISSOR_TEST);
    glScissor(x, m_height - 1 - y, 1, 1);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // 펼쳐진 부품에 바로 붙은 메쉬(예: 배터리팩의 검은 뚜껑)는 건너뛴다 → 그 아래 모듈·셀이 클릭된다.
    const std::vector<Model::Node>& nodes = m_model.Nodes();
    std::vector<char> skip(nodes.size(), 0);
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        if (!nodes[i].meshes.empty() && !nodes[i].isPart && nodes[i].part >= 0 && OpenForClick(nodes[i].part))
            skip[i] = 1;
    }

    const glm::mat4 viewProj = ViewProj();
    glUseProgram(m_idProgram);
    glUniformMatrix4fv(m_locIdViewProj, 1, GL_FALSE, glm::value_ptr(viewProj));
    m_model.DrawIds(m_locIdModel, m_locIdColor, skip);

    unsigned char pixel[4] = { 0, 0, 0, 0 };
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(x, m_height - 1 - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);   // 윈도우 y 는 뒤집는다

    glDisable(GL_SCISSOR_TEST);
    glBindVertexArray(0);
    glUseProgram(0);
    glEnable(GL_DITHER);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous));

    const int id = pixel[0] | (pixel[1] << 8) | (pixel[2] << 16);   // ID = R + G·256 + B·65536
    const int node = id - 1;                                          // 0 = 배경 → -1
    return (node >= 0 && node < static_cast<int>(m_model.Nodes().size())) ? node : -1;
}

int GLRenderer::PartAt(int x, int y)
{
    const int node = PickNode(x, y);
    if (node < 0)
        return -1;

    // 맞은 노드에서 위로 올라가며 부품 사슬을 모은다: [셀, 모듈, 배터리팩, 차량]
    const std::vector<Model::Node>& nodes = m_model.Nodes();
    std::vector<int> chain;
    for (int part = nodes[node].part; part >= 0;)
    {
        chain.push_back(part);
        const int parent = nodes[part].parent;
        part = parent >= 0 ? nodes[parent].part : -1;
    }
    if (chain.empty())
        return -1;

    // 차량부터 내려가며 '펼쳐지지 않은 첫 부품'이 클릭 단위다. 다 펼쳐져 있으면 가장 아래 부품.
    for (auto it = chain.rbegin(); it != chain.rend(); ++it)
    {
        if (!OpenForClick(*it))
            return *it;
    }
    return chain.front();
}

bool GLRenderer::OpenForClick(int partNode) const
{
    const SemanticZoom::PartView* view = m_zoom.Find(partNode);
    if (!view)
        return false;
    return view->open || (ShellsForced() && view->hasShell);   // S 키(속 보기)면 겉껍데기 있는 부품은 펼친 것으로
}

void GLRenderer::Select(int partNode, const glm::vec3& color)
{
    m_model.SetHighlight(partNode, color);
}

SelectedPart GLRenderer::DescribePart(int partNode) const
{
    SelectedPart out;
    const std::vector<Model::Node>& nodes = m_model.Nodes();
    if (partNode < 0 || partNode >= static_cast<int>(nodes.size()))
        return out;
    const Model::Node& node = nodes[partNode];
    out.node = partNode;
    out.name = node.name;
    out.level = node.level;
    const int parentPart = node.parent >= 0 ? nodes[node.parent].part : -1;
    if (parentPart >= 0)
        out.parentName = nodes[parentPart].name;
    for (size_t i = static_cast<size_t>(partNode) + 1; i < nodes.size(); ++i)
    {
        const Model::Node& child = nodes[i];
        if (child.isPart && child.parent >= 0 && nodes[child.parent].part == partNode)
            ++out.childParts;
    }
    return out;
}

void GLRenderer::FlyToSelection()
{
    const int part = Selected();
    if (part < 0)
        return;
    const Model::Node& node = m_model.Nodes()[part];
    if (!node.hasBounds)
        return;
    m_camera.FlyTo((node.boundsMin + node.boundsMax) * 0.5f, glm::length(node.boundsMax - node.boundsMin) * 0.5f);
}

void GLRenderer::FlyInto(int partNode)
{
    const SemanticZoom::PartView* view = m_zoom.Find(partNode);
    const std::vector<Model::Node>& nodes = m_model.Nodes();
    if (!view || partNode < 0 || partNode >= static_cast<int>(nodes.size()) || !nodes[partNode].hasBounds)
        return;
    const Model::Node& node = nodes[partNode];
    const glm::vec3 center = (node.boundsMin + node.boundsMax) * 0.5f;
    const float radius = glm::length(node.boundsMax - node.boundsMin) * 0.5f;
    // 부품이 화면을 채우는 거리(더블클릭과 같다)와, 그 부품이 펼쳐지는 거리의 85% 중 가까운 쪽
    const float fill = radius / std::sin(glm::radians(m_camera.FovYDegrees()) * 0.5f) * 1.2f;
    m_camera.FlyToDistance(center, std::min(fill, view->openDistance * 0.85f));
}

bool GLRenderer::EnsureImageTarget(int width, int height)
{
    if (m_imageFbo && m_imageWidth == width && m_imageHeight == height)
        return true;
    if (!m_imageFbo)
    {
        glGenFramebuffers(1, &m_imageFbo);
        glGenRenderbuffers(1, &m_imageColor);
        glGenRenderbuffers(1, &m_imageDepth);
        glGenFramebuffers(1, &m_imageResolveFbo);
        glGenRenderbuffers(1, &m_imageResolveColor);
    }
    GLint previous = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);

    // 읽는 곳: 보통 RGBA8 한 장(sRGB 아님 → 셰이더가 감마까지 넣은 값이 그대로 읽힌다. 3D 뷰와 같은 색)
    glBindFramebuffer(GL_FRAMEBUFFER, m_imageResolveFbo);
    glBindRenderbuffer(GL_RENDERBUFFER, m_imageResolveColor);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, m_imageResolveColor);
    bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;

    // 그리는 곳: 픽셀마다 4번 나눠 그려(MSAA) 가장자리 계단을 줄인다. 그래픽 카드가 못 하면 나누지 않고 그린다.
    GLint maxSamples = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    m_imageSamples = std::min(4, std::max(maxSamples, 0));
    for (;;)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, m_imageFbo);
        glBindRenderbuffer(GL_RENDERBUFFER, m_imageColor);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, m_imageSamples, GL_RGBA8, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, m_imageColor);
        glBindRenderbuffer(GL_RENDERBUFFER, m_imageDepth);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, m_imageSamples, GL_DEPTH24_STENCIL8, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_imageDepth);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE || m_imageSamples == 0)
            break;
        m_imageSamples = 0;   // MSAA 를 못 쓰면 나누지 않는 보통 렌더버퍼로 한 번 더
    }
    ok = ok && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;

    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous));
    m_imageWidth = ok ? width : 0;
    m_imageHeight = ok ? height : 0;
    return ok;
}

bool GLRenderer::RenderPartImage(int partNode, int width, int height, float yawDeg, float pitchDeg, int bottomMargin,
                                 std::vector<unsigned char>* bgra)
{
    const std::vector<Model::Node>& nodes = m_model.Nodes();
    if (!bgra || width <= 0 || height <= 0 || partNode < 0 || partNode >= static_cast<int>(nodes.size()) ||
        !nodes[partNode].hasBounds || !EnsureImageTarget(width, height))
        return false;

    GLint previous = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);
    glBindFramebuffer(GL_FRAMEBUFFER, m_imageFbo);
    glViewport(0, 0, width, height);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);   // F 키(와이어프레임)와 상관없이 면으로
    glDepthMask(GL_TRUE);
    glClearColor(kBackdropEdge[0], kBackdropEdge[1], kBackdropEdge[2], 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // 1) 배경: 가운데가 밝은 부채꼴(깊이는 안 쓴다). 가로세로 비율대로 늘여 둥근 빛이 칸 모양을 따른다
    const glm::mat4 identity(1.0f);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(m_colorProgram);
    glUniformMatrix4fv(m_locColorMvp, 1, GL_FALSE, glm::value_ptr(identity));
    glBindVertexArray(m_backdropVao);
    glDrawArrays(GL_TRIANGLE_FAN, 0, m_backdropVertices);
    glEnable(GL_DEPTH_TEST);

    // 2) 카메라 거리: 부품이 세로축으로 한 바퀴 돌며 쓸고 지나가는 원기둥
    //    (반지름 = 가로·깊이 대각선의 절반, 높이 = 부품 높이)이 화면에 꼭 들어오는 거리.
    //    yaw 와 상관없는 값이라 돌아가는 동안 크기가 출렁이지 않는다. 원기둥 위·아래 둘레의 점 64개로 잰다.
    const Model::Node& node = nodes[partNode];
    const glm::vec3 center = (node.boundsMin + node.boundsMax) * 0.5f;
    const glm::vec3 size = node.boundsMax - node.boundsMin;
    const float ringRadius = std::max(0.5f * std::sqrt(size.x * size.x + size.z * size.z), 1e-4f);
    const float halfHeight = 0.5f * size.y;
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    // 아래 글자 줄(bottomMargin)을 뺀 높이에 맞춘다
    const int margin = std::min(std::max(bottomMargin, 0), height / 3);
    const float usable = static_cast<float>(height - margin) / static_cast<float>(height);
    const float tanY = std::tan(glm::radians(kImageFovY) * 0.5f) * usable;
    const float tanX = std::tan(glm::radians(kImageFovY) * 0.5f) * aspect;
    const float pitch = glm::radians(std::min(std::max(pitchDeg, -80.0f), 80.0f));
    const float cosP = std::cos(pitch), sinP = std::sin(pitch);
    float distance = 0.0f;
    for (int k = 0; k < 32; ++k)
    {
        const float a = 6.2831853f * static_cast<float>(k) / 32.0f;
        for (int side = -1; side <= 1; side += 2)
        {
            // 원기둥 위의 점을 카메라 기준(오른쪽 x, 위 y, 카메라 쪽으로 나온 정도 z)으로
            const glm::vec3 p(ringRadius * std::cos(a), static_cast<float>(side) * halfHeight, ringRadius * std::sin(a));
            const float x = p.x;
            const float y = p.y * cosP - p.z * sinP;
            const float z = p.y * sinP + p.z * cosP;
            distance = std::max(distance, z + std::max(std::abs(x) / tanX, std::abs(y) / tanY));
        }
    }
    distance *= 1.03f;   // 가장자리에 조금 여유
    const float reach = std::sqrt(ringRadius * ringRadius + halfHeight * halfHeight);   // 가운데에서 가장 먼 곳

    const float yaw = glm::radians(yawDeg);
    const glm::vec3 toEye(cosP * std::sin(yaw), sinP, cosP * std::cos(yaw));
    const glm::vec3 eye = center + toEye * distance;
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    const glm::mat4 view = glm::lookAt(eye, center, up);
    const float nearPlane = std::max(distance - reach * 1.2f, distance * 0.02f);
    // 부품을 글자 줄 위 칸의 가운데로: 화면 좌표에서 margin/2 픽셀만큼 위로 민다(렌즈를 옮긴 것과 같다)
    const glm::mat4 shiftUp = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, static_cast<float>(margin) / static_cast<float>(height), 0.0f));
    const glm::mat4 viewProj = shiftUp * glm::perspective(glm::radians(kImageFovY), aspect, nearPlane, distance + reach * 1.2f) * view;

    // 빛: 카메라 쪽 + 위 + 조금 왼쪽에서(사진 조명처럼 면이 구분되게)
    const glm::vec3 right = glm::normalize(glm::cross(-toEye, up));
    const glm::vec3 lightDir = glm::normalize(toEye + up * 0.8f - right * 0.35f);

    // 3) 부품: 불투명 → 반투명. 겉껍데기는 S 키(속 보기)면 흐리게, 아니면 불투명(부품 본래 모습)
    const float shellAlpha = ShellsForced() ? m_forcedShellAlpha : 1.0f;
    glUseProgram(m_litProgram);
    glUniformMatrix4fv(m_locViewProj, 1, GL_FALSE, glm::value_ptr(viewProj));
    glUniform3fv(m_locLightDir, 1, glm::value_ptr(lightDir));
    glUniform3fv(m_locEye, 1, glm::value_ptr(eye));
    m_model.DrawPart(m_modelUniforms, partNode, false, eye, shellAlpha);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    m_model.DrawPart(m_modelUniforms, partNode, true, eye, shellAlpha);
    glDisable(GL_BLEND);

    // 4) MSAA 로 그렸으면 한 장으로 합친 뒤 읽는다
    GLuint readFbo = m_imageFbo;
    if (m_imageSamples > 0)
    {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, m_imageFbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_imageResolveFbo);
        glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        readFbo = m_imageResolveFbo;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, readFbo);
    bgra->resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, width, height, GL_BGRA, GL_UNSIGNED_BYTE, bgra->data());

    glBindVertexArray(0);
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous));
    glViewport(0, 0, m_width, m_height);
    return true;
}

void GLRenderer::Render()
{
    glViewport(0, 0, m_width, m_height);
    glClearColor(kViewBottom[0], kViewBottom[1], kViewBottom[2], 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);   // 양면 재질이 많아서 뒷면도 그린다
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    // 0) 배경 그러데이션(깊이는 안 쓴다 — 커서 줌·클릭과 상관없다)
    {
        const glm::mat4 identity(1.0f);
        glDisable(GL_DEPTH_TEST);
        glUseProgram(m_colorProgram);
        glUniformMatrix4fv(m_locColorMvp, 1, GL_FALSE, glm::value_ptr(identity));
        glBindVertexArray(m_viewBackdropVao);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);

    const glm::mat4 viewProj = ViewProj();
    const glm::vec3 eye = m_camera.Eye();

    // 1) 바닥 격자: 모델 바로 아래(바퀴가 닿는 높이).
    //    깊이는 남기지 않는다 → 커서 줌이 바닥을 잡지 않는다(모델이 나중에 그려지므로 가려지는 것은 그대로).
    //    7주차 2차: 모델 둘레 밖으로 갈수록 흐려진다(모델 반지름 ~ 격자 끝).
    if (m_showGrid)
    {
        const glm::vec3 c = m_model.Loaded() ? m_model.Center() : glm::vec3(0.0f);
        const float groundY = m_model.Loaded() ? m_model.BoundsMin().y - 0.002f : -1.0f;
        const glm::mat4 gridModel = glm::translate(glm::mat4(1.0f), glm::vec3(c.x, groundY, c.z));
        const glm::mat4 mvp = viewProj * gridModel;
        const float fadeStart = std::min(std::max(m_model.Loaded() ? m_model.Radius() : 2.0f, 1.0f), kGridHalf * 0.7f);
        glUseProgram(m_colorProgram);
        glUniformMatrix4fv(m_locColorMvp, 1, GL_FALSE, glm::value_ptr(mvp));
        glUniform2f(m_locColorFade, fadeStart, kGridHalf);
        glBindVertexArray(m_gridVao);
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDrawArrays(GL_LINES, 0, m_gridVertexCount);
        glDisable(GL_BLEND);
        glUniform2f(m_locColorFade, 0.0f, 0.0f);
        glDepthMask(GL_TRUE);
    }

    // 2) 모델: 불투명 → 반투명(유리창 등) 순서
    if (m_model.Loaded())
    {
        // 빛은 카메라 쪽에서 조금 위에서 온다 → 어느 방향에서 봐도 밝게 보인다.
        const glm::vec3 toEye = glm::normalize(eye - m_camera.Target());
        const glm::vec3 lightDir = glm::normalize(toEye + glm::vec3(0.0f, 0.8f, 0.0f));

        glUseProgram(m_litProgram);
        glUniformMatrix4fv(m_locViewProj, 1, GL_FALSE, glm::value_ptr(viewProj));
        glUniform3fv(m_locLightDir, 1, glm::value_ptr(lightDir));
        glUniform3fv(m_locEye, 1, glm::value_ptr(eye));
        glPolygonMode(GL_FRONT_AND_BACK, m_wireframe ? GL_LINE : GL_FILL);

        m_model.ResetDrawCalls();
        m_model.Draw(m_modelUniforms, false, eye);

        // 반투명 재질(Ioniq 차체 텍스처의 유리창, 12V 배터리 속 물)과 흐려진 겉껍데기는 마지막에 섞어 그린다.
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        m_model.Draw(m_modelUniforms, true, eye);
        // 선택한 부품이 다른 부품 뒤에 가려져 있으면 그 자리를 강조색으로 옅게 비춘다(엑스레이)
        if (!m_wireframe)
            m_model.DrawHighlightGhost(m_modelUniforms);
        glDisable(GL_BLEND);
        m_lastDrawCalls = m_model.DrawCalls();

        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }

    // 3) 2D 판들(위치 경로·상태·성능·안내·이름표). 번호 순서대로 위에 쌓는다.
    //    깊이는 건드리지 않는다(커서 줌이 깊이를 읽는다). 그림 픽셀 = 화면 픽셀(1:1).
    if (m_overlayVisible)
    {
        bool begun = false;
        for (const OverlayLayer& layer : m_layers)
        {
            if (!layer.visible || !layer.texture || layer.width <= 0 || layer.height <= 0)
                continue;
            if (!begun)
            {
                glDisable(GL_DEPTH_TEST);
                glDepthMask(GL_FALSE);
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
                glUseProgram(m_overlayProgram);
                glActiveTexture(GL_TEXTURE0);
                glBindVertexArray(m_overlayVao);
                begun = true;
            }
            const float x0 = -1.0f + 2.0f * static_cast<float>(layer.x) / static_cast<float>(m_width);
            const float x1 = -1.0f + 2.0f * static_cast<float>(layer.x + layer.width) / static_cast<float>(m_width);
            const float y1 = 1.0f - 2.0f * static_cast<float>(layer.y) / static_cast<float>(m_height);
            const float y0 = 1.0f - 2.0f * static_cast<float>(layer.y + layer.height) / static_cast<float>(m_height);
            glUniform4f(m_locOverlayRect, x0, y0, x1, y1);
            glBindTexture(GL_TEXTURE_2D, layer.texture);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        }
        if (begun)
        {
            glDisable(GL_BLEND);
            glDepthMask(GL_TRUE);
            glEnable(GL_DEPTH_TEST);
        }
    }

    glBindVertexArray(0);
    glUseProgram(0);
}

void GLRenderer::SetOverlayImage(int index, const unsigned char* bgra, int width, int height)
{
    if (index < 0 || index >= kOverlayLayers)
        return;
    OverlayLayer& layer = m_layers[index];
    if (!bgra || width <= 0 || height <= 0)
    {
        layer.width = layer.height = 0;
        return;
    }
    if (!layer.texture)
    {
        glGenTextures(1, &layer.texture);
        glBindTexture(GL_TEXTURE_2D, layer.texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);   // 글자가 흐려지지 않게 픽셀 그대로
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, layer.texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, bgra);
    glBindTexture(GL_TEXTURE_2D, 0);
    layer.width = width;
    layer.height = height;
}

void GLRenderer::PlaceOverlay(int index, int x, int y, bool visible)
{
    if (index < 0 || index >= kOverlayLayers)
        return;
    m_layers[index].x = x;
    m_layers[index].y = y;
    m_layers[index].visible = visible;
}

void GLRenderer::CollectGpuTimers()
{
    for (int i = 0; i < kTimerSlots; ++i)
    {
        if (!m_timerPending[i])
            continue;
        GLint ready = 0;
        glGetQueryObjectiv(m_timerQueries[i], GL_QUERY_RESULT_AVAILABLE, &ready);
        if (!ready)
            continue;
        GLuint64 nanoseconds = 0;
        glGetQueryObjectui64v(m_timerQueries[i], GL_QUERY_RESULT, &nanoseconds);
        m_timerPending[i] = false;
        const float ms = static_cast<float>(static_cast<double>(nanoseconds) / 1.0e6);
        m_gpuMs = m_gpuMsValid ? m_gpuMs * 0.9f + ms * 0.1f : ms;   // 출렁이지 않게 평균
        m_gpuMsValid = true;
    }
}

void GLRenderer::BeginGpuTimer()
{
    if (!m_timerQueries[0] || m_timerActive)
        return;
    CollectGpuTimers();
    if (m_timerPending[m_timerNext])
        return;   // 결과를 아직 못 받은 질의는 다시 쓰지 않는다(이번 프레임은 재지 않음)
    glBeginQuery(GL_TIME_ELAPSED, m_timerQueries[m_timerNext]);
    m_timerActive = true;
}

void GLRenderer::EndGpuTimer()
{
    if (!m_timerActive)
        return;
    glEndQuery(GL_TIME_ELAPSED);
    m_timerPending[m_timerNext] = true;
    m_timerNext = (m_timerNext + 1) % kTimerSlots;
    m_timerActive = false;
}

GLRenderer::FrameTiming GLRenderer::MeasureFrames(bool instanced, double seconds, int maxFrames)
{
    FrameTiming result;
    result.instanced = instanced;
    if (!m_model.Loaded())
        return result;
    const bool savedInstancing = m_model.Instancing();
    const bool savedOverlay = m_overlayVisible;
    m_model.SetInstancing(instanced);
    m_overlayVisible = false;

    for (int i = 0; i < 3; ++i)   // 준비 운동(드라이버가 상태를 만들고 버퍼를 올리게)
        Render();
    glFinish();

    using Clock = std::chrono::steady_clock;
    const Clock::time_point start = Clock::now();
    double elapsed = 0.0;
    int frames = 0;
    do
    {
        Render();
        glFinish();   // GPU 가 이 장을 다 그릴 때까지 기다린다 → 한 장의 실제 시간
        ++frames;
        elapsed = std::chrono::duration<double>(Clock::now() - start).count();
    } while ((elapsed < seconds && frames < maxFrames) || frames < 10);

    result.frames = frames;
    result.msPerFrame = elapsed * 1000.0 / static_cast<double>(frames);
    result.drawCalls = m_lastDrawCalls;
    m_model.SetInstancing(savedInstancing);
    m_overlayVisible = savedOverlay;
    return result;
}

std::string GLRenderer::GpuName() const
{
    const GLubyte* s = glGetString(GL_RENDERER);
    return s ? std::string(reinterpret_cast<const char*>(s)) : std::string("?");
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
