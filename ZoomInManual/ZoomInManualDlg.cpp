// ZoomInManualDlg.cpp: 구현 파일
//

#include "pch.h"
#include "framework.h"
#include "ZoomInManual.h"
#include "ZoomInManualDlg.h"
#include "afxdialogex.h"
#include <windowsx.h>   // GET_X_LPARAM, GET_Y_LPARAM

namespace
{
	constexpr UINT kGlViewId = 5000;   // 3D 뷰 창의 컨트롤 ID(다른 ID와 안 겹치는 아무 숫자)
}


#ifdef _DEBUG
#define new DEBUG_NEW
#endif


// 응용 프로그램 정보에 사용되는 CAboutDlg 대화 상자입니다.

class CAboutDlg : public CDialogEx
{
public:
	CAboutDlg();

	// 대화 상자 데이터입니다.
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_ABOUTBOX };
#endif

protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV 지원입니다.

	// 구현입니다.
protected:
	DECLARE_MESSAGE_MAP()
};

CAboutDlg::CAboutDlg() : CDialogEx(IDD_ABOUTBOX)
{
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialogEx)
END_MESSAGE_MAP()


// CZoomInManualDlg 대화 상자



CZoomInManualDlg::CZoomInManualDlg(CWnd* pParent /*=nullptr*/)
	: CDialogEx(IDD_ZOOMINMANUAL_DIALOG, pParent)
{
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

void CZoomInManualDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CZoomInManualDlg, CDialogEx)
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_WM_SIZE()
	ON_MESSAGE(WM_ZOOMIN_PART_SELECTED, &CZoomInManualDlg::OnPartSelected)
	ON_BN_CLICKED(IDC_BTN_SEARCH, &CZoomInManualDlg::OnBnClickedSearch)
END_MESSAGE_MAP()


// CZoomInManualDlg 메시지 처리기

BOOL CZoomInManualDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	// 시스템 메뉴에 "정보..." 메뉴 항목을 추가합니다.

	// IDM_ABOUTBOX는 시스템 명령 범위에 있어야 합니다.
	ASSERT((IDM_ABOUTBOX & 0xFFF0) == IDM_ABOUTBOX);
	ASSERT(IDM_ABOUTBOX < 0xF000);

	CMenu* pSysMenu = GetSystemMenu(FALSE);
	if (pSysMenu != nullptr)
	{
		BOOL bNameValid;
		CString strAboutMenu;
		bNameValid = strAboutMenu.LoadString(IDS_ABOUTBOX);
		ASSERT(bNameValid);
		if (!strAboutMenu.IsEmpty())
		{
			pSysMenu->AppendMenu(MF_SEPARATOR);
			pSysMenu->AppendMenu(MF_STRING, IDM_ABOUTBOX, strAboutMenu);
		}
	}

	// 이 대화 상자의 아이콘을 설정합니다.  응용 프로그램의 주 창이 대화 상자가 아닐 경우에는
	//  프레임워크가 이 작업을 자동으로 수행합니다.
	SetIcon(m_hIcon, TRUE);			// 큰 아이콘을 설정합니다.
	SetIcon(m_hIcon, FALSE);		// 작은 아이콘을 설정합니다.

	// TODO: 여기에 추가 초기화 작업을 추가합니다.

	// 창 크기 조절 테두리·최대화 버튼을 켜고,
	// 대화상자가 자식 창(3D 뷰) 위를 덮어 그리지 않게 한다(깜빡임 방지).
	ModifyStyle(0, WS_THICKFRAME | WS_MAXIMIZEBOX | WS_CLIPCHILDREN, SWP_FRAMECHANGED);

	// 화면 ①: 리소스 편집기에 그려 둔 IDC_GL_AREA 자리에 OpenGL 창을 만든다.
	if (CWnd* area = GetDlgItem(IDC_GL_AREA))
	{
		area->ShowWindow(SW_HIDE);   // 자리 표시용 Picture Control은 숨긴다
		CRect rc;
		area->GetWindowRect(&rc);
		ScreenToClient(&rc);
		m_glView.Create(this, rc, kGlViewId);
		SetWindowText(_T("줌인 매뉴얼 - ") + m_glView.GetGlInfo());
	}

	m_infoList.SubclassDlgItem(IDC_LIST_INFO, this);
	m_infoList.Setup();

	m_preview.CreateInPanel(this, IDC_EDIT_SEARCH, IDC_BTN_SEARCH, IDC_TREE_PARTS);
	m_glView.AttachPreview(&m_preview);

	return TRUE;  // 포커스를 컨트롤에 설정하지 않으면 TRUE를 반환합니다.
}

void CZoomInManualDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) == IDM_ABOUTBOX)
	{
		CAboutDlg dlgAbout;
		dlgAbout.DoModal();
	}
	else
	{
		CDialogEx::OnSysCommand(nID, lParam);
	}
}

// 대화 상자에 최소화 단추를 추가할 경우 아이콘을 그리려면
//  아래 코드가 필요합니다.  문서/뷰 모델을 사용하는 MFC 애플리케이션의 경우에는
//  프레임워크에서 이 작업을 자동으로 수행합니다.

void CZoomInManualDlg::OnPaint()
{
	if (IsIconic())
	{
		CPaintDC dc(this); // 그리기를 위한 디바이스 컨텍스트입니다.

		SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

		// 클라이언트 사각형에서 아이콘을 가운데에 맞춥니다.
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		// 아이콘을 그립니다.
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CDialogEx::OnPaint();
	}
}

// 사용자가 최소화된 창을 끄는 동안에 커서가 표시되도록 시스템에서
//  이 함수를 호출합니다.
HCURSOR CZoomInManualDlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}


void CZoomInManualDlg::LayoutGlView()
{
	CWnd* area = GetDlgItem(IDC_GL_AREA);
	if (!area || !m_glView.GetSafeHwnd())
		return;
	CRect rc;
	area->GetWindowRect(&rc);
	ScreenToClient(&rc);
	m_glView.MoveWindow(rc);
}

void CZoomInManualDlg::OnSize(UINT nType, int cx, int cy)
{
	CDialogEx::OnSize(nType, cx, cy);
	LayoutGlView();
	m_preview.Relayout();   // ③ 칸(부품 이미지·검색 칸·[검색] 단추)을 ③ 그룹 상자의 새 자리에 맞춘다
}

BOOL CZoomInManualDlg::PreTranslateMessage(MSG* pMsg)
{
	// 엔터·ESC: 검색 칸에서 엔터 = [검색] 단추, 그 밖의 엔터·ESC 로는 창이 닫히지 않게(닫기는 X 버튼)
	if (pMsg->message == WM_KEYDOWN && (pMsg->wParam == VK_RETURN || pMsg->wParam == VK_ESCAPE))
	{
		if (pMsg->wParam == VK_RETURN && ::GetFocus() == ::GetDlgItem(m_hWnd, IDC_EDIT_SEARCH))
			RunSearch();          // 검색 칸에서 엔터 = [검색] 단추
		return TRUE;              // 그 밖의 엔터·ESC 로는 창이 닫히지 않게
	}

	// 마우스 휠은 '키보드 포커스가 있는 창'으로 가기 때문에,
	// 커서가 3D 뷰 위에 있으면 3D 뷰로 직접 보내 준다.
	if (pMsg->message == WM_MOUSEWHEEL && m_glView.GetSafeHwnd())
	{
		CPoint pt(GET_X_LPARAM(pMsg->lParam), GET_Y_LPARAM(pMsg->lParam));   // 화면 좌표
		CRect rc;
		m_glView.GetWindowRect(&rc);
		if (rc.PtInRect(pt))
		{
			m_glView.SendMessage(WM_MOUSEWHEEL, pMsg->wParam, pMsg->lParam);
			return TRUE;
		}
	}
	return CDialogEx::PreTranslateMessage(pMsg);
}

LRESULT CZoomInManualDlg::OnPartSelected(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
	m_infoList.ShowPart(m_glView.GetSelectedPart(), m_glView.GetPartDB());
	return 0;
}

void CZoomInManualDlg::OnBnClickedSearch()
{
	RunSearch();
}

void CZoomInManualDlg::RunSearch()
{
	CString query;
	GetDlgItemText(IDC_EDIT_SEARCH, query);
	m_glView.SearchNext(query);   // 맞는 부품으로 이동(같은 검색어로 다시 하면 다음 부품). 결과는 ③ 맨 아래 한 줄에
}