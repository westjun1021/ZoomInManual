// GLView.cpp
#include "pch.h"
#include <glad/glad.h>
#include "GLView.h"
#include "PartPreview.h"
#include "UiOverlay.h"
#include <mmsystem.h>   // timeBeginPeriod(타이머 간격 1ms) — MFC 기본 설정(VC_EXTRALEAN)에서는 windows.h 가 안 넣어 준다

#include <algorithm>
#include <cstdio>
#include <set>
#include <cstdlib>
#include <cstring>
#include <cwchar>

#ifdef _MSC_VER
#pragma comment(lib, "winmm.lib")   // timeBeginPeriod / timeEndPeriod
#endif

namespace
{
    // 보기 모드 색(선형 색 + 세기). 고전압 = 선택 강조와 같은 주황, 재고 품절 = 빨강, 부족 = 노랑
    const glm::vec4 kHvTint(1.00f, 0.35f, 0.00f, 0.70f);
    const glm::vec4 kOutTint(1.00f, 0.08f, 0.08f, 0.60f);
    const glm::vec4 kLowTint(1.00f, 0.62f, 0.04f, 0.55f);
    constexpr UINT_PTR kRenderTimerId = 1;
    constexpr UINT     kRenderIntervalMs = 16;   // 약 60 FPS
    // 마우스 올리기(7주차 2차): 커서가 움직이는 동안은 0.12초에 한 번, 멈추면(0.04초) 바로 한 번 고른다.
    //   고를 때마다 클릭 식별 그림을 한 번 더 그리고 1픽셀을 읽는다(그동안 GPU 를 기다린다) → 너무 자주 하면 FPS 가 떨어진다.
    constexpr ULONGLONG kHoverIntervalMs = 120;
    constexpr ULONGLONG kHoverSettleMs = 40;

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

    // 고전압 차단 순서(7주차 4차, P 키): 단계마다 고를 부품(앞에서부터 모델에 있는 것), 목록 글, 설명.
    // 일반 정비 상식 수준의 순서다. 실제 기다리는 시간·기준 전압은 차종 정비 지침서를 따른다(마지막 글에 적는다).
    // 7주차 5차: 맨 앞에 보호구 준비(고를 부품 없음 → 선택을 풀고 처음 시점), 마지막 단계는 '0 V' 대신 '기준값 미만'.
    struct ProcedureStep
    {
        const char* parts[2];
        const wchar_t* title;
        const wchar_t* detail;
    };
    const ProcedureStep kHvShutdown[] = {
        { { nullptr, nullptr }, L"보호구 착용·점검",
          L"절연 장갑(0등급)·가죽 겉장갑·보안경을 쓴다. 장갑에 구멍·찢김이 없는지, 시험 날짜가 지나지 않았는지 보고, "
          L"시계·반지 같은 금속 물품은 뺀다." },
        { { "EV-AUX-001", nullptr }, L"12V 보조 배터리 (-) 단자 분리",
          L"시동을 끄고 스마트키를 차에서 멀리 둔 뒤, 12V 보조 배터리의 (-) 단자를 뗀다. "
          L"고전압 릴레이가 다시 붙지 않게 제어 전원부터 끊는다." },
        { { "EV-MSD-001", "EV-BP-001" }, L"서비스 플러그 분리",
          L"절연 장갑을 끼고 배터리팩 위 서비스 플러그를 뽑아 팩 안의 고전압 회로를 끊는다. "
          L"뽑은 플러그는 작업자가 직접 보관한다." },
        { { "EV-HVC-002", "EV-BP-001" }, L"5분 이상 기다리기",
          L"인버터 안 커패시터에 남은 전기가 빠지도록 5분 이상 기다린다. 그동안 주황색 고전압 케이블·부품은 만지지 않는다." },
        { { "EV-INV-001", "EV-MTB-001" }, L"인버터 DC 단자 무전압 확인",
          L"절연 장갑을 낀 채 인버터 DC 입력 단자 사이 전압을 잰다. 기준값(차종 정비 지침서) 미만이어야 하고, "
          L"넘으면 작업을 멈추고 다시 확인한다." },
    };
    constexpr int kHvShutdownSteps = static_cast<int>(sizeof(kHvShutdown) / sizeof(kHvShutdown[0]));

    // 1277719 → "1,277,719"
    std::wstring WithCommas(long long value)
    {
        const std::wstring digits = std::to_wstring(value < 0 ? -value : value);
        std::wstring out;
        const size_t n = digits.size();
        for (size_t i = 0; i < n; ++i)
        {
            if (i > 0 && (n - i) % 3 == 0)
                out += L',';
            out += digits[i];
        }
        return value < 0 ? L"-" + out : out;
    }

    std::wstring PositionOf(const std::string& nodeName)   // "EV-BM-12S@03" → "03"
    {
        const size_t at = nodeName.find('@');
        return at == std::string::npos ? std::wstring() : ui::Wide(nodeName.substr(at + 1));
    }

    bool Inside(const RECT& rc, int x, int y)
    {
        return x >= rc.left && x < rc.right && y >= rc.top && y < rc.bottom;
    }
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
    ON_WM_MOUSELEAVE()
    ON_WM_MOUSEWHEEL()
    ON_WM_SETCURSOR()
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
    m_gpu = ui::Wide(m_renderer.GpuName());

    // 4) 차 모델(GLB) 불러오기. 실패해도 창은 띄우고 바닥 격자 + 가운데 알림 판을 그린다.
    CString modelProblem;
    const CString modelPath = FindModelPath();
    if (modelPath.IsEmpty())
    {
        modelProblem = _T("data 폴더나 C:\\dev\\zoomin_models 에 model.glb 를 두고 다시 실행하세요.");
        m_info += _T("  |  model.glb 없음");
    }
    else
    {
        CWaitCursor wait;   // 읽는 동안 모래시계 커서
        std::string error;
        if (m_renderer.LoadModel(std::string(CW2A(modelPath, CP_UTF8)), &error))
        {
            const Model::Stats& s = m_renderer.GetModel().GetStats();
            CString text;
            text.Format(_T("  |  부품 %d개 · 메쉬 %d개(같은 모양 묶음 %d개) · 삼각형 %lld개 · %.1f초"),
                        s.parts, s.meshNodes, s.instanceGroups, s.triangles, s.loadSeconds);
            m_info += text;
        }
        else
        {
            modelProblem = _T("파일이 깨졌거나 GLB 가 아니에요: ") + CString(CA2W(error.c_str(), CP_UTF8));
            m_info += _T("  |  모델 읽기 실패");
        }
    }
    m_modelMissing = !modelProblem.IsEmpty();

