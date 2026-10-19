// InfoList.cpp
#include "pch.h"
#include "InfoList.h"

#include <algorithm>

// ------------------------------------------------------------------ 카드 창

BEGIN_MESSAGE_MAP(CInfoCard, CWnd)
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_WM_SIZE()
    ON_WM_MOUSEWHEEL()
    ON_WM_LBUTTONDOWN()
    ON_WM_LBUTTONUP()
    ON_WM_MOUSEMOVE()
    ON_WM_CAPTURECHANGED()
END_MESSAGE_MAP()

BOOL CInfoCard::Create(CWnd* parent, const CRect& rect)
{
    LPCTSTR className = AfxRegisterWndClass(0, ::LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr);
    return CWnd::Create(className, _T("InfoCard"), WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, rect, parent, 0);
}

void CInfoCard::SetData(const ui::CardData& data)
{
    m_data = data;
    m_scroll = 0;
    Measure();
    if (GetSafeHwnd())
        Invalidate(FALSE);
}

void CInfoCard::Measure()
{
    if (!GetSafeHwnd())
        return;
    CRect client;
    GetClientRect(&client);
    m_contentHeight = ui::LayoutCard(nullptr, m_data, client.Width(), 0, &m_headerHeight);
    ScrollTo(m_scroll);
}

void CInfoCard::ScrollTo(int y)
{
    CRect client;
    GetClientRect(&client);
    const int maxScroll = std::max(m_contentHeight - client.Height(), 0);
    const int clamped = std::min(std::max(y, 0), maxScroll);
    if (clamped != m_scroll)
    {
        m_scroll = clamped;
        Invalidate(FALSE);
    }
}

CRect CInfoCard::ThumbRect() const
{
    CRect client;
    GetClientRect(&client);
    // 머리(품번·이름) 아래 칸에서만 움직이는 막대. 스크롤할 게 없으면 빈 사각형
    const RECT rc = ui::CardThumbRect(client.Width(), client.Height(), m_headerHeight, m_contentHeight, m_scroll);
    return CRect(rc.left, rc.top, rc.right, rc.bottom);
}

BOOL CInfoCard::OnEraseBkgnd(CDC* /*pDC*/)
{
    return TRUE;   // OnPaint 가 전부 그린다(깜빡임 방지)
}

void CInfoCard::OnPaint()
{
    CPaintDC paintDc(this);
    CRect client;
    GetClientRect(&client);
    if (client.Width() <= 0 || client.Height() <= 0)
        return;
    ui::Canvas canvas;
    if (!canvas.Create(client.Width(), client.Height(), true, ui::kPanelBg))
        return;
    m_contentHeight = ui::LayoutCard(&canvas, m_data, client.Width(), m_scroll, &m_headerHeight);
    const CRect thumb = ThumbRect();
    if (!thumb.IsRectEmpty())
        canvas.FillRound(static_cast<float>(thumb.left), static_cast<float>(thumb.top), static_cast<float>(thumb.Width()),
                         static_cast<float>(thumb.Height()), thumb.Width() * 0.5f, RGB(255, 255, 255), m_dragging ? 0.45f : 0.25f);
    canvas.BlitTo(paintDc.GetSafeHdc(), 0, 0);
}

void CInfoCard::OnSize(UINT nType, int cx, int cy)
{
    CWnd::OnSize(nType, cx, cy);
    Measure();   // 너비가 바뀌면 줄바꿈도 바뀐다
    Invalidate(FALSE);
}

BOOL CInfoCard::OnMouseWheel(UINT /*nFlags*/, short zDelta, CPoint /*pt*/)
{
    ScrollTo(m_scroll - zDelta * ui::Px(48) / WHEEL_DELTA);   // 한 칸 = 약 세 줄
    return TRUE;
}

void CInfoCard::OnLButtonDown(UINT nFlags, CPoint point)
{
    CRect thumb = ThumbRect();
    thumb.InflateRect(ui::Px(4), 0);
    if (!thumb.IsRectEmpty() && thumb.PtInRect(point))
    {
        m_dragging = true;
        m_dragStartY = point.y;
        m_dragStartScroll = m_scroll;
        SetCapture();
        Invalidate(FALSE);
    }
    CWnd::OnLButtonDown(nFlags, point);
}

void CInfoCard::OnMouseMove(UINT nFlags, CPoint point)
{
    if (m_dragging)
    {
        CRect client;
        GetClientRect(&client);
        const CRect thumb = ThumbRect();
        const int track = client.Height() - m_headerHeight - ui::Px(8) - thumb.Height();   // 막대가 움직일 수 있는 길이
        const int maxScroll = std::max(m_contentHeight - client.Height(), 0);
        if (track > 0)
            ScrollTo(m_dragStartScroll + (point.y - m_dragStartY) * maxScroll / track);
    }
    CWnd::OnMouseMove(nFlags, point);
}

void CInfoCard::OnLButtonUp(UINT nFlags, CPoint point)
{
    if (m_dragging)
    {
        m_dragging = false;
        ReleaseCapture();
        Invalidate(FALSE);
    }
    CWnd::OnLButtonUp(nFlags, point);
}

void CInfoCard::OnCaptureChanged(CWnd* pWnd)
{
    m_dragging = false;
    CWnd::OnCaptureChanged(pWnd);
}

// ------------------------------------------------------------------ 리스트 컨트롤(자리만 빌려 준다)

BEGIN_MESSAGE_MAP(CInfoList, CListCtrl)
    ON_WM_SIZE()
    ON_WM_ERASEBKGND()
END_MESSAGE_MAP()

void CInfoList::Setup()
{
    // 테두리·머리글·스크롤 막대 없이, 그 안을 카드 창이 꽉 채운다(리스트 자체에는 항목을 넣지 않는다)
    DeleteAllItems();
    while (DeleteColumn(0))
    {
    }
    ModifyStyleEx(WS_EX_CLIENTEDGE | WS_EX_STATICEDGE, 0);
    ModifyStyle(WS_BORDER | WS_VSCROLL | WS_HSCROLL, LVS_NOCOLUMNHEADER | WS_CLIPCHILDREN, SWP_FRAMECHANGED);
    if (CHeaderCtrl* header = GetHeaderCtrl())
        header->ShowWindow(SW_HIDE);
    CRect client;
    GetClientRect(&client);
    if (!m_card.GetSafeHwnd())
        m_card.Create(this, client);
    m_card.SetWindowPos(&CWnd::wndTop, 0, 0, client.Width(), client.Height(), SWP_NOACTIVATE);
    ShowHint();
}

void CInfoList::ShowHint()
{
    m_card.SetData(ui::CardData());
}

void CInfoList::ShowPart(const SelectedPart& part, const PartDB& db)
{
    m_card.SetData(ui::MakeCardData(part, db));   // part.node < 0 이면 사용법 화면
}

void CInfoList::OnSize(UINT nType, int cx, int cy)
{
    CListCtrl::OnSize(nType, cx, cy);
    if (m_card.GetSafeHwnd())
        m_card.MoveWindow(0, 0, cx, cy);   // 창 크기 조절(동적 레이아웃)을 따라간다
}

BOOL CInfoList::OnEraseBkgnd(CDC* /*pDC*/)
{
    return TRUE;   // 카드가 덮는다
}
