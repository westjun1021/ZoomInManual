// InfoCard.h
// 7주차(UI 다듬기) 화면 ②: 부품 정보·재고를 '카드'로 그린다(어두운 패널, 큰 이름, 고전압 띠, 재고 상자, 사양·주의사항).
// 순수 Win32 + C++ (헤더만). CInfoList(MFC)가 이 함수들로 그리고, 리눅스 시험(Wine)도 같은 함수로 그림을 만든다.
//
//   MakeCardData : 고른 부품(SelectedPart) + parts.json·inventory.json(PartDB) → 화면에 쓸 글자들
//   LayoutCard   : 너비 width 에 맞춰 위에서부터 배치. c 가 있으면 그리고(scroll 만큼 위로 민 자리), 없으면 높이만 잰다.
//   CardThumbRect: 오른쪽 얇은 스크롤 막대 자리
// 7주차 2차: 이름 머리 고정(스크롤해도 품번·이름은 위에), 아래 흐림, 'L2'·고전압 꼬리표 빼기(고전압은 빨간 띠 하나),
//   재고 상자에서 위치·단가를 굵게(먼저 볼 것), 공급사·담당자는 흐리게.
// 7주차 3차: '이런 증상이면' 한 줄(parts.json 의 symptoms — 검색이 이 낱말로도 찾는다), 키 목록에 V(고전압·재고 보기).
// 7주차 5차: 차량 전체(L0)를 고르면 재고 상자에 '재고 관리 품목 아님'(빠진 데이터처럼 보이지 않게).
#pragma once
#include "UiTheme.h"
#include "GLRenderer.h"   // SelectedPart
#include "PartDB.h"

#include <string>
#include <utility>
#include <vector>

namespace ui
{
    struct CardData
    {
        bool selected = false;          // false = 고른 부품 없음(사용법 화면)
        std::wstring number;            // 품번(예: EV-BRC-001)
        std::wstring position;          // 위치(예: FL, 03, FL-1). 없으면 빈 글
        std::wstring name;              // 이름(예: 브레이크 캘리퍼)
        std::wstring category;          // 분류(예: 섀시)
        int level = -1;
        bool hv = false;
        bool hasInfo = false;           // parts.json 에 있는지
        std::wstring parentLabel;       // 상위 부품 이름 + 위치(예: 브레이크 어셈블리 FL)
        std::wstring parentNode;        // 상위 부품 노드 이름(예: EV-BRK-001@FL)
        int childParts = 0;
        std::wstring desc;
        std::vector<std::pair<std::wstring, std::wstring>> specs;
        std::vector<std::wstring> cautions;
        std::vector<std::wstring> symptoms;   // 이런 증상이면(7주차 3차)
        std::wstring source;            // 3D 모델 출처
        bool hasStock = false;
        int stockState = 2;             // 0 품절, 1 부족, 2 충분
        int stock = 0;
        int safety = 0;
        std::wstring location, price, lead, supplier, supplyDate, manager, phone;
    };

    // 1250000 → "1,250,000원"
    inline std::wstring Won(long long value)
    {
        std::wstring digits = std::to_wstring(value < 0 ? -value : value);
        std::wstring out;
        for (size_t i = 0; i < digits.size(); ++i)
        {
            out += digits[i];
            const size_t left = digits.size() - 1 - i;
            if (left > 0 && left % 3 == 0)
                out += L',';
        }
        return (value < 0 ? L"-" : L"") + out + L"원";
    }

    // "2026-09-14" → "2026. 09. 14." (교수님 그림의 날짜 모양). 모양이 다르면 그대로.
    inline std::wstring DotDate(const std::string& iso)
    {
        if (iso.size() == 10 && iso[4] == '-' && iso[7] == '-')
            return Wide(iso.substr(0, 4) + ". " + iso.substr(5, 2) + ". " + iso.substr(8, 2) + ".");
        return Wide(iso);
    }

    inline std::wstring PositionOf(const std::string& nodeName)   // "EV-BM-12S@03" → "03"
    {
        const size_t at = nodeName.find('@');
        return at == std::string::npos ? std::wstring() : Wide(nodeName.substr(at + 1));
    }

