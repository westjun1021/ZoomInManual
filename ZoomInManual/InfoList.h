// InfoList.h
// 화면 ②: 부품 정보·재고. 리소스 편집기에 그려 둔 IDC_LIST_INFO 자리를 그대로 쓴다.
// 7주차(UI 다듬기): 표(열 2개) 대신 어두운 '카드'로 그린다 — 큰 이름, 품번(파랑)·위치 꼬리표, 고전압 빨간 띠,
//   재고 상자(충분 초록 · 부족 주황 · 품절 빨강 + 수량, 위치·단가·입고 기간·공급일자·공급사·담당자), 설명·사양·주의사항.
//   그리기는 InfoCard.h(순수 Win32), 이 파일은 MFC 창만. 내용이 길면 휠로 스크롤(오른쪽에 얇은 막대, 끌어도 된다).
//   리스트 컨트롤(CInfoList)은 테두리·머리글을 없애고, 그 안을 카드 창(CInfoCard)이 꽉 채운다.
//   → 대화상자 코드는 4주차 그대로(아래 두 줄)
// 7주차 2차: 품번·이름 '머리'는 스크롤해도 위에 고정, 스크롤은 그 아래만(막대도 머리 아래 칸에서), 아래에 더 있으면 흐림.
//
// 대화상자에서 쓰는 법:
//   OnInitDialog : m_infoList.SubclassDlgItem(IDC_LIST_INFO, this);  m_infoList.Setup();
//   선택 알림    : m_infoList.ShowPart(m_glView.GetSelectedPart(), m_glView.GetPartDB());
#pragma once
#include "GLRenderer.h"
#include "PartDB.h"
#include "InfoCard.h"

// 카드를 그리는 창(CInfoList 안을 꽉 채운다)
class CInfoCard : public CWnd
{
public:
    BOOL Create(CWnd* parent, const CRect& rect);
    void SetData(const ui::CardData& data);   // 내용이 바뀌면 맨 위부터

protected:
    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
    afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
    afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
    afx_msg void OnMouseMove(UINT nFlags, CPoint point);
    afx_msg void OnCaptureChanged(CWnd* pWnd);
    DECLARE_MESSAGE_MAP()

private:
    void Measure();                // 내용 높이(창 너비에 따라 줄바꿈이 달라진다)
    void ScrollTo(int y);
    CRect ThumbRect() const;       // 스크롤 막대(내용이 창보다 길 때만)

    ui::CardData m_data;
    int m_contentHeight = 0;
    int m_headerHeight = 0;        // 고정 머리(품번·이름) 높이. 사용법 화면이면 0
    int m_scroll = 0;
    bool m_dragging = false;       // 스크롤 막대를 끄는 중
    int m_dragStartY = 0;
    int m_dragStartScroll = 0;
};

class CInfoList : public CListCtrl
{
public:
    void Setup();                                              // 테두리·머리글을 없애고 카드 창을 만든다
    void ShowPart(const SelectedPart& part, const PartDB& db);  // part.node < 0 이면 사용법 화면
    void ShowHint();

protected:
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    DECLARE_MESSAGE_MAP()

private:
    CInfoCard m_card;
};
