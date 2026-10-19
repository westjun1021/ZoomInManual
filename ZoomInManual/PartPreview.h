// PartPreview.h
// 5주차(새 안) 화면 ③: 고른 부품 하나만 크게 보여 주는 창(교수님 그림의 '부품 이미지').
//   - 그림은 CGLView 가 OpenGL 로 화면 밖에 그려서 넘겨준다(TakeImage). 이 창은 그 픽셀을 GDI 로 그리기만 한다.
//   - 위 띠: 품번 + 이름(고전압 부품은 주황 띠에 '고전압'). 가운데: 부품 그림(천천히 돈다).
//     맨 아래 한 줄: 검색 결과(예: '모듈' 10개 중 1번째) 또는 사용법.
//   - 마우스로 끌면 돌아가고, 더블클릭하면 처음 각도로. 손을 떼고 2.5초 뒤 다시 천천히 돈다.
//
// 7주차(UI 다듬기): 어두운 화면(그리기는 UiTheme.h 의 PaintPreview).
//   CreateInPanel 이 대화상자 전체도 어두운 화면으로 바꾼다(ui::ThemeDialog — 그룹 상자 대신 둥근 패널, 검색 칸·단추, 제목 줄).
// 7주차 2차: 위 띠(이름·품번·고전압)를 뺐다 — ② 카드 머리와 같은 글이라. 칸 전체가 그림.
//   맨 아래 사용법 줄('끌어서 돌리기 …')은 마우스가 ③ 위에 있을 때만(검색 결과 줄은 늘). ② 와 ③ 은 한 판으로 이어 그린다.
//
// 대화상자에서 쓰는 법(README [2]):
//   OnInitDialog : m_preview.CreateInPanel(this, IDC_EDIT_SEARCH, IDC_BTN_SEARCH, IDC_TREE_PARTS);
//                  m_glView.AttachPreview(&m_preview);
//   OnSize       : m_preview.Relayout();      (창 크기 조절 — 아래 Relayout 설명)
#pragma once
#include <vector>

class CPartPreview : public CWnd
{
public:
    // ③ 칸을 다시 나눈다: 리소스의 검색 칸·[검색] 단추·트리가 차지하던 자리 전체에서
    //   위 = 부품 이미지(이 창), 아래 한 줄 = 검색 칸 + [검색] 단추(교수님 그림처럼 이미지 아래).
    //   트리는 숨기고, ③ 그룹 상자 제목도 "③ 부품 이미지 · 검색"으로 바꾼다. OnInitDialog 에서 한 번.
    BOOL CreateInPanel(CWnd* dialog, UINT searchEditId, UINT searchButtonId, UINT oldTreeId);

    // 창 크기가 바뀐 뒤(대화상자 OnSize) 부른다. ③ 그룹 상자의 지금 자리에 맞춰 이미지·검색 칸·[검색] 단추를 다시 놓는다.
    // 리소스에 '동적 레이아웃'(Moving/Sizing Type)이 켜져 있으면 MFC 가 크기를 바꿀 때마다 검색 칸·단추를
    // 리소스의 원래 자리로 되돌리므로, 그 뒤에 한 번 더 맞춘다(바로 한 번 + 메시지 처리가 끝난 뒤 한 번).
    void Relayout();

    // node = -1 이면 안내 글. number = 노드 이름(예: EV-BM-12S@04), name = 이름(예: 배터리 모듈, 없으면 빈 글)
    void ShowPart(int node, const CString& number, const CString& name, bool highVoltage);
    void SetStatus(const CString& text);                                 // 맨 아래 한 줄. 빈 글 = 사용법
    void Refresh() { m_dirty = true; }                                   // 다시 그려야 할 때(S 키 등)

    // CGLView 가 매 프레임 부른다
    bool Advance(float dtSeconds);       // 시간만큼 돌리고, 새 그림이 필요하면 true(돌 때는 초당 약 30장)
    int Part() const { return m_part; }
    float Yaw() const { return m_yaw; }
    float Pitch() const { return m_pitch; }
    CSize ImageSize() const;             // 그림이 들어갈 칸의 픽셀 크기(OpenGL 로 이 크기만큼 그린다)
    int BottomMargin() const { return m_lineHeight + m_lineHeight / 2; }   // 그림 칸 맨 아래 알림 줄 자리(부품은 그 위에 그린다)
    // OpenGL 이 그린 픽셀(BGRA, 아래 줄부터)을 가져간다(bgra 와 내용을 맞바꾼다 → 복사 없음)
    void TakeImage(std::vector<unsigned char>& bgra, int width, int height);

protected:
    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
    afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
    afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
    afx_msg void OnMouseMove(UINT nFlags, CPoint point);
    afx_msg void OnMouseLeave();
    afx_msg void OnCaptureChanged(CWnd* pWnd);
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg LRESULT OnDeferredLayout(WPARAM wParam, LPARAM lParam);
    DECLARE_MESSAGE_MAP()

private:
    void ApplyLayout();                  // 그룹 상자 자리 → 이미지 창·검색 칸·단추 위치
    void EnsureFonts();                  // 알림 줄 높이(글꼴은 ui::GetFont). 위 띠는 7주차 2차에 없앴다(높이 0)
    CRect ImageRect() const;             // 창 안에서 그림 칸(지금은 창 전체)
    void ResetAngles();

    int m_part = -1;                     // 보여 주는 부품 노드(-1 = 없음)
    CString m_number;                    // 위 띠: 품번(@위치 포함)
    CString m_name;                      // 위 띠: 이름
    bool m_highVoltage = false;
    CString m_status;                    // 맨 아래 한 줄(검색 결과)
    float m_yaw = 0.0f;
    float m_pitch = 0.0f;
    bool m_dirty = false;                // 각도·부품이 바뀌어 새 그림이 필요
    bool m_urgent = false;               // 부품이 막 바뀜 → 30fps 제한 없이 바로 그린다
    bool m_dragging = false;
    bool m_mouseOver = false;            // 마우스가 이 창 위에 있다 → 사용법 줄을 보인다
    bool m_trackingLeave = false;
    float m_waitSeconds = 0.0f;          // 끌기를 멈춘 뒤 다시 돌기까지 남은 시간
    float m_sinceFrame = 0.0f;           // 마지막 그림 뒤 흐른 시간
    CPoint m_lastPoint;
    std::vector<unsigned char> m_pixels; // 마지막 그림(BGRA, 아래 줄부터)
    int m_imageWidth = 0;
    int m_imageHeight = 0;
    bool m_fontsReady = false;
    int m_titleHeight = 34;
    int m_lineHeight = 26;

    // 배치(창 크기가 바뀔 때마다 다시 계산)
    HWND m_dialog = nullptr;
    HWND m_edit = nullptr;
    HWND m_button = nullptr;
    HWND m_group = nullptr;              // ③ 그룹 상자(못 찾으면 nullptr → 처음 자리 그대로)
    std::vector<HWND> m_groupBoxes;      // 대화상자의 그룹 상자 전부(크기가 바뀌면 테두리를 다시 그린다)
    CRect m_area;                        // 처음 자리(검색 칸·단추·트리가 차지하던 곳)
    CRect m_inset;                       // 그룹 상자 테두리에서 그 자리까지의 여백(왼쪽·위·오른쪽·아래)
    int m_rowHeight = 0;
    int m_buttonWidth = 0;
    int m_gap = 0;
    bool m_layoutPosted = false;         // 메시지 처리 뒤 한 번 더 맞추기를 이미 예약했는지
};