    // 5) 부품 정보·재고(화면 ②). 없어도 3D 는 그대로 동작한다 → 오른쪽 위 빨간 꼬리표로 알린다.
    {
        const CString partsPath = FindDataFile(_T("parts.json"));
        const CString stockPath = FindDataFile(_T("inventory.json"));
        std::string error;
        m_partDb.Load(std::string(CW2A(partsPath, CP_UTF8)), std::string(CW2A(stockPath, CP_UTF8)), &error);
        m_partsMissing = m_partDb.PartCount() == 0;
        m_stockMissing = m_partDb.StockCount() == 0;
        CString text;
        text.Format(_T("  |  부품 정보 %d개 · 재고 %d개"),
                    static_cast<int>(m_partDb.PartCount()), static_cast<int>(m_partDb.StockCount()));
        m_info += text;
        if (!error.empty())
            TRACE(_T("PartDB: %s\n"), static_cast<LPCTSTR>(CString(CA2W(error.c_str(), CP_UTF8))));
    }

    m_ready = true;
    if (m_modelMissing)
    {
        Upload(kMessage, ui::BuildMessage(L"model.glb 를 읽지 못했어요", static_cast<LPCWSTR>(modelProblem)));
    }
    // 7주차 3차: Windows 기본 타이머 간격(약 15.6ms)이면 SetTimer(16) 이 16ms·31ms 로 섞여 46 FPS 쯤에 머문다
    //   → 이 프로그램이 도는 동안만 1ms 로(끝날 때 되돌린다). 약 60 FPS.
    m_timerPeriod = ::timeBeginPeriod(1) == TIMERR_NOERROR;
    SetTimer(kRenderTimerId, kRenderIntervalMs, nullptr);
    m_focusUntil = ::GetTickCount64() + 3000;   // 켜자마자 S·H·V 같은 키가 3D 뷰로 오게(TakeFocusOnce)
    return 0;
}

void CGLView::TakeFocusOnce()
{
    // 대화상자는 처음 뜰 때 첫 탭 컨트롤(검색 칸·② 등)에 포커스를 주고, 창이 활성화될 때 그 포커스를 되살린다.
    // → 창이 보이고 활성화된 뒤 첫 프레임에 한 번만 3D 뷰가 가져온다. 3초 안에 못 하면(다른 창이 앞에 있는 등) 그만둔다.
    if (m_focusUntil == 0)
        return;
    if (::GetTickCount64() > m_focusUntil)
    {
        m_focusUntil = 0;
        return;
    }
    CWnd* top = GetParent();
    if (!top || !top->IsWindowVisible() || ::GetActiveWindow() != top->GetSafeHwnd())
        return;   // 아직 안 보이거나 다른 창이 앞에 있다 → 다음 프레임에 다시
    m_focusUntil = 0;
    if (::GetFocus() != m_hWnd)
        SetFocus();
}

void CGLView::OnDestroy()
{
    KillTimer(kRenderTimerId);
    if (m_timerPeriod)
        ::timeEndPeriod(1);
    m_timerPeriod = false;
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
        dc.FillSolidRect(rc, ui::kWindowBg);
        dc.SetBkMode(TRANSPARENT);
        dc.SetTextColor(RGB(255, 190, 190));
        dc.DrawText(m_info, rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }

    m_context.MakeCurrent();
    const float dt = FrameSeconds();
    m_renderer.Update(dt);   // 카메라가 목표 쪽으로 조금 움직이고, 시맨틱 줌을 다시 계산

    // 그리기 명령에 든 CPU 시간과(바로 잰다) GPU 시간(몇 프레임 뒤에 결과가 온다)
    LARGE_INTEGER before{}, after{};
    ::QueryPerformanceCounter(&before);
    m_renderer.BeginGpuTimer();
    m_renderer.Render();
    m_renderer.EndGpuTimer();
    ::QueryPerformanceCounter(&after);
    if (m_tickFrequency.QuadPart != 0)
    {
        const float ms = static_cast<float>(after.QuadPart - before.QuadPart) * 1000.0f / static_cast<float>(m_tickFrequency.QuadPart);
        m_cpuMs = (m_cpuMs <= 0.0f) ? ms : m_cpuMs * 0.9f + ms * 0.1f;
    }

    m_context.Swap();
    UpdatePreview(dt);       // 화면 ③: 고른 부품 하나(돌 때 약 30fps)
    UpdateOverlays(false);   // 3D 뷰 위 판들(바뀐 것만 다시 그림)
    UpdateHover();           // 커서 아래 부품(살짝 밝게 + 이름표)
    UpdateTitle();
}

// ------------------------------------------------------------------ 3D 뷰 위 판

void CGLView::Upload(Layer layer, const ui::OverlayImage& image)
{
    LayerState& state = m_layers[layer];
    state.width = image.width;
    state.height = image.height;
    state.inset = image.inset;
    state.hits = image.hits;
    m_context.MakeCurrent();
    m_renderer.SetOverlayImage(layer, image.Empty() ? nullptr : image.pixels.data(), image.width, image.height);
}

std::wstring CGLView::PartLabel(int node, bool vehicleOpen) const
{
    const std::vector<Model::Node>& nodes = m_renderer.GetModel().Nodes();
    if (node < 0 || node >= static_cast<int>(nodes.size()))
        return std::wstring();
    if (nodes[node].level == 0)
        return vehicleOpen ? L"차량" : L"차량 전체";
    const PartInfo* info = m_partDb.FindPart(nodes[node].name);
    if (!info)
        return ui::Wide(nodes[node].name);
    const std::wstring pos = PositionOf(nodes[node].name);
    return ui::Wide(info->name) + (pos.empty() ? L"" : L" " + pos);
}

std::vector<int> CGLView::PartChain(int part) const
{
    // 위로 올라가며 부품 노드만 모은다: 구동 모터 → 후륜 구동 유닛 → 차량, 그다음 뒤집는다
    const std::vector<Model::Node>& nodes = m_renderer.GetModel().Nodes();
    std::vector<int> chain;
    if (part < 0 || part >= static_cast<int>(nodes.size()))
        return chain;
    for (int p = nodes[part].part; p >= 0 && p < static_cast<int>(nodes.size());)
    {
        chain.push_back(p);
        const int parent = nodes[p].parent;
        p = parent >= 0 ? nodes[parent].part : -1;
    }
    std::reverse(chain.begin(), chain.end());
    return chain;
}

