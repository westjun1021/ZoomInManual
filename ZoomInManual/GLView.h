// GLView.h
// 화면 ①: 대화상자 안에 붙는 OpenGL 창(MFC CWnd).
// 창 메시지(그리기·크기·마우스·키)를 받아서 WglContext와 GLRenderer에 넘겨 준다.
// 2주차: 시작할 때 model.glb 를 찾아 불러온다.
// 3주차: 휠은 커서가 가리키는 쪽으로 줌.
// 4주차: 클릭 = 부품 선택(강조색, 고전압은 주황), 더블클릭 = 그 부품 앞으로 이동.
//        선택이 바뀌면 부모 창(대화상자)에 WM_ZOOMIN_PART_SELECTED 를 보낸다 → 화면 ② 갱신.
//        시작할 때 data\parts.json · data\inventory.json 도 읽는다(GetPartDB).
// 5주차(새 안): 화면 ③ = 고른 부품 하나의 3D 이미지(CPartPreview) + 검색 한 줄(SearchNext).
// 6주차: 성능 표시(FPS · CPU·GPU 시간 · 그리기 호출 수). B = 성능 측정(benchmark.csv).
// 7주차(UI 다듬기): 3D 뷰 위에 안내 판들(그림은 UiOverlay.h 가 만들고 GLRenderer 가 텍스처로 붙인다)
//   - 왼쪽 위 '위치 경로': ● 차량 › 배터리팩 › 배터리 모듈 03 (펼쳐진 부품 사슬). 누르면 그 단계로 다가간다(FlyInto).
//   - 오른쪽 위: 켜져 있는 상태 꼬리표(속 보기 S · 와이어프레임 F · 인스턴싱 끔 I, 누르면 끔) + 성능 표시(D).
//   - 아래: 조작 안내 한 줄(H 로 끄고 켬), 처음에는 '휠을 굴려 다가가 보세요' 안내(차체가 열리면 사라짐).
//   - 마우스를 올리면 그 부품을 살짝 밝게 + 커서 옆에 이름·품번(클릭하면 고를 부품과 같다).
//   - 제목 줄은 짧게: "줌인 매뉴얼 — 배터리 모듈 03". 개발용 숫자(OpenGL·메쉬 수)는 성능 표시로 옮겼다.
//   키: S 속 보기, D 성능 표시, H 안내, I 인스턴싱, B 성능 측정, G 바닥 격자, R 처음 시점, F 와이어프레임, Z·X 확대·축소
// 7주차 2차(평가 반영): 처음에는 3D 가 주인공이 되게 덜어 냈다.
//   - 성능 표시는 처음에 꺼 둔다(D 로 켬, B 측정 때는 저절로 켜짐).
//   - 조작 안내는 자주 쓰는 3개 + [H] 단축키. H 를 누를 때마다 간단히 → 단축키 전체 → 숨김 → 간단히.
//   - 위치 경로는 고른 부품이 있으면 그 부품까지의 사슬(차량 › 후륜 구동 유닛 › 구동 모터), 없으면 펼쳐진 사슬.
//     마지막(고른 부품)을 누르면 그 부품 앞으로, 그 위 단계를 누르면 그 단계 안이 보이는 거리로.
//   - 마우스 올리기: 카메라가 움직이는 동안은 고르지 않고(이름표 숨김) 멈추면 한 번, 마우스가 움직이는 동안은
//     0.12초에 한 번 + 멈추면 한 번 → 클릭 식별 그리기(GPU 를 기다린다)를 덜 해서 FPS 가 덜 떨어진다.
// 7주차 3차(심사 기준 반영, 기능은 둘만):
//   - V 보기 모드: 기본 → 고전압(고전압 부품 주황, 구성품은 그 안까지) → 재고(품절 빨강·부족 노랑) → 기본.
//     켤 때 속 보기가 꺼져 있었으면 함께 켜고(안쪽 색이 보이게) 끌 때 되돌린다. 오른쪽 위 범례(누르면 끔).
//     재고 보기에서는 이름표 아래 줄이 그 부품의 재고(상태·수량).
//   - 검색은 증상 낱말도 찾는다(PartDB). 빈 검색 안내 글도 바꿈.
//   - 켜자마자 3D 뷰가 키보드를 받는다(창이 보이고 활성화된 뒤 첫 프레임에 한 번 포커스). 타이머 간격 1ms(timeBeginPeriod) → 약 60 FPS.
// 7주차 4차: 고전압 차단 순서(P) — 매뉴얼의 '절차'. P 를 누를 때마다 다음 단계, Backspace = 이전 단계.
//   단계마다 그 부품을 고르고 앞으로 날아간다(검색과 같다 → ② 정보·③ 그림도 그 부품). 오른쪽 위(범례 자리)에 단계 목록.
//   켤 때 보기 모드(V)는 끄고 속 보기를 켠다(끝나면 되돌린다). 판을 누르면 다음 단계, × 를 누르면 닫기.
#pragma once
#include "WglContext.h"
#include "GLRenderer.h"
#include "PartDB.h"
#include "PartSearch.h"

