// GLView.cpp
#include "pch.h"
#include <glad/glad.h>
#include "GLView.h"
#include "PartPreview.h"

#include <cstdlib>

namespace
{
    constexpr UINT_PTR kRenderTimerId = 1;
    constexpr UINT     kRenderIntervalMs = 16;   // 약 60 FPS

    // model.glb 를 찾는 순서
    //   1) 현재 폴더의 data\model.glb  (Visual Studio 에서 F5 로 실행하면 현재 폴더 = 프로젝트 폴더)
    //   2) 실행 파일 옆 data\model.glb (exe 를 다른 곳에서 실행할 때)
    //   3) C:\dev\zoomin_models\model.glb (Blender 합치기 스크립트가 내보내는 곳)
    CString FindModelPath()
    {
        TCHAR exePath[MAX_PATH] = {};
        ::GetModuleFileName(nullptr, exePath, MAX_PATH);
        CString exeDir(exePath);
        exeDir = exeDir.Left(exeDir.ReverseFind(_T('\\')) + 1);

        const CString candidates[] = {
            _T("data\\model.glb"),
            exeDir + _T("data\\model.glb"),
            _T("C:\\dev\\zoomin_models\\model.glb"),
        };
        for (const CString& path : candidates)
        {
            if (::GetFileAttributes(path) != INVALID_FILE_ATTRIBUTES)
                return path;
        }
        return CString();
    }

    // parts.json · inventory.json 을 찾는 순서(GitHub 에 같이 올리는 파일이라 프로젝트의 data 폴더에 둔다)
    //   1) 현재 폴더의 data\    2) 실행 파일 옆 data\  .  못 찾으면 1) 경로를 돌려준다(오류 글자용).
    CString FindDataFile(LPCTSTR fileName)
    {
        TCHAR exePath[MAX_PATH] = {};
        ::GetModuleFileName(nullptr, exePath, MAX_PATH);
        CString exeDir(exePath);
        exeDir = exeDir.Left(exeDir.ReverseFind(_T('\\')) + 1);

        const CString local = CString(_T("data\\")) + fileName;
        const CString besideExe = exeDir + _T("data\\") + fileName;
        if (::GetFileAttributes(local) != INVALID_FILE_ATTRIBUTES)
            return local;
        if (::GetFileAttributes(besideExe) != INVALID_FILE_ATTRIBUTES)
            return besideExe;
        return local;
    }

    // 강조색: 보통 부품은 파랑, 고전압 부품은 주황(실제 EV 고전압 배선 색)
    const glm::vec3 kSelectColor(0.20f, 0.65f, 1.00f);
    const glm::vec3 kHighVoltageColor(1.00f, 0.35f, 0.00f);
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
    ON_WM_LBUTTONDBLCLK()
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

    ::QueryPerformanceFrequency(&m_tickFrequency);   // 프레임 시간 재기(카메라 애니메이션용)

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

    // 4) 차 모델(GLB) 불러오기. 실패해도 창은 띄우고 바닥 격자만 그린다.
    const CString modelPath = FindModelPath();
    if (modelPath.IsEmpty())
    {
        m_info += _T("  |  model.glb 없음 (data 폴더나 C:\\dev\\zoomin_models 에 두세요)");
    }
    else
    {
        CWaitCursor wait;   // 읽는 동안 모래시계 커서
        std::string error;
        if (m_renderer.LoadModel(std::string(CW2A(modelPath, CP_UTF8)), &error))
        {
            const Model::Stats& s = m_renderer.GetModel().GetStats();
            CString text;
            text.Format(_T("  |  부품 %d개 · 그리기 %d번 · 삼각형 %lld개 · %.1f초"),
                        s.parts, s.meshNodes, s.triangles, s.loadSeconds);
            m_info += text;
        }
        else
        {
            m_info += _T("  |  모델 읽기 실패: ") + CString(CA2W(error.c_str(), CP_UTF8));
        }
    }

