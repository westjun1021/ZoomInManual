// GLView.h
// 화면 ①: 대화상자 안에 붙는 OpenGL 창(MFC CWnd).
// 창 메시지(그리기·크기·마우스·키)를 받아서 WglContext와 GLRenderer에 넘겨 준다.
// 2주차: 시작할 때 model.glb 를 찾아 불러오고, 결과를 제목 표시줄 글자(GetGlInfo)에 붙인다.
// 3주차: 휠은 커서가 가리키는 쪽으로 줌. 펼쳐진 부품이 바뀌면 대화상자 제목 맨 앞에 보여 준다.
// 4주차: 클릭 = 부품 선택(강조색, 고전압은 주황), 더블클릭 = 그 부품 앞으로 이동.
//        선택이 바뀌면 부모 창(대화상자)에 WM_ZOOMIN_PART_SELECTED 를 보낸다 → 화면 ② 갱신.
//        시작할 때 data\parts.json · data\inventory.json 도 읽는다(GetPartDB).
// 5주차(새 안): 화면 ③ = 고른 부품 하나의 3D 이미지(CPartPreview) + 검색 한 줄.
//        고른 부품이 바뀌면 ③ 에도 알리고, 매 프레임 3D 뷰를 그린 뒤 ③ 의 부품 그림도 화면 밖에 그려 넘겨준다.
//        SearchNext — 검색어에 맞는 부품을 고르고 그 앞으로 이동(같은 검색어로 다시 하면 다음 부품).
//        D 키에 FPS 표시.
//        키: S = 겉껍데기 강제 반투명, D = r_px·투명도 숫자, G = 바닥 격자, R = 처음 시점,
//            F = 와이어프레임, Z·X = 확대·축소
#pragma once
#include "WglContext.h"
#include "GLRenderer.h"
#include "PartDB.h"
#include "PartSearch.h"

#include <vector>

class CPartPreview;

// 선택한 부품이 바뀌었다는 알림(wParam = 노드 번호, -1 = 선택 해제). 대화상자에서 ON_MESSAGE 로 받는다.
#define WM_ZOOMIN_PART_SELECTED (WM_APP + 1)

class CGLView : public CWnd
{
public:
    // parent 안의 rect 위치에 OpenGL 창을 만든다.
    BOOL Create(CWnd* parent, const CRect& rect, UINT id);

    // 제목 표시줄에 띄울 OpenGL 정보(또는 실패 이유)
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
    afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
    afx_msg void OnCaptureChanged(CWnd* pWnd);
    afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
    afx_msg UINT OnGetDlgCode();
    DECLARE_MESSAGE_MAP()

private:
    float FrameSeconds();          // 지난 프레임부터 흐른 시간(초)
    CString MakeStatusText() const;
    void UpdateTitle();            // 상태 글자가 바뀌었으면 대화상자 제목을 고친다
    void SelectAt(CPoint point);   // 클릭한 곳의 부품을 고르고, 바뀌었으면 대화상자에 알린다
    // 강조색(고전압 = 주황) + 선택 + 대화상자·화면 ③ 에 알림. 바뀌었으면 true.
    // fromSearch = 검색으로 고른 것(검색 결과 글자를 남긴다). 아니면 검색을 처음부터 다시 하게 한다.
    bool ApplySelection(int part, bool fromSearch = false);
    void ShowSelectionInPreview(int part);   // 화면 ③ 위 띠(품번·이름·고전압)와 그림 대상
    void UpdatePreview(float dtSeconds);     // 3D 뷰를 그린 뒤: 화면 ③ 부품 그림(돌 때 약 30fps)
    void SetPreviewStatus(const CString& text);

    WglContext m_context;
    GLRenderer m_renderer;
    PartDB  m_partDb;
    PartSearch m_search;
    CPartPreview* m_preview = nullptr;           // 화면 ③(없어도 3D 는 그대로 동작)
    std::vector<unsigned char> m_previewPixels;  // ③ 그림을 받아 둘 곳(매번 새로 만들지 않게)
    bool    m_ready = false;       // OpenGL 준비가 끝났는지
    bool    m_dragging = false;    // 왼쪽 버튼을 누르고 있는지
    bool    m_moved = false;       // 누른 뒤 4px 넘게 움직였는지(움직였으면 회전, 아니면 클릭)
    CPoint  m_downPoint;
    CPoint  m_lastPoint;
    CString m_info;                // OpenGL·모델·데이터 정보(시작할 때 한 번)
    CString m_status;              // 제목에 마지막으로 쓴 상태 글자
    bool    m_showDetail = false;  // D 키
    LARGE_INTEGER m_tickFrequency{};
    LARGE_INTEGER m_lastTick{};
    ULONGLONG m_lastTitleMs = 0;
    float   m_fps = 0.0f;          // D 키 표시용(지난 프레임들의 평균)
};