#include <string>
#include <vector>

class CPartPreview;
namespace ui { struct OverlayImage; }

// 선택한 부품이 바뀌었다는 알림(wParam = 노드 번호, -1 = 선택 해제). 대화상자에서 ON_MESSAGE 로 받는다.
#define WM_ZOOMIN_PART_SELECTED (WM_APP + 1)

class CGLView : public CWnd
{
public:
    // parent 안의 rect 위치에 OpenGL 창을 만든다.
    BOOL Create(CWnd* parent, const CRect& rect, UINT id);

    // OpenGL·모델·데이터 정보(또는 실패 이유). 대화상자가 처음 제목에 잠깐 쓴다.
    CString GetGlInfo() const { return m_info; }

    // 화면 ②에 보여 줄 것: 지금 고른 부품(없으면 node = -1)과 부품 정보·재고 데이터
    SelectedPart GetSelectedPart() const { return m_renderer.DescribePart(m_renderer.Selected()); }
    const PartDB& GetPartDB() const { return m_partDb; }
    const Model& GetModel() const { return m_renderer.GetModel(); }

    // 부품(노드 번호, -1 = 선택 해제)을 코드에서 고른다. flyTo 면 그 부품이 화면을 채우는 곳으로 이동.
    void SelectPart(int node, bool flyTo);

    // 화면 ③: 고른 부품 하나를 그려 보여 줄 창. 대화상자 OnInitDialog 에서 한 번.
    void AttachPreview(CPartPreview* preview);

    // 검색(화면 ③ 아래 한 줄): query 에 맞는 부품을 고르고 그 앞으로 이동한다.
    // 같은 검색어로 다시 부르면 다음 부품(엔터를 누를 때마다). 결과는 ③ 맨 아래 한 줄에 글자로.
    void SearchNext(const CString& query);

protected:
    afx_msg int  OnCreate(LPCREATESTRUCT lpCreateStruct);
    afx_msg void OnDestroy();
    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
    afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
    afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
    afx_msg void OnMouseMove(UINT nFlags, CPoint point);
    afx_msg void OnMouseLeave();
    afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
    afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);
    afx_msg void OnCaptureChanged(CWnd* pWnd);
    afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
    afx_msg UINT OnGetDlgCode();
    DECLARE_MESSAGE_MAP()

