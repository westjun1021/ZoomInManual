// PartPreview.cpp
#include "pch.h"
#include "PartPreview.h"
#include "UiTheme.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr UINT  kPreviewId = 5001;             // 이 창의 컨트롤 ID(3D 뷰 5000 과 안 겹치게)
    constexpr UINT  kDeferredLayout = WM_APP + 3;  // 이 창에 보내는 '배치 한 번 더' 메시지(대화상자 메시지와 안 겹침)
    constexpr float kStartYaw = 35.0f;             // 처음 보는 방향: 앞쪽 비스듬히
    constexpr float kStartPitch = 15.0f;           //               조금 위에서
    constexpr float kSpinDegreesPerSecond = 24.0f; // 15초에 한 바퀴
    constexpr float kResumeSeconds = 2.5f;         // 끌기를 멈추고 다시 돌기까지
    constexpr float kFrameSeconds = 1.0f / 30.0f;  // 돌 때 새 그림 간격(약 30fps)
    constexpr float kDegreesPerPixel = 0.5f;       // 끌기 1픽셀 = 0.5°

    float Clamp(float v, float lo, float hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }
}

BEGIN_MESSAGE_MAP(CPartPreview, CWnd)
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_WM_LBUTTONDOWN()
    ON_WM_LBUTTONUP()
    ON_WM_LBUTTONDBLCLK()
    ON_WM_MOUSEMOVE()
    ON_WM_MOUSELEAVE()
    ON_WM_CAPTURECHANGED()
    ON_WM_SIZE()
    ON_MESSAGE(kDeferredLayout, &CPartPreview::OnDeferredLayout)
END_MESSAGE_MAP()

BOOL CPartPreview::CreateInPanel(CWnd* dialog, UINT searchEditId, UINT searchButtonId, UINT oldTreeId)
{
    if (!dialog || !dialog->GetSafeHwnd())
        return FALSE;
    CWnd* edit = dialog->GetDlgItem(searchEditId);
    CWnd* button = dialog->GetDlgItem(searchButtonId);
    CWnd* tree = dialog->GetDlgItem(oldTreeId);

    // 1) ③ 칸에 있던 컨트롤 세 개가 차지하던 자리 전체(대화상자 안 좌표)
    CRect area;
    bool found = false;
    CWnd* controls[] = { edit, button, tree };
    for (CWnd* control : controls)
    {
        if (!control)
            continue;
        CRect rc;
        control->GetWindowRect(&rc);
        dialog->ScreenToClient(&rc);
        if (found)
        {
            CRect merged;
            merged.UnionRect(&area, &rc);
            area = merged;
        }
        else
        {
            area = rc;
        }
        found = true;
    }
    if (!found)
        return FALSE;

    // 2) 크기는 대화상자 단위(DLU)로 정한다 → 글꼴·화면 배율이 달라도 리소스 편집기에서 그린 것과 같은 비율.
    //    한 줄 높이 14, [검색] 단추 너비 50, 간격 4 (Visual Studio 의 기본 단추·편집 칸 크기)
    //    7주차: 검색 줄은 조금 넉넉하게(높이 30px·단추 64px 이상, 화면 배율대로)
    CRect unit(0, 0, 50, 14);
    ::MapDialogRect(dialog->GetSafeHwnd(), &unit);
    CRect spacing(0, 0, 4, 4);
    ::MapDialogRect(dialog->GetSafeHwnd(), &spacing);
    m_rowHeight = std::max(static_cast<int>(unit.Height()), ui::Px(30));
    m_buttonWidth = std::max(static_cast<int>(unit.Width()), ui::Px(64));
    m_gap = std::max(static_cast<int>(spacing.Width()), ui::Px(6));

    // 3) 검색 칸: 비어 있을 때 흐린 글자로 보이는 안내(Windows 의 cue banner). 트리는 이제 안 쓴다(리소스에서 지워도 된다).
    if (edit)
        edit->SendMessage(EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(_T("품번·이름·증상 검색 (예: 끽 소리)")));   // 7주차 3차: 칸에 다 들어가게 짧게
    if (tree)
    {
        tree->ShowWindow(SW_HIDE);
        tree->EnableWindow(FALSE);
    }

    // 4) 그룹 상자들
    //    - 대화상자에 WS_CLIPCHILDREN(3D 뷰 깜빡임 방지)이 켜져 있어서 그룹 상자 안쪽은 아무도 다시 칠하지 않는다
    //      → 창 크기를 바꾸면 옛 그림이 남는다. WS_EX_TRANSPARENT 를 주면 대화상자가 그 안쪽 바탕을 칠한다.
    //    - 이 자리를 둘러싼 그룹 상자 = ③: 제목을 바꾸고, 창 크기가 바뀌면 이 상자를 기준으로 다시 놓는다.
    const CPoint middle = area.CenterPoint();
    for (CWnd* child = dialog->GetWindow(GW_CHILD); child; child = child->GetWindow(GW_HWNDNEXT))
    {
        TCHAR className[16] = {};
        ::GetClassName(child->GetSafeHwnd(), className, 16);
        if (_tcsicmp(className, _T("Button")) != 0 || (child->GetStyle() & BS_TYPEMASK) != BS_GROUPBOX)
            continue;
        child->ModifyStyleEx(0, WS_EX_TRANSPARENT);
        m_groupBoxes.push_back(child->GetSafeHwnd());
        CRect box;
        child->GetWindowRect(&box);
        dialog->ScreenToClient(&box);
        if (!m_group && box.PtInRect(middle))
        {
            child->SetWindowText(_T("③ 부품 이미지 · 검색"));
            m_group = child->GetSafeHwnd();
            m_inset = CRect(area.left - box.left, area.top - box.top, box.right - area.right, box.bottom - area.bottom);
        }
    }
    m_dialog = dialog->GetSafeHwnd();
    m_edit = edit ? edit->GetSafeHwnd() : nullptr;
    m_button = button ? button->GetSafeHwnd() : nullptr;
    m_area = area;

    // 5) 부품 이미지 창. 손 모양 커서(끌어서 돌리기), 더블클릭 받기. 자리는 ApplyLayout 이 정한다.
    LPCTSTR className = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(nullptr, IDC_HAND), nullptr, nullptr);
    if (!CWnd::Create(className, _T("PartPreview"), WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, area, dialog, kPreviewId))
        return FALSE;
    SetWindowPos(&CWnd::wndTop, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);   // 그룹 상자보다 위에
    EnsureFonts();
    ResetAngles();
    // 6) 7주차: 대화상자 전체를 어두운 화면으로(그룹 상자 → 둥근 패널, 검색 칸·[검색] 단추, 제목 줄)
    ui::ThemeDialog(m_dialog, m_edit, m_button);
    ApplyLayout();
    return TRUE;
}

