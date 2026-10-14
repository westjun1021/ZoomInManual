// GLView.cpp
#include "pch.h"
#include <glad/glad.h>
#include "GLView.h"

namespace
{
    constexpr UINT_PTR kRenderTimerId = 1;
    constexpr UINT     kRenderIntervalMs = 16;   // 약 60 FPS
}

BEGIN_MESSAGE_MAP(CGLView, CWnd)
    ON_WM_CREATE()
    ON_WM_DESTROY()
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_WM_SIZE()
    ON_WM_TIMER()
    ON_WM_LBUTTONDOWN()
    ON_WM_LBUTTONUP()
    ON_WM_MOUSEMOVE()
    ON_WM_MOUSEWHEEL()
    ON_WM_CAPTURECHANGED()
    ON_WM_KEYDOWN()
    ON_WM_GETDLGCODE()
END_MESSAGE_MAP()

BOOL CGLView::Create(CWnd* parent, const CRect& rect, UINT id)
{
    // CS_OWNDC: OpenGL은 창마다 자기 DC를 계속 들고 있어야 한다.
    LPCTSTR className = AfxRegisterWndClass(CS_OWNDC | CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS,
                                            ::LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr);
    return CWnd::Create(className, _T("GLView"),
                        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_TABSTOP,
                        rect, parent, id);
}

int CGLView::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
    if (CWnd::OnCreate(lpCreateStruct) == -1)
        return -1;

    // 1) OpenGL 3.3 Core 컨텍스트
    if (!m_context.Create(m_hWnd))
    {
        m_info = CString(m_context.Message().c_str());
        return 0;   // 창은 남겨 두고 OnPaint에서 실패 이유를 보여 준다
    }

    // 2) OpenGL 함수 주소 불러오기(GLAD)
    if (!gladLoadGL() || GLVersion.major < 3 || (GLVersion.major == 3 && GLVersion.minor < 3))
    {
        m_info = _T("OpenGL 3.3을 쓸 수 없습니다. 그래픽 드라이버를 업데이트하세요.");
        return 0;
    }

    // 3) 셰이더·버퍼 준비
    std::string log;
    if (!m_renderer.Init(&log))
    {
        m_info = CString(log.c_str());
        return 0;
    }

    CRect rc;
    GetClientRect(&rc);
    m_renderer.Resize(rc.Width(), rc.Height());

    m_info = CString(m_renderer.GlInfo().c_str());
    if (!m_context.IsCore())
        m_info += _T("  [호환 모드]");

    m_ready = true;
    SetTimer(kRenderTimerId, kRenderIntervalMs, nullptr);
    return 0;
}

void CGLView::OnDestroy()
{
    KillTimer(kRenderTimerId);
    if (m_ready && m_context.MakeCurrent())
        m_renderer.Shutdown();
    m_ready = false;
    m_context.Destroy();
    CWnd::OnDestroy();
}

void CGLView::OnPaint()
{
    CPaintDC dc(this);   // 그리기 영역을 '처리됨'으로 표시

    if (!m_ready)
    {
        // OpenGL 준비에 실패했으면 이유를 글자로 보여 준다.
        CRect rc;
        GetClientRect(&rc);
        dc.FillSolidRect(rc, RGB(30, 30, 38));
        dc.SetBkMode(TRANSPARENT);
        dc.SetTextColor(RGB(255, 190, 190));
        dc.DrawText(m_info, rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }

    m_context.MakeCurrent();
    m_renderer.Render();
    m_context.Swap();
}

BOOL CGLView::OnEraseBkgnd(CDC* /*pDC*/)
{
    return TRUE;   // 배경을 지우지 않는다(깜빡임 방지). 어차피 OpenGL이 전부 다시 그린다.
}

void CGLView::OnSize(UINT nType, int cx, int cy)
{
    CWnd::OnSize(nType, cx, cy);
    if (m_ready)
        m_renderer.Resize(cx, cy);
}

void CGLView::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == kRenderTimerId)
        Invalidate(FALSE);   // 다음 OnPaint에서 다시 그림
    CWnd::OnTimer(nIDEvent);
}

void CGLView::OnLButtonDown(UINT nFlags, CPoint point)
{
    SetFocus();     // 키보드(F, R, Z, X)와 휠을 이 창이 받도록
    SetCapture();   // 창 밖으로 끌고 나가도 계속 마우스를 받도록
    m_dragging = true;
    m_lastPoint = point;
    CWnd::OnLButtonDown(nFlags, point);
}

void CGLView::OnLButtonUp(UINT nFlags, CPoint point)
{
    if (m_dragging)
    {
        m_dragging = false;
        ReleaseCapture();
    }
    CWnd::OnLButtonUp(nFlags, point);
}

void CGLView::OnMouseMove(UINT nFlags, CPoint point)
{
    if (m_dragging && m_ready)
    {
        m_renderer.Orbit(static_cast<float>(point.x - m_lastPoint.x),
                         static_cast<float>(point.y - m_lastPoint.y));
        m_lastPoint = point;
        Invalidate(FALSE);
    }
    CWnd::OnMouseMove(nFlags, point);
}

BOOL CGLView::OnMouseWheel(UINT /*nFlags*/, short zDelta, CPoint /*pt*/)
{
    if (m_ready)
    {
        m_renderer.Zoom(zDelta);
        Invalidate(FALSE);
    }
    return TRUE;
}

void CGLView::OnCaptureChanged(CWnd* pWnd)
{
    m_dragging = false;   // 다른 창이 마우스를 가져가면 드래그 종료
    CWnd::OnCaptureChanged(pWnd);
}

void CGLView::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    if (m_ready)
    {
        switch (nChar)
        {
        case 'F': m_renderer.ToggleWireframe(); break;   // GL_ENC의 f 키
        case 'R': m_renderer.ResetView();       break;   // Texture_Mapping의 R 키
        case 'Z': m_renderer.Zoom(+120);        break;   // GL_ENC의 z 키(확대)
        case 'X': m_renderer.Zoom(-120);        break;   // GL_ENC의 x 키(축소)
        default: break;
        }
        Invalidate(FALSE);
    }
    CWnd::OnKeyDown(nChar, nRepCnt, nFlags);
}

UINT CGLView::OnGetDlgCode()
{
    // 대화상자 안에서도 모든 키 입력을 이 창이 받는다.
    return DLGC_WANTALLKEYS | DLGC_WANTARROWS | DLGC_WANTCHARS;
}