private:
    // 3D 뷰 위 판(아래 → 위 순서). GLRenderer 의 판 번호와 같다.
    enum Layer { kHints = 0, kTip, kPath, kBadges, kLegend, kStats, kMessage, kTooltip, kLayerCount };
    struct LayerState
    {
        int width = 0, height = 0, inset = 0;   // 그림 크기, 그림자 여백
        int x = 0, y = 0;                       // 3D 뷰 안 자리(왼쪽 위)
        bool visible = false;
        std::vector<RECT> hits;                 // 누를 수 있는 곳(그림 안 좌표)
    };

    float FrameSeconds();          // 지난 프레임부터 흐른 시간(초)
    CString MakeStatusText() const;              // benchmark.csv 의 '화면' 칸(펼침: …)
    void UpdateTitle();                          // 제목: "줌인 매뉴얼 — 지금 위치"
    void SelectAt(CPoint point);   // 클릭한 곳의 부품을 고르고, 바뀌었으면 대화상자에 알린다
    // 강조색(고전압 = 주황) + 선택 + 대화상자·화면 ③ 에 알림. 바뀌었으면 true.
    // fromSearch = 검색으로 고른 것(검색 결과 글자를 남긴다). 아니면 검색을 처음부터 다시 하게 한다.
    bool ApplySelection(int part, bool fromSearch = false);
    void ShowSelectionInPreview(int part);   // 화면 ③ 위 띠(품번·이름·고전압)와 그림 대상
    void UpdatePreview(float dtSeconds);     // 3D 뷰를 그린 뒤: 화면 ③ 부품 그림(돌 때 약 30fps)
    void SetPreviewStatus(const CString& text);
    void RunBenchmark();                     // B 키: 인스턴싱 끔·켬을 같은 화면으로 재서 비교
    bool SaveBenchmark(const GLRenderer::FrameTiming& off, const GLRenderer::FrameTiming& on);   // benchmark.csv
    bool HandleKey(UINT key);                // 키 하나 처리(꼬리표를 눌렀을 때도). 처리했으면 true

    // 7주차: 3D 뷰 위 판
    void UpdateOverlays(bool forceStats);    // 바뀐 판만 다시 그리고 자리를 맞춘다(매 프레임)
    void BuildStatsLayer();
    void BuildPathLayer(const std::vector<int>& nodes, bool vehicleOpen);
    void BuildBadgesLayer();
    void BuildHintsLayer();
    void BuildTooltip(int part);
    void Upload(Layer layer, const ui::OverlayImage& image);
    void PlaceLayers();                      // 판 자리(창 크기·커서 위치에 따라)
    int HitLayer(CPoint point, int* item) const;   // 누를 수 있는 판(위치 경로·꼬리표) 위면 그 판 번호, 아니면 -1
    bool OverPanel(CPoint point) const;      // 이름표를 뺀 판 위인지(그 아래 부품은 마우스 반응 안 함)
    std::wstring PartLabel(int node, bool vehicleOpen = true) const;   // "배터리 모듈 03", 차량은 "차량"
    std::vector<int> PartChain(int part) const;   // 차량 → … → part (부품 노드만)
    std::vector<int> CurrentPath(bool* vehicleOpen) const;   // 위치 경로에 보일 사슬(고른 부품 우선)
    void UpdateHover();                      // 커서 아래 부품(카메라가 멈춰 있을 때, 커서가 움직이면 0.12초마다 + 멈추면 한 번)
    void SetHoverPart(int part);

    // 7주차 3차: 보기 모드(V)
    void SetViewMode(int mode);              // 0 기본, 1 고전압, 2 재고
    void ApplyViewMode();                    // 부품마다 색을 정해 GLRenderer 에 넘기고 품목 수를 센다
    void BuildLegendLayer();
    void SetXray(bool on);                   // 속 보기 켬/끔(S 키와 같다: ③ 그림도 다시)
    void TakeFocusOnce();                    // 창이 보이고 활성화된 뒤 첫 프레임에 한 번: 키보드 포커스를 3D 뷰로

    // 7주차 4차: 고전압 차단 순서(P)
    void SetProcedureStep(int step);         // -1 = 닫기, 0 ~ 단계 수-1 = 그 단계(그 부품을 고르고 앞으로), 단계 수 = 다 했음
    void BuildProcedureLayer();              // 범례 자리에 단계 목록 판

    WglContext m_context;
    GLRenderer m_renderer;
    PartDB  m_partDb;
    PartSearch m_search;
    CPartPreview* m_preview = nullptr;           // 화면 ③(없어도 3D 는 그대로 동작)
    std::vector<unsigned char> m_previewPixels;  // ③ 그림을 받아 둘 곳(매번 새로 만들지 않게)
    bool    m_ready = false;       // OpenGL 준비가 끝났는지
    bool    m_dragging = false;    // 왼쪽 버튼을 누르고 있는지
    bool    m_moved = false;       // 누른 뒤 4px 넘게 움직였는지(움직였으면 회전, 아니면 클릭)
    bool    m_overlayClick = false;// 판을 눌렀다(이번 클릭은 부품 선택이 아님)
    CPoint  m_downPoint;
    CPoint  m_lastPoint;
    CString m_info;                // OpenGL·모델·데이터 정보(시작할 때 한 번)
    CString m_title;               // 제목에 마지막으로 쓴 글자
    std::wstring m_gpu;            // 그래픽 카드 이름(성능 표시 넷째 줄)
    bool    m_modelMissing = false;
    bool    m_partsMissing = false;
    bool    m_stockMissing = false;
    bool    m_showOverlay = false; // D 키: 성능 표시(7주차 2차: 처음에는 끔)
    int     m_hintMode = 0;        // H 키: 0 = 간단히(3개 + H), 1 = 단축키 전체, 2 = 숨김
    bool    m_tipDone = false;     // 첫 안내('휠을 굴려 다가가 보세요')를 다 봤는지(차체가 한 번 열리면)
    LARGE_INTEGER m_tickFrequency{};
    LARGE_INTEGER m_lastTick{};
    float   m_fps = 0.0f;          // 성능 표시용(지난 프레임들의 평균)
    float   m_cpuMs = 0.0f;        // 화면 한 장 그리기 명령에 든 CPU 시간(평균)
    ULONGLONG m_lastStatsMs = 0;
    CString m_benchText;           // 성능 측정 결과(표시 다섯째 줄)

    LayerState m_layers[kLayerCount];
    std::vector<int> m_pathNodes;  // 위치 경로에 지금 그려 둔 부품들
    bool    m_pathOpen = false;    // 그때 차체가 열려 있었는지
    bool    m_pathBuilt = false;
    std::vector<UINT> m_badgeKeys; // 꼬리표 i 를 누르면 보낼 키(0 = 누를 수 없음)
    unsigned m_badgeState = ~0u;
    int     m_hintsWidth = -1;     // 조작 안내를 그린 3D 뷰 너비(바뀌면 다시 맞춘다)
    int     m_hintsBuiltMode = -1; // 그때의 안내 모양(간단히·전체)

    // 마우스 올리기
    bool    m_mouseInside = false;
    bool    m_trackingLeave = false;
    bool    m_mouseMoved = false;
    bool    m_hoverDirty = false;  // S 키 등으로 클릭 단위가 바뀜 → 다시 고른다
    CPoint  m_mouse;
    ULONGLONG m_lastHoverMs = 0;
    ULONGLONG m_lastMouseMoveMs = 0;   // 커서가 마지막으로 움직인 때(멈췄는지 보려고)
    glm::vec3 m_hoverEye{ 0.0f };
    glm::vec3 m_hoverTarget{ 0.0f };
    int     m_hoverPart = -1;

    // 보기 모드(7주차 3차)
    int     m_viewMode = 0;        // 0 기본, 1 고전압, 2 재고
    bool    m_modeXray = false;    // 보기 모드가 속 보기를 켰다(끌 때 되돌린다)
    int     m_modeCounts[3] = {};  // 고전압 품목 수, 품절 품목 수, 부족 품목 수
    bool    m_timerPeriod = false; // timeBeginPeriod(1) 을 했다(끝날 때 timeEndPeriod)
    ULONGLONG m_focusUntil = 0;    // 이때까지 '첫 포커스 가져오기'를 시도(0 = 끝)

    // 고전압 차단 순서(7주차 4차)
    int     m_procStep = -1;       // -1 꺼짐, 0 ~ 단계 수-1 = 지금 단계, 단계 수 = 다 했음
    bool    m_procXray = false;    // 순서 안내가 속 보기를 켰다(닫을 때 되돌린다)
};