std::vector<int> CGLView::CurrentPath(bool* vehicleOpen) const
{
    const Model& model = m_renderer.GetModel();
    const std::vector<int>& focus = m_renderer.GetSemanticZoom().FocusPath();
    std::vector<int> path = PartChain(m_renderer.Selected());   // 고른 부품이 있으면 그 사슬
    if (path.empty())
        path = focus;                                           // 없으면 지금 펼쳐진 사슬
    const bool open = path.size() > 1 || !focus.empty();
    if (path.empty())
    {
        for (int i = 0; i < static_cast<int>(model.Nodes().size()); ++i)
        {
            if (model.Nodes()[i].isPart && model.Nodes()[i].level == 0)
            {
                path.push_back(i);   // 아무것도 없으면 '차량 전체' 하나
                break;
            }
        }
    }
    if (vehicleOpen)
        *vehicleOpen = open;
    return path;
}

void CGLView::BuildPathLayer(const std::vector<int>& nodes, bool vehicleOpen)
{
    std::vector<std::wstring> labels;
    for (int node : nodes)
        labels.push_back(PartLabel(node, vehicleOpen));
    Upload(kPath, ui::BuildPath(labels));
    m_pathNodes = nodes;
    m_pathOpen = vehicleOpen;
    m_pathBuilt = true;
}

void CGLView::BuildBadgesLayer()
{
    std::vector<ui::Badge> badges;
    m_badgeKeys.clear();
    if (m_renderer.ShellsForced())
    {
        badges.push_back({ L"속 보기 · S", ui::kAccent });
        m_badgeKeys.push_back('S');
    }
    if (m_renderer.Wireframe())
    {
        badges.push_back({ L"와이어프레임 · F", RGB(176, 136, 255) });
        m_badgeKeys.push_back('F');
    }
    if (!m_renderer.Instancing())
    {
        badges.push_back({ L"인스턴싱 끔 · I", ui::kLow });
        m_badgeKeys.push_back('I');
    }
    if (m_partsMissing)
    {
        badges.push_back({ L"parts.json 없음 (data 폴더)", ui::kOut });
        m_badgeKeys.push_back(0);
    }
    if (m_stockMissing)
    {
        badges.push_back({ L"inventory.json 없음", ui::kOut });
        m_badgeKeys.push_back(0);
    }
    Upload(kBadges, ui::BuildBadges(badges));
}

void CGLView::BuildStatsLayer()
{
    const Model::Stats& stats = m_renderer.GetModel().GetStats();
    std::vector<std::wstring> lines(5);
    wchar_t buffer[160] = {};
    const float gpu = m_renderer.GpuMilliseconds();
    if (gpu >= 0.0f)
        std::swprintf(buffer, 160, L"%.0f FPS  ·  CPU %.1f ms  ·  GPU %.1f ms", m_fps, m_cpuMs, gpu);
    else
        std::swprintf(buffer, 160, L"%.0f FPS  ·  CPU %.1f ms", m_fps, m_cpuMs);
    lines[0] = buffer;
    std::swprintf(buffer, 160, L"그리기 %d번  ·  인스턴싱 %ls (I)", m_renderer.LastDrawCalls(), m_renderer.Instancing() ? L"켬" : L"끔");
    lines[1] = buffer;
    lines[2] = L"삼각형 " + WithCommas(stats.triangles) + L"개  ·  부품 " + std::to_wstring(stats.parts) + L"개";
    lines[3] = m_gpu;
    lines[4] = m_benchText.IsEmpty() ? std::wstring(L"B = 성능 측정  ·  D = 이 표시 끄기") : std::wstring(static_cast<LPCWSTR>(m_benchText));
    const std::vector<COLORREF> colors = { ui::kText, m_renderer.Instancing() ? ui::kLink : ui::kLow, ui::kTextSub, ui::kTextDim,
                                           m_benchText.IsEmpty() ? ui::kTextDim : RGB(255, 222, 120) };
    Upload(kStats, ui::BuildStats(lines, colors));
}

void CGLView::BuildHintsLayer()
{
    m_hintsWidth = m_renderer.Width();
    m_hintsBuiltMode = m_hintMode;
    if (m_hintMode == 1)
    {
        // 단축키 전체(두 칸): 왼쪽 = 마우스·자주 쓰는 키, 오른쪽 = 나머지 키
        const std::vector<std::pair<std::wstring, std::wstring>> items = {
            { L"드래그", L"돌려 보기" }, { L"휠", L"다가가기 · 물러나기" }, { L"클릭", L"고르기 (빈 곳 = 해제)" },
            { L"더블클릭", L"그 부품 앞으로" }, { L"R", L"처음 시점" }, { L"S", L"속 보기" },
            { L"V", L"고전압 · 재고 보기" }, { L"P", L"고전압 차단 순서" }, { L"D", L"성능 표시" }, { L"B", L"성능 측정" },
            { L"F", L"와이어프레임" }, { L"I", L"인스턴싱 켬 · 끔" }, { L"G", L"바닥 격자" }, { L"H", L"안내 숨기기" },
        };
        Upload(kHints, ui::BuildHelp(L"조작 · 단축키", items, m_renderer.Width() - ui::Px(20)));
        return;
    }
    // 간단히: 자주 쓰는 3개 + V(고전압·재고 보기) + P(고전압 차단 순서) + 나머지는 H
    const std::vector<std::pair<std::wstring, std::wstring>> items = {
        { L"드래그", L"돌려 보기" }, { L"휠", L"다가가기" }, { L"클릭", L"고르기" }, { L"V", L"고전압·재고" }, { L"P", L"차단 순서" },
        { L"H", L"단축키" },
    };
    Upload(kHints, ui::BuildHints(items, m_renderer.Width() - ui::Px(20)));
}

void CGLView::BuildTooltip(int part)
{
    const std::vector<Model::Node>& nodes = m_renderer.GetModel().Nodes();
    const Model::Node& node = nodes[part];
    const PartInfo* info = m_partDb.FindPart(node.name);
    std::wstring title = info ? ui::Wide(info->name) : ui::Wide(node.name);
    std::wstring hint = L"클릭 = 정보  ·  더블클릭 = 그 앞으로";
    if (node.level == 0)
        hint = L"휠로 다가가면 안쪽 부품을 고를 수 있어요";
    if (part == m_renderer.Selected())
        hint = L"고른 부품  ·  더블클릭 = 그 앞으로";
    if (m_viewMode == 2 && node.level > 0)
    {
        // 재고 보기: 아래 줄 = 그 부품의 재고
        if (const StockInfo* stock = m_partDb.FindStock(node.name))
        {
            const StockInfo::Status status = stock->GetStatus();
            hint = std::wstring(L"재고 ") + (status == StockInfo::Status::Out ? L"품절" : (status == StockInfo::Status::Low ? L"부족" : L"충분")) +
                   L"  " + std::to_wstring(stock->stock) + L"개  ·  안전재고 " + std::to_wstring(stock->safetyStock) + L"개";
        }
        else
        {
            hint = L"재고 정보 없음";
        }
    }
    Upload(kTooltip, ui::BuildTooltip(title, ui::Wide(node.name), hint, info && info->hv));
}