    inline CardData MakeCardData(const SelectedPart& part, const PartDB& db)
    {
        CardData d;
        if (part.node < 0)
            return d;
        d.selected = true;
        const PartInfo* info = db.FindPart(part.name);
        const StockInfo* stock = db.FindStock(part.name);
        d.number = Wide(PartDB::PartNumberOf(part.name));
        d.position = PositionOf(part.name);
        d.level = part.level;
        d.childParts = part.childParts;
        d.hasInfo = info != nullptr;
        if (info)
        {
            d.name = Wide(info->name);
            d.category = Wide(info->category);
            if (info->level >= 0)
                d.level = info->level;
            d.hv = info->hv;
            d.desc = Wide(info->desc);
            for (const auto& spec : info->specs)
                d.specs.push_back({ Wide(spec.first), Wide(spec.second) });
            for (const std::string& caution : info->cautions)
                d.cautions.push_back(Wide(caution));
            for (const std::string& symptom : info->symptoms)
                d.symptoms.push_back(Wide(symptom));
            d.source = Wide(info->source);
        }
        if (!part.parentName.empty())
        {
            d.parentNode = Wide(part.parentName);
            const PartInfo* parent = db.FindPart(part.parentName);
            const std::wstring pos = PositionOf(part.parentName);
            d.parentLabel = parent ? Wide(parent->name) + (pos.empty() ? L"" : L" " + pos) : d.parentNode;
        }
        d.hasStock = stock != nullptr;
        if (stock)
        {
            switch (stock->GetStatus())
            {
            case StockInfo::Status::Out: d.stockState = 0; break;
            case StockInfo::Status::Low: d.stockState = 1; break;
            default:                     d.stockState = 2; break;
            }
            d.stock = stock->stock;
            d.safety = stock->safetyStock;
            d.location = Wide(stock->location);
            d.price = Won(stock->price);
            d.lead = std::to_wstring(stock->leadTimeDays) + L"일";
            d.supplier = Wide(stock->supplier);
            d.supplyDate = DotDate(stock->supplyDate);
            d.manager = Wide(stock->manager);
            d.phone = Wide(stock->phone);
        }
        return d;
    }

