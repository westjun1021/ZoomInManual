// UiOverlay.h
// 7주차(UI 다듬기): 3D 뷰 위에 얹는 안내 판들을 그림(BGRA + 알파)으로 만든다. CGLView 가 OpenGL 텍스처로 올려 붙인다.
// 순수 Win32 코드(헤더만). 판마다 둘레에 그림자 여백(inset)이 있어서, 보이는 판은 그림의 (inset, inset) 부터다.
//
//   위치 경로  : ● 차량 › 후륜 구동 유닛 › 구동 모터     (고른 부품의 사슬, 안 골랐으면 펼쳐진 사슬. 누르면 그 단계로)
//   상태 꼬리표: [속 보기 · S] [와이어프레임 · F] …       (켜져 있는 것만, 누르면 끈다)
//   성능 표시  : FPS · CPU·GPU ms · 그리기 수 …           (D 키. 7주차 2차부터 처음에는 꺼져 있다)
//   조작 안내  : [드래그] 돌려 보기 [휠] 다가가기 [클릭] 고르기 [H] 단축키   (간단히)
//   단축키 판  : 조작·단축키 전체 두 칸                    (H 한 번 더 → 숨김 → 다시 간단히)
//   보기 범례  : 고전압 보기 / 재고 보기 — 색 뜻과 품목 수  (7주차 3차, V 키. 누르면 끈다)
//   차단 순서  : 고전압 차단 순서 — 단계 목록 + 지금 단계 설명 (7주차 4차, P 키. 범례 자리에 대신 나온다)
//   첫 안내    : 휠을 굴려 차로 다가가 보세요 …            (처음 한 번, 차체가 열리면 사라진다)
//   부품 이름표: 마우스를 올린 부품의 이름·품번(커서 옆)
//   알림 판    : model.glb 를 못 찾았을 때 등(가운데)
#pragma once
#include "UiTheme.h"

#include <string>
#include <utility>
#include <vector>

namespace ui
{
    struct OverlayImage
    {
        int width = 0;
        int height = 0;
        int inset = 0;                       // 그림자 여백(보이는 판의 왼쪽 위 = (inset, inset))
        std::vector<unsigned char> pixels;   // BGRA, 위 줄부터, 알파 = 불투명도(OpenGL 은 SRC_ALPHA 로 섞는다)
        std::vector<RECT> hits;              // 누를 수 있는 곳(그림 안 좌표)
        bool Empty() const { return width <= 0 || height <= 0; }
    };

    struct Badge
    {
        std::wstring text;
        COLORREF color = kAccent;
    };

