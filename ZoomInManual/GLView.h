// GLView.h
// 화면 ①: 대화상자 안에 붙는 OpenGL 창(MFC CWnd).
// 창 메시지(그리기·크기·마우스·키)를 받아서 WglContext와 GLRenderer에 넘겨 준다.
#pragma once
#include "WglContext.h"
#include "GLRenderer.h"

class CGLView : public CWnd
{
public:
    // parent 안의 rect 위치에 OpenGL 창을 만든다.
    BOOL Create(CWnd* parent, const CRect& rect, UINT id);

    // 제목 표시줄에 띄울 OpenGL 정보(또는 실패 이유)
    CString GetGlInfo() const { return m_info; }

protected:
    afx_msg int  OnCreate(LPCREATESTRUCT lpCreateStruct);
    afx_msg void OnDestroy();
    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
    afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
    afx_msg void OnMouseMove(UINT nFlags, CPoint point);
    afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
    afx_msg void OnCaptureChanged(CWnd* pWnd);
    afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
    afx_msg UINT OnGetDlgCode();
    DECLARE_MESSAGE_MAP()

private:
    WglContext m_context;
    GLRenderer m_renderer;
    bool    m_ready = false;      // OpenGL 준비가 끝났는지
    bool    m_dragging = false;   // 왼쪽 버튼으로 끌고 있는지
    CPoint  m_lastPoint;
    CString m_info;
};