    // 카드 배치(그리기). 돌려주는 값 = 내용 전체 높이(스크롤 범위 계산용)
    // 7주차 2차: 부품을 골랐으면 맨 위 '머리'(품번·위치 + 이름)는 스크롤해도 제자리(headerHeight = 그 높이, 없으면 0).
    //   머리 아래부터 스크롤: 고전압 띠 → 분류·하위 부품 → 상위 → 재고 상자 → 설명·사양·주의사항·출처.
    //   스크롤하면 머리 밑에 옅은 그림자, 아래에 더 있으면 맨 아래가 흐려진다(더 있다는 표시).
    inline int LayoutCard(Canvas* c, const CardData& d, int width, int scroll, int* headerHeight = nullptr)
    {
        const bool paint = c != nullptr;
        const int P = Px(14);
        const int inner = std::max(width - 2 * P, Px(80));
        const int viewH = paint ? c->Height() : 0;
        const int smallH = LineHeight(GetFont(Font::Small));
        const int chipH = smallH + Px(6);

        // ---- 머리 높이(고른 부품이 있을 때만)
        const std::wstring title = d.name.empty() ? d.number : d.name;
        int headerH = 0;
        if (d.selected)
            headerH = Px(14) + chipH + Px(4) + static_cast<int>(MeasureText(GetFont(Font::Title), title, inner).cy) + Px(14);
        if (headerHeight)
            *headerHeight = headerH;

        int y = d.selected ? headerH + Px(4) : Px(12);
        auto top = [&](int v) { return v - scroll; };
        auto visible = [&](int y0, int y1) { return paint && top(y1) >= 0 && top(y0) <= viewH; };
        // 줄바꿈 글자: 높이를 돌려준다(그리기는 보이는 곳만)
        auto text = [&](HFONT f, const std::wstring& s, int x, int w, COLORREF color, int atY, UINT format = DT_LEFT) {
            if (s.empty() || w <= 0)
                return 0;
            const TextSize size = MeasureText(f, s, w);
            if (visible(atY, atY + size.cy))
            {
                RECT rc = { x, top(atY), x + w, top(atY) + size.cy };
                c->Text(f, s, rc, format | DT_WORDBREAK, color);
            }
            return static_cast<int>(size.cy);
        };
        auto fill = [&](int x, int atY, int w, int h, float r, COLORREF color, float a = 1.0f) {
            if (visible(atY, atY + h))
                c->FillRound(static_cast<float>(x), static_cast<float>(top(atY)), static_cast<float>(w), static_cast<float>(h), r, color, a);
        };
        auto caption = [&](const std::wstring& s) {
            y += text(GetFont(Font::Caption), s, P, inner, kTextDim, y) + Px(6);
        };
        // 맨 아래 흐림: 내용이 아래로 더 있으면(스크롤할 게 남았으면)
        auto bottomFade = [&](int contentH) {
            if (!paint || contentH - scroll <= viewH + 1)
                return;
            const int fadeH = std::min(Px(30), viewH / 4);
            for (int k = 0; k < fadeH; ++k)
            {
                const float t = static_cast<float>(k + 1) / static_cast<float>(fadeH);
                c->FillRound(0, static_cast<float>(viewH - fadeH + k), static_cast<float>(c->Width()), 1.0f, 0, kPanelBg, t * t * 0.95f);
            }
        };

        if (!d.selected)
        {
            // ---- 사용법 화면(패널 제목 + 줄)
            y += text(GetFont(Font::Caption), L"부품 정보 · 재고", P, inner, kTextDim, y) + Px(9);
            fill(P, y, inner, 1, 0, kPanelLine);
            y += Px(14);
            y += text(GetFont(Font::Title), L"부품을 골라 보세요", P, inner, kText, y) + Px(12);
            const wchar_t* bullets[] = {
                L"3D 뷰에서 부품을 클릭하면 정보와 재고가 여기에 나와요.",
                L"휠로 다가가면 차체·커버가 반투명해지고 안쪽 부품을 고를 수 있어요.",
                L"마우스를 올리면 부품 이름이, 더블클릭하면 그 부품 앞으로 이동해요.",
                L"아래 검색 칸에 품번·이름·증상을 넣어도 돼요. (예: 모듈 03, 끽 소리)",
            };
            const int bodyH = LineHeight(GetFont(Font::Body));
            for (const wchar_t* b : bullets)
            {
                if (visible(y, y + bodyH))
                    c->FillCircle(static_cast<float>(P + Px(4)), static_cast<float>(top(y)) + bodyH * 0.5f, PxF(3), kAccent);
                y += text(GetFont(Font::Body), b, P + Px(16), inner - Px(16), kTextSub, y) + Px(8);
            }
            y += Px(10);
            caption(L"키보드 (3D 뷰를 한 번 클릭한 뒤)");
            const std::pair<const wchar_t*, const wchar_t*> keys[] = {
                { L"R", L"처음 시점" }, { L"S", L"속 보기" }, { L"V", L"고전압·재고" }, { L"P", L"고전압 차단 순서" },
                { L"H", L"조작 안내" }, { L"D", L"성능 표시" }, { L"B", L"성능 측정" }, { L"F", L"와이어프레임" },
                { L"G", L"바닥 격자" }, { L"I", L"인스턴싱" },
            };
            const int keyH = LineHeight(GetFont(Font::SmallBold)) + Px(6);
            const int columns = inner >= Px(260) ? 2 : 1;
            const int colW = inner / columns;
            for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i)
            {
                const int col = static_cast<int>(i) % columns;
                if (col == 0 && i > 0)
                    y += keyH + Px(7);
                KeyCap(visible(y, y + keyH) ? c : nullptr, visible(y, y + keyH), GetFont(Font::SmallBold), GetFont(Font::Small),
                       keys[i].first, keys[i].second, P + col * colW, top(y), keyH, kChipBg, kInputLine, kText, kTextSub);
            }
            y += keyH + P;
            bottomFade(y);
            return y;
        }

        if (!d.hasInfo)
            y += text(GetFont(Font::Small), L"parts.json 에 이 품번이 없어요. data 폴더의 parts.json 을 확인하세요.", P, inner, kLow, y) + Px(10);