    // 5) 부품 정보·재고(화면 ②). 없어도 3D 는 그대로 동작한다.
    {
        const CString partsPath = FindDataFile(_T("parts.json"));
        const CString stockPath = FindDataFile(_T("inventory.json"));
        std::string error;
        m_partDb.Load(std::string(CW2A(partsPath, CP_UTF8)), std::string(CW2A(stockPath, CP_UTF8)), &error);
        CString text;
        text.Format(_T("  |  부품 정보 %d개 · 재고 %d개"),
                    static_cast<int>(m_partDb.PartCount()), static_cast<int>(m_partDb.StockCount()));
        m_info += text;
        if (!error.empty())
        {
            TRACE(_T("PartDB: %s\n"), static_cast<LPCTSTR>(CString(CA2W(error.c_str(), CP_UTF8))));
            m_info += _T(" (data 폴더 확인)");
        }
    }

    m_ready = true;
    SetTimer(kRenderTimerId, kRenderIntervalMs, nullptr);
    return 0;
}

void CGLView::OnDestroy()
{
    KillTimer(kRenderTimerId);
    m_preview = nullptr;
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
    const float dt = FrameSeconds();
    m_renderer.Update(dt);   // 카메라가 목표 쪽으로 조금 움직이고, 시맨틱 줌을 다시 계산
    m_renderer.Render();
    m_context.Swap();
    UpdatePreview(dt);       // 화면 ③: 고른 부품 하나(돌 때 약 30fps)
    UpdateTitle();
}

void CGLView::AttachPreview(CPartPreview* preview)
{
    m_preview = preview;
    if (m_ready)
        ShowSelectionInPreview(m_renderer.Selected());
}

void CGLView::ShowSelectionInPreview(int part)
{
    if (!m_preview || !m_preview->GetSafeHwnd())
        return;
    const std::vector<Model::Node>& nodes = m_renderer.GetModel().Nodes();
    if (part < 0 || part >= static_cast<int>(nodes.size()))
    {
        m_preview->ShowPart(-1, CString(), false);
        return;
    }
    // 위 띠 글자: "EV-BM-12S@04  배터리 모듈"
    CString title(CA2W(nodes[part].name.c_str(), CP_UTF8));
    bool highVoltage = false;
    if (const PartInfo* info = m_partDb.FindPart(nodes[part].name))
    {
        title += _T("  ") + CString(CA2W(info->name.c_str(), CP_UTF8));
        highVoltage = info->hv;
    }
    m_preview->ShowPart(part, title, highVoltage);
}

void CGLView::UpdatePreview(float dtSeconds)
{
    if (!m_preview || !m_preview->GetSafeHwnd() || !m_preview->Advance(dtSeconds))
        return;   // ③ 이 없거나, 멈춰 있고 바뀐 것도 없다
    const CSize size = m_preview->ImageSize();
    if (size.cx < 8 || size.cy < 8)
        return;
    // 3D 뷰를 화면에 내보낸(Swap) 다음, 같은 OpenGL 로 화면 밖에 고른 부품만 그려 ③ 에 넘긴다
    if (m_renderer.RenderPartImage(m_preview->Part(), size.cx, size.cy, m_preview->Yaw(), m_preview->Pitch(),
                                   m_preview->BottomMargin(), &m_previewPixels))
        m_preview->TakeImage(m_previewPixels, size.cx, size.cy);
}

void CGLView::SetPreviewStatus(const CString& text)
{
    if (m_preview && m_preview->GetSafeHwnd())
        m_preview->SetStatus(text);
}

void CGLView::SearchNext(const CString& query)
{
    if (!m_ready)
        return;
    CString words = query;
    words.Trim();
    if (words.IsEmpty())
    {
        m_search.Reset();
        SetPreviewStatus(_T("품번이나 이름을 넣고 엔터 (예: 모듈 03, msd, 타이어)"));
        return;
    }

    const int node = m_search.Next(m_renderer.GetModel(), m_partDb, std::string(CW2A(words, CP_UTF8)));
    CString status;
    if (node < 0)
    {
        status.Format(_T("'%s' 에 맞는 부품이 없어요"), static_cast<LPCTSTR>(words));
    }
    else
    {
        ApplySelection(node, true);
        m_renderer.FlyToSelection();   // 차 밖에서 찾아도 다가가는 동안 차체·커버가 저절로 반투명해진다(3주차)
        if (m_search.Count() > 1)
            status.Format(_T("'%s' %d개 중 %d번째 · 엔터 = 다음"), static_cast<LPCTSTR>(words), m_search.Count(), m_search.Index() + 1);
        else
            status.Format(_T("'%s' 1개"), static_cast<LPCTSTR>(words));
    }
    SetPreviewStatus(status);
    Invalidate(FALSE);
}

