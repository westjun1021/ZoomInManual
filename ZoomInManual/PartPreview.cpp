// PartPreview.cpp
#include "pch.h"
#include "PartPreview.h"

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

    const COLORREF kTitleNormal = RGB(52, 60, 74);      // 위 띠: 보통 부품(짙은 회청색)
    const COLORREF kTitleHighVoltage = RGB(230, 90, 0); // 위 띠: 고전압 부품(3D 강조색과 같은 주황)
    const COLORREF kTitleIdle = RGB(120, 128, 140);     // 위 띠: 고른 부품 없음
    const COLORREF kIdleBack = RGB(236, 239, 243);      // 그림이 없을 때 바탕(OpenGL 배경 위쪽과 비슷한 색)
    const COLORREF kIdleText = RGB(110, 116, 128);
    const COLORREF kStatusText = RGB(25, 45, 90);       // 검색 결과 글자
    const COLORREF kHintText = RGB(85, 92, 105);        // 사용법 글자
    const COLORREF kHalo = RGB(244, 246, 249);          // 글자 둘레(부품 위에 겹쳐도 읽히게)
    const COLORREF kBorder = RGB(171, 173, 179);        // 테두리(편집 칸 테두리와 같은 색)

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
    CRect unit(0, 0, 50, 14);
    ::MapDialogRect(dialog->GetSafeHwnd(), &unit);
    CRect spacing(0, 0, 4, 4);
    ::MapDialogRect(dialog->GetSafeHwnd(), &spacing);
    m_rowHeight = unit.Height();
    m_buttonWidth = unit.Width();
    m_gap = spacing.Width();

    // 3) 검색 칸: 비어 있을 때 흐린 글자로 보이는 안내(Windows 의 cue banner). 트리는 이제 안 쓴다(리소스에서 지워도 된다).
    if (edit)
        edit->SendMessage(EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(_T("품번·이름 검색 (예: 모듈 03, msd)")));
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
    const CRect row(area.left, area.bottom - m_rowHeight, area.right, area.bottom);
    if (m_button && ::IsWindow(m_button))
        ::MoveWindow(m_button, row.right - m_buttonWidth, row.top, m_buttonWidth, m_rowHeight, TRUE);
    if (m_edit && ::IsWindow(m_edit))
        ::MoveWindow(m_edit, row.left, row.top, m_button ? row.Width() - m_buttonWidth - m_gap : row.Width(), m_rowHeight, TRUE);
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
    if (m_font.GetSafeHandle())
        return;
    LOGFONT lf = {};
    CWnd* parent = GetParent();
    CFont* dialogFont = parent ? parent->GetFont() : nullptr;
    if (!dialogFont || !dialogFont->GetLogFont(&lf))
        ::GetObject(::GetStockObject(DEFAULT_GUI_FONT), sizeof(lf), &lf);
    m_font.CreateFontIndirect(&lf);
    lf.lfWeight = FW_BOLD;
    m_boldFont.CreateFontIndirect(&lf);

    // 위 띠 높이 = 굵은 글자 높이 + 위아래 여백
    CClientDC dc(this);
    CFont* oldFont = dc.SelectObject(&m_boldFont);
    TEXTMETRIC tm = {};
    dc.GetTextMetrics(&tm);
    dc.SelectObject(oldFont);
    m_lineHeight = tm.tmHeight;
    m_titleHeight = tm.tmHeight + 8;
}

void CPartPreview::ResetAngles()
{
    m_yaw = kStartYaw;
    m_pitch = kStartPitch;
    m_waitSeconds = 0.0f;
    m_dirty = true;
}