void CPartPreview::ApplyLayout()
{
    if (!GetSafeHwnd() || !::IsWindow(m_dialog) || ::IsIconic(m_dialog))
        return;   // 최소화 중에는 그대로 둔다
    CWnd* dialog = CWnd::FromHandle(m_dialog);

    // ③ 그룹 상자의 지금 자리에서 처음과 같은 여백만큼 안쪽(그룹 상자를 못 찾았으면 처음 자리 그대로)
    CRect area = m_area;
    if (m_group && ::IsWindow(m_group))
    {
        CRect box;
        ::GetWindowRect(m_group, &box);
        dialog->ScreenToClient(&box);
        area = CRect(box.left + m_inset.left, box.top + m_inset.top, box.right - m_inset.right, box.bottom - m_inset.bottom);
    }
    if (area.Width() < m_buttonWidth + 40 || area.Height() < m_rowHeight * 3)
        return;   // 너무 작으면 건드리지 않는다

    // 맨 아래 한 줄 = 검색 칸 + [검색] 단추(교수님 그림처럼 이미지 아래), 그 위 = 부품 이미지
    // 검색 칸은 테두리 없이 줄 가운데에 놓고, 둘레(둥근 입력 칸 모양)는 대화상자 바탕이 그린다(ui::ThemeDialog)
    const CRect row(area.left, area.bottom - m_rowHeight, area.right, area.bottom);
    const int buttonLeft = row.right - m_buttonWidth;
    if (m_button && ::IsWindow(m_button))
        ::MoveWindow(m_button, buttonLeft, row.top, m_buttonWidth, m_rowHeight, TRUE);
    if (m_edit && ::IsWindow(m_edit))
    {
        const int frameRight = m_button ? buttonLeft - m_gap : row.right;
        const int editHeight = std::min(ui::EditHeight(), m_rowHeight);
        ::MoveWindow(m_edit, row.left + ui::Px(10), row.top + (m_rowHeight - editHeight) / 2,
                     frameRight - row.left - ui::Px(16), editHeight, TRUE);
    }
    MoveWindow(area.left, area.top, area.Width(), row.top - m_gap - area.top, TRUE);

    // 옛 그림 지우기: 대화상자 바탕(그룹 상자 안쪽 포함)을 다시 칠하고, 그 위에 그룹 상자 테두리·제목을 다시 그린다.
    // 3D 뷰·② 표 같은 다른 컨트롤은 건드리지 않는다 → 크기를 바꾸는 동안 깜빡이지 않는다. (바로 칠하지 않고 모아서)
    dialog->RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_NOCHILDREN);
    for (HWND box : m_groupBoxes)
    {
        if (::IsWindow(box))
            ::RedrawWindow(box, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE);
    }
}