        // ---- 고전압 경고 띠(이름 바로 아래 — 작업 전에 먼저 읽는다)
        if (d.hv)
        {
            const int pad = Px(12);
            const std::wstring head = L"고전압 부품";
            const std::wstring body = L"서비스 플러그를 분리하고 절연 장갑을 낀 뒤 작업하세요.";
            const int th = MeasureText(GetFont(Font::BodyBold), head, inner - pad * 2).cy;
            const int bh = MeasureText(GetFont(Font::Small), body, inner - pad * 2).cy;
            const int h = Px(10) + th + Px(2) + bh + Px(10);
            fill(P, y, inner, h, PxF(8), kDanger);
            text(GetFont(Font::BodyBold), head, P + pad, inner - pad * 2, RGB(255, 255, 255), y + Px(10));
            text(GetFont(Font::Small), body, P + pad, inner - pad * 2, RGB(255, 226, 226), y + Px(10) + th + Px(2));
            y += h + Px(12);
        }

        // ---- 꼬리표 줄: 분류 · 하위 부품 수 (단계 'L2' 는 개발용이라 뺐다. 고전압은 위 빨간 띠로)
        {
            std::vector<std::wstring> chips;
            if (!d.category.empty())
                chips.push_back(d.category);
            if (d.childParts > 0)
                chips.push_back(L"하위 부품 " + std::to_wstring(d.childParts) + L"개");
            int x = P;
            for (const std::wstring& chip : chips)
            {
                const int w = Chip(nullptr, false, GetFont(Font::Small), chip, 0, 0, chipH, 0, 0);
                if (x > P && x + w > P + inner)
                {
                    x = P;
                    y += chipH + Px(6);
                }
                Chip(visible(y, y + chipH) ? c : nullptr, visible(y, y + chipH), GetFont(Font::Small), chip, x, top(y), chipH,
                     kChipBg, kTextSub);
                x += w + Px(6);
            }
            if (!chips.empty())
                y += chipH + Px(10);
        }

        // ---- 상위 부품
        if (!d.parentLabel.empty())
        {
            const int labelW = Px(36);
            text(GetFont(Font::Small), L"상위", P, labelW, kTextDim, y + Px(1));
            std::wstring value = d.parentLabel;
            if (d.parentNode != d.parentLabel)
                value += L"  (" + d.parentNode + L")";
            y += text(GetFont(Font::Small), value, P + labelW, inner - labelW, kTextSub, y + Px(1)) + Px(14);
        }