// ------------------------------------------------------------------ 보기 모드(V): 고전압·재고를 3D 에 색으로

void CGLView::SetXray(bool on)
{
    if (on == m_renderer.ShellsForced())
        return;
    m_renderer.ToggleShells();
    m_hoverDirty = true;                          // 클릭 단위가 바뀐다(차 → 안쪽 부품)
    if (m_preview && m_preview->GetSafeHwnd())
        m_preview->Refresh();                     // ③ 그림도 속 보기로
}

void CGLView::SetViewMode(int mode)
{
    if (!m_ready || mode == m_viewMode || mode < 0 || mode > 2)
        return;
    if (mode != 0 && m_procStep >= 0)
        SetProcedureStep(-1);   // 고전압 차단 순서가 켜져 있으면 닫고 보기 모드로(판 자리가 같다)
    const bool wasOff = m_viewMode == 0;
    m_viewMode = mode;
    if (wasOff && mode != 0 && !m_renderer.ShellsForced())
    {
        SetXray(true);          // 안쪽 부품의 색이 보이게 속 보기를 함께 켠다
        m_modeXray = true;
    }
    if (mode == 0 && m_modeXray)
    {
        SetXray(false);         // 보기 모드가 켰던 속 보기는 되돌린다
        m_modeXray = false;
    }
    ApplyViewMode();
    BuildLegendLayer();
    if (m_hoverPart >= 0)
        BuildTooltip(m_hoverPart);
    UpdateOverlays(false);
    Invalidate(FALSE);
}

void CGLView::ApplyViewMode()
{
    const Model& model = m_renderer.GetModel();
    const std::vector<Model::Node>& nodes = model.Nodes();
    m_modeCounts[0] = m_modeCounts[1] = m_modeCounts[2] = 0;
    m_context.MakeCurrent();
    if (m_viewMode == 0 || !model.Loaded() || nodes.empty())
    {
        m_renderer.SetTints(std::vector<glm::vec4>());   // 기본 보기: 색 없음
        return;
    }
    // 부품 노드마다 색을 정한다(부모가 늘 자식보다 앞 → 앞에서부터 한 번 훑으면 부모 색을 알 수 있다)
    std::vector<glm::vec4> partTint(nodes.size(), glm::vec4(0.0f));
    std::set<std::string> hv, out, low;
    for (int i = 0; i < static_cast<int>(nodes.size()); ++i)
    {
        if (!nodes[i].isPart)
            continue;
        const std::string& name = nodes[i].name;
        if (m_viewMode == 1)
        {
            // 고전압: 고전압 부품 + 고전압 '구성품'(L2 이하) 안의 부품(구동 모터 안의 하우징·축 등).
            //   큰 묶음(L1: 배터리팩·후륜 구동 유닛)은 그 자신만 — 안의 감속기·드라이브 샤프트·트레이 같은 일반 부품은 칠하지 않는다.
            const PartInfo* info = m_partDb.FindPart(name);
            const int parentNode = nodes[i].parent;
            const int parentPart = parentNode >= 0 ? nodes[parentNode].part : -1;
            const bool inside = parentPart >= 0 && partTint[parentPart].a > 0.0f && nodes[parentPart].level >= 2;
            if (info && info->hv)
                hv.insert(PartDB::PartNumberOf(name));
            if ((info && info->hv) || inside)
                partTint[i] = kHvTint;
        }
        else if (const StockInfo* stock = m_partDb.FindStock(name))
        {
            // 재고: 그 부품 자신의 상태(품절 빨강, 부족 노랑, 충분은 색 없음)
            const StockInfo::Status status = stock->GetStatus();
            if (status == StockInfo::Status::Out)
            {
                partTint[i] = kOutTint;
                out.insert(PartDB::PartNumberOf(name));
            }
            else if (status == StockInfo::Status::Low)
            {
                partTint[i] = kLowTint;
                low.insert(PartDB::PartNumberOf(name));
            }
        }
    }
    // 메쉬 노드는 자기가 속한 부품의 색
    std::vector<glm::vec4> tints(nodes.size(), glm::vec4(0.0f));
    for (int i = 0; i < static_cast<int>(nodes.size()); ++i)
    {
        const int part = nodes[i].part;
        if (part >= 0 && part < static_cast<int>(nodes.size()))
            tints[i] = partTint[part];
    }
    m_renderer.SetTints(tints);
    m_modeCounts[0] = static_cast<int>(hv.size());
    m_modeCounts[1] = static_cast<int>(out.size());
    m_modeCounts[2] = static_cast<int>(low.size());
}

void CGLView::BuildLegendLayer()
{
    if (m_procStep >= 0)
    {
        BuildProcedureLayer();   // 고전압 차단 순서가 이 자리를 쓴다
        return;
    }
    if (m_viewMode == 1)
    {
        Upload(kLegend, ui::BuildLegend(L"고전압 보기", L"V",
                                        { { ui::kHighVoltage, L"고전압 부품 " + std::to_wstring(m_modeCounts[0]) + L"품목" } },
                                        L"작업 전에는 고전압 차단 순서(P 키)를 따른다"));
    }
    else if (m_viewMode == 2)
    {
        Upload(kLegend, ui::BuildLegend(L"재고 보기", L"V",
                                        { { ui::kOut, L"품절 " + std::to_wstring(m_modeCounts[1]) + L"품목" },
                                          { ui::kLow, L"부족 " + std::to_wstring(m_modeCounts[2]) + L"품목 (안전재고 이하)" } },
                                        L"색 없음 = 충분 · 부품에 마우스를 올리면 수량"));
    }
    else
    {
        Upload(kLegend, ui::OverlayImage());   // 기본 보기: 범례 없음
    }
}

// ------------------------------------------------------------------ 고전압 차단 순서(P): 매뉴얼의 '절차'