float CGLView::FrameSeconds()
{
    LARGE_INTEGER now;
    ::QueryPerformanceCounter(&now);
    float dt = 0.0f;
    if (m_lastTick.QuadPart != 0 && m_tickFrequency.QuadPart != 0)
        dt = static_cast<float>(now.QuadPart - m_lastTick.QuadPart) / static_cast<float>(m_tickFrequency.QuadPart);
    m_lastTick = now;
    if (dt > 0.0f)
        m_fps = (m_fps <= 0.0f) ? 1.0f / dt : m_fps * 0.9f + (1.0f / dt) * 0.1f;   // 출렁이지 않게 평균
    return dt > 0.1f ? 0.1f : dt;   // 창을 끌다 놓는 등 오래 멈췄어도 한 번에 크게 튀지 않게
}

CString CGLView::MakeStatusText() const
{
    const Model& model = m_renderer.GetModel();
    if (!model.Loaded())
        return CString();
    const SemanticZoom& zoom = m_renderer.GetSemanticZoom();
    const std::vector<int>& path = zoom.FocusPath();

    // 펼쳐진 부품 사슬: 차량 → 배터리팩 → 모듈 …
    CString text;
    if (path.empty())
    {
        text = _T("보기: 차량 전체");
    }
    else
    {
        text = _T("펼침: ");
        for (size_t i = 0; i < path.size(); ++i)
        {
            if (i > 0)
                text += _T(" › ");
            text += CString(CA2W(model.Nodes()[path[i]].name.c_str(), CP_UTF8));
        }
    }
    if (m_renderer.ShellsForced())
        text += _T("  [S 속 보기]");

    // D 키: FPS·그리기 호출 수, 겉껍데기가 있는 부품과 가장 깊이 펼쳐진 부품의 r_px·거리·d_open·불투명도
    if (m_showDetail)
    {
        CString perf;
        perf.Format(_T("  |  FPS %.0f · 그리기 호출 %d번"), m_fps, model.GetStats().meshNodes);
        text += perf;
        for (const SemanticZoom::PartView& v : zoom.Parts())
        {
            const bool deepest = !path.empty() && v.node == path.back();
            if (!v.hasShell && !deepest)
                continue;
            const CString name(CA2W(model.Nodes()[v.node].name.c_str(), CP_UTF8));
            CString item;
            item.Format(_T("  |  %s r_px %.0f · d %.2fm · d_open %.2fm · α %.2f"),
                        static_cast<LPCTSTR>(name), v.screenPx, v.distance, v.openDistance, v.alpha);
            text += item;
        }
    }
    return text;
}

void CGLView::UpdateTitle()
{
    // D 키로 숫자를 볼 때는 매 프레임 바뀌므로 0.2초에 한 번만 고친다.
    const ULONGLONG nowMs = ::GetTickCount64();
    if (m_showDetail && nowMs - m_lastTitleMs < 200)
        return;
    const CString status = MakeStatusText();
    if (status == m_status)
        return;
    m_status = status;
    m_lastTitleMs = nowMs;
    if (CWnd* parent = GetParent())
        parent->SetWindowText(_T("줌인 매뉴얼 - ") + m_status + _T("  |  ") + m_info);
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
    SetFocus();     // 키보드(S, D, G, R, F, Z, X)와 휠을 이 창이 받도록
    SetCapture();   // 창 밖으로 끌고 나가도 계속 마우스를 받도록
    m_dragging = true;
    m_moved = false;
    m_downPoint = point;
    m_lastPoint = point;
    CWnd::OnLButtonDown(nFlags, point);
}

