// InfoList.h
// 화면 ②: 부품 정보·재고를 보여 주는 리스트(리소스 편집기에 그려 둔 IDC_LIST_INFO, 보기 = Report).
// 열 2개(항목 | 내용). 고전압 부품은 맨 위에 빨간 경고 띠, 재고 상태는 글자색(품절 빨강 · 부족 주황 · 충분 초록).
// 긴 설명·주의사항은 칸 너비에 맞춰 여러 줄로 나눈다.
//
// 대화상자에서 쓰는 법(README 의 [1]-4 참고):
//   OnInitDialog : m_infoList.SubclassDlgItem(IDC_LIST_INFO, this);  m_infoList.Setup();
//   선택 알림    : m_infoList.ShowPart(m_glView.GetSelectedPart(), m_glView.GetPartDB());
#pragma once
#include "GLRenderer.h"
#include "PartDB.h"

class CInfoList : public CListCtrl
{
public:
    void Setup();                                              // 열·스타일을 만들고 안내 글을 띄운다
    void ShowPart(const SelectedPart& part, const PartDB& db);  // part.node < 0 이면 안내 글
    void ShowHint();

protected:
    afx_msg void OnCustomDraw(NMHDR* pNMHDR, LRESULT* pResult);
    DECLARE_MESSAGE_MAP()

private:
    enum RowStyle { Normal = 0, Warning, Section, StockOut, StockLow, StockOk, Hint };

    void AddRow(const CString& key, const CString& value, RowStyle style = Normal);
    void AddWrapped(const CString& key, const CString& value, RowStyle style = Normal);   // 길면 여러 줄로

    CFont m_boldFont;
};