void CPartPreview::Relayout()
{
    if (!GetSafeHwnd())
        return;
    ApplyLayout();   // 지금 바로
    // MFC 동적 레이아웃이 이 OnSize 다음에 검색 칸·단추를 다시 옮겨도, 메시지 처리가 끝난 뒤 한 번 더 맞춘다
    if (!m_layoutPosted)
    {
        m_layoutPosted = true;
        PostMessage(kDeferredLayout);
    }
}

LRESULT CPartPreview::OnDeferredLayout(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    m_layoutPosted = false;
    ApplyLayout();
    return 0;
}

void CPartPreview::OnSize(UINT nType, int cx, int cy)
{
    CWnd::OnSize(nType, cx, cy);
    m_dirty = true;    // 그림 칸 크기가 바뀌었으니 새 크기로 바로 다시 그린다
    m_urgent = true;
    Invalidate(FALSE);
}

void CPartPreview::EnsureFonts()
{
    if (m_fontsReady)
        return;
    m_fontsReady = true;
    m_titleHeight = ui::PreviewTitleHeight();   // 위 띠(7주차 2차부터 0 = 없음)
    m_lineHeight = ui::PreviewLineHeight();     // 아래 알림 줄 칸
}

void CPartPreview::ResetAngles()
{
    m_yaw = kStartYaw;
    m_pitch = kStartPitch;
    m_waitSeconds = 0.0f;
    m_dirty = true;
}

void CPartPreview::ShowPart(int node, const CString& number, const CString& name, bool highVoltage)
{
    m_part = node;
    m_number = number;
    m_name = name;
    m_highVoltage = highVoltage;
    ResetAngles();
    m_urgent = true;
    if (node < 0)
    {
        m_pixels.clear();
        m_imageWidth = m_imageHeight = 0;
    }
    if (GetSafeHwnd())
        Invalidate(FALSE);
}

void CPartPreview::SetStatus(const CString& text)
{
    m_status = text;
    if (GetSafeHwnd())
        Invalidate(FALSE);
}

bool CPartPreview::Advance(float dtSeconds)
{
    if (m_part < 0 || !GetSafeHwnd() || !IsWindowVisible())
        return false;

    bool spinning = false;
    if (!m_dragging)
    {
        if (m_waitSeconds > 0.0f)
        {
            m_waitSeconds -= dtSeconds;
        }
        else
        {
            m_yaw = std::fmod(m_yaw + kSpinDegreesPerSecond * dtSeconds, 360.0f);
            spinning = true;
        }
    }
    m_sinceFrame += dtSeconds;
    if (!m_dirty && !spinning)
        return false;                     // 멈춰 있고 바뀐 것도 없다
    if (!m_urgent && m_sinceFrame < kFrameSeconds)
        return false;                     // 너무 자주 그리지 않는다(약 30fps)
    m_dirty = false;
    m_urgent = false;
    m_sinceFrame = 0.0f;
    return true;
}

CRect CPartPreview::ImageRect() const
{
    CRect client;
    GetClientRect(&client);
    CRect image(client.left, client.top + m_titleHeight, client.right, client.bottom);
    if (image.bottom < image.top)
        image.bottom = image.top;
    return image;
}

CSize CPartPreview::ImageSize() const
{
    if (!GetSafeHwnd())
        return CSize(0, 0);
    const CRect image = ImageRect();
    return CSize(image.Width() > 0 ? image.Width() : 0, image.Height() > 0 ? image.Height() : 0);
}

void CPartPreview::TakeImage(std::vector<unsigned char>& bgra, int width, int height)
{
    if (width <= 0 || height <= 0 || bgra.size() < static_cast<size_t>(width) * static_cast<size_t>(height) * 4)
    {
        m_pixels.clear();
        m_imageWidth = m_imageHeight = 0;
    }
    else
    {
        m_pixels.swap(bgra);
        m_imageWidth = width;
        m_imageHeight = height;
    }
    if (GetSafeHwnd())
        Invalidate(FALSE);
}