void CGLView::OnLButtonUp(UINT nFlags, CPoint point)
{
    if (m_dragging)
    {
        const bool click = !m_moved;   // 거의 안 움직이고 뗐으면 클릭
        m_dragging = false;
        ReleaseCapture();
        if (click && m_ready)
            SelectAt(point);
    }
    CWnd::OnLButtonUp(nFlags, point);
}

void CGLView::OnLButtonDblClk(UINT nFlags, CPoint point)
{
    // 더블클릭: 그 부품을 고르고 그 부품이 화면을 채우는 거리로 이동한다.
    if (m_ready)
    {
        SelectAt(point);
        m_renderer.FlyToSelection();
        Invalidate(FALSE);
    }
    CWnd::OnLButtonDblClk(nFlags, point);
}

void CGLView::OnMouseMove(UINT nFlags, CPoint point)
{
    if (m_dragging && m_ready)
    {
        // 4px 넘게 움직여야 회전을 시작한다(클릭할 때 손이 조금 떨려도 화면이 안 돌게).
        if (!m_moved && std::abs(point.x - m_downPoint.x) + std::abs(point.y - m_downPoint.y) > 4)
            m_moved = true;
        if (m_moved)
        {
            m_renderer.Orbit(static_cast<float>(point.x - m_lastPoint.x),
                             static_cast<float>(point.y - m_lastPoint.y));
            m_lastPoint = point;
            Invalidate(FALSE);
        }
    }
    CWnd::OnMouseMove(nFlags, point);
}

void CGLView::SelectAt(CPoint point)
{
    m_context.MakeCurrent();
    ApplySelection(m_renderer.PartAt(point.x, point.y));
}

bool CGLView::ApplySelection(int part, bool fromSearch)
{
    if (!fromSearch)
    {
        // 3D 에서 직접 고르면 검색은 처음부터 다시(같은 검색어로 엔터 → 첫 부품), 검색 결과 글자도 지운다
        m_search.Reset();
        SetPreviewStatus(CString());
    }
    if (part == m_renderer.Selected())
        return false;   // 같은 부품을 다시 골랐으면 그대로

    glm::vec3 color = kSelectColor;
    if (part >= 0)
    {
        const PartInfo* info = m_partDb.FindPart(m_renderer.GetModel().Nodes()[part].name);
        if (info && info->hv)
            color = kHighVoltageColor;
    }
    m_renderer.Select(part, color);
    ShowSelectionInPreview(part);   // 화면 ③: 이 부품 하나만
    Invalidate(FALSE);
    if (CWnd* parent = GetParent())
        parent->PostMessage(WM_ZOOMIN_PART_SELECTED, static_cast<WPARAM>(part), 0);
    return true;
}

void CGLView::SelectPart(int node, bool flyTo)
{
    if (!m_ready)
        return;
    const int count = static_cast<int>(m_renderer.GetModel().Nodes().size());
    if (node < -1 || node >= count || (node >= 0 && !m_renderer.GetModel().Nodes()[node].isPart))
        return;
    ApplySelection(node);
    if (flyTo && node >= 0)
        m_renderer.FlyToSelection();   // 이미 고른 부품이어도 다시 그 앞으로 간다
    Invalidate(FALSE);
}

BOOL CGLView::OnMouseWheel(UINT /*nFlags*/, short zDelta, CPoint pt)
{
    if (m_ready)
    {
        // 휠 메시지의 좌표는 화면 기준 → 이 창 안 좌표로 바꾼 뒤, 커서가 가리키는 점 쪽으로 줌
        ScreenToClient(&pt);
        m_context.MakeCurrent();
        m_renderer.ZoomAtCursor(pt.x, pt.y, zDelta);
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
        case 'G': m_renderer.ToggleGrid();      break;   // 바닥 격자 켜기/끄기
        case 'S':                                         // 거리와 상관없이 겉껍데기 반투명(속 보기)
            m_renderer.ToggleShells();
            if (m_preview && m_preview->GetSafeHwnd())
                m_preview->Refresh();                     // ③ 그림도 속 보기로
            break;
        case 'D': m_showDetail = !m_showDetail; m_lastTitleMs = 0; break;   // r_px·투명도 숫자 보기
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