    namespace detail
    {
        inline void Glass(Canvas& c, int x, int y, int w, int h, float r, float alpha = kGlassAlpha)
        {
            c.Shadow(static_cast<float>(x), static_cast<float>(y) + PxF(1.5f), static_cast<float>(w), static_cast<float>(h), r, PxF(6), 0.45f);
            c.FillRound(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h), r, kGlassBg, alpha);
            c.StrokeRound(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h), r, 1.0f, RGB(255, 255, 255), 0.10f);
        }
        inline void Finish(Canvas& c, int inset, OverlayImage& out)
        {
            out.width = c.Width();
            out.height = c.Height();
            out.inset = inset;
            out.pixels = c.Pixels();
        }
    }

    // ● 차량 › 배터리팩 › 배터리 모듈 03  (마지막 = 지금 위치, 굵게)
    inline OverlayImage BuildPath(const std::vector<std::wstring>& items)
    {
        OverlayImage out;
        if (items.empty())
            return out;
        HFONT normal = GetFont(Font::OverlayBody), bold = GetFont(Font::OverlayBold);
        const int S = Px(8);
        const int h = LineHeight(bold) + Px(14);
        const int padL = Px(12), dotW = Px(16), padR = Px(15), gap = Px(7);
        const std::wstring sep = L"›";
        const int sepW = MeasureText(normal, sep).cx;
        std::vector<int> widths;
        int w = padL + dotW + padR;
        for (size_t i = 0; i < items.size(); ++i)
        {
            const bool last = i + 1 == items.size();
            widths.push_back(MeasureText(last ? bold : normal, items[i]).cx);
            w += widths.back() + (last ? 0 : gap * 2 + sepW);
        }
        Canvas c;
        if (!c.Create(w + 2 * S, h + 2 * S, false))
            return out;
        detail::Glass(c, S, S, w, h, h * 0.5f);
        c.FillCircle(static_cast<float>(S + padL + Px(4)), S + h * 0.5f, PxF(4), kAccent);
        int x = S + padL + dotW;
        for (size_t i = 0; i < items.size(); ++i)
        {
            const bool last = i + 1 == items.size();
            RECT rc = { x, S, x + widths[i] + Px(2), S + h };
            c.Text(last ? bold : normal, items[i], rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE, last ? kText : kLink);
            out.hits.push_back(RECT{ x - Px(5), S, x + widths[i] + Px(5), S + h });
            x += widths[i];
            if (!last)
            {
                RECT sr = { x + gap, S, x + gap + sepW + Px(2), S + h };
                c.Text(normal, sep, sr, DT_LEFT | DT_VCENTER | DT_SINGLELINE, kTextDim);
                x += gap * 2 + sepW;
            }
        }
        detail::Finish(c, S, out);
        return out;
    }

    // 켜져 있는 상태 꼬리표들(한 줄). hits[i] = badges[i]
    inline OverlayImage BuildBadges(const std::vector<Badge>& badges)
    {
        OverlayImage out;
        if (badges.empty())
            return out;
        HFONT font = GetFont(Font::OverlaySmallBold);
        const int S = Px(8), h = LineHeight(font) + Px(12), gap = Px(6), padX = Px(11);
        std::vector<int> widths;
        int w = 0;
        for (size_t i = 0; i < badges.size(); ++i)
        {
            widths.push_back(MeasureText(font, badges[i].text).cx + padX * 2);
            w += widths.back() + (i + 1 < badges.size() ? gap : 0);
        }
        Canvas c;
        if (!c.Create(w + 2 * S, h + 2 * S, false))
            return out;
        int x = S;
        for (size_t i = 0; i < badges.size(); ++i)
        {
            const float fx = static_cast<float>(x), fy = static_cast<float>(S), fw = static_cast<float>(widths[i]), fh = static_cast<float>(h);
            c.Shadow(fx, fy + PxF(1.5f), fw, fh, fh * 0.5f, PxF(5), 0.40f);
            c.FillRound(fx, fy, fw, fh, fh * 0.5f, kGlassBg, 0.88f);
            c.FillRound(fx, fy, fw, fh, fh * 0.5f, badges[i].color, 0.22f);
            c.StrokeRound(fx, fy, fw, fh, fh * 0.5f, 1.0f, badges[i].color, 0.85f);
            RECT rc = { x, S, x + widths[i], S + h };
            c.Text(font, badges[i].text, rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE, Mix(badges[i].color, RGB(255, 255, 255), 0.55f));
            out.hits.push_back(rc);
            x += widths[i] + gap;
        }
        detail::Finish(c, S, out);
        return out;
    }

    // 성능 표시: 첫 줄은 굵게, 나머지는 작게. colors[i] = 줄 색
    inline OverlayImage BuildStats(const std::vector<std::wstring>& lines, const std::vector<COLORREF>& colors)
    {
        OverlayImage out;
        if (lines.empty())
            return out;
        HFONT first = GetFont(Font::OverlayBold), rest = GetFont(Font::OverlaySmall);
        const int S = Px(8), pad = Px(11), gap = Px(3);
        std::vector<TextSize> sizes;
        int w = 0, h = pad * 2;
        for (size_t i = 0; i < lines.size(); ++i)
        {
            sizes.push_back(MeasureText(i == 0 ? first : rest, lines[i]));
            w = std::max(w, static_cast<int>(sizes.back().cx));
            h += sizes.back().cy + (i + 1 < lines.size() ? gap : 0);
        }
        w += pad * 2;
        Canvas c;
        if (!c.Create(w + 2 * S, h + 2 * S, false))
            return out;
        detail::Glass(c, S, S, w, h, PxF(9));
        int y = S + pad;
        for (size_t i = 0; i < lines.size(); ++i)
        {
            RECT rc = { S + pad, y, S + pad + sizes[i].cx + Px(2), y + sizes[i].cy };
            c.Text(i == 0 ? first : rest, lines[i], rc, DT_LEFT | DT_SINGLELINE, i < colors.size() ? colors[i] : kTextSub);
            y += sizes[i].cy + gap;
        }
        detail::Finish(c, S, out);
        return out;
    }

    namespace detail
    {
        // 조작 안내의 키 모양: 3D 보다 먼저 눈에 띄지 않게 차분하게(옅은 칸 + 옅은 테두리 + 밝은 회색 글자)
        constexpr COLORREF kKeyLine = RGB(86, 95, 109);
        constexpr COLORREF kKeyText = RGB(204, 211, 221);
        constexpr float    kKeyFill = 0.06f;   // 흰색을 이만큼
    }

    // [키] 설명 … 한 줄(간단히). maxWidth(그림 전체 너비)를 넘는 뒤쪽 항목은 뺀다.
    inline OverlayImage BuildHints(const std::vector<std::pair<std::wstring, std::wstring>>& items, int maxWidth)
    {
        OverlayImage out;
        HFONT keyFont = GetFont(Font::OverlaySmallBold), labelFont = GetFont(Font::OverlaySmall);
        const int S = Px(8), keyH = LineHeight(keyFont) + Px(6), padX = Px(12), padY = Px(6), gap = Px(14);
        const int h = keyH + padY * 2;
        std::vector<int> widths;
        int w = padX * 2;
        for (size_t i = 0; i < items.size(); ++i)
        {
            const int iw = KeyCap(nullptr, false, keyFont, labelFont, items[i].first, items[i].second, 0, 0, keyH, 0, 0, 0, 0);
            const int next = w + iw + (widths.empty() ? 0 : gap);
            if (next + 2 * S > maxWidth && !widths.empty())
                break;
            w = next;
            widths.push_back(iw);
        }
        if (widths.empty())
            return out;
        Canvas c;
        if (!c.Create(w + 2 * S, h + 2 * S, false))
            return out;
        detail::Glass(c, S, S, w, h, PxF(9), 0.74f);
        int x = S + padX;
        for (size_t i = 0; i < widths.size(); ++i)
        {
            KeyCap(&c, true, keyFont, labelFont, items[i].first, items[i].second, x, S + padY, keyH,
                   RGB(255, 255, 255), detail::kKeyLine, detail::kKeyText, kTextSub, detail::kKeyFill);
            x += widths[i] + gap;
        }
        detail::Finish(c, S, out);
        return out;
    }

    // 조작·단축키 전체(H 를 한 번 누르면): 제목 + [키] 설명을 두 칸으로(왼쪽 칸 위→아래, 다음 오른쪽 칸).
    // 키는 키 칸 오른쪽에 붙이고 설명은 한 줄로 맞춘다. 두 칸이 maxWidth 를 넘으면 한 칸.
    inline OverlayImage BuildHelp(const std::wstring& title, const std::vector<std::pair<std::wstring, std::wstring>>& items, int maxWidth)
    {
        OverlayImage out;
        if (items.empty())
            return out;
        HFONT titleFont = GetFont(Font::OverlayBold), keyFont = GetFont(Font::OverlaySmallBold), labelFont = GetFont(Font::OverlaySmall);
        const int S = Px(8), pad = Px(14), keyH = LineHeight(keyFont) + Px(6), rowGap = Px(6), colGap = Px(24), labelGap = Px(8);
        int keyColumn = keyH, labelColumn = 0;
        for (const auto& item : items)
        {
            keyColumn = std::max(keyColumn, std::max(static_cast<int>(MeasureText(keyFont, item.first).cx) + Px(10), keyH));
            labelColumn = std::max(labelColumn, static_cast<int>(MeasureText(labelFont, item.second).cx));
        }
        const int colW = keyColumn + labelGap + labelColumn;
        const int columns = (pad * 2 + colW * 2 + colGap + 2 * S <= maxWidth) ? 2 : 1;
        const int rows = (static_cast<int>(items.size()) + columns - 1) / columns;
        const TextSize t = MeasureText(titleFont, title);
        const int w = std::max(pad * 2 + colW * columns + colGap * (columns - 1), pad * 2 + static_cast<int>(t.cx));
        const int h = pad + t.cy + Px(10) + rows * keyH + (rows - 1) * rowGap + pad;
        Canvas c;
        if (!c.Create(w + 2 * S, h + 2 * S, false))
            return out;
        detail::Glass(c, S, S, w, h, PxF(10), 0.88f);
        RECT tr = { S + pad, S + pad, S + w - pad, S + pad + t.cy };
        c.Text(titleFont, title, tr, DT_LEFT | DT_SINGLELINE, kText);
        const int top0 = S + pad + t.cy + Px(10);
        for (size_t i = 0; i < items.size(); ++i)
        {
            const int col = static_cast<int>(i) / rows;
            const int row = static_cast<int>(i) % rows;
            const int x = S + pad + col * (colW + colGap);
            const int y = top0 + row * (keyH + rowGap);
            const int kw = std::max(static_cast<int>(MeasureText(keyFont, items[i].first).cx) + Px(10), keyH);
            const int kx = x + keyColumn - kw;   // 키 칸 오른쪽에 붙인다
            c.FillRound(static_cast<float>(kx), static_cast<float>(y), static_cast<float>(kw), static_cast<float>(keyH), PxF(4),
                        RGB(255, 255, 255), detail::kKeyFill);
            c.StrokeRound(static_cast<float>(kx), static_cast<float>(y), static_cast<float>(kw), static_cast<float>(keyH), PxF(4), 1.0f,
                          detail::kKeyLine, 0.9f);
            RECT k = { kx, y, kx + kw, y + keyH };
            c.Text(keyFont, items[i].first, k, DT_CENTER | DT_VCENTER | DT_SINGLELINE, detail::kKeyText);
            RECT l = { x + keyColumn + labelGap, y, x + colW + Px(2), y + keyH };
            c.Text(labelFont, items[i].second, l, DT_LEFT | DT_VCENTER | DT_SINGLELINE, kTextSub);
        }
        detail::Finish(c, S, out);
        return out;
    }

    // 보기 모드 범례(7주차 3차): 제목 + 오른쪽 키 [V] / ● 색 + 글 줄들 / 아래 작은 안내. 판 전체가 누를 곳(hits[0] = 끄기)
    struct LegendRow
    {
        COLORREF color;
        std::wstring text;
    };
    inline OverlayImage BuildLegend(const std::wstring& title, const std::wstring& key, const std::vector<LegendRow>& rows,
                                    const std::wstring& note)
    {
        OverlayImage out;
        HFONT titleFont = GetFont(Font::OverlayBold), rowFont = GetFont(Font::OverlaySmall), keyFont = GetFont(Font::OverlaySmallBold);
        const int S = Px(8), pad = Px(12), dotW = Px(16), rowGap = Px(4), maxNote = Px(230);
        const TextSize t = MeasureText(titleFont, title);
        const int keyH = LineHeight(keyFont) + Px(4);
        const int keyW = key.empty() ? 0 : std::max(static_cast<int>(MeasureText(keyFont, key).cx) + Px(10), keyH);
        int contentW = t.cx + (keyW ? Px(16) + keyW : 0);
        std::vector<TextSize> sizes;
        for (const LegendRow& row : rows)
        {
            sizes.push_back(MeasureText(rowFont, row.text));
            contentW = std::max(contentW, dotW + static_cast<int>(sizes.back().cx));
        }
        const TextSize n = note.empty() ? TextSize{} : MeasureText(rowFont, note, std::max(contentW, maxNote));
        contentW = std::max(contentW, static_cast<int>(n.cx));
        int h = pad + std::max(static_cast<int>(t.cy), keyH) + Px(6);
        for (const TextSize& size : sizes)
            h += size.cy + rowGap;
        h += note.empty() ? 0 : Px(4) + n.cy;
        h += pad - rowGap;
        const int w = pad * 2 + contentW;
        Canvas c;
        if (!c.Create(w + 2 * S, h + 2 * S, false))
            return out;
        detail::Glass(c, S, S, w, h, PxF(9), 0.86f);
        int y = S + pad;
        const int headH = std::max(static_cast<int>(t.cy), keyH);
        RECT tr = { S + pad, y, S + pad + t.cx + Px(2), y + headH };
        c.Text(titleFont, title, tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE, kText);
        if (keyW)
        {
            const int kx = S + w - pad - keyW, ky = y + (headH - keyH) / 2;
            c.FillRound(static_cast<float>(kx), static_cast<float>(ky), static_cast<float>(keyW), static_cast<float>(keyH), PxF(4),
                        RGB(255, 255, 255), detail::kKeyFill);
            c.StrokeRound(static_cast<float>(kx), static_cast<float>(ky), static_cast<float>(keyW), static_cast<float>(keyH), PxF(4), 1.0f,
                          detail::kKeyLine, 0.9f);
            RECT kr = { kx, ky, kx + keyW, ky + keyH };
            c.Text(keyFont, key, kr, DT_CENTER | DT_VCENTER | DT_SINGLELINE, detail::kKeyText);
        }
        y += headH + Px(6);
        for (size_t i = 0; i < rows.size(); ++i)
        {
            c.FillCircle(static_cast<float>(S + pad + Px(5)), y + sizes[i].cy * 0.5f, PxF(4.5f), rows[i].color);
            RECT rr = { S + pad + dotW, y, S + pad + dotW + sizes[i].cx + Px(2), y + sizes[i].cy };
            c.Text(rowFont, rows[i].text, rr, DT_LEFT | DT_SINGLELINE, kText);
            y += sizes[i].cy + rowGap;
        }
        if (!note.empty())
        {
            RECT nr = { S + pad, y + Px(4) - rowGap, S + pad + std::max(static_cast<int>(n.cx), 1) + Px(2), y + Px(4) - rowGap + n.cy };
            c.Text(rowFont, note, nr, DT_LEFT | DT_WORDBREAK, kTextDim);
        }
        out.hits.push_back(RECT{ S, S, S + w, S + h });
        detail::Finish(c, S, out);
        return out;
    }

    // 고전압 차단 순서(7주차 4차, P 키): 제목 + 'n / 4'(다 하면 '완료') + 닫기(×) / 단계 목록(끝낸 단계 ✓ 초록,
    // 지금 단계 주황 번호·굵게·띠, 남은 단계 흐리게) / 구분선 / 지금 단계 설명 / 맨 아래 작은 안내.
    // hits[0] = 닫기 단추, hits[1] = 판 전체(다음 단계)
    struct ProcedureView
    {
        std::wstring title;
        std::vector<std::wstring> steps;   // 단계마다 짧은 글
        int current = 0;                   // 지금 단계(0부터). steps.size() 면 다 했음
        std::wstring detail;               // 지금 단계 설명(다 했으면 마무리 글)
        std::wstring note;                 // 맨 아래 작은 안내(키)
        int detailLines = 0;               // 설명 칸을 이 줄 수만큼은 잡는다(단계를 넘겨도 판 크기가 그대로)
    };
    inline OverlayImage BuildProcedure(const ProcedureView& v)
    {
        OverlayImage out;
        if (v.steps.empty())
            return out;
        HFONT titleFont = GetFont(Font::OverlayBold), rowFont = GetFont(Font::OverlaySmall), rowBold = GetFont(Font::OverlaySmallBold);
        const int S = Px(8), pad = Px(14), circle = Px(20), rowGap = Px(4), closeD = Px(22);
        const int count = static_cast<int>(v.steps.size());
        const bool done = v.current >= count;
        const std::wstring counter = done ? std::wstring(L"완료") : std::to_wstring(v.current + 1) + L" / " + std::to_wstring(count);
        const TextSize t = MeasureText(titleFont, v.title);
        const TextSize k = MeasureText(rowBold, counter);
        int contentW = std::max(Px(300), static_cast<int>(t.cx) + Px(14) + static_cast<int>(k.cx) + Px(10) + closeD);
        for (int i = 0; i < count; ++i)
            contentW = std::max(contentW, circle + Px(10) + static_cast<int>(MeasureText(rowBold, v.steps[i]).cx) + Px(4));
        contentW = std::min(contentW, Px(380));
        const TextSize d = v.detail.empty() ? TextSize{} : MeasureText(rowFont, v.detail, contentW);
        const TextSize n = v.note.empty() ? TextSize{} : MeasureText(rowFont, v.note, contentW);
        const int headH = std::max(static_cast<int>(t.cy), closeD);
        const int rowH = std::max(circle, LineHeight(rowBold)) + Px(6);
        const int detailH = std::max(static_cast<int>(d.cy), v.detailLines * LineHeight(rowFont));
        int h = pad + headH + Px(8) + count * rowH + (count - 1) * rowGap + Px(10) + 1 + Px(10) + detailH;
        h += (v.note.empty() ? 0 : Px(8) + n.cy) + pad;
        const int w = pad * 2 + contentW;
        Canvas c;
        if (!c.Create(w + 2 * S, h + 2 * S, false))
            return out;
        detail::Glass(c, S, S, w, h, PxF(10), 0.90f);
        c.FillRound(static_cast<float>(S + Px(6)), static_cast<float>(S + pad), PxF(3), static_cast<float>(headH), PxF(1.5f), kHighVoltage);

        // 제목 줄: 제목 … 'n / 4' [×]
        int y = S + pad;
        const int x0 = S + pad;
        RECT tr = { x0, y, x0 + t.cx + Px(2), y + headH };
        c.Text(titleFont, v.title, tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE, kText);
        const int bx = S + w - pad - closeD, by = y + (headH - closeD) / 2;
        const float fbx = static_cast<float>(bx), fby = static_cast<float>(by), fd = static_cast<float>(closeD);
        c.FillRound(fbx, fby, fd, fd, fd * 0.5f, RGB(255, 255, 255), detail::kKeyFill);
        c.StrokeRound(fbx, fby, fd, fd, fd * 0.5f, 1.0f, detail::kKeyLine, 0.9f);
        const float m = fd * 0.34f;
        c.Line(fbx + m, fby + m, fbx + fd - m, fby + fd - m, PxF(1.6f), detail::kKeyText);
        c.Line(fbx + fd - m, fby + m, fbx + m, fby + fd - m, PxF(1.6f), detail::kKeyText);
        RECT kr = { bx - Px(10) - static_cast<int>(k.cx) - Px(2), y, bx - Px(10), y + headH };
        c.Text(rowBold, counter, kr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE, done ? kOk : kHighVoltage);
        y += headH + Px(8);

        // 단계 목록
        for (int i = 0; i < count; ++i)
        {
            const bool finished = i < v.current, now = i == v.current;
            if (now)
                c.FillRound(static_cast<float>(x0 - Px(6)), static_cast<float>(y), static_cast<float>(contentW + Px(12)),
                            static_cast<float>(rowH), PxF(6), kHighVoltage, 0.16f);
            const float r = circle * 0.5f;
            const float cx = x0 + r, cy = y + rowH * 0.5f;
            if (finished)
            {
                c.FillCircle(cx, cy, r, kOk, 0.92f);
                c.Line(cx - r * 0.45f, cy + r * 0.02f, cx - r * 0.10f, cy + r * 0.38f, PxF(2.0f), RGB(255, 255, 255));
                c.Line(cx - r * 0.10f, cy + r * 0.38f, cx + r * 0.48f, cy - r * 0.36f, PxF(2.0f), RGB(255, 255, 255));
            }
            else
            {
                if (now)
                    c.FillCircle(cx, cy, r, kHighVoltage);
                else
                    c.StrokeRound(cx - r, cy - r, r * 2, r * 2, r, 1.2f, kTextDim, 0.9f);
                RECT nr = { x0, y, x0 + circle, y + rowH };
                c.Text(rowBold, std::to_wstring(i + 1), nr, DT_CENTER | DT_VCENTER | DT_SINGLELINE,
                       now ? RGB(255, 255, 255) : kTextSub);
            }
            RECT rr = { x0 + circle + Px(10), y, x0 + contentW, y + rowH };
            c.Text(now ? rowBold : rowFont, v.steps[i], rr, DT_LEFT | DT_VCENTER | DT_SINGLELINE,
                   finished ? kTextDim : (now ? kText : kTextSub));
            y += rowH + rowGap;
        }
        y += Px(10) - rowGap;
        c.FillRound(static_cast<float>(x0), static_cast<float>(y), static_cast<float>(contentW), 1.0f, 0.0f, RGB(255, 255, 255), 0.12f);
        y += 1 + Px(10);

        // 지금 단계 설명(다 했으면 마무리 글, 초록) + 맨 아래 안내
        if (!v.detail.empty())
        {
            RECT dr = { x0, y, x0 + std::max(static_cast<int>(d.cx), 1) + Px(2), y + d.cy };
            c.Text(rowFont, v.detail, dr, DT_LEFT | DT_WORDBREAK, done ? Mix(kOk, kText, 0.35f) : kText);
        }
        y += detailH;
        if (!v.note.empty())
        {
            y += Px(8);
            RECT nr = { x0, y, x0 + std::max(static_cast<int>(n.cx), 1) + Px(2), y + n.cy };
            c.Text(rowFont, v.note, nr, DT_LEFT | DT_WORDBREAK, kTextDim);
        }
        out.hits.push_back(RECT{ bx - Px(4), by - Px(4), bx + closeD + Px(4), by + closeD + Px(4) });   // [0] 닫기
        out.hits.push_back(RECT{ S, S, S + w, S + h });                                                     // [1] 판 전체 = 다음
        detail::Finish(c, S, out);
        return out;
    }

    // 첫 안내(마우스 휠 그림 + 글). 파란 테두리
    inline OverlayImage BuildTip(const std::wstring& text)
    {
        OverlayImage out;
        HFONT font = GetFont(Font::OverlayBody);
        const int S = Px(10), h = LineHeight(font) + Px(18), padL = Px(14), iconW = Px(26), padR = Px(18);
        const int textW = MeasureText(font, text).cx;
        const int w = padL + iconW + textW + padR;
        Canvas c;
        if (!c.Create(w + 2 * S, h + 2 * S, false))
            return out;
        detail::Glass(c, S, S, w, h, h * 0.5f, 0.88f);
        c.StrokeRound(static_cast<float>(S), static_cast<float>(S), static_cast<float>(w), static_cast<float>(h), h * 0.5f, PxF(1.5f), kAccent, 0.85f);
        // 마우스 그림: 몸통 + 가운데 휠
        const float mw = PxF(11), mh = PxF(16);
        const float mx = static_cast<float>(S + padL), my = S + (h - mh) * 0.5f;
        c.StrokeRound(mx, my, mw, mh, mw * 0.5f, PxF(1.3f), kText, 0.9f);
        c.Line(mx + mw * 0.5f, my + PxF(3.5f), mx + mw * 0.5f, my + PxF(7.0f), PxF(1.8f), kAccent);
        RECT rc = { S + padL + iconW, S, S + padL + iconW + textW + Px(2), S + h };
        c.Text(font, text, rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE, kText);
        detail::Finish(c, S, out);
        return out;
    }

    // 마우스를 올린 부품의 이름표: 이름(굵게, 고전압이면 주황 꼬리표) / 품번 / 안내
    inline OverlayImage BuildTooltip(const std::wstring& title, const std::wstring& sub, const std::wstring& hint, bool highVoltage)
    {
        OverlayImage out;
        HFONT titleFont = GetFont(Font::OverlayBold), smallFont = GetFont(Font::OverlaySmall), chipFont = GetFont(Font::OverlaySmallBold);
        const int S = Px(8), padL = Px(17), padR = Px(12), padY = Px(8), gap = Px(2);
        const TextSize t = MeasureText(titleFont, title);
        const TextSize s = sub.empty() ? TextSize{} : MeasureText(smallFont, sub);
        const TextSize n = hint.empty() ? TextSize{} : MeasureText(smallFont, hint);
        const int chipH = t.cy;
        const int chipW = highVoltage ? Chip(nullptr, false, chipFont, L"고전압", 0, 0, chipH, 0, 0) : 0;
        const int contentW = std::max(static_cast<int>(t.cx) + (highVoltage ? chipW + Px(8) : 0), static_cast<int>(std::max(s.cx, n.cx)));
        const int w = padL + contentW + padR;
        const int h = padY * 2 + t.cy + (sub.empty() ? 0 : gap + s.cy) + (hint.empty() ? 0 : gap + n.cy);
        Canvas c;
        if (!c.Create(w + 2 * S, h + 2 * S, false))
            return out;
        detail::Glass(c, S, S, w, h, PxF(7), 0.92f);
        c.FillRound(static_cast<float>(S + Px(6)), static_cast<float>(S + padY), PxF(3), static_cast<float>(h - padY * 2), PxF(1.5f),
                    highVoltage ? kHighVoltage : kAccent);
        int y = S + padY;
        RECT tr = { S + padL, y, S + padL + t.cx + Px(2), y + t.cy };
        c.Text(titleFont, title, tr, DT_LEFT | DT_SINGLELINE, kText);
        if (highVoltage)
            Chip(&c, true, chipFont, L"고전압", tr.right + Px(6), y, chipH, kHighVoltage, RGB(255, 255, 255));
        y += t.cy + gap;
        if (!sub.empty())
        {
            RECT sr = { S + padL, y, S + padL + s.cx + Px(2), y + s.cy };
            c.Text(smallFont, sub, sr, DT_LEFT | DT_SINGLELINE, kAccent);
            y += s.cy + gap;
        }
        if (!hint.empty())
        {
            RECT hr = { S + padL, y, S + padL + n.cx + Px(2), y + n.cy };
            c.Text(smallFont, hint, hr, DT_LEFT | DT_SINGLELINE, kTextDim);
        }
        detail::Finish(c, S, out);
        return out;
    }

    // 가운데 알림 판(제목 + 여러 줄 글)
    inline OverlayImage BuildMessage(const std::wstring& title, const std::wstring& body)
    {
        OverlayImage out;
        HFONT titleFont = GetFont(Font::OverlayTitle), bodyFont = GetFont(Font::OverlayBody);
        const int S = Px(10), pad = Px(18), maxText = Px(440);
        const TextSize t = MeasureText(titleFont, title);
        const TextSize b = MeasureText(bodyFont, body, maxText);
        const int w = pad * 2 + std::max(static_cast<int>(t.cx), static_cast<int>(b.cx));
        const int h = pad * 2 + t.cy + Px(8) + b.cy;
        Canvas c;
        if (!c.Create(w + 2 * S, h + 2 * S, false))
            return out;
        detail::Glass(c, S, S, w, h, PxF(10), 0.92f);
        RECT tr = { S + pad, S + pad, S + w - pad, S + pad + t.cy };
        c.Text(titleFont, title, tr, DT_LEFT | DT_SINGLELINE, kText);
        RECT br = { S + pad, tr.bottom + Px(8), S + pad + std::max(static_cast<int>(b.cx), 1) + Px(2), tr.bottom + Px(8) + b.cy };
        c.Text(bodyFont, body, br, DT_LEFT | DT_WORDBREAK, kTextSub);
        detail::Finish(c, S, out);
        return out;
    }
}