BOOL CPartPreview::OnEraseBkgnd(CDC* /*pDC*/)
{
    return TRUE;   // OnPaint 가 전부 다시 그린다(깜빡임 방지)
}

void CPartPreview::OnPaint()
{
    CPaintDC paintDc(this);
    CRect client;
    GetClientRect(&client);
    if (client.Width() <= 0 || client.Height() <= 0)
        return;
    EnsureFonts();

    // 메모리 그림판에 다 그린 뒤 한 번에 내보낸다(깜빡임 방지). 그리는 방법은 UiTheme.h 의 PaintPreview
    ui::Canvas canvas;
    if (!canvas.Create(client.Width(), client.Height(), true, ui::kPanelBg))
        return;
    ui::PreviewView view;
    view.hasPart = m_part >= 0;
    view.number = static_cast<LPCWSTR>(m_number);
    view.name = static_cast<LPCWSTR>(m_name);
    view.highVoltage = m_highVoltage;
    if (!m_status.IsEmpty())
        view.line = static_cast<LPCWSTR>(m_status);          // 검색 결과(늘 보인다)
    else if (m_part >= 0 && (m_mouseOver || m_dragging))
        view.line = L"끌어서 돌리기 · 더블클릭 = 처음 각도";   // 사용법(마우스가 ③ 위에 있을 때만)
    view.lineIsResult = !m_status.IsEmpty();
    const bool hasImage = m_part >= 0 && !m_pixels.empty();
    ui::PaintPreview(canvas, view, hasImage ? m_pixels.data() : nullptr, m_imageWidth, m_imageHeight);
    canvas.BlitTo(paintDc.GetSafeHdc(), 0, 0);
}

void CPartPreview::OnLButtonDown(UINT nFlags, CPoint point)
{
    if (m_part >= 0)
    {
        SetCapture();
        m_dragging = true;
        m_lastPoint = point;
    }
    CWnd::OnLButtonDown(nFlags, point);
}

void CPartPreview::OnMouseMove(UINT nFlags, CPoint point)
{
    if (!m_trackingLeave)
    {
        TRACKMOUSEEVENT track = { sizeof(track), TME_LEAVE, m_hWnd, 0 };
        m_trackingLeave = ::TrackMouseEvent(&track) != FALSE;
    }
    if (!m_mouseOver)
    {
        m_mouseOver = true;   // 사용법 줄을 보인다
        Invalidate(FALSE);
    }
    if (m_dragging)
    {
        // 3D 뷰와 같은 방향: 오른쪽으로 끌면 부품이 오른쪽으로 돌고, 아래로 끌면 위에서 내려다본다
        m_yaw -= static_cast<float>(point.x - m_lastPoint.x) * kDegreesPerPixel;
        m_pitch = Clamp(m_pitch + static_cast<float>(point.y - m_lastPoint.y) * kDegreesPerPixel, -80.0f, 80.0f);
        m_lastPoint = point;
        m_dirty = true;
    }
    CWnd::OnMouseMove(nFlags, point);
}

void CPartPreview::OnMouseLeave()
{
    m_trackingLeave = false;
    if (m_mouseOver)
    {
        m_mouseOver = false;   // 사용법 줄을 숨긴다(끄는 중이면 OnPaint 가 m_dragging 으로 계속 보인다)
        Invalidate(FALSE);
    }
    CWnd::OnMouseLeave();
}

void CPartPreview::OnLButtonUp(UINT nFlags, CPoint point)
{
    if (m_dragging)
    {
        m_dragging = false;
        m_waitSeconds = kResumeSeconds;   // 잠깐 멈췄다가 다시 천천히 돈다
        ReleaseCapture();
    }
    CWnd::OnLButtonUp(nFlags, point);
}

void CPartPreview::OnLButtonDblClk(UINT nFlags, CPoint point)
{
    if (m_part >= 0)
    {
        ResetAngles();
        m_urgent = true;
    }
    CWnd::OnLButtonDblClk(nFlags, point);
}

void CPartPreview::OnCaptureChanged(CWnd* pWnd)
{
    if (m_dragging)
    {
        m_dragging = false;
        m_waitSeconds = kResumeSeconds;
    }
    CWnd::OnCaptureChanged(pWnd);
}
