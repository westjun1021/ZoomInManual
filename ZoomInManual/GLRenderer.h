// GLRenderer.h
// OpenGL 3.3 Core 로 장면을 그리는 클래스. 창(MFC)과 상관없는 순수 OpenGL 코드다.
// 1주차: 색 큐브 + 바닥 격자 + 궤도 카메라
// 2주차: 큐브 대신 GLB 모델(Assimp) + 조명 셰이더 + 모델 크기에 맞춘 카메라(Camera 클래스로 분리)
// 3주차: 시맨틱 줌(가까이 가면 차체 → 배터리 커버 순서로 반투명), 커서 쪽 휠 줌, 부드러운 카메라
// 4주차: 클릭한 부품 찾기(화면 밖 FBO 에 ID 색으로 그려 1픽셀 읽기), 선택 강조, 더블클릭 이동
// 5주차(새 안): 화면 ③ 부품 이미지 — 고른 부품 하나만 화면 밖 FBO 에 그려 픽셀로 돌려준다(RenderPartImage)
// 6주차: 인스턴싱(Model), 그리기 호출 수·GPU 시간 재기, 3D 뷰 왼쪽 위 성능 표시(글자 그림을 받아 그린다),
//        성능 측정(MeasureFrames: 같은 화면을 여러 번 그려 한 장에 걸린 시간)
// 7주차(UI 다듬기): 3D 뷰 위 2D 판을 여러 장(위치 경로·상태·성능·안내·이름표 …)으로. 판마다 그림·자리를 따로 넣는다.
//        마우스를 올린 부품을 살짝 밝게(SetHover). 배경은 위가 조금 밝은 그러데이션, ③ 그림 배경은 가운데가 밝은 어두운 회색.
//        FlyInto: 위치 경로를 누르면 그 부품이 '펼쳐진' 거리까지 다가간다(더블클릭 FlyTo 와 달리 안쪽이 보이게).
// 7주차 2차: 바닥 격자는 회색 한 가지(빨강·파랑 축 선 없앰) + 모델 둘레 밖으로 갈수록 흐리게.
//        흐려진 겉껍데기(차체·커버)는 가장자리(윤곽)를 덜 투명하게 → 속 보기에서도 차 모양이 또렷하다.
// 7주차 3차: 보기 모드 색(SetTints → Model) — 고전압 부품·재고 상태를 3D 에 색으로.
#pragma once
#include "Camera.h"
#include "Model.h"
#include "SemanticZoom.h"

#include <string>
#include <vector>

// 화면 ②에 보여 줄 선택 부품의 모델 쪽 정보(이름·정보는 PartDB 에서 찾는다)
struct SelectedPart
{
    int node = -1;               // 모델 노드 번호, -1 = 고른 것 없음
    std::string name;            // 노드 이름(UTF-8) 예: EV-BM-12S@03
    std::string parentName;      // 상위 부품 노드 이름 예: EV-BP-001 (없으면 빈 문자열)
    int level = -1;              // L0 = 0, L1 = 1 …
    int childParts = 0;          // 바로 아래 부품 수
};

class GLRenderer
{
public:
    // OpenGL 컨텍스트가 현재 스레드에 연결되고 gladLoadGL()이 끝난 뒤에 호출한다.
    bool Init(std::string* log = nullptr);
    void Shutdown();

    void Resize(int width, int height);
    void Update(float dtSeconds);   // 매 프레임 Render 앞에: 카메라 움직임 + 시맨틱 줌 계산
    void Render();

    // GLB 모델 불러오기(path 는 UTF-8). 성공하면 카메라를 모델 전체가 보이게 맞춘다.
    bool LoadModel(const std::string& path, std::string* error);
    const Model& GetModel() const { return m_model; }
    const SemanticZoom& GetSemanticZoom() const { return m_zoom; }
    const Camera& GetCamera() const { return m_camera; }
    int Width() const { return m_width; }
    int Height() const { return m_height; }

    // 카메라 조작
    void Orbit(float dxPixels, float dyPixels) { m_camera.Orbit(dxPixels, dyPixels); }
    void Zoom(int wheelDelta) { m_camera.Zoom(wheelDelta); }
    // 커서(창 안 픽셀 좌표 x, y) 아래 점 쪽으로 줌. 커서 아래 깊이 1픽셀을 읽는다.
    // OpenGL 컨텍스트가 연결된 상태에서 호출한다.
    void ZoomAtCursor(int x, int y, int wheelDelta);
    void ResetView() { m_camera.Reset(); }

