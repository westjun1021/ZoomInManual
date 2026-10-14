// WglContext.h
// 창(HWND) 하나에 OpenGL 3.3 Core 컨텍스트를 만들어 주는 클래스.
// MFC를 쓰지 않는 순수 Win32 코드라서 나중에 다른 창에도 그대로 쓸 수 있다.
#pragma once
#include <windows.h>
#include <string>

class WglContext
{
public:
    ~WglContext() { Destroy(); }

    // hwnd에 픽셀 포맷을 정하고 3.3 Core 컨텍스트를 만든다.
    // 3.3 Core를 못 만들면 예전 방식(호환) 컨텍스트로 대신 만든다 → IsCore()가 false.
    bool Create(HWND hwnd, bool debugContext = false);
    void Destroy();

    bool MakeCurrent();   // 이 컨텍스트를 현재 스레드에 연결
    void Swap();          // 더블 버퍼 교체(그린 결과를 화면에 표시)

    bool IsCore() const { return m_isCore; }
    const std::string& Message() const { return m_message; }   // 실패 이유나 안내 문구

private:
    HWND  m_hwnd = nullptr;
    HDC   m_hdc = nullptr;
    HGLRC m_hglrc = nullptr;
    bool  m_isCore = false;
    std::string m_message;
};