void CGLView::SetProcedureStep(int step)
{
    if (!m_ready || !m_renderer.GetModel().Loaded())
        return;
    step = std::min(std::max(step, -1), kHvShutdownSteps);
    if (step == m_procStep)
        return;
    if (step < 0)
    {
        // 닫기: 순서 안내가 켠 속 보기만 되돌린다(고른 부품·시점은 그대로)
        m_procStep = -1;
        if (m_procXray)
        {
            SetXray(false);
            m_procXray = false;
        }
        BuildLegendLayer();
        UpdateOverlays(false);
        Invalidate(FALSE);
        return;
    }
    if (m_procStep < 0)
    {
        // 켜기: 보기 모드(V)는 끈다. 보기 모드가 켰던 속 보기는 순서 안내가 이어받는다(껐다 켜며 깜빡이지 않게).
        if (m_viewMode != 0)
        {
            const bool modeXray = m_modeXray;
            m_modeXray = false;
            SetViewMode(0);
            m_procXray = modeXray;
        }
        if (!m_renderer.ShellsForced())
        {
            SetXray(true);   // 차체·배터리 커버 안의 부품(서비스 플러그 등)이 보이게
            m_procXray = true;
        }
    }
    m_procStep = step;
    if (step < kHvShutdownSteps && kHvShutdown[step].parts[0] == nullptr)
    {
        // 고를 부품이 없는 단계(보호구 준비): 선택을 풀고 차 전체가 보이는 처음 시점으로
        m_context.MakeCurrent();
        ApplySelection(-1);
        m_renderer.ResetView();
    }
    else if (step < kHvShutdownSteps)
    {
        // 그 단계의 부품을 고르고 그 앞으로(검색과 같다 → ② 정보·③ 그림도 그 부품)
        const Model& model = m_renderer.GetModel();
        int node = -1;
        for (const char* number : kHvShutdown[step].parts)
        {
            if (number != nullptr && (node = model.FindPart(number)) >= 0)
                break;
        }
        if (node >= 0)
        {
            m_context.MakeCurrent();
            ApplySelection(node);
            m_renderer.FlyToSelection();
        }
    }
    BuildProcedureLayer();
    if (m_hoverPart >= 0)
        BuildTooltip(m_hoverPart);
    UpdateOverlays(false);
    Invalidate(FALSE);
}

void CGLView::BuildProcedureLayer()
{
    if (m_procStep < 0)
    {
        Upload(kLegend, ui::OverlayImage());
        return;
    }
    const bool done = m_procStep >= kHvShutdownSteps;
    ui::ProcedureView view;
    view.title = L"고전압 차단 순서";
    for (const ProcedureStep& s : kHvShutdown)
        view.steps.push_back(s.title);
    view.current = m_procStep;
    view.detail = done ? L"고전압 차단 끝 — 이제 고전압 부품을 작업할 수 있다. 절연 장갑은 작업 내내 낀다. "
                         L"(기다리는 시간·기준 전압은 차종 정비 지침서를 따른다)"
                       : kHvShutdown[m_procStep].detail;
    view.note = done ? L"P · 판 누르기 = 닫기   ·   Backspace = 이전" : L"P · 판 누르기 = 다음   ·   Backspace = 이전";
    view.detailLines = 3;   // 단계를 넘겨도 판 크기가 그대로
    Upload(kLegend, ui::BuildProcedure(view));
}

void CGLView::UpdateOverlays(bool forceStats)
{
    if (!m_ready)
        return;
    const Model& model = m_renderer.GetModel();
    const bool loaded = model.Loaded();

    // 위치 경로: 고른 부품이 있으면 그 부품까지의 사슬, 없으면 펼쳐진 사슬(차량 › 배터리팩 › …). 아무것도 없으면 '차량 전체'.
    if (loaded)
    {
        bool open = false;
        const std::vector<int> path = CurrentPath(&open);
        if (!m_pathBuilt || path != m_pathNodes || open != m_pathOpen)
            BuildPathLayer(path, open);
        if (!m_renderer.GetSemanticZoom().FocusPath().empty() || m_renderer.Selected() >= 0)
            m_tipDone = true;   // 한 번 다가갔거나 골랐으면 첫 안내는 그만
    }

    // 상태 꼬리표: 켜져 있는 것이 바뀔 때만
    const unsigned state = (m_renderer.ShellsForced() ? 1u : 0u) | (m_renderer.Wireframe() ? 2u : 0u) |
                           (m_renderer.Instancing() ? 0u : 4u) | (m_partsMissing ? 8u : 0u) | (m_stockMissing ? 16u : 0u);
    if (state != m_badgeState)
    {
        m_badgeState = state;
        BuildBadgesLayer();
    }

    // 조작 안내(간단히·전체)·첫 안내
    if (m_hintMode != 2 && (m_hintsWidth != m_renderer.Width() || m_hintsBuiltMode != m_hintMode))
        BuildHintsLayer();
    if (loaded && !m_tipDone && m_layers[kTip].width == 0)
        Upload(kTip, ui::BuildTip(L"휠을 굴려 차로 다가가 보세요  ·  차체가 반투명해지며 안쪽 부품이 보여요"));

    // 성능 표시(0.25초마다)
    if (m_showOverlay && loaded)
    {
        const ULONGLONG nowMs = ::GetTickCount64();
        if (forceStats || nowMs - m_lastStatsMs >= 250)
        {
            m_lastStatsMs = nowMs;
            BuildStatsLayer();
        }
    }
    PlaceLayers();
}

void CGLView::PlaceLayers()
{
    const int W = m_renderer.Width(), H = m_renderer.Height();
    const int M = ui::Px(10);
    const bool loaded = m_renderer.GetModel().Loaded();
    auto visualW = [&](Layer l) { return m_layers[l].width - 2 * m_layers[l].inset; };
    auto visualH = [&](Layer l) { return m_layers[l].height - 2 * m_layers[l].inset; };
    auto set = [&](Layer l, int visualX, int visualY, bool visible) {
        LayerState& s = m_layers[l];
        s.x = visualX - s.inset;
        s.y = visualY - s.inset;
        s.visible = visible && s.width > 0;
        m_renderer.PlaceOverlay(l, s.x, s.y, s.visible);
    };

    set(kPath, M, M, loaded);
    const bool badges = m_layers[kBadges].width > 0;
    set(kBadges, W - M - visualW(kBadges), M, badges);
    int rightTop = M + (badges ? visualH(kBadges) + ui::Px(6) : 0);
    const bool legend = (m_viewMode != 0 || m_procStep >= 0) && loaded && m_layers[kLegend].width > 0;   // 보기 범례·차단 순서: 꼬리표 아래
    set(kLegend, W - M - visualW(kLegend), rightTop, legend);
    if (legend)
        rightTop += visualH(kLegend) + ui::Px(6);
    set(kStats, W - M - visualW(kStats), rightTop, m_showOverlay && loaded);
    const bool hints = m_hintMode != 2 && loaded;
    const int hintsTop = H - M - visualH(kHints);
    set(kHints, M, hintsTop, hints);
    const int tipBottom = hints ? hintsTop - ui::Px(10) : H - M;
    set(kTip, (W - visualW(kTip)) / 2, tipBottom - visualH(kTip), loaded && m_hintMode == 0 && !m_tipDone);   // 단축키 전체를 펼치면 첫 안내는 숨김
    set(kMessage, (W - visualW(kMessage)) / 2, (H - visualH(kMessage)) / 2, m_modelMissing);

    // 이름표: 커서 오른쪽 아래, 창 밖으로 나가면 반대쪽으로
    if (m_hoverPart >= 0 && m_mouseInside)
    {
        int x = m_mouse.x + ui::Px(16), y = m_mouse.y + ui::Px(20);
        if (x + visualW(kTooltip) > W - 4)
            x = m_mouse.x - ui::Px(10) - visualW(kTooltip);
        if (y + visualH(kTooltip) > H - 4)
            y = m_mouse.y - ui::Px(12) - visualH(kTooltip);
        set(kTooltip, std::max(x, 4), std::max(y, 4), true);
    }
    else
    {
        set(kTooltip, 0, 0, false);
    }
}