void CPartPreview::ShowPart(int node, const CString& title, bool highVoltage)
{
    m_part = node;
    m_title = title;
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
    CRect image(client.left + 1, client.top + m_titleHeight, client.right - 1, client.bottom - 1);
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

    // 메모리에 다 그린 뒤 한 번에 내보낸다(깜빡임 방지)
    CDC dc;
    dc.CreateCompatibleDC(&paintDc);
    CBitmap canvas;
    canvas.CreateCompatibleBitmap(&paintDc, client.Width(), client.Height());
    CBitmap* oldBitmap = dc.SelectObject(&canvas);
    CFont* oldFont = dc.SelectObject(&m_boldFont);
    dc.SetBkMode(TRANSPARENT);

    // 1) 위 띠: 품번 + 이름, 고전압이면 주황 띠 오른쪽에 '고전압'
    CRect title(client.left, client.top, client.right, client.top + m_titleHeight);
    dc.FillSolidRect(&title, m_part < 0 ? kTitleIdle : (m_highVoltage ? kTitleHighVoltage : kTitleNormal));
    dc.SetTextColor(RGB(255, 255, 255));
    CRect titleText = title;
    titleText.DeflateRect(8, 0);
    if (m_part >= 0 && m_highVoltage)
    {
        const CString mark(_T("고전압"));
        dc.DrawText(mark, &titleText, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        titleText.right -= dc.GetTextExtent(mark).cx + 12;
    }
    dc.DrawText(m_part >= 0 ? m_title : CString(_T("부품 이미지")), &titleText,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    dc.SelectObject(&m_font);

    // 2) 그림 칸
    const CRect image = ImageRect();
    if (m_part >= 0 && !m_pixels.empty())
    {
        BITMAPINFO info = {};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = m_imageWidth;
        info.bmiHeader.biHeight = m_imageHeight;   // 양수 = 아래 줄부터(OpenGL 이 읽은 순서 그대로)
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        ::SetStretchBltMode(dc.GetSafeHdc(), COLORONCOLOR);
        ::StretchDIBits(dc.GetSafeHdc(), image.left, image.top, image.Width(), image.Height(),
                        0, 0, m_imageWidth, m_imageHeight, m_pixels.data(), &info, DIB_RGB_COLORS, SRCCOPY);
    }
    else
    {
        dc.FillSolidRect(&image, kIdleBack);
        if (m_part < 0)
        {
            // 가운데에 안내 글 두 줄
            const CString hint(_T("3D 뷰에서 부품을 클릭하면\n여기에 그 부품만 크게 보여요."));
            CRect measure(image.left + 12, 0, image.right - 12, 0);
            dc.DrawText(hint, &measure, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
            const int top = image.top + (image.Height() - measure.Height()) / 2;
            CRect hintRect(image.left + 12, top, image.right - 12, top + measure.Height());
            dc.SetTextColor(kIdleText);
            dc.DrawText(hint, &hintRect, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
        }
    }

    // 3) 맨 아래 한 줄: 검색 결과, 없으면 사용법. 둘레를 밝게 둘러 부품 위에 겹쳐도 읽히게 한다.
    CString line = m_status;
    if (line.IsEmpty() && m_part >= 0)
        line = _T("끌어서 돌리기 · 더블클릭 = 처음 각도");
    if (!line.IsEmpty() && image.Height() > m_lineHeight + 8)
    {
        const CRect lineRect(image.left + 8, image.bottom - m_lineHeight - 5, image.right - 8, image.bottom - 3);
        const UINT format = DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX;
        dc.SetTextColor(kHalo);
        for (int dx = -1; dx <= 1; ++dx)
        {
            for (int dy = -1; dy <= 1; ++dy)
            {
                if (dx == 0 && dy == 0)
                    continue;
                CRect halo = lineRect;
                halo.OffsetRect(dx, dy);
                dc.DrawText(line, &halo, format);
            }
        }
        CRect textRect = lineRect;
        dc.SetTextColor(m_status.IsEmpty() ? kHintText : kStatusText);
        dc.DrawText(line, &textRect, format);
    }

    // 4) 테두리
    dc.Draw3dRect(&client, kBorder, kBorder);

    paintDc.BitBlt(0, 0, client.Width(), client.Height(), &dc, 0, 0, SRCCOPY);
    dc.SelectObject(oldFont);
    dc.SelectObject(oldBitmap);
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
