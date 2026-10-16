// InfoList.cpp
#include "pch.h"
#include "InfoList.h"

namespace
{
    // JSON 의 글자는 UTF-8 → 화면(유니코드)으로 바꿀 때 CP_UTF8 을 꼭 넣는다(안 넣으면 한글이 깨진다).
    CString Wide(const std::string& utf8)
    {
        return CString(CA2W(utf8.c_str(), CP_UTF8));
    }

    // 1250000 → "1,250,000원"
    CString Won(long long value)
    {
        CString digits;
        digits.Format(_T("%lld"), value < 0 ? -value : value);
        CString out;
        const int count = digits.GetLength();
        for (int i = 0; i < count; ++i)
        {
            out += digits[i];
            const int left = count - 1 - i;
            if (left > 0 && left % 3 == 0)
                out += _T(',');
        }
        return CString(value < 0 ? _T("-") : _T("")) + out + _T("원");
    }

    // "2026-09-14" → "2026. 09. 14." (교수님 그림의 날짜 모양). 모양이 다르면 그대로.
    CString DotDate(const std::string& iso)
    {
        if (iso.size() == 10 && iso[4] == '-' && iso[7] == '-')
            return Wide(iso.substr(0, 4) + ". " + iso.substr(5, 2) + ". " + iso.substr(8, 2) + ".");
        return Wide(iso);
    }

    // '항목' 열에 들어가는 가장 긴 이름들. 열 너비는 이 글자들의 실제 폭으로 정한다(글꼴·화면 배율이 달라도 안 잘리게).
    const LPCTSTR kLongKeys[] = { _T("고전압 주의"), _T("분류·단계"), _T("상위 부품"), _T("하위 부품"),
                                  _T("입고 기간"), _T("3D 모델"), _T("정격 전압"), _T("공칭 전압") };
}

BEGIN_MESSAGE_MAP(CInfoList, CListCtrl)
    ON_NOTIFY_REFLECT(NM_CUSTOMDRAW, &CInfoList::OnCustomDraw)
END_MESSAGE_MAP()