    // 클릭(창 안 픽셀 좌표). 시맨틱 줌 단계에 맞는 부품 노드를 돌려준다(없으면 -1).
    //   차량이 접혀 있으면 차량 전체, 차량만 펼쳐져 있으면 배터리팩 같은 L1, 배터리팩도 펼쳐져 있으면 모듈 …
    // OpenGL 컨텍스트가 연결된 상태에서 호출한다.
    int PartAt(int x, int y);
    int PickNode(int x, int y);     // 커서 아래 메쉬 노드(부품 단계 고르기 전), 없으면 -1

    // 선택: 그 부품과 아래 전체를 color 로 물들인다(-1 = 선택 해제).
    void Select(int partNode, const glm::vec3& color);
    int Selected() const { return m_model.Highlighted(); }
    SelectedPart DescribePart(int partNode) const;
    void FlyToSelection();          // 선택한 부품이 화면을 채우는 거리로 이동(더블클릭)
    // 7주차: 마우스를 올린 부품(살짝 밝게, -1 = 없음). 선택한 부품의 강조색이 먼저다.
    void SetHover(int partNode) { m_model.SetHover(partNode); }
    void SetTints(const std::vector<glm::vec4>& tints) { m_model.SetTints(tints); }   // 7주차 3차: 보기 모드 색
    int Hovered() const { return m_model.Hovered(); }
    // 7주차: 위치 경로를 눌렀을 때 — 그 부품 가운데를 보며, 그 부품이 펼쳐지는 거리(시맨틱 줌 d_open)의 85% 까지 다가간다.
    //        차량(L0)이면 차체가 반투명해지는 거리. 선택은 바꾸지 않는다.
    void FlyInto(int partNode);

    // 화면 ③ 부품 이미지: partNode 와 그 아래만 화면 밖(FBO)에 width × height 로 그려 픽셀을 돌려준다.
    //   bgra   : 한 픽셀 4바이트(파랑·초록·빨강·알파), 아래 줄부터(OpenGL 순서 = Windows 의 아래→위 DIB 와 같다)
    //   yaw·pitch : 보는 방향(도, Camera 와 같은 뜻). 부품이 돌아도 화면에 꼭 들어오는 거리에서 본다.
    //   bottomMargin : 아래쪽 이만큼(픽셀)은 글자 줄 자리로 비워 두고, 부품은 그 위 칸의 가운데에 그린다(배경은 끝까지).
    //   겉껍데기(차체·배터리 커버)는 S 키(속 보기) 상태를 따르고, 강조색은 칠하지 않는다(부품 본래 색).
    // OpenGL 컨텍스트가 연결된 상태에서 호출한다. 그릴 것이 없거나 FBO 를 못 만들면 false.
    bool RenderPartImage(int partNode, int width, int height, float yawDeg, float pitchDeg, int bottomMargin,
                         std::vector<unsigned char>* bgra);

    // 6주차: 인스턴싱 켬/끔(I 키)과 성능 숫자
    void SetInstancing(bool on) { m_model.SetInstancing(on); }
    bool Instancing() const { return m_model.Instancing(); }
    int LastDrawCalls() const { return m_lastDrawCalls; }   // 마지막 Render 의 모델 그리기 호출 수
    // GPU 시간: 화면 그리기(Render) 앞뒤에 부른다. 결과는 몇 프레임 뒤에 나오므로 기다리지 않고 평균만 갱신한다.
    void BeginGpuTimer();
    void EndGpuTimer();
    float GpuMilliseconds() const { return m_gpuMsValid ? m_gpuMs : -1.0f; }   // 아직 없으면 -1

    // 성능 측정(B 키): 지금 화면을 instanced 상태로 seconds 초 동안(최대 maxFrames 장) 거듭 그려
    // 한 장에 걸린 평균 시간을 잰다. 한 장마다 glFinish 로 GPU 가 끝날 때까지 기다리므로 화면 동기화(60Hz)와 상관없다.
    // 화면에는 내보내지 않는다. 끝나면 인스턴싱 상태를 원래대로 돌린다.
    struct FrameTiming
    {
        bool instanced = false;
        int frames = 0;
        double msPerFrame = 0.0;
        int drawCalls = 0;
    };
    FrameTiming MeasureFrames(bool instanced, double seconds, int maxFrames);

    // 3D 뷰 위 2D 판(7주차: 여러 장). layer = 0 ~ kOverlayLayers-1, 번호가 클수록 위에 그린다.
    //   SetOverlayImage : 그림(BGRA, 위 줄부터, 알파 = 불투명도)을 텍스처로 올린다. bgra = nullptr 이면 비운다.
    //   PlaceOverlay    : 3D 뷰 안 픽셀 자리(왼쪽 위 기준)와 보일지. 그림은 화면 픽셀에 1:1 로 붙는다.
    //   SetOverlayVisible(false) : 모든 판을 잠깐 숨긴다(성능 측정 중).
    static constexpr int kOverlayLayers = 8;
    void SetOverlayImage(int layer, const unsigned char* bgra, int width, int height);
    void PlaceOverlay(int layer, int x, int y, bool visible);
    void SetOverlayVisible(bool show) { m_overlayVisible = show; }
    bool OverlayVisible() const { return m_overlayVisible; }
    std::string GpuName() const;   // GL_RENDERER

