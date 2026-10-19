// UiTheme.h
// 7주차(UI 다듬기): 어두운 화면 한 벌 — 색, 글꼴, 매끈한 도형을 그리는 그림판(Canvas), 대화상자 꾸미기.
// MFC 를 쓰지 않는 순수 Win32 코드라서 리눅스 시험(Wine)에서도 그대로 돌려 본다. 헤더만 있는 파일(프로젝트에 안 넣어도 됨).
//
//   - 색: 3D 뷰(어두운 남색 회색)에 맞춘 바탕·패널·글자·강조색. 고전압 = 주황, 선택 = 파랑(3D 강조색과 같은 계열).
//   - 글꼴: 맑은 고딕. 크기는 화면 배율(DPI)에 맞춰 키운다(Px).
//   - Canvas: 32비트 그림판. 둥근 사각형·원·그림자의 가장자리를 부드럽게(안티에일리어싱) 칠한다.
//       불투명 판 = ②·③ 패널(글자는 ClearType), 투명 판 = 3D 뷰 위에 얹는 안내 글자 판(알파 포함, OpenGL 텍스처로).
//   - ThemeDialog: 대화상자 바탕을 어둡게, 그룹 상자 대신 둥근 패널, 검색 칸·[검색] 단추·제목 줄도 어둡게.
//       대화상자 코드는 그대로 두고 CPartPreview::CreateInPanel 이 한 번 부른다.
#pragma once
#include <windows.h>
#include <commctrl.h>

// Windows 헤더(MFC)의 min/max 매크로가 std::min/std::max 와 부딪히지 않게 여기서 지운다(이 헤더를 넣은 파일 전체).
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

#ifdef _MSC_VER
#pragma comment(lib, "comctl32.lib")   // SetWindowSubclass
#endif

namespace ui
{
    // ------------------------------------------------------------------ 색
    constexpr COLORREF kWindowBg   = RGB(22, 24, 29);     // 대화상자 바탕(패널 사이 틈)
    constexpr COLORREF kPanelBg    = RGB(31, 35, 42);     // ②·③ 패널
    constexpr COLORREF kPanelLine  = RGB(48, 54, 64);     // 패널 테두리·구분선
    constexpr COLORREF kHeaderBg   = RGB(38, 43, 51);     // 패널 위 띠, 재고 상자
    constexpr COLORREF kChipBg     = RGB(47, 53, 63);     // 작은 꼬리표(분류·단계)
    constexpr COLORREF kInputBg    = RGB(21, 24, 29);     // 검색 칸
    constexpr COLORREF kInputLine  = RGB(64, 71, 83);
    constexpr COLORREF kText       = RGB(234, 238, 244);  // 글자
    constexpr COLORREF kTextSub    = RGB(174, 183, 196);  // 덜 중요한 글자
    constexpr COLORREF kTextDim    = RGB(124, 134, 149);  // 제목표·안내
    constexpr COLORREF kAccent     = RGB(58, 160, 255);   // 선택 파랑(3D 강조 0.20, 0.65, 1.00 과 같은 계열)
    constexpr COLORREF kAccentHot  = RGB(84, 176, 255);
    constexpr COLORREF kAccentDown = RGB(36, 116, 200);
    constexpr COLORREF kLink       = RGB(136, 196, 255);  // 3D 뷰 위치 경로의 누를 수 있는 글자
    constexpr COLORREF kHighVoltage= RGB(255, 124, 38);   // 고전압 주황(3D 강조 1.00, 0.35, 0.00 계열)
    constexpr COLORREF kDanger     = RGB(206, 52, 52);    // 고전압 경고 띠
    constexpr COLORREF kOk         = RGB(66, 204, 124);   // 재고 충분
    constexpr COLORREF kLow        = RGB(255, 178, 48);   // 재고 부족
    constexpr COLORREF kOut        = RGB(255, 94, 94);    // 품절
    constexpr COLORREF kGlassBg    = RGB(16, 18, 23);     // 3D 뷰 위 반투명 판
    constexpr float    kGlassAlpha = 0.80f;

    inline COLORREF Mix(COLORREF a, COLORREF b, float t)   // a → b 로 t 만큼
    {
        auto ch = [t](int x, int y) { return static_cast<BYTE>(std::lround(x + (y - x) * t)); };
        return RGB(ch(GetRValue(a), GetRValue(b)), ch(GetGValue(a), GetGValue(b)), ch(GetBValue(a), GetBValue(b)));
    }

    // ------------------------------------------------------------------ 화면 배율
    inline int Dpi()
    {
        static const int dpi = [] {
            HDC dc = ::GetDC(nullptr);
            const int value = dc ? ::GetDeviceCaps(dc, LOGPIXELSY) : 96;
            if (dc)
                ::ReleaseDC(nullptr, dc);
            return value > 0 ? value : 96;
        }();
        return dpi;
    }
    inline int Px(int dip96) { return ::MulDiv(dip96, Dpi(), 96); }                 // 96 DPI 기준 → 지금 화면 픽셀
    inline float PxF(float dip96) { return dip96 * static_cast<float>(Dpi()) / 96.0f; }