int CGLView::HitLayer(CPoint point, int* item) const
{
    const Layer clickable[] = { kBadges, kLegend, kPath };
    for (Layer l : clickable)
    {
        const LayerState& s = m_layers[l];
        if (!s.visible)
            continue;
        for (size_t i = 0; i < s.hits.size(); ++i)
        {
            if (l == kBadges && (i >= m_badgeKeys.size() || m_badgeKeys[i] == 0))
                continue;   // 누를 수 없는 꼬리표(데이터 없음 알림)
            if (Inside(s.hits[i], point.x - s.x, point.y - s.y))
            {
                if (item)
                    *item = static_cast<int>(i);
                return l;
            }
        }
    }
    return -1;
}

bool CGLView::OverPanel(CPoint point) const
{
    for (int l = 0; l < kLayerCount; ++l)
    {
        const LayerState& s = m_layers[l];
        if (l == kTooltip || !s.visible)
            continue;
        const RECT panel = { s.x + s.inset, s.y + s.inset, s.x + s.width - s.inset, s.y + s.height - s.inset };
        if (Inside(panel, point.x, point.y))
            return true;
    }
    return false;
}

// ------------------------------------------------------------------ 마우스 올리기

void CGLView::UpdateHover()
{
    if (!m_ready || !m_renderer.GetModel().Loaded())
        return;
    if (!m_mouseInside || (m_dragging && m_moved) || OverPanel(m_mouse))
    {
        SetHoverPart(-1);
        return;
    }
    // 카메라가 움직이는 동안(휠·이동 애니메이션)은 고르지 않는다: 이름표를 숨기고, 멈추면 한 번 다시 고른다.
    // '움직임' = 한 프레임에 화면에서 약 0.5픽셀 넘게(거리에 비례) — 거의 멈춘 마지막 꼬리는 멈춘 것으로 본다.
    const glm::vec3 eye = m_renderer.GetCamera().Eye();
    const glm::vec3 target = m_renderer.GetCamera().Target();
    const float tolerance = std::max(glm::length(eye - target) * 5e-4f, 1e-5f);
    const bool cameraMoved = glm::length(eye - m_hoverEye) > tolerance || glm::length(target - m_hoverTarget) > tolerance;
    m_hoverEye = eye;
    m_hoverTarget = target;
    if (cameraMoved)
    {
        SetHoverPart(-1);
        m_hoverDirty = true;
        return;
    }
    if (!m_mouseMoved && !m_hoverDirty)
        return;
    const ULONGLONG now = ::GetTickCount64();
    const bool settled = now - m_lastMouseMoveMs >= kHoverSettleMs;   // 커서가 멈췄다 → 바로
    if (!settled && now - m_lastHoverMs < kHoverIntervalMs)
        return;                                                     // 움직이는 중 → 0.12초에 한 번
    m_lastHoverMs = now;
    m_mouseMoved = false;
    m_hoverDirty = false;
    m_context.MakeCurrent();
    SetHoverPart(m_renderer.PartAt(m_mouse.x, m_mouse.y));   // 클릭하면 고를 부품과 같다
}

void CGLView::SetHoverPart(int part)
{
    if (part == m_hoverPart)
    {
        PlaceLayers();   // 같은 부품이어도 이름표는 커서를 따라간다
        return;
    }
    m_hoverPart = part;
    const std::vector<Model::Node>& nodes = m_renderer.GetModel().Nodes();
    const bool vehicle = part >= 0 && part < static_cast<int>(nodes.size()) && nodes[part].level == 0;
    m_renderer.SetHover(vehicle ? -1 : part);   // 차 전체는 밝게 하지 않는다(처음 화면에서 차가 번쩍이지 않게)
    if (part >= 0)
        BuildTooltip(part);
    PlaceLayers();
}

// ------------------------------------------------------------------ 성능 측정(B)

void CGLView::RunBenchmark()
{
    if (!m_ready || !m_renderer.GetModel().Loaded())
        return;
    m_context.MakeCurrent();

    // 1) 측정 중이라고 먼저 한 번 보여 준다(재는 동안은 화면이 멈춘다)
    m_showOverlay = true;
    m_benchText = _T("성능 측정 중… 4초쯤 화면이 멈춰요");
    SetHoverPart(-1);
    UpdateOverlays(true);
    m_renderer.Render();
    m_context.Swap();

    // 2) 같은 화면을 인스턴싱 끔 → 켬 순서로 1.5초씩 거듭 그린다(끝나면 원래 상태로 돌아온다. 판은 안 그린다)
    CWaitCursor wait;
    const GLRenderer::FrameTiming off = m_renderer.MeasureFrames(false, 1.5, 3000);
    const GLRenderer::FrameTiming on = m_renderer.MeasureFrames(true, 1.5, 3000);
    const double fpsOff = 1000.0 / off.msPerFrame;
    const double fpsOn = 1000.0 / on.msPerFrame;
    m_benchText.Format(_T("측정: 끔 %.0f FPS(%.2f ms) · 켬 %.0f FPS(%.2f ms) · 속도 %.2f배"),
                       fpsOff, off.msPerFrame, fpsOn, on.msPerFrame, fpsOn / fpsOff);
    if (SaveBenchmark(off, on))
        m_benchText += _T(" · benchmark.csv");

    FrameSeconds();          // 멈춰 있던 시간을 다음 프레임 시간에 넣지 않는다
    UpdateOverlays(true);
    Invalidate(FALSE);
}

