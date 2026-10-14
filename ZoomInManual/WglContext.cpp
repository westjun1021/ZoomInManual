// WglContext.cpp
#include "pch.h"
#include "WglContext.h"

#pragma comment(lib, "opengl32.lib")

namespace
{
    // wglext.h에 있는 상수들. 헤더를 따로 받지 않으려고 직접 적었다.
    constexpr int kContextMajorVersion = 0x2091;   // WGL_CONTEXT_MAJOR_VERSION_ARB
    constexpr int kContextMinorVersion = 0x2092;   // WGL_CONTEXT_MINOR_VERSION_ARB
    constexpr int kContextFlags        = 0x2094;   // WGL_CONTEXT_FLAGS_ARB
    constexpr int kContextProfileMask  = 0x9126;   // WGL_CONTEXT_PROFILE_MASK_ARB
    constexpr int kCoreProfileBit      = 0x0001;   // WGL_CONTEXT_CORE_PROFILE_BIT_ARB
    constexpr int kDebugBit            = 0x0001;   // WGL_CONTEXT_DEBUG_BIT_ARB

    using CreateContextAttribsFn = HGLRC(WINAPI*)(HDC, HGLRC, const int*);
}

bool WglContext::Create(HWND hwnd, bool debugContext)
{
    Destroy();
    m_hwnd = hwnd;
    m_hdc = ::GetDC(hwnd);
    if (!m_hdc)
    {
        m_message = "GetDC failed";
        return false;
    }

    // 1) 픽셀 포맷: 32비트 색 + 24비트 깊이 + 8비트 스텐실, 더블 버퍼
    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cAlphaBits = 8;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;

    const int format = ::ChoosePixelFormat(m_hdc, &pfd);
    if (format == 0 || !::SetPixelFormat(m_hdc, format, &pfd))
    {
        m_message = "SetPixelFormat failed";
        Destroy();
        return false;
    }

    // 2) 임시(예전 방식) 컨텍스트를 만들어야 wglCreateContextAttribsARB 주소를 얻을 수 있다.
    HGLRC temp = ::wglCreateContext(m_hdc);
    if (!temp || !::wglMakeCurrent(m_hdc, temp))
    {
        if (temp) ::wglDeleteContext(temp);
        m_message = "wglCreateContext failed";
        Destroy();
        return false;
    }

    // 3) 3.3 Core 컨텍스트 생성
    auto createContextAttribs = reinterpret_cast<CreateContextAttribsFn>(
        reinterpret_cast<void (*)()>(::wglGetProcAddress("wglCreateContextAttribsARB")));
    if (createContextAttribs)
    {
        const int attribs[] = {
            kContextMajorVersion, 3,
            kContextMinorVersion, 3,
            kContextProfileMask,  kCoreProfileBit,
            kContextFlags,        debugContext ? kDebugBit : 0,
            0
        };
        HGLRC core = createContextAttribs(m_hdc, nullptr, attribs);
        if (core)
        {
            ::wglMakeCurrent(nullptr, nullptr);
            ::wglDeleteContext(temp);
            temp = nullptr;
            m_hglrc = core;
            m_isCore = true;
        }
    }

    // 4) 실패하면 임시 컨텍스트를 그대로 쓴다(그래픽 드라이버가 오래됐거나 원격 데스크톱일 때).
    if (!m_hglrc)
    {
        m_hglrc = temp;
        m_isCore = false;
        m_message = "OpenGL 3.3 Core context not available - using compatibility context";
    }

    if (!::wglMakeCurrent(m_hdc, m_hglrc))
    {
        m_message = "wglMakeCurrent failed";
        Destroy();
        return false;
    }
    return true;
}

void WglContext::Destroy()
{
    if (m_hglrc)
    {
        if (::wglGetCurrentContext() == m_hglrc)
            ::wglMakeCurrent(nullptr, nullptr);
        ::wglDeleteContext(m_hglrc);
        m_hglrc = nullptr;
    }
    if (m_hdc && m_hwnd)
        ::ReleaseDC(m_hwnd, m_hdc);
    m_hdc = nullptr;
    m_hwnd = nullptr;
    m_isCore = false;
}

bool WglContext::MakeCurrent()
{
    return m_hdc && m_hglrc && ::wglMakeCurrent(m_hdc, m_hglrc);
}

void WglContext::Swap()
{
    if (m_hdc)
        ::SwapBuffers(m_hdc);
}