        // ---- 재고 상자: 상태 + 수량 → 위치·단가(굵게, 먼저 볼 것) → 입고 기간·공급일자 → 공급사·담당자(흐리게)
        {
            const int pad = Px(12);
            const int boxW = inner;
            auto block = [&](bool draw) {
                int by = y + pad;
                if (!d.hasStock)
                {
                    // 7주차 5차: 차량 전체(L0)는 재고로 다루는 품목이 아니다 — 빠진 데이터처럼 보이지 않게
                    const wchar_t* none = d.level == 0 ? L"차량 전체 — 재고 관리 품목 아님 (부품을 고르면 재고가 보인다)"
                                                       : L"재고 정보 없음 (inventory.json 확인)";
                    by += text(GetFont(Font::Body), none, P + pad, boxW - pad * 2, kTextDim, draw ? by : -1000000) + pad;
                    return by - y;
                }
                // 첫 줄: 상태 꼬리표 + 수량
                const COLORREF stateColor = d.stockState == 0 ? kOut : (d.stockState == 1 ? kLow : kOk);
                const std::wstring state = d.stockState == 0 ? L"품절" : (d.stockState == 1 ? L"부족" : L"충분");
                HFONT big = GetFont(Font::Title);
                const int bigH = LineHeight(big);
                const int stateW = Chip(nullptr, false, GetFont(Font::SmallBold), state, 0, 0, chipH, 0, 0) + Px(12);
                if (draw && visible(by, by + bigH))
                {
                    const int cy = top(by) + (bigH - chipH) / 2;
                    c->FillRound(static_cast<float>(P + pad), static_cast<float>(cy), static_cast<float>(stateW), static_cast<float>(chipH),
                                 chipH * 0.5f, stateColor, 0.18f);
                    c->FillCircle(static_cast<float>(P + pad + Px(10)), cy + chipH * 0.5f, PxF(3.5f), stateColor);
                    RECT sr = { P + pad + Px(18), cy, P + pad + stateW, cy + chipH };
                    c->Text(GetFont(Font::SmallBold), state, sr, DT_LEFT | DT_VCENTER | DT_SINGLELINE, stateColor);
                    const std::wstring count = std::to_wstring(d.stock) + L"개";
                    const int countW = MeasureText(big, count).cx;
                    RECT cr = { P + pad + stateW + Px(10), top(by), P + pad + stateW + Px(10) + countW + Px(2), top(by) + bigH };
                    c->Text(big, count, cr, DT_LEFT | DT_VCENTER | DT_SINGLELINE, kText);
                    RECT sr2 = { cr.right + Px(6), top(by), P + boxW - pad, top(by) + bigH };
                    c->Text(GetFont(Font::Small), L"안전재고 " + std::to_wstring(d.safety) + L"개", sr2,
                            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, kTextDim);
                }
                by += bigH + Px(12);
                // 칸: 위 = 항목(작게), 아래 = 값. weight 0 = 굵게(먼저 볼 것), 1 = 보통, 2 = 흐리게(나중에 볼 것)
                struct Cell { const wchar_t* key; std::wstring value; bool full; int weight; };
                std::wstring person = d.manager;
                if (!d.phone.empty())
                    person += (person.empty() ? L"" : L"  ") + d.phone;
                const Cell cells[] = {
                    { L"위치", d.location, false, 0 }, { L"단가", d.price, false, 0 },
                    { L"입고 기간", d.lead, false, 1 }, { L"공급일자", d.supplyDate, false, 1 },
                    { L"공급사", d.supplier, true, 2 }, { L"담당자", person, true, 2 },
                };
                const int half = (boxW - pad * 2 - Px(10)) / 2;
                int column = 0, rowH = 0;
                for (const Cell& cell : cells)
                {
                    if (cell.value.empty())
                        continue;
                    if (cell.full && column == 1)
                    {
                        by += rowH + Px(10);
                        column = 0;
                        rowH = 0;
                    }
                    const int cw = cell.full ? boxW - pad * 2 : half;
                    const int cx = P + pad + column * (half + Px(10));
                    HFONT valueFont = GetFont(cell.weight == 0 ? Font::BodyBold : Font::Body);
                    const COLORREF valueColor = cell.weight == 2 ? kTextSub : kText;
                    const int keyH = text(GetFont(Font::Small), cell.key, cx, cw, kTextDim, draw ? by : -1000000);
                    const int valH = text(valueFont, cell.value, cx, cw, valueColor, draw ? by + keyH + Px(2) : -1000000);
                    rowH = std::max(rowH, keyH + Px(2) + valH);
                    if (cell.full || column == 1)
                    {
                        by += rowH + Px(10);
                        column = 0;
                        rowH = 0;
                    }
                    else
                    {
                        column = 1;
                    }
                }
                if (column == 1)
                    by += rowH + Px(10);
                return by - Px(10) + pad - y;
            };
            const int h = block(false);
            fill(P, y, boxW, h, PxF(8), kHeaderBg);
            block(true);
            y += h + Px(18);
        }

