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
#include <cmath>
#include <vector>

namespace
{
    // 격자: 정점마다 색을 받아 그대로 칠한다(1주차 셰이더).
    const char* kColorVertexShader = R"(#version 330 core
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

    const char* kColorFragmentShader = R"(#version 330 core
in vec3 vColor;
out vec4 FragColor;
void main()
{
    FragColor = vec4(vColor, 1.0);
}
)";

    // 모델: 재질 색 × 텍스처, 카메라 쪽 조명(확산 + 약한 반사광), 양면 조명, 감마 보정
    const char* kLitVertexShader = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
uniform mat4 uModel;
uniform mat3 uNormalMatrix;
uniform mat4 uViewProj;
out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
void main()
{
    vec4 world = uModel * vec4(aPos, 1.0);
    vWorldPos = world.xyz;
    vNormal = uNormalMatrix * aNormal;
    vUV = aUV;
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
uniform vec4 uHighlight;      // 선택 강조: rgb = 색, a = 세기(0 = 강조 없음)
uniform vec3 uLightDir;       // 빛이 오는 방향(월드)
uniform vec3 uEye;
out vec4 FragColor;
void main()
{
    vec4 base = uBaseColor;
    if (uHasTexture)
        base *= texture(uTexture, vUV * uUvScale);
    base.a *= uAlpha;
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
    if (uHighlight.a > 0.0)
        color = mix(color, uHighlight.rgb * (0.35 + 0.75 * diffuse), uHighlight.a);   // 음영은 남기고 색만 바꾼다
    FragColor = vec4(pow(color, vec3(1.0 / 2.2)), base.a);
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
    // 배경: 위는 밝고 아래로 갈수록 조금 어두운 회색(흰 셀·검은 트레이 모두 잘 보이게)
    constexpr float kBackdropTop[3] = { 0.95f, 0.96f, 0.97f };
    constexpr float kBackdropBottom[3] = { 0.74f, 0.77f, 0.81f };

    // 바닥 격자: 가로세로 10m, 0.5m 간격(모델 단위가 미터). 가운데 X선은 붉게, Z선은 푸르게.
    std::vector<ColorVertex> MakeGrid()
    {
        std::vector<ColorVertex> v;
        const float half = 5.0f;
        const float step = 0.5f;
        const int count = static_cast<int>(half / step);
        for (int i = -count; i <= count; ++i)
        {
            const float t = static_cast<float>(i) * step;
            const bool center = (i == 0);
            const bool major = (i % 2 == 0);
            const float base = major ? 0.36f : 0.27f;
            v.push_back({ -half, 0.0f, t, center ? 0.85f : base, center ? 0.30f : base, center ? 0.30f : base + 0.05f });
            v.push_back({  half, 0.0f, t, center ? 0.85f : base, center ? 0.30f : base, center ? 0.30f : base + 0.05f });
            v.push_back({ t, 0.0f, -half, center ? 0.30f : base, center ? 0.40f : base, center ? 0.90f : base + 0.05f });
            v.push_back({ t, 0.0f,  half, center ? 0.30f : base, center ? 0.40f : base, center ? 0.90f : base + 0.05f });
        }
        return v;
    }
}

bool GLRenderer::Init(std::string* log)
{
    m_colorProgram = BuildProgram(kColorVertexShader, kColorFragmentShader, log);
    m_litProgram = BuildProgram(kLitVertexShader, kLitFragmentShader, log);
    m_idProgram = BuildProgram(kIdVertexShader, kIdFragmentShader, log);
    if (!m_colorProgram || !m_litProgram || !m_idProgram)
        return false;
    m_locIdViewProj = glGetUniformLocation(m_idProgram, "uViewProj");
    m_locIdModel = glGetUniformLocation(m_idProgram, "uModel");
    m_locIdColor = glGetUniformLocation(m_idProgram, "uIdColor");

    m_locColorMvp = glGetUniformLocation(m_colorProgram, "uMVP");
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
    glUseProgram(m_litProgram);
    glUniform1i(glGetUniformLocation(m_litProgram, "uTexture"), 0);
    glUseProgram(0);

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

    // 부품 이미지 배경: 화면 전체를 덮는 사각형(좌표가 곧 화면 좌표 -1~1), 위아래 색이 다르다
    const ColorVertex backdrop[4] = {
        { -1.0f, -1.0f, 0.0f, kBackdropBottom[0], kBackdropBottom[1], kBackdropBottom[2] },
        {  1.0f, -1.0f, 0.0f, kBackdropBottom[0], kBackdropBottom[1], kBackdropBottom[2] },
        { -1.0f,  1.0f, 0.0f, kBackdropTop[0], kBackdropTop[1], kBackdropTop[2] },
        {  1.0f,  1.0f, 0.0f, kBackdropTop[0], kBackdropTop[1], kBackdropTop[2] },
    };
    glGenVertexArrays(1, &m_backdropVao);
    glGenBuffers(1, &m_backdropVbo);
    glBindVertexArray(m_backdropVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_backdropVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(backdrop), backdrop, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ColorVertex), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(ColorVertex), reinterpret_cast<void*>(3 * sizeof(float)));
    glBindVertexArray(0);

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
    glClearColor(kBackdropBottom[0], kBackdropBottom[1], kBackdropBottom[2], 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // 1) 배경 그러데이션(깊이는 안 쓴다)
    const glm::mat4 identity(1.0f);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(m_colorProgram);
    glUniformMatrix4fv(m_locColorMvp, 1, GL_FALSE, glm::value_ptr(identity));
    glBindVertexArray(m_backdropVao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
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
    glClearColor(0.12f, 0.12f, 0.15f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);   // 양면 재질이 많아서 뒷면도 그린다

    const glm::mat4 viewProj = ViewProj();
    const glm::vec3 eye = m_camera.Eye();

    // 1) 바닥 격자: 모델 바로 아래(바퀴가 닿는 높이).
    //    깊이는 남기지 않는다 → 커서 줌이 바닥을 잡지 않는다(모델이 나중에 그려지므로 가려지는 것은 그대로).
    if (m_showGrid)
    {
        const glm::vec3 c = m_model.Loaded() ? m_model.Center() : glm::vec3(0.0f);
        const float groundY = m_model.Loaded() ? m_model.BoundsMin().y - 0.002f : -1.0f;
        const glm::mat4 gridModel = glm::translate(glm::mat4(1.0f), glm::vec3(c.x, groundY, c.z));
        const glm::mat4 mvp = viewProj * gridModel;
        glUseProgram(m_colorProgram);
        glUniformMatrix4fv(m_locColorMvp, 1, GL_FALSE, glm::value_ptr(mvp));
        glBindVertexArray(m_gridVao);
        glDepthMask(GL_FALSE);
        glDrawArrays(GL_LINES, 0, m_gridVertexCount);
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

        m_model.Draw(m_modelUniforms, false, eye);

        // 반투명 재질(Ioniq 차체 텍스처의 유리창, 12V 배터리 속 물)과 흐려진 겉껍데기는 마지막에 섞어 그린다.
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        m_model.Draw(m_modelUniforms, true, eye);
        // 선택한 부품이 다른 부품 뒤에 가려져 있으면 그 자리를 강조색으로 옅게 비춘다(엑스레이)
        if (!m_wireframe)
            m_model.DrawHighlightGhost(m_modelUniforms);
        glDisable(GL_BLEND);

        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }

    glBindVertexArray(0);
    glUseProgram(0);
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