void CInfoList::Setup()
{
    ModifyStyle(LVS_TYPEMASK, LVS_REPORT | LVS_SHOWSELALWAYS);   // 리소스에서 Report 로 안 바꿨어도 Report 로
    SetExtendedStyle(GetExtendedStyle() | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    while (DeleteColumn(0))
    {
    }
    // 굵은 글씨(경고 줄)도 들어가게 가장 긴 이름 폭 × 1.15 + 여백
    int keyWidth = 72;
    for (LPCTSTR key : kLongKeys)
    {
        const int width = GetStringWidth(key) * 115 / 100 + 24;
        if (width > keyWidth)
            keyWidth = width;
    }
    CRect rc;
    GetClientRect(&rc);
    int valueWidth = rc.Width() - keyWidth - ::GetSystemMetrics(SM_CXVSCROLL);
    if (valueWidth < 120)
        valueWidth = 120;
    InsertColumn(0, _T("항목"), LVCFMT_LEFT, keyWidth);
    InsertColumn(1, _T("내용"), LVCFMT_LEFT, valueWidth);

    if (CFont* font = GetFont())
    {
        LOGFONT lf = {};
        font->GetLogFont(&lf);
        lf.lfWeight = FW_BOLD;
        m_boldFont.DeleteObject();
        m_boldFont.CreateFontIndirect(&lf);
    }
    ShowHint();
}

void CInfoList::ShowHint()
{
    SetRedraw(FALSE);
    DeleteAllItems();
    AddWrapped(_T("사용법"), _T("3D 뷰에서 부품을 클릭하세요."), Hint);
    AddWrapped(_T(""), _T("휠로 다가가 차체·커버가 반투명해지면 안쪽 부품을 고를 수 있어요."), Hint);
    AddWrapped(_T(""), _T("더블클릭하면 그 부품 앞으로 이동해요."), Hint);
    SetRedraw(TRUE);
    Invalidate();
}

void CInfoList::ShowPart(const SelectedPart& part, const PartDB& db)
{
    if (part.node < 0)
    {
        ShowHint();
        return;
    }
    SetRedraw(FALSE);
    DeleteAllItems();

    const PartInfo* info = db.FindPart(part.name);
    const StockInfo* stock = db.FindStock(part.name);
    const CString number = Wide(PartDB::PartNumberOf(part.name));
    const CString nodeName = Wide(part.name);

    // 고전압 부품: 맨 위 빨간 띠
    if (info && info->hv)
        AddWrapped(_T("고전압 주의"), _T("서비스 플러그를 분리하고 절연 장갑을 낀 뒤 작업하세요."), Warning);

    // 품번(같은 품번이 여러 개면 위치도)
    const int at = nodeName.Find(_T('@'));
    if (at >= 0)
        AddRow(_T("품번"), number + _T("   (위치 ") + nodeName.Mid(at + 1) + _T(")"));
    else
        AddRow(_T("품번"), number);

    if (info)
    {
        AddWrapped(_T("이름"), Wide(info->name));
        CString kind;
        kind.Format(_T("%s · L%d"), static_cast<LPCTSTR>(Wide(info->category)), info->level >= 0 ? info->level : part.level);
        AddRow(_T("분류·단계"), kind);
    }
    else
    {
        AddWrapped(_T("이름"), _T("parts.json 에 이 품번이 없어요."), Hint);
    }

    if (!part.parentName.empty())
    {
        CString parent = Wide(PartDB::PartNumberOf(part.parentName));
        if (const PartInfo* parentInfo = db.FindPart(part.parentName))
            parent += _T("  ") + Wide(parentInfo->name);
        AddWrapped(_T("상위 부품"), parent);
    }
    if (part.childParts > 0)
    {
        CString children;
        children.Format(_T("%d개"), part.childParts);
        AddRow(_T("하위 부품"), children);
    }
    if (info && !info->desc.empty())
        AddWrapped(_T("설명"), Wide(info->desc));

    if (info && !info->specs.empty())
    {
        AddRow(_T("사양"), CString(), Section);
        for (const auto& spec : info->specs)
            AddWrapped(Wide(spec.first), Wide(spec.second));
    }
    if (info && !info->cautions.empty())
    {
        AddRow(_T("주의사항"), CString(), Section);
        for (size_t i = 0; i < info->cautions.size(); ++i)
        {
            CString no;
            no.Format(_T("%d"), static_cast<int>(i) + 1);
            AddWrapped(no, Wide(info->cautions[i]));
        }
    }

    // 재고: 0 이면 품절(빨강), 안전재고 미만이면 부족(주황), 그 외 충분(초록)
    AddRow(_T("재고"), CString(), Section);
    if (!stock)
    {
        AddRow(_T("상태"), _T("재고 정보 없음"), Hint);
    }
    else
    {
        CString state;
        RowStyle style = StockOk;
        switch (stock->GetStatus())
        {
        case StockInfo::Status::Out: state = _T("품절"); style = StockOut; break;
        case StockInfo::Status::Low: state = _T("부족"); style = StockLow; break;
        default:                     state = _T("충분"); style = StockOk;  break;
        }
        CString line;
        line.Format(_T("%s   %d개 (안전재고 %d개)"), static_cast<LPCTSTR>(state), stock->stock, stock->safetyStock);
        AddRow(_T("상태"), line, style);
        AddRow(_T("위치"), Wide(stock->location));
        // 5주차(새 안): 교수님 그림의 담당자·공급사·공급일자
        if (!stock->manager.empty())
        {
            CString manager = Wide(stock->manager);
            if (!stock->phone.empty())
                manager += _T(" (") + Wide(stock->phone) + _T(")");
            AddRow(_T("담당자"), manager);
        }
        if (!stock->supplier.empty())
            AddWrapped(_T("공급사"), Wide(stock->supplier));
        if (!stock->supplyDate.empty())
            AddRow(_T("공급일자"), DotDate(stock->supplyDate));
        AddRow(_T("단가"), Won(stock->price));
        CString lead;
        lead.Format(_T("%d일"), stock->leadTimeDays);
        AddRow(_T("입고 기간"), lead);
    }
    if (info && !info->source.empty())
        AddWrapped(_T("3D 모델"), Wide(info->source));

    SetRedraw(TRUE);
    Invalidate();
}

void CInfoList::AddRow(const CString& key, const CString& value, RowStyle style)
{
    const int row = InsertItem(GetItemCount(), key);
    SetItemText(row, 1, value);
    SetItemData(row, static_cast<DWORD_PTR>(style));
}

void CInfoList::AddWrapped(const CString& key, const CString& value, RowStyle style)
{
    // '내용' 열 너비에 들어가는 만큼씩 잘라 여러 줄로. 띄어쓰기가 있으면 거기서 끊는다.
    const int width = GetColumnWidth(1) - 14;
    CString rest = value;
    rest.Trim();
    if (rest.IsEmpty())
    {
        AddRow(key, CString(), style);
        return;
    }
    bool first = true;
    while (!rest.IsEmpty())
    {
        int fit = rest.GetLength();
        while (fit > 1 && GetStringWidth(rest.Left(fit)) > width)
            --fit;
        if (fit < rest.GetLength())
        {
            const int space = rest.Left(fit + 1).ReverseFind(_T(' '));
            if (space > 0)
                fit = space;
        }
        CString line = rest.Left(fit);
        line.TrimRight();
        AddRow(first ? key : CString(), line, style);
        rest = rest.Mid(fit);
        rest.TrimLeft();
        first = false;
    }
}

void CInfoList::OnCustomDraw(NMHDR* pNMHDR, LRESULT* pResult)
{
    NMLVCUSTOMDRAW* draw = reinterpret_cast<NMLVCUSTOMDRAW*>(pNMHDR);
    *pResult = CDRF_DODEFAULT;
    if (draw->nmcd.dwDrawStage == CDDS_PREPAINT)
    {
        *pResult = CDRF_NOTIFYITEMDRAW;   // 줄마다 색을 정하겠다고 알린다
        return;
    }
    if (draw->nmcd.dwDrawStage != CDDS_ITEMPREPAINT)
        return;

    const bool bold = m_boldFont.GetSafeHandle() != nullptr;
    switch (static_cast<RowStyle>(draw->nmcd.lItemlParam))
    {
    case Warning:
        draw->clrText = RGB(255, 255, 255);
        draw->clrTextBk = RGB(200, 32, 32);
        if (bold)
        {
            ::SelectObject(draw->nmcd.hdc, m_boldFont.GetSafeHandle());
            *pResult = CDRF_NEWFONT;
        }
        break;
    case Section:
        draw->clrText = RGB(40, 40, 40);
        draw->clrTextBk = RGB(228, 232, 240);
        if (bold)
        {
            ::SelectObject(draw->nmcd.hdc, m_boldFont.GetSafeHandle());
            *pResult = CDRF_NEWFONT;
        }
        break;
    case StockOut: draw->clrText = RGB(210, 30, 30);   break;
    case StockLow: draw->clrText = RGB(225, 120, 0);   break;
    case StockOk:  draw->clrText = RGB(20, 140, 60);   break;
    case Hint:     draw->clrText = RGB(110, 110, 110); break;
    default: break;
    }
}