        // ---- 설명 · 사양 · 주의사항 · 출처
        if (!d.desc.empty())
        {
            caption(L"설명");
            y += text(GetFont(Font::Body), d.desc, P, inner, kTextSub, y) + Px(16);
        }
        if (!d.symptoms.empty())
        {
            // 이런 증상이면 이 부품을 본다(검색 칸에 이 낱말을 넣어도 이 부품이 나온다)
            std::wstring line;
            for (const std::wstring& symptom : d.symptoms)
                line += (line.empty() ? L"" : L"  ·  ") + symptom;
            caption(L"이런 증상이면");
            y += text(GetFont(Font::Body), line, P, inner, kTextSub, y) + Px(16);
        }
        if (!d.specs.empty())
        {
            caption(L"사양");
            int keyW = 0;
            for (const auto& spec : d.specs)
                keyW = std::max(keyW, static_cast<int>(MeasureText(GetFont(Font::Small), spec.first).cx));
            keyW = std::min(keyW + Px(12), inner * 2 / 5);
            for (const auto& spec : d.specs)
            {
                const int kh = text(GetFont(Font::Small), spec.first, P, keyW - Px(6), kTextDim, y + Px(1));
                const int vh = text(GetFont(Font::Body), spec.second, P + keyW, inner - keyW, kText, y);
                y += std::max(kh + Px(1), vh) + Px(6);
            }
            y += Px(10);
        }
        if (!d.cautions.empty())
        {
            caption(L"주의사항");
            const int numW = Px(24);
            const int bodyH = LineHeight(GetFont(Font::Body));
            for (size_t i = 0; i < d.cautions.size(); ++i)
            {
                if (visible(y, y + bodyH))
                {
                    const float r = PxF(8.5f);
                    c->FillCircle(static_cast<float>(P) + r, static_cast<float>(top(y)) + bodyH * 0.5f, r, kLow, 0.20f);
                    RECT nr = { P, top(y), P + static_cast<int>(r * 2), top(y) + bodyH };
                    c->Text(GetFont(Font::SmallBold), std::to_wstring(i + 1), nr, DT_CENTER | DT_VCENTER | DT_SINGLELINE, kLow);
                }
                y += text(GetFont(Font::Body), d.cautions[i], P + numW, inner - numW, kTextSub, y) + Px(8);
            }
            y += Px(8);
        }
        if (!d.source.empty())
        {
            caption(L"3D 모델 출처");
            y += text(GetFont(Font::Small), d.source, P, inner, kTextDim, y) + Px(4);
        }
        const int contentH = y + P;

        // ---- 머리(제자리): 스크롤된 내용 위에 덮어 그린다
        if (paint)
        {
            const float fw = static_cast<float>(c->Width());
            c->FillRound(0, 0, fw, static_cast<float>(headerH), 0, kPanelBg);
            int hy = Px(14);
            HFONT f = GetFont(Font::SmallBold);
            const int nw = MeasureText(f, d.number).cx;
            RECT nr = { P, hy, P + nw + Px(2), hy + chipH };
            c->Text(f, d.number, nr, DT_LEFT | DT_VCENTER | DT_SINGLELINE, kAccent);
            if (!d.position.empty())
                Chip(c, true, GetFont(Font::Small), L"위치 " + d.position, P + nw + Px(8), hy, chipH, kChipBg, kTextSub);
            hy += chipH + Px(4);
            const int th = MeasureText(GetFont(Font::Title), title, inner).cy;
            RECT tr = { P, hy, P + inner, hy + th };
            c->Text(GetFont(Font::Title), title, tr, DT_LEFT | DT_WORDBREAK, kText);
            // 머리 아래 선. 스크롤했으면 진하게 + 옅은 그림자(내용이 머리 밑으로 들어갔다)
            c->FillRound(static_cast<float>(P), static_cast<float>(headerH - 1), static_cast<float>(inner), 1.0f, 0, kPanelLine,
                         scroll > 0 ? 1.0f : 0.6f);
            if (scroll > 0)
            {
                const int shadowH = Px(8);
                for (int k = 0; k < shadowH; ++k)
                {
                    const float t = 1.0f - static_cast<float>(k) / static_cast<float>(shadowH);
                    c->FillRound(0, static_cast<float>(headerH + k), fw, 1.0f, 0, RGB(0, 0, 0), 0.22f * t * t);
                }
            }
        }
        bottomFade(contentH);
        return contentH;
    }

    // 오른쪽 얇은 스크롤 막대 자리(스크롤할 게 없으면 빈 사각형). 막대는 머리 아래 칸에서만 움직인다.
    inline RECT CardThumbRect(int viewW, int viewH, int headerH, int contentH, int scroll)
    {
        RECT empty = { 0, 0, 0, 0 };
        const int trackTop = headerH + Px(4);
        const int track = viewH - trackTop - Px(4);
        if (contentH <= viewH || viewH <= 0 || track <= Px(12))
            return empty;
        const int visibleBody = std::max(viewH - headerH, 1);
        const int bodyH = std::max(contentH - headerH, 1);
        const int thumb = std::min(track, std::max(Px(28), track * visibleBody / bodyH));
        const int maxScroll = contentH - viewH;
        const int y = trackTop + (track - thumb) * std::min(std::max(scroll, 0), maxScroll) / std::max(maxScroll, 1);
        RECT rc = { viewW - Px(8), y, viewW - Px(3), y + thumb };
        return rc;
    }
}