bool CGLView::SaveBenchmark(const GLRenderer::FrameTiming& off, const GLRenderer::FrameTiming& on)
{
    // 실행할 때의 현재 폴더(Visual Studio 에서 F5 = 프로젝트 폴더)에 한 번에 두 줄씩 덧붙인다
    const CString path = _T("benchmark.csv");
    const bool isNew = ::GetFileAttributes(path) == INVALID_FILE_ATTRIBUTES;
    FILE* file = nullptr;
    if (_tfopen_s(&file, path, _T("ab")) != 0 || !file)
        return false;

    SYSTEMTIME now = {};
    ::GetLocalTime(&now);
    CString when;
    when.Format(_T("%04d-%02d-%02d %02d:%02d:%02d"), now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    CString view = MakeStatusText();
    view.Replace(_T("\""), _T("\"\""));
    CString gpu(CA2W(m_renderer.GpuName().c_str(), CP_UTF8));
    gpu.Replace(_T("\""), _T("\"\""));
    const Model::Stats& stats = m_renderer.GetModel().GetStats();

    CString text;
    if (isNew)
        text = _T("시각,화면,3D 뷰 크기,인스턴싱,측정 프레임,한 장 평균(ms),FPS 환산,그리기 호출,삼각형,그래픽\r\n");
    const GLRenderer::FrameTiming* rows[2] = { &off, &on };
    for (const GLRenderer::FrameTiming* t : rows)
    {
        CString row;
        row.Format(_T("%s,\"%s\",%dx%d,%s,%d,%.3f,%.1f,%d,%lld,\"%s\"\r\n"),
                   static_cast<LPCTSTR>(when), static_cast<LPCTSTR>(view), m_renderer.Width(), m_renderer.Height(),
                   t->instanced ? _T("켬") : _T("끔"), t->frames, t->msPerFrame, 1000.0 / t->msPerFrame,
                   t->drawCalls, stats.triangles, static_cast<LPCTSTR>(gpu));
        text += row;
    }
    CW2A utf8(text, CP_UTF8);
    const char* bytes = utf8;
    if (isNew)
        std::fwrite("\xEF\xBB\xBF", 1, 3, file);   // 엑셀이 한글을 UTF-8 로 읽게(BOM)
    std::fwrite(bytes, 1, std::strlen(bytes), file);
    std::fclose(file);
    return true;
}

// ------------------------------------------------------------------ 화면 ③ · 검색

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
        m_preview->ShowPart(-1, CString(), CString(), false);
        return;
    }
    // 위 띠: 이름(굵게) + 품번(파랑), 고전압이면 주황 꼬리표
    const CString number(CA2W(nodes[part].name.c_str(), CP_UTF8));
    CString name;
    bool highVoltage = false;
    if (const PartInfo* info = m_partDb.FindPart(nodes[part].name))
    {
        name = CString(CA2W(info->name.c_str(), CP_UTF8));
        highVoltage = info->hv;
    }
    m_preview->ShowPart(part, number, name, highVoltage);
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
        SetPreviewStatus(_T("품번·이름·증상을 넣고 엔터 (예: 모듈 03, 끽 소리)"));
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

// ------------------------------------------------------------------ 상태 글자 · 제목

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
    const std::vector<int>& path = m_renderer.GetSemanticZoom().FocusPath();

    // 펼쳐진 부품 사슬: 차량 → 배터리팩 → 모듈 … (benchmark.csv 의 '화면' 칸)
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
    return text;
}

void CGLView::UpdateTitle()
{
    // 제목은 짧게: "줌인 매뉴얼 — 지금 위치(가장 안쪽에 펼쳐진 부품)". 숫자·OpenGL 정보는 성능 표시(D)로 옮겼다.
    CString title = _T("줌인 매뉴얼");
    if (m_renderer.GetModel().Loaded())
    {
        const std::vector<int> path = CurrentPath(nullptr);   // 위치 경로의 마지막(고른 부품, 없으면 가장 안쪽 펼친 부품)
        if (!path.empty() && m_renderer.GetModel().Nodes()[path.back()].level > 0)
            title += _T(" — ") + CString(PartLabel(path.back()).c_str());
    }
    if (title == m_title)
        return;
    m_title = title;
    if (CWnd* parent = GetParent())
        parent->SetWindowText(m_title);
}

// ------------------------------------------------------------------ 창 메시지

BOOL CGLView::OnEraseBkgnd(CDC* /*pDC*/)
{
    return TRUE;   // 배경을 지우지 않는다(깜빡임 방지). 어차피 OpenGL이 전부 다시 그린다.
}

void CGLView::OnSize(UINT nType, int cx, int cy)
{
    CWnd::OnSize(nType, cx, cy);
    if (m_ready)
    {
        m_renderer.Resize(cx, cy);
        PlaceLayers();
    }
}

void CGLView::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == kRenderTimerId)
    {
        TakeFocusOnce();     // 켜진 뒤 한 번: 키보드를 3D 뷰로
        Invalidate(FALSE);   // 다음 OnPaint에서 다시 그림
    }
    CWnd::OnTimer(nIDEvent);
}

void CGLView::OnLButtonDown(UINT nFlags, CPoint point)
{
    SetFocus();     // 키보드(S, D, G, R, F, Z, X)와 휠을 이 창이 받도록
    int item = -1;
    const int layer = m_ready ? HitLayer(point, &item) : -1;
    if (layer == kPath && item >= 0 && item < static_cast<int>(m_pathNodes.size()))
    {
        const int node = m_pathNodes[item];
        const bool last = item + 1 == static_cast<int>(m_pathNodes.size());
        if (last && node == m_renderer.Selected() && m_renderer.GetModel().Nodes()[node].level > 0)
            m_renderer.FlyToSelection();   // 마지막 = 고른 부품 → 그 부품 앞으로(더블클릭과 같다)
        else
            m_renderer.FlyInto(node);      // 그 위 단계 → 그 단계 안이 보이는 거리로(선택은 그대로)
        m_overlayClick = true;
        Invalidate(FALSE);
        return;
    }
    if (layer == kLegend)
    {
        if (m_procStep >= 0)            // 차단 순서: × = 닫기, 판 = 다음 단계(다 했으면 닫기)
            SetProcedureStep(item == 0 || m_procStep >= kHvShutdownSteps ? -1 : m_procStep + 1);
        else
            SetViewMode(0);             // 범례를 누르면 기본 보기로
        m_overlayClick = true;
        Invalidate(FALSE);
        return;
    }
    if (layer == kBadges && item >= 0 && item < static_cast<int>(m_badgeKeys.size()))
    {
        HandleKey(m_badgeKeys[item]);   // 꼬리표 = 그 키를 한 번 더 누른 것(끄기)
        m_overlayClick = true;
        Invalidate(FALSE);
        return;
    }
    m_overlayClick = false;
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
        if (click && m_ready && !OverPanel(point))
            SelectAt(point);           // 안내 판(단축키 등) 위를 누른 것은 그 아래 부품을 고르지 않는다
        m_hoverDirty = true;          // 돌린 뒤에는 커서 아래 부품을 다시 고른다
    }
    m_overlayClick = false;
    CWnd::OnLButtonUp(nFlags, point);
}