    void ToggleWireframe() { m_wireframe = !m_wireframe; }
    bool Wireframe() const { return m_wireframe; }
    void ToggleGrid() { m_showGrid = !m_showGrid; }
    // S 키: 거리와 상관없이 모든 겉껍데기를 α_min(0.15)으로(속 보기). 한 번 더 누르면 시맨틱 줌대로.
    void ToggleShells();
    bool ShellsForced() const { return m_forcedShellAlpha < 1.0f; }
    std::string GlInfo() const;   // 예: "OpenGL 4.6.0 ... (Core) / GPU 이름"

private:
    // 커서 아래 모델 표면의 월드 좌표. 아무것도 없으면 목표점 깊이의 평면 위 점을 돌려준다.
    glm::vec3 PickPoint(int x, int y);
    glm::mat4 ViewProj() const;
    bool EnsurePickTarget();        // 클릭 식별용 FBO 를 창 크기에 맞춰 만든다
    bool EnsureImageTarget(int width, int height);   // 부품 이미지용 FBO(가능하면 4× 안티에일리어싱)
    // 클릭할 때 '펼쳐진' 부품으로 볼지: 시맨틱 줌으로 펼쳐졌거나, S 키로 겉껍데기를 걷어 낸 부품
    bool OpenForClick(int partNode) const;

    // 격자용(정점 색 그대로)
    unsigned int m_colorProgram = 0;
    int m_locColorMvp = -1;
    int m_locColorFade = -1;   // 7주차 2차: 격자 가장자리 흐리기(uFade)
    unsigned int m_gridVao = 0;
    unsigned int m_gridVbo = 0;
    int m_gridVertexCount = 0;

    // 모델용(조명 + 재질 색 + 텍스처)
    unsigned int m_litProgram = 0;
    int m_locViewProj = -1;
    int m_locLightDir = -1;
    int m_locEye = -1;
    Model::DrawUniforms m_modelUniforms;

    // 클릭 식별용(ID 색 한 가지)
    unsigned int m_idProgram = 0;
    int m_locIdViewProj = -1;
    int m_locIdModel = -1;
    int m_locIdColor = -1;
    unsigned int m_pickFbo = 0;
    unsigned int m_pickColor = 0;
    unsigned int m_pickDepth = 0;
    int m_pickWidth = 0;
    int m_pickHeight = 0;

    // 부품 이미지(화면 ③): 그리는 곳(MSAA) → 한 장으로 합친 곳(읽는 곳), 배경 그러데이션
    unsigned int m_imageFbo = 0;
    unsigned int m_imageColor = 0;
    unsigned int m_imageDepth = 0;
    unsigned int m_imageResolveFbo = 0;
    unsigned int m_imageResolveColor = 0;
    int m_imageWidth = 0;
    int m_imageHeight = 0;
    int m_imageSamples = 0;         // 0 = 안티에일리어싱 없음
    unsigned int m_backdropVao = 0;     // ③ 배경(가운데가 밝은 부채꼴)
    unsigned int m_backdropVbo = 0;
    int m_backdropVertices = 0;
    unsigned int m_viewBackdropVao = 0; // 3D 뷰 배경(위가 조금 밝은 그러데이션)
    unsigned int m_viewBackdropVbo = 0;

    // 6주차: 성능 표시(글자 그림 텍스처) · GPU 시간 질의 · 그리기 호출 수
    void CollectGpuTimers();
    unsigned int m_overlayProgram = 0;
    int m_locOverlayRect = -1;
    unsigned int m_overlayVao = 0;
    unsigned int m_overlayVbo = 0;
    struct OverlayLayer
    {
        unsigned int texture = 0;
        int width = 0;
        int height = 0;
        int x = 0;
        int y = 0;
        bool visible = false;
    };
    OverlayLayer m_layers[kOverlayLayers];
    bool m_overlayVisible = true;      // 전체 켬/끔(측정 중에만 끈다)
    static constexpr int kTimerSlots = 4;
    unsigned int m_timerQueries[kTimerSlots] = {};
    bool m_timerPending[kTimerSlots] = {};
    int m_timerNext = 0;
    bool m_timerActive = false;
    float m_gpuMs = 0.0f;
    bool m_gpuMsValid = false;
    int m_lastDrawCalls = 0;

    Camera m_camera;
    Model m_model;
    SemanticZoom m_zoom;

    int m_width = 1;
    int m_height = 1;
    bool m_wireframe = false;
    bool m_showGrid = true;
    float m_forcedShellAlpha = 1.0f;   // S 키로 0.15
};
