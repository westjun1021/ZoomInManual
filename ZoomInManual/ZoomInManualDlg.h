
// ZoomInManualDlg.h: 헤더 파일
//

#pragma once
#include "GLView.h"

// CZoomInManualDlg 대화 상자
class CZoomInManualDlg : public CDialogEx
{
// 생성입니다.
public:
	CZoomInManualDlg(CWnd* pParent = nullptr);	// 표준 생성자입니다.

// 대화 상자 데이터입니다.
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_ZOOMINMANUAL_DIALOG };
#endif

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV 지원입니다.


// 구현입니다.
protected:
	HICON m_hIcon;

	// 생성된 메시지 맵 함수
	virtual BOOL OnInitDialog();
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	CGLView m_glView;                 // 화면 ①: OpenGL 3D 뷰
	void LayoutGlView();              // 창 크기가 바뀌면 3D 뷰 위치를 맞춤
	afx_msg void OnSize(UINT nType, int cx, int cy);
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	DECLARE_MESSAGE_MAP()
public:
	afx_msg void OnEnChangeEditSearch();
	afx_msg void OnLvnItemchangedListInfo(NMHDR* pNMHDR, LRESULT* pResult);
};