void CGLView::OnLButtonDblClk(UINT nFlags, CPoint point)
{
    // 더블클릭: 그 부품을 고르고 그 부품이 화면을 채우는 거리로 이동한다(판 위는 무시).
    if (m_ready && !OverPanel(point))
    {
        SelectAt(point);
        m_renderer.FlyToSelection();
        Invalidate(FALSE);
    }
    CWnd::OnLButtonDblClk(nFlags, point);
}

void CGLView::OnMouseMove(UINT nFlags, CPoint point)
{
    if (!m_trackingLeave)
    {
        TRACKMOUSEEVENT track = { sizeof(track), TME_LEAVE, m_hWnd, 0 };
        m_trackingLeave = ::TrackMouseEvent(&track) != FALSE;
    }
    m_mouseInside = true;
    if (point != m_mouse)
    {
        m_mouse = point;
        m_mouseMoved = true;
        m_lastMouseMoveMs = ::GetTickCount64();
    }
    if (m_dragging && m_ready)
    {
        // 4px 넘게 움직여야 회전을 시작한다(클릭할 때 손이 조금 떨려도 화면이 안 돌게).
        if (!m_moved && std::abs(point.x - m_downPoint.x) + std::abs(point.y - m_downPoint.y) > 4)
        {
            m_moved = true;
            SetHoverPart(-1);   // 돌리는 동안은 이름표를 숨긴다
        }
        if (m_moved)
        {
            m_renderer.Orbit(static_cast<float>(point.x - m_lastPoint.x),
                             static_cast<float>(point.y - m_lastPoint.y));
            m_lastPoint = point;
            Invalidate(FALSE);
        }
    }
    else if (m_ready && m_hoverPart >= 0)
    {
        PlaceLayers();   // 이름표가 커서를 따라간다
    }
    CWnd::OnMouseMove(nFlags, point);
}

void CGLView::OnMouseLeave()
{
    m_trackingLeave = false;
    m_mouseInside = false;
    if (m_ready)
        SetHoverPart(-1);
    CWnd::OnMouseLeave();
}

BOOL CGLView::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
{
    if (nHitTest == HTCLIENT && m_ready)
    {
        CPoint point;
        ::GetCursorPos(&point);
        ScreenToClient(&point);
        if (HitLayer(point, nullptr) >= 0)
        {
            ::SetCursor(::LoadCursor(nullptr, IDC_HAND));   // 누를 수 있는 판(위치 경로·꼬리표) 위
            return TRUE;
        }
    }
    return CWnd::OnSetCursor(pWnd, nHitTest, message);
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
    if (m_hoverPart >= 0)
        BuildTooltip(m_hoverPart);  // 이름표의 안내 줄('고른 부품')
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

BOOL CGLView::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
    // 휠 메시지는 키보드 포커스가 있는 창으로 온다. 커서가 3D 뷰 밖(② 카드 등)에 있으면 그 창으로 넘긴다.
    CRect window;
    GetWindowRect(&window);
    if (!window.PtInRect(pt))
    {
        HWND target = ::WindowFromPoint(pt);
        CWnd* parent = GetParent();
        if (target && target != m_hWnd && parent && ::IsChild(parent->GetSafeHwnd(), target))
            ::SendMessage(target, WM_MOUSEWHEEL, MAKEWPARAM(nFlags, static_cast<WORD>(zDelta)), MAKELPARAM(pt.x, pt.y));
        return TRUE;
    }
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

bool CGLView::HandleKey(UINT key)
{
    switch (key)
    {
    case 'F': m_renderer.ToggleWireframe(); break;   // GL_ENC의 f 키
    case 'R': m_renderer.ResetView();       break;   // Texture_Mapping의 R 키
    case 'Z': m_renderer.Zoom(+120);        break;   // GL_ENC의 z 키(확대)
    case 'X': m_renderer.Zoom(-120);        break;   // GL_ENC의 x 키(축소)
    case 'G': m_renderer.ToggleGrid();      break;   // 바닥 격자 켜기/끄기
    case 'S':                                         // 거리와 상관없이 겉껍데기 반투명(속 보기)
        SetXray(!m_renderer.ShellsForced());
        break;
    case 'V':                                         // 보기 모드: 기본 → 고전압 → 재고 → 기본
        SetViewMode((m_viewMode + 1) % 3);
        break;
    case 'P':                                         // 고전압 차단 순서: 시작 → 다음 단계 … → 다 했음 → 닫기
        SetProcedureStep(m_procStep >= kHvShutdownSteps ? -1 : m_procStep + 1);
        break;
    case VK_BACK:                                     // 차단 순서의 이전 단계
        if (m_procStep <= 0)
            return false;
        SetProcedureStep(m_procStep - 1);
        break;
    case 'D':                                         // 성능 표시 켬/끔
        m_showOverlay = !m_showOverlay;
        UpdateOverlays(true);
        break;
    case 'H':                                         // 조작 안내: 간단히 → 단축키 전체 → 숨김 → 간단히
        m_hintMode = (m_hintMode + 1) % 3;
        UpdateOverlays(false);                        // 모양이 바뀌면 다시 그리고 자리를 맞춘다
        break;
    case 'I':                                         // 인스턴싱 켬/끔(같은 모양 부품을 한 번에 그리기) — 전후 비교
        m_renderer.SetInstancing(!m_renderer.Instancing());
        m_lastStatsMs = 0;                            // 새 그리기 호출 수로 바로 고친다
        break;
    case 'B':                                         // 성능 측정(인스턴싱 끔·켬, 약 4초)
        RunBenchmark();
        break;
    default:
        return false;
    }
    return true;
}

void CGLView::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    if (m_ready)
    {
        HandleKey(nChar);
        Invalidate(FALSE);
    }
    CWnd::OnKeyDown(nChar, nRepCnt, nFlags);
}

UINT CGLView::OnGetDlgCode()
{
    // 대화상자 안에서도 모든 키 입력을 이 창이 받는다.
    return DLGC_WANTALLKEYS | DLGC_WANTARROWS | DLGC_WANTCHARS;
}