    // ------------------------------------------------------------------ 글꼴(맑은 고딕, 한 번 만들어 계속 쓴다)
    enum class Font
    {
        Body, BodyBold, Small, SmallBold, Caption, Title,            // 패널(불투명 바탕, ClearType)
        OverlayBody, OverlayBold, OverlaySmall, OverlaySmallBold, OverlayTitle,   // 3D 뷰 위(투명 판, 회색 안티에일리어싱)
        Count
    };
    inline HFONT GetFont(Font which)
    {
        // 7주차 2차: 한 단계씩 키웠다(본문 14px = Windows 11 기본 본문 크기, 가장 작은 글 12px)
        struct Spec { int px; int weight; bool overlay; };
        static const Spec specs[] = {
            { 14, FW_NORMAL, false }, { 14, FW_BOLD, false }, { 13, FW_NORMAL, false }, { 13, FW_BOLD, false },
            { 12, FW_BOLD, false },   { 20, FW_BOLD, false },
            { 14, FW_NORMAL, true },  { 14, FW_BOLD, true },  { 13, FW_NORMAL, true }, { 13, FW_BOLD, true },
            { 16, FW_BOLD, true },
        };
        static HFONT cache[static_cast<int>(Font::Count)] = {};
        const int i = static_cast<int>(which);
        if (i < 0 || i >= static_cast<int>(Font::Count))
            return static_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT));
        if (!cache[i])
        {
            const Spec& s = specs[i];
            cache[i] = ::CreateFontW(-Px(s.px), 0, 0, 0, s.weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     s.overlay ? ANTIALIASED_QUALITY : CLEARTYPE_QUALITY,
                                     DEFAULT_PITCH | FF_SWISS, L"Malgun Gothic");
        }
        return cache[i];
    }
    inline int LineHeight(HFONT font)
    {
        HDC dc = ::CreateCompatibleDC(nullptr);
        HGDIOBJ old = ::SelectObject(dc, font);
        TEXTMETRICW tm = {};
        ::GetTextMetricsW(dc, &tm);
        ::SelectObject(dc, old);
        ::DeleteDC(dc);
        return tm.tmHeight;
    }

    // 줄바꿈: 띄어쓰기(어절) 단위로 자른다. 한 어절이 너비보다 길 때만 글자 단위로.
    // (DrawText 의 줄바꿈은 Windows·글꼴에 따라 한글을 글자 단위로 자르기도 해서 직접 한다. 재기·그리기가 같은 규칙)
    // dc 에는 글꼴이 골라져 있어야 한다. "\n" 은 줄을 바꾼다.
    inline std::vector<std::wstring> WrapText(HDC dc, const std::wstring& text, int maxWidth)
    {
        auto width = [dc](const std::wstring& t) {
            SIZE size = {};
            ::GetTextExtentPoint32W(dc, t.c_str(), static_cast<int>(t.size()), &size);
            return static_cast<int>(size.cx);
        };
        auto trimRight = [](std::wstring& t) {
            while (!t.empty() && t.back() == L' ')
                t.pop_back();
        };
        std::vector<std::wstring> lines;
        size_t start = 0;
        for (;;)
        {
            const size_t newline = text.find(L'\n', start);
            const std::wstring para = text.substr(start, newline == std::wstring::npos ? std::wstring::npos : newline - start);
            std::wstring line;
            size_t i = 0;
            while (i < para.size())
            {
                size_t end = para.find(L' ', i);
                if (end == std::wstring::npos)
                    end = para.size();
                const std::wstring word = para.substr(i, end - i);
                size_t next = end;
                while (next < para.size() && para[next] == L' ')
                    ++next;
                if (!line.empty() && width(line + word) > maxWidth)
                {
                    trimRight(line);
                    lines.push_back(line);
                    line.clear();
                }
                if (line.empty() && width(word) > maxWidth)
                {
                    for (wchar_t ch : word)   // 너무 긴 어절(긴 품번 등)은 글자 단위로
                    {
                        if (!line.empty() && width(line + ch) > maxWidth)
                        {
                            lines.push_back(line);
                            line.clear();
                        }
                        line += ch;
                    }
                }
                else
                {
                    line += word;
                }
                line += para.substr(end, next - end);
                i = next;
            }
            trimRight(line);
            lines.push_back(line);
            if (newline == std::wstring::npos)
                break;
            start = newline + 1;
        }
        return lines;
    }

    // 글자 크기: maxWidth > 0 이면 그 너비에서 줄을 바꿨을 때의 크기
    struct TextSize
    {
        int cx = 0;
        int cy = 0;
    };
    inline TextSize MeasureText(HFONT font, const std::wstring& text, int maxWidth = 0)
    {
        static HDC dc = ::CreateCompatibleDC(nullptr);
        HGDIOBJ old = ::SelectObject(dc, font);
        TextSize size;
        if (maxWidth > 0)
        {
            TEXTMETRICW tm = {};
            ::GetTextMetricsW(dc, &tm);
            const std::vector<std::wstring> lines = WrapText(dc, text, maxWidth);
            for (const std::wstring& line : lines)
            {
                SIZE s = {};
                ::GetTextExtentPoint32W(dc, line.c_str(), static_cast<int>(line.size()), &s);
                size.cx = std::max(size.cx, static_cast<int>(s.cx));
            }
            size.cy = static_cast<int>(lines.size()) * tm.tmHeight;
        }
        else
        {
            RECT rc = { 0, 0, 0, 0 };
            ::DrawTextW(dc, text.empty() ? L" " : text.c_str(), -1, &rc, DT_CALCRECT | DT_NOPREFIX | DT_SINGLELINE);
            size.cx = text.empty() ? 0 : static_cast<int>(rc.right - rc.left);
            size.cy = static_cast<int>(rc.bottom - rc.top);
        }
        ::SelectObject(dc, old);
        return size;
    }

    // UTF-8(JSON) → 화면 글자
    inline std::wstring Wide(const std::string& utf8)
    {
        if (utf8.empty())
            return std::wstring();
        const int n = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
        std::wstring out(static_cast<size_t>(n > 0 ? n : 0), L'\0');
        if (n > 0)
            ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), &out[0], n);
        return out;
    }

    // ------------------------------------------------------------------ 그림판
    // 32비트 DIB(위 줄부터) + 그 DIB 를 고른 메모리 DC. 도형은 픽셀을 직접 섞어 가장자리를 부드럽게,
    // 글자는 GDI 로. 불투명 판은 ClearType 글자를 바로 쓰고, 투명 판은 흑백 판에 흰 글자를 써서
    // 그 밝기를 '덮는 정도'로 섞는다(알파가 살아 있는 글자 → OpenGL 로 3D 위에 얹을 수 있다).
    class Canvas
    {
    public:
        Canvas() = default;
        Canvas(const Canvas&) = delete;
        Canvas& operator=(const Canvas&) = delete;
        ~Canvas() { Release(); }

        bool Create(int width, int height, bool opaque, COLORREF fill = RGB(0, 0, 0), float alpha = 0.0f)
        {
            Release();
            if (width <= 0 || height <= 0)
                return false;
            m_w = width;
            m_h = height;
            m_opaque = opaque;
            m_dc = MakeDib(width, height, &m_bitmap, &m_old, &m_bits);
            if (!m_dc)
            {
                Release();
                return false;
            }
            Fill(fill, opaque ? 1.0f : alpha);
            return true;
        }

        int Width() const { return m_w; }
        int Height() const { return m_h; }
        HDC Dc() const { return m_dc; }
        const unsigned char* Bits() const { return m_bits; }

        void Fill(COLORREF c, float a)
        {
            ::GdiFlush();
            const BYTE A = static_cast<BYTE>(std::lround(std::min(std::max(a, 0.0f), 1.0f) * 255.0f));
            for (int i = 0; i < m_w * m_h; ++i)
            {
                unsigned char* p = m_bits + static_cast<size_t>(i) * 4;
                p[0] = GetBValue(c);
                p[1] = GetGValue(c);
                p[2] = GetRValue(c);
                p[3] = m_opaque ? 255 : A;
            }
        }

        // 둥근 사각형(r = 모서리 반지름, 0 이면 각진 사각형)
        void FillRound(float x, float y, float w, float h, float r, COLORREF c, float a = 1.0f)
        {
            r = std::min(std::max(r, 0.0f), std::min(w, h) * 0.5f);
            Shape(x - 1, y - 1, x + w + 1, y + h + 1, c, a, [=](float px, float py) {
                return Coverage(RoundDistance(px, py, x, y, w, h, r));
            });
        }
        // 테두리(두께 t, 안쪽으로)
        void StrokeRound(float x, float y, float w, float h, float r, float t, COLORREF c, float a = 1.0f)
        {
            r = std::min(std::max(r, 0.0f), std::min(w, h) * 0.5f);
            const float ri = std::max(r - t, 0.0f);
            Shape(x - 1, y - 1, x + w + 1, y + h + 1, c, a, [=](float px, float py) {
                const float outer = Coverage(RoundDistance(px, py, x, y, w, h, r));
                const float inner = Coverage(RoundDistance(px, py, x + t, y + t, w - 2 * t, h - 2 * t, ri));
                return std::max(outer - inner, 0.0f);
            });
        }
        // 부드러운 그림자(blur = 번지는 거리)
        void Shadow(float x, float y, float w, float h, float r, float blur, float a)
        {
            r = std::min(std::max(r, 0.0f), std::min(w, h) * 0.5f);
            Shape(x - blur, y - blur, x + w + blur, y + h + blur, RGB(0, 0, 0), a, [=](float px, float py) {
                const float d = RoundDistance(px, py, x, y, w, h, r);
                float t = std::min(std::max((blur - d) / (2.0f * blur), 0.0f), 1.0f);
                return t * t * (3.0f - 2.0f * t);
            });
        }
        void FillCircle(float cx, float cy, float radius, COLORREF c, float a = 1.0f)
        {
            FillRound(cx - radius, cy - radius, radius * 2, radius * 2, radius, c, a);
        }
        // 굵기 t 인 선(끝은 둥글게)
        void Line(float x0, float y0, float x1, float y1, float t, COLORREF c, float a = 1.0f)
        {
            const float dx = x1 - x0, dy = y1 - y0;
            const float len2 = std::max(dx * dx + dy * dy, 1e-6f);
            Shape(std::min(x0, x1) - t, std::min(y0, y1) - t, std::max(x0, x1) + t, std::max(y0, y1) + t, c, a,
                  [=](float px, float py) {
                      float s = ((px - x0) * dx + (py - y0) * dy) / len2;
                      s = std::min(std::max(s, 0.0f), 1.0f);
                      const float qx = x0 + dx * s - px, qy = y0 + dy * s - py;
                      return Coverage(std::sqrt(qx * qx + qy * qy) - t * 0.5f);
                  });
        }

        // 글자. format 은 DrawText 와 같다(DT_NOPREFIX 는 늘 붙인다). DT_WORDBREAK 면 WrapText 규칙으로 여러 줄.
        // 투명 판에서는 a 로 흐리게도 쓴다.
        void Text(HFONT font, const std::wstring& text, const RECT& rc, UINT format, COLORREF c, float a = 1.0f)
        {
            if (text.empty() || rc.right <= rc.left || rc.bottom <= rc.top || !m_dc)
                return;
            format |= DT_NOPREFIX;
            if (m_opaque)
            {
                ::GdiFlush();
                HGDIOBJ old = ::SelectObject(m_dc, font);
                ::SetBkMode(m_dc, TRANSPARENT);
                ::SetTextColor(m_dc, a >= 0.999f ? c : Mix(PixelAt((rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2), c, a));
                DrawLines(m_dc, text, rc, format);
                ::SelectObject(m_dc, old);
                ::GdiFlush();
                return;
            }
            if (!m_maskDc)
                m_maskDc = MakeDib(m_w, m_h, &m_maskBitmap, &m_maskOld, &m_maskBits);
            if (!m_maskDc)
                return;
            RECT r = { std::max(rc.left, 0L), std::max(rc.top, 0L), std::min(rc.right, static_cast<LONG>(m_w)),
                       std::min(rc.bottom, static_cast<LONG>(m_h)) };
            if (r.right <= r.left || r.bottom <= r.top)
                return;
            ::GdiFlush();
            ::FillRect(m_maskDc, &r, static_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH)));
            HGDIOBJ old = ::SelectObject(m_maskDc, font);
            ::SetBkMode(m_maskDc, TRANSPARENT);
            ::SetTextColor(m_maskDc, RGB(255, 255, 255));
            DrawLines(m_maskDc, text, rc, format);
            ::SelectObject(m_maskDc, old);
            ::GdiFlush();
            for (LONG y = r.top; y < r.bottom; ++y)
            {
                for (LONG x = r.left; x < r.right; ++x)
                {
                    const unsigned char* m = m_maskBits + (static_cast<size_t>(y) * m_w + x) * 4;
                    const int cover = std::max(m[0], std::max(m[1], m[2]));
                    if (cover > 0)
                        Blend(x, y, c, a * static_cast<float>(cover) / 255.0f);
                }
            }
        }

        // OpenGL 이 읽은 그림(BGRA, 아래 줄부터)을 dst 에 붙인다(불투명 판)
        void Image(const unsigned char* bgraBottomUp, int width, int height, const RECT& dst)
        {
            if (!bgraBottomUp || width <= 0 || height <= 0 || !m_dc)
                return;
            BITMAPINFO info = {};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = width;
            info.bmiHeader.biHeight = height;   // 양수 = 아래 줄부터
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            ::GdiFlush();
            ::SetStretchBltMode(m_dc, COLORONCOLOR);
            ::StretchDIBits(m_dc, dst.left, dst.top, dst.right - dst.left, dst.bottom - dst.top, 0, 0, width, height,
                            bgraBottomUp, &info, DIB_RGB_COLORS, SRCCOPY);
            ::GdiFlush();
            if (m_opaque)
            {
                for (LONG y = std::max(dst.top, 0L); y < std::min(dst.bottom, static_cast<LONG>(m_h)); ++y)
                    for (LONG x = std::max(dst.left, 0L); x < std::min(dst.right, static_cast<LONG>(m_w)); ++x)
                        m_bits[(static_cast<size_t>(y) * m_w + x) * 4 + 3] = 255;
            }
        }

        void BlitTo(HDC target, int x, int y) const
        {
            if (m_dc && target)
            {
                ::GdiFlush();
                ::BitBlt(target, x, y, m_w, m_h, m_dc, 0, 0, SRCCOPY);
            }
        }
        // 위 줄부터인 BGRA(알파 = 불투명도) 사본 → OpenGL 텍스처로
        std::vector<unsigned char> Pixels() const
        {
            ::GdiFlush();
            return std::vector<unsigned char>(m_bits, m_bits + static_cast<size_t>(m_w) * m_h * 4);
        }
        COLORREF PixelAt(int x, int y) const
        {
            if (!m_bits || x < 0 || y < 0 || x >= m_w || y >= m_h)
                return RGB(0, 0, 0);
            const unsigned char* p = m_bits + (static_cast<size_t>(y) * m_w + x) * 4;
            return RGB(p[2], p[1], p[0]);
        }

    private:
        // DT_WORDBREAK 면 WrapText 로 나눈 줄을 위에서부터 한 줄씩(가로 정렬은 format 대로), 아니면 DrawText 그대로
        static void DrawLines(HDC dc, const std::wstring& text, const RECT& rc, UINT format)
        {
            if (!(format & DT_WORDBREAK))
            {
                RECT r = rc;
                ::DrawTextW(dc, text.c_str(), -1, &r, format);
                return;
            }
            TEXTMETRICW tm = {};
            ::GetTextMetricsW(dc, &tm);
            const std::vector<std::wstring> lines = WrapText(dc, text, rc.right - rc.left);
            const UINT lineFormat = (format & (DT_CENTER | DT_RIGHT)) | DT_SINGLELINE | DT_NOPREFIX | DT_TOP;
            LONG y = rc.top;
            for (const std::wstring& line : lines)
            {
                RECT r = { rc.left, y, rc.right, y + tm.tmHeight };
                if (!line.empty())
                    ::DrawTextW(dc, line.c_str(), -1, &r, lineFormat);
                y += tm.tmHeight;
            }
        }
        static HDC MakeDib(int w, int h, HBITMAP* bitmap, HGDIOBJ* old, unsigned char** bits)
        {
            BITMAPINFO info = {};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = w;
            info.bmiHeader.biHeight = -h;   // 음수 = 위 줄부터
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            HDC dc = ::CreateCompatibleDC(nullptr);
            if (!dc)
                return nullptr;
            void* p = nullptr;
            *bitmap = ::CreateDIBSection(dc, &info, DIB_RGB_COLORS, &p, nullptr, 0);
            if (!*bitmap || !p)
            {
                ::DeleteDC(dc);
                *bitmap = nullptr;
                return nullptr;
            }
            *old = ::SelectObject(dc, *bitmap);
            *bits = static_cast<unsigned char*>(p);
            return dc;
        }
        void Release()
        {
            auto drop = [](HDC& dc, HBITMAP& bitmap, HGDIOBJ& old, unsigned char*& bits) {
                if (dc)
                {
                    ::SelectObject(dc, old);
                    ::DeleteDC(dc);
                }
                if (bitmap)
                    ::DeleteObject(bitmap);
                dc = nullptr;
                bitmap = nullptr;
                old = nullptr;
                bits = nullptr;
            };
            drop(m_dc, m_bitmap, m_old, m_bits);
            drop(m_maskDc, m_maskBitmap, m_maskOld, m_maskBits);
            m_w = m_h = 0;
        }
        // 둥근 사각형까지의 거리(안쪽은 음수)
        static float RoundDistance(float px, float py, float x, float y, float w, float h, float r)
        {
            const float cx = x + w * 0.5f, cy = y + h * 0.5f;
            const float qx = std::fabs(px - cx) - (w * 0.5f - r);
            const float qy = std::fabs(py - cy) - (h * 0.5f - r);
            const float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
            return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - r;
        }
        static float Coverage(float distance) { return std::min(std::max(0.5f - distance, 0.0f), 1.0f); }

        template <class F>
        void Shape(float x0, float y0, float x1, float y1, COLORREF c, float a, F coverage)
        {
            if (!m_bits || a <= 0.0f)
                return;
            ::GdiFlush();
            const int ix0 = std::max(static_cast<int>(std::floor(x0)), 0);
            const int iy0 = std::max(static_cast<int>(std::floor(y0)), 0);
            const int ix1 = std::min(static_cast<int>(std::ceil(x1)), m_w);
            const int iy1 = std::min(static_cast<int>(std::ceil(y1)), m_h);
            for (int y = iy0; y < iy1; ++y)
                for (int x = ix0; x < ix1; ++x)
                {
                    const float cover = coverage(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f);
                    if (cover > 0.0f)
                        Blend(x, y, c, cover * a);
                }
        }
        // 한 픽셀에 색 c 를 불투명도 a 로 얹는다(투명 판은 알파까지 '덮기' 규칙대로)
        void Blend(int x, int y, COLORREF c, float a)
        {
            if (a <= 0.0f || x < 0 || y < 0 || x >= m_w || y >= m_h)
                return;
            a = std::min(a, 1.0f);
            unsigned char* p = m_bits + (static_cast<size_t>(y) * m_w + x) * 4;
            const float s[3] = { static_cast<float>(GetBValue(c)), static_cast<float>(GetGValue(c)), static_cast<float>(GetRValue(c)) };
            if (m_opaque)
            {
                for (int k = 0; k < 3; ++k)
                    p[k] = static_cast<unsigned char>(std::lround(static_cast<float>(p[k]) + (s[k] - static_cast<float>(p[k])) * a));
                p[3] = 255;
                return;
            }
            const float da = static_cast<float>(p[3]) / 255.0f;
            const float oa = a + da * (1.0f - a);
            if (oa <= 0.0f)
                return;
            for (int k = 0; k < 3; ++k)
                p[k] = static_cast<unsigned char>(std::lround((s[k] * a + static_cast<float>(p[k]) * da * (1.0f - a)) / oa));
            p[3] = static_cast<unsigned char>(std::lround(oa * 255.0f));
        }

        HDC m_dc = nullptr;
        HBITMAP m_bitmap = nullptr;
        HGDIOBJ m_old = nullptr;
        unsigned char* m_bits = nullptr;
        HDC m_maskDc = nullptr;
        HBITMAP m_maskBitmap = nullptr;
        HGDIOBJ m_maskOld = nullptr;
        unsigned char* m_maskBits = nullptr;
        int m_w = 0;
        int m_h = 0;
        bool m_opaque = true;
    };

    // 작은 꼬리표(둥근 칸 + 글자). 너비를 돌려준다. paint = false 면 재기만.
    inline int Chip(Canvas* c, bool paint, HFONT font, const std::wstring& text, int x, int y, int height,
                    COLORREF back, COLORREF fore, float backAlpha = 1.0f)
    {
        const int padX = Px(8);
        const int width = MeasureText(font, text).cx + padX * 2;
        if (paint && c)
        {
            c->FillRound(static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height),
                         static_cast<float>(height) * 0.5f, back, backAlpha);
            RECT rc = { x, y, x + width, y + height };
            c->Text(font, text, rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE, fore);
        }
        return width;
    }

    // 키 모양(예: [R]) + 설명. 너비를 돌려준다.
    inline int KeyCap(Canvas* c, bool paint, HFONT keyFont, HFONT labelFont, const std::wstring& key, const std::wstring& label,
                      int x, int y, int height, COLORREF keyBack, COLORREF keyLine, COLORREF keyText, COLORREF labelText,
                      float backAlpha = 1.0f)
    {
        const int keyWidth = std::max(MeasureText(keyFont, key).cx + Px(10), height);
        const int labelWidth = label.empty() ? 0 : MeasureText(labelFont, label).cx;
        if (paint && c)
        {
            const float fx = static_cast<float>(x), fy = static_cast<float>(y);
            c->FillRound(fx, fy, static_cast<float>(keyWidth), static_cast<float>(height), PxF(4), keyBack, backAlpha);
            c->StrokeRound(fx, fy, static_cast<float>(keyWidth), static_cast<float>(height), PxF(4), 1.0f, keyLine, 0.9f);
            RECT k = { x, y, x + keyWidth, y + height };
            c->Text(keyFont, key, k, DT_CENTER | DT_VCENTER | DT_SINGLELINE, keyText);
            if (!label.empty())
            {
                RECT l = { x + keyWidth + Px(6), y, x + keyWidth + Px(6) + labelWidth + Px(2), y + height };
                c->Text(labelFont, label, l, DT_LEFT | DT_VCENTER | DT_SINGLELINE, labelText);
            }
        }
        return keyWidth + (label.empty() ? 0 : Px(6) + labelWidth);
    }

    // 정육면체 선 그림(③ 칸이 비었을 때)
    inline void CubeIcon(Canvas& c, float cx, float cy, float size, COLORREF color, float a)
    {
        const float s = size * 0.5f, t = PxF(1.6f);
        const float topY = cy - s, midY = cy - s * 0.42f, lowY = cy + s * 0.58f, botY = cy + s;
        const float lx = cx - s * 0.87f, rx = cx + s * 0.87f;
        c.Line(cx, topY, rx, midY, t, color, a);
        c.Line(rx, midY, cx, cy + s * 0.16f, t, color, a);
        c.Line(cx, cy + s * 0.16f, lx, midY, t, color, a);
        c.Line(lx, midY, cx, topY, t, color, a);
        c.Line(lx, midY, lx, lowY, t, color, a);
        c.Line(rx, midY, rx, lowY, t, color, a);
        c.Line(cx, cy + s * 0.16f, cx, botY, t, color, a);
        c.Line(lx, lowY, cx, botY, t, color, a);
        c.Line(rx, lowY, cx, botY, t, color, a);
    }

    // ------------------------------------------------------------------ ③ 부품 이미지 칸 그리기
    // 7주차 2차: 위 띠(이름·품번·고전압)를 뺐다 — 같은 글이 ② 카드 머리에 있다. ③ 은 그림만(칸 전체).
    struct PreviewView
    {
        bool hasPart = false;
        std::wstring number;          // 품번(@위치 포함) 예: EV-BRC-001@FL  (그리지는 않는다 — ② 카드 머리에 있다)
        std::wstring name;            // 이름 예: 브레이크 캘리퍼           (〃)
        bool highVoltage = false;     //                                    (〃)
        std::wstring line;            // 그림 아래쪽 한 줄(검색 결과 또는 사용법). 빈 글 = 없음
        bool lineIsResult = false;    // 검색 결과면 강조색
    };
    inline int PreviewTitleHeight() { return 0; }   // 위 띠 없음(예전 34px)
    inline int PreviewLineHeight() { return LineHeight(GetFont(Font::Small)) + Px(10); }   // 아래 알림 줄 칸 높이

    // client 크기의 불투명 판에 ③ 칸 전체를 그린다. image 는 OpenGL 이 그린 부품 그림(없으면 nullptr).
    inline void PaintPreview(Canvas& c, const PreviewView& v, const unsigned char* image, int imageWidth, int imageHeight)
    {
        const int w = c.Width(), h = c.Height();
        const int titleH = PreviewTitleHeight();
        c.Fill(kPanelBg, 1.0f);

        // 그림 칸(칸 전체)
        RECT area = { 0, titleH, w, h };
        if (v.hasPart && image)
        {
            c.Image(image, imageWidth, imageHeight, area);
        }
        else
        {
            const float cx = w * 0.5f;
            const float cy = titleH + (h - titleH) * 0.42f;
            CubeIcon(c, cx, cy, PxF(46), kTextDim, 0.7f);
            const std::wstring hint = v.hasPart ? L"그림을 준비하는 중…" : L"3D 뷰에서 부품을 클릭하면\n여기에 그 부품만 크게 보여요.";
            const int textW = std::max(w - Px(32), Px(60));
            const TextSize size = MeasureText(GetFont(Font::Body), hint, textW);
            const int top = static_cast<int>(cy + PxF(40));
            RECT rc = { (w - textW) / 2, top, (w + textW) / 2, top + size.cy };
            c.Text(GetFont(Font::Body), hint, rc, DT_CENTER | DT_WORDBREAK, kTextDim);
        }

        // 3) 아래 알림 줄: 반투명 둥근 칸 + 글자(검색 결과는 파랑 점)
        if (!v.line.empty() && h - titleH > PreviewLineHeight() * 2)
        {
            HFONT font = GetFont(Font::Small);
            const int lineH = PreviewLineHeight() - Px(8);
            const int dot = v.lineIsResult ? Px(14) : 0;
            const int textW = std::min(MeasureText(font, v.line).cx, w - Px(16) - Px(20) - dot);
            const int boxW = textW + Px(20) + dot;
            const int x = Px(8), y = h - Px(8) - lineH;
            c.FillRound(static_cast<float>(x), static_cast<float>(y), static_cast<float>(boxW), static_cast<float>(lineH),
                        static_cast<float>(lineH) * 0.5f, RGB(12, 14, 18), 0.82f);
            if (dot)
                c.FillCircle(static_cast<float>(x + Px(12)), static_cast<float>(y) + lineH * 0.5f, PxF(3.5f), kAccent);
            RECT rc = { x + Px(10) + dot, y, x + Px(10) + dot + textW, y + lineH };
            c.Text(font, v.line, rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, v.lineIsResult ? kText : kTextSub);
        }
    }

    // ------------------------------------------------------------------ 대화상자 꾸미기
    namespace detail
    {
        struct DialogTheme
        {
            HWND dialog = nullptr;
            HWND edit = nullptr;
            HWND button = nullptr;
            std::vector<HWND> groups;   // 숨긴 그룹 상자(자리만 쓴다: 그 안의 컨트롤을 둘러싼 둥근 패널)
            bool buttonHot = false;
            HBRUSH windowBrush = nullptr;
            HBRUSH inputBrush = nullptr;
        };
        inline DialogTheme& Theme()
        {
            static DialogTheme theme;
            return theme;
        }

        inline RECT ChildRect(HWND dialog, HWND child)
        {
            RECT rc = {};
            ::GetWindowRect(child, &rc);
            ::MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT*>(&rc), 2);
            return rc;
        }

        // 그룹 상자 자리 안에 보이는 컨트롤들을 둘러싼 사각형 + 여백 8 → 둥근 패널 자리. inner = 여백 전(컨트롤들만)
        inline RECT PanelRect(HWND dialog, HWND group, RECT* innerOut = nullptr)
        {
            const RECT box = ChildRect(dialog, group);
            if (innerOut)
                *innerOut = box;
            RECT inner = { 0, 0, 0, 0 };
            bool any = false;
            for (HWND child = ::GetWindow(dialog, GW_CHILD); child; child = ::GetWindow(child, GW_HWNDNEXT))
            {
                if (child == group || !::IsWindowVisible(child))
                    continue;
                const RECT rc = ChildRect(dialog, child);
                const POINT mid = { (rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2 };
                if (!::PtInRect(&box, mid))
                    continue;
                if (!any)
                    inner = rc;
                else
                    ::UnionRect(&inner, &inner, &rc);
                any = true;
            }
            if (!any)
                return box;
            if (innerOut)
                *innerOut = inner;
            ::InflateRect(&inner, Px(8), Px(8));
            RECT out = {};
            ::IntersectRect(&out, &inner, &box);
            return out;
        }

        // 3D 뷰(CGLView, 창 글자 "GLView") 자리. 없으면 false
        inline bool FindGlView(HWND dialog, RECT* rc)
        {
            for (HWND child = ::GetWindow(dialog, GW_CHILD); child; child = ::GetWindow(child, GW_HWNDNEXT))
            {
                wchar_t text[16] = {};
                ::GetWindowTextW(child, text, 16);
                if (::lstrcmpW(text, L"GLView") == 0 && ::IsWindowVisible(child))
                {
                    *rc = ChildRect(dialog, child);
                    return true;
                }
            }
            return false;
        }

        // 검색 칸 둘레(대화상자가 그리는 입력 칸 모양): 편집 칸보다 조금 크게, 높이는 [검색] 단추와 같게
        inline RECT SearchFrame(const DialogTheme& t)
        {
            RECT edit = ChildRect(t.dialog, t.edit);
            RECT frame = edit;
            frame.left -= Px(10);
            frame.right += Px(6);
            if (t.button && ::IsWindow(t.button))
            {
                const RECT button = ChildRect(t.dialog, t.button);
                frame.top = button.top;
                frame.bottom = button.bottom;
            }
            else
            {
                ::InflateRect(&frame, 0, Px(4));
            }
            return frame;
        }

        // 위아래로 붙어 있고 좌우 끝이 같은 패널(② 와 ③)은 한 판으로 잇는다. 이은 자리에는 가는 구분선.
        // 판의 위·아래 끝이 3D 뷰의 위·아래 끝과 조금(24px 안) 어긋나 있으면 3D 뷰에 맞춘다(안의 컨트롤은 늘 판 안에).
        struct PanelRun
        {
            RECT rc = {};
            RECT inner = {};             // 판 안 컨트롤들이 차지한 곳(여백 전)
            std::vector<int> dividers;   // 구분선 y(대화상자 좌표)
        };
        inline std::vector<PanelRun> PanelRuns(const DialogTheme& t)
        {
            std::vector<PanelRun> panels;
            for (HWND group : t.groups)
            {
                if (!::IsWindow(group))
                    continue;
                PanelRun p;
                p.rc = PanelRect(t.dialog, group, &p.inner);
                if (p.rc.right - p.rc.left > 2 && p.rc.bottom - p.rc.top > 2)
                    panels.push_back(p);
            }
            std::sort(panels.begin(), panels.end(), [](const PanelRun& a, const PanelRun& b) { return a.rc.top < b.rc.top; });
            std::vector<PanelRun> runs;
            for (const PanelRun& p : panels)
            {
                bool joined = false;
                for (PanelRun& run : runs)
                {
                    const LONG gap = p.rc.top - run.rc.bottom;
                    if (std::abs(p.rc.left - run.rc.left) <= Px(6) && std::abs(p.rc.right - run.rc.right) <= Px(6) &&
                        gap >= -Px(2) && gap <= Px(28))
                    {
                        run.dividers.push_back((run.rc.bottom + p.rc.top) / 2);
                        run.rc.left = std::min(run.rc.left, p.rc.left);
                        run.rc.right = std::max(run.rc.right, p.rc.right);
                        run.rc.bottom = std::max(run.rc.bottom, p.rc.bottom);
                        run.inner.bottom = std::max(run.inner.bottom, p.inner.bottom);
                        joined = true;
                        break;
                    }
                }
                if (!joined)
                    runs.push_back(p);
            }
            RECT gl = {};
            if (FindGlView(t.dialog, &gl))
            {
                for (PanelRun& run : runs)
                {
                    if (std::abs(run.rc.top - gl.top) <= Px(24) && gl.top <= run.inner.top - Px(4))
                        run.rc.top = gl.top;
                    if (std::abs(run.rc.bottom - gl.bottom) <= Px(24) && gl.bottom >= run.inner.bottom + Px(4))
                        run.rc.bottom = gl.bottom;
                }
            }
            return runs;
        }

        inline void PaintBackground(HDC dc)
        {
            DialogTheme& t = Theme();
            RECT client = {};
            ::GetClientRect(t.dialog, &client);
            ::FillRect(dc, &client, t.windowBrush);
            for (const PanelRun& run : PanelRuns(t))
            {
                const RECT& rc = run.rc;
                const int w = rc.right - rc.left, h = rc.bottom - rc.top;
                Canvas panel;
                if (w <= 2 || h <= 2 || !panel.Create(w, h, true, kWindowBg))
                    continue;
                panel.FillRound(0, 0, static_cast<float>(w), static_cast<float>(h), PxF(8), kPanelBg);
                panel.StrokeRound(0, 0, static_cast<float>(w), static_cast<float>(h), PxF(8), 1.0f, kPanelLine);
                for (int y : run.dividers)
                    panel.FillRound(static_cast<float>(Px(12)), static_cast<float>(y - rc.top), static_cast<float>(w - Px(24)), 1.0f, 0,
                                    kPanelLine);
                panel.BlitTo(dc, rc.left, rc.top);
            }
            if (t.edit && ::IsWindow(t.edit) && ::IsWindowVisible(t.edit))
            {
                const RECT rc = SearchFrame(t);
                const int w = rc.right - rc.left, h = rc.bottom - rc.top;
                Canvas frame;
                if (w > 4 && h > 4 && frame.Create(w, h, true, kPanelBg))
                {
                    const bool focused = ::GetFocus() == t.edit;
                    frame.FillRound(0, 0, static_cast<float>(w), static_cast<float>(h), PxF(6), kInputBg);
                    frame.StrokeRound(0, 0, static_cast<float>(w), static_cast<float>(h), PxF(6), focused ? 1.5f : 1.0f,
                                      focused ? kAccent : kInputLine);
                    frame.BlitTo(dc, rc.left, rc.top);
                }
            }
        }

        inline void PaintButton(const DRAWITEMSTRUCT* item)
        {
            const DialogTheme& t = Theme();
            const int w = item->rcItem.right - item->rcItem.left, h = item->rcItem.bottom - item->rcItem.top;
            Canvas c;
            if (!c.Create(w, h, true, kPanelBg))
                return;
            const bool down = (item->itemState & ODS_SELECTED) != 0;
            const bool disabled = (item->itemState & ODS_DISABLED) != 0;
            const COLORREF back = disabled ? kChipBg : (down ? kAccentDown : (t.buttonHot ? kAccentHot : kAccent));
            c.FillRound(0, 0, static_cast<float>(w), static_cast<float>(h), PxF(6), back);
            if (item->itemState & ODS_FOCUS)
                c.StrokeRound(1, 1, static_cast<float>(w - 2), static_cast<float>(h - 2), PxF(5), 1.0f, RGB(255, 255, 255), 0.55f);
            wchar_t text[64] = {};
            ::GetWindowTextW(item->hwndItem, text, 64);
            RECT rc = { 0, down ? 1 : 0, w, h + (down ? 1 : 0) };
            c.Text(GetFont(Font::BodyBold), text, rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE, disabled ? kTextDim : RGB(255, 255, 255));
            c.BlitTo(item->hDC, item->rcItem.left, item->rcItem.top);
        }

        inline LRESULT CALLBACK ButtonProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR)
        {
            DialogTheme& t = Theme();
            switch (msg)
            {
            case WM_MOUSEMOVE:
                if (!t.buttonHot)
                {
                    t.buttonHot = true;
                    TRACKMOUSEEVENT track = { sizeof(track), TME_LEAVE, hwnd, 0 };
                    ::TrackMouseEvent(&track);
                    ::InvalidateRect(hwnd, nullptr, FALSE);
                }
                break;
            case WM_MOUSELEAVE:
                t.buttonHot = false;
                ::InvalidateRect(hwnd, nullptr, FALSE);
                break;
            case WM_NCDESTROY:
                ::RemoveWindowSubclass(hwnd, ButtonProc, id);
                break;
            default:
                break;
            }
            return ::DefSubclassProc(hwnd, msg, wParam, lParam);
        }

        inline LRESULT CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR)
        {
            DialogTheme& t = Theme();
            switch (msg)
            {
            case WM_ERASEBKGND:
                PaintBackground(reinterpret_cast<HDC>(wParam));
                return 1;
            case WM_CTLCOLORDLG:
                return reinterpret_cast<LRESULT>(t.windowBrush);
            case WM_CTLCOLORSTATIC:
            case WM_CTLCOLORBTN:
                ::SetTextColor(reinterpret_cast<HDC>(wParam), kTextSub);
                ::SetBkColor(reinterpret_cast<HDC>(wParam), kWindowBg);
                return reinterpret_cast<LRESULT>(t.windowBrush);
            case WM_CTLCOLOREDIT:
                if (reinterpret_cast<HWND>(lParam) == t.edit)
                {
                    ::SetTextColor(reinterpret_cast<HDC>(wParam), kText);
                    ::SetBkColor(reinterpret_cast<HDC>(wParam), kInputBg);
                    return reinterpret_cast<LRESULT>(t.inputBrush);
                }
                break;
            case WM_DRAWITEM:
                if (reinterpret_cast<const DRAWITEMSTRUCT*>(lParam)->hwndItem == t.button)
                {
                    PaintButton(reinterpret_cast<const DRAWITEMSTRUCT*>(lParam));
                    return TRUE;
                }
                break;
            case WM_COMMAND:
                // 검색 칸에 커서가 들어오고 나갈 때 테두리 색(파랑 ↔ 회색)
                if (reinterpret_cast<HWND>(lParam) == t.edit && (HIWORD(wParam) == EN_SETFOCUS || HIWORD(wParam) == EN_KILLFOCUS))
                {
                    RECT rc = SearchFrame(t);
                    ::InvalidateRect(hwnd, &rc, TRUE);
                }
                break;
            case WM_NCDESTROY:
                ::RemoveWindowSubclass(hwnd, DialogProc, id);
                if (t.windowBrush)
                    ::DeleteObject(t.windowBrush);
                if (t.inputBrush)
                    ::DeleteObject(t.inputBrush);
                t = DialogTheme();
                break;
            default:
                break;
            }
            return ::DefSubclassProc(hwnd, msg, wParam, lParam);
        }

        // Windows 10(20H1~)·11: 제목 줄도 어둡게. 11 은 제목 줄 색까지 바탕과 같게. 안 되는 Windows 면 그냥 넘어간다.
        inline void DarkTitleBar(HWND hwnd)
        {
            using SetAttribute = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
            static SetAttribute set = [] {
                HMODULE dwm = ::LoadLibraryW(L"dwmapi.dll");
                return dwm ? reinterpret_cast<SetAttribute>(reinterpret_cast<void*>(::GetProcAddress(dwm, "DwmSetWindowAttribute"))) : nullptr;
            }();
            if (!set)
                return;
            const BOOL on = TRUE;
            if (FAILED(set(hwnd, 20, &on, sizeof(on))))   // DWMWA_USE_IMMERSIVE_DARK_MODE
                set(hwnd, 19, &on, sizeof(on));           // 예전 번호(1809~1909)
            const COLORREF caption = kWindowBg, text = kText, border = kPanelLine;
            set(hwnd, 35, &caption, sizeof(caption));     // DWMWA_CAPTION_COLOR (11)
            set(hwnd, 36, &text, sizeof(text));           // DWMWA_TEXT_COLOR (11)
            set(hwnd, 34, &border, sizeof(border));       // DWMWA_BORDER_COLOR (11)
        }
    }

    // 편집 칸 높이: 글꼴 한 줄 + 조금(검색 줄 가운데에 놓는다)
    inline int EditHeight() { return LineHeight(GetFont(Font::Body)) + Px(4); }

    // 대화상자를 어두운 화면으로. edit·button = ③ 아래 검색 칸과 [검색] 단추(없으면 nullptr).
    inline void ThemeDialog(HWND dialog, HWND edit, HWND button)
    {
        if (!dialog || !::IsWindow(dialog))
            return;
        detail::DialogTheme& t = detail::Theme();
        if (t.dialog == dialog)
            return;   // 이미 했다
        t.dialog = dialog;
        t.edit = edit;
        t.button = button;
        t.windowBrush = ::CreateSolidBrush(kWindowBg);
        t.inputBrush = ::CreateSolidBrush(kInputBg);

        // 그룹 상자는 숨기고 자리만 쓴다(둥근 패널을 대화상자 바탕에 그린다. 제목은 패널 안에서 각자 그린다)
        for (HWND child = ::GetWindow(dialog, GW_CHILD); child; child = ::GetWindow(child, GW_HWNDNEXT))
        {
            wchar_t name[16] = {};
            ::GetClassNameW(child, name, 16);
            if (::lstrcmpiW(name, L"Button") == 0 && (::GetWindowLongPtrW(child, GWL_STYLE) & BS_TYPEMASK) == BS_GROUPBOX)
            {
                ::ShowWindow(child, SW_HIDE);
                t.groups.push_back(child);
            }
        }
        if (edit && ::IsWindow(edit))
        {
            // 테두리 없는 편집 칸(둘레는 대화상자 바탕에 둥글게 그린다), 맑은 고딕, 양옆 여백
            ::SetWindowLongPtrW(edit, GWL_EXSTYLE, ::GetWindowLongPtrW(edit, GWL_EXSTYLE) & ~static_cast<LONG_PTR>(WS_EX_CLIENTEDGE));
            ::SetWindowLongPtrW(edit, GWL_STYLE, ::GetWindowLongPtrW(edit, GWL_STYLE) & ~static_cast<LONG_PTR>(WS_BORDER));
            ::SetWindowPos(edit, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            ::SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(GetFont(Font::Body)), TRUE);
            ::SendMessageW(edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELONG(Px(2), Px(2)));
        }
        if (button && ::IsWindow(button))
        {
            // 단추는 직접 그린다(파란 둥근 단추). 누르면 BN_CLICKED 는 그대로 대화상자로 간다.
            const LONG_PTR style = ::GetWindowLongPtrW(button, GWL_STYLE);
            ::SetWindowLongPtrW(button, GWL_STYLE, (style & ~static_cast<LONG_PTR>(BS_TYPEMASK)) | BS_OWNERDRAW);
            ::SetWindowSubclass(button, detail::ButtonProc, 1, 0);
        }
        ::SetWindowSubclass(dialog, detail::DialogProc, 1, 0);
        detail::DarkTitleBar(dialog);
        ::RedrawWindow(dialog, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    }
}
