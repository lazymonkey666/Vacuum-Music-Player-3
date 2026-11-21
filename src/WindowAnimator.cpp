#include "pch.h"
#include "WindowAnimator.h"
#include <AppState.h>
#include <algorithm>
#include <cmath>

WindowAnimator& WindowAnimator::Get() {
    static WindowAnimator inst;
    return inst;
}

void WindowAnimator::Init(WindowHandle hwnd) {
    m_hwnd = hwnd;
    m_alpha = 255;
    m_hidden = false;
    m_animating = false;
    UpdateSide();
}

void WindowAnimator::Reset() {
    if (m_animTimer) { StopWindowTimer(m_hwnd, m_animTimer); m_animTimer = 0; }
    m_animating = false;
    m_alpha = 255;
    m_hidden = false;
    SetWindowAlpha(m_hwnd, 255);
}

void WindowAnimator::UpdateSide() {
    RECT rc = GetWindowRectPlat(m_hwnd);
    int screenWidth = GetScreenWidth();
    int centerX = rc.left + (rc.right - rc.left) / 2;
    m_side = (centerX < screenWidth / 2) ? 0 : 1;
    m_displayX = (m_side == 0) ? 0 : screenWidth - (rc.right - rc.left);
    m_displayY = rc.top;
}

void WindowAnimator::HideShow(bool hide) {
    if (m_animating) return;

    RECT cur = GetWindowRectPlat(m_hwnd);
    int winW = cur.right - cur.left;
    int winH = cur.bottom - cur.top;
    RECT target = cur;
    BYTE targetAlpha = 255;

    m_animStartW = winW;
    m_animStartH = winH;

    if (hide) {
        targetAlpha = 0;
        if (g_enableSnapToEdge.Get()) {
            if (m_side == 0) { target.left = -winW; target.right = 0; }
            else { target.left = GetScreenWidth(); target.right = GetScreenWidth() + winW; }
            m_animTargetW = winW;
            m_animTargetH = winH;
        }
        else {
            m_animTargetW = 0;
            m_animTargetH = 0;
        }
    }
    else {
        targetAlpha = 255;
        if (g_enableSnapToEdge.Get()) {
            target.left = m_displayX;
            target.right = m_displayX + winW;
            target.top = m_displayY;
            target.bottom = m_displayY + winH;
            m_animTargetW = winW;
            m_animTargetH = winH;
        }
        else {
            ShowWindowPlat(m_hwnd);
            m_animTargetW = g_normalWidth;
            m_animTargetH = g_normalHeight;
        }
    }

    m_animStartRect = cur;
    m_animTargetRect = target;
    m_animStartAlpha = m_alpha;
    m_animTargetAlpha = targetAlpha;
    m_animStep = 0;
    m_animating = true;
    m_targetHidden = hide;

    if (m_animTimer) StopWindowTimer(m_hwnd, m_animTimer);
    m_animTimer = StartWindowTimer(m_hwnd, 10);
}

void WindowAnimator::Stop() {
    if (!m_animating) return;
    if (m_animTimer) { StopWindowTimer(m_hwnd, m_animTimer); m_animTimer = 0; }
    m_animating = false;

    RECT rc = GetWindowRectPlat(m_hwnd);
    int w = g_normalWidth > 0 ? g_normalWidth : 700;
    int h = g_normalHeight > 0 ? g_normalHeight : 230;
    MoveResizeWindowPlat(m_hwnd, rc.left, rc.top, w, h);
    SetWindowAlpha(m_hwnd, 255);
    m_alpha = 255;
    m_hidden = false;
    UpdateSide();
}

void WindowAnimator::SnapToEdge() {
    if (m_animating) return;
    if (!g_enableSnapToEdge.Get()) return;

    RECT rc = GetWindowRectPlat(m_hwnd);
    int winW = rc.right - rc.left;
    int winH = rc.bottom - rc.top;
    int screenW = GetScreenWidth();

    int centerX = rc.left + winW / 2;
    int targetX, targetSide;
    if (centerX < screenW / 2) { targetX = 0; targetSide = 0; }
    else { targetX = screenW - winW; targetSide = 1; }
    int targetY = rc.top;

    if (rc.left == targetX && rc.top == targetY) { UpdateSide(); return; }

    m_animStartRect = rc;
    m_animTargetRect = { targetX, targetY, targetX + winW, targetY + winH };
    m_animStartAlpha = m_alpha;
    m_animTargetAlpha = m_alpha;
    m_animStep = 0;
    m_animating = true;
    m_targetHidden = false;

    m_animStartW = m_animTargetW = winW;
    m_animStartH = m_animTargetH = winH;

    m_displayX = targetX;
    m_displayY = targetY;
    m_side = targetSide;

    if (m_animTimer) StopWindowTimer(m_hwnd, m_animTimer);
    m_animTimer = StartWindowTimer(m_hwnd, 10);
}

void WindowAnimator::OnTick() {
    m_animStep++;
    float t = (float)m_animStep / m_animTotalSteps;

    if (t >= 1.0f) {
        StopWindowTimer(m_hwnd, m_animTimer);
        m_animTimer = 0;
        m_animating = false;

        int finalW = (m_animTargetW < 1) ? 1 : m_animTargetW;
        int finalH = (m_animTargetH < 1) ? 1 : m_animTargetH;
        MoveResizeWindowPlat(m_hwnd, m_animTargetRect.left, m_animTargetRect.top, finalW, finalH);
        SetWindowAlpha(m_hwnd, 255);

        if (m_targetHidden && !g_enableSnapToEdge.Get())
            HideWindowPlat(m_hwnd);

        m_alpha = m_animTargetAlpha;
        m_hidden = m_targetHidden;
        UpdateSide();
        return;
    }

    float eased = m_targetHidden ? t * t * t : 1.0f - powf(1.0f - t, 3);

    int x = m_animStartRect.left + (int)((m_animTargetRect.left - m_animStartRect.left) * eased);
    int y = m_animStartRect.top + (int)((m_animTargetRect.top - m_animStartRect.top) * eased);
    int w = m_animStartW + (int)((m_animTargetW - m_animStartW) * eased);
    int h = m_animStartH + (int)((m_animTargetH - m_animStartH) * eased);
    if (w < 1) w = 1;
    if (h < 1) h = 1;

    BYTE alpha = (BYTE)(m_animStartAlpha + (m_animTargetAlpha - m_animStartAlpha) * eased);
    MoveResizeWindowPlat(m_hwnd, x, y, w, h);
    SetWindowAlpha(m_hwnd, alpha);
    m_alpha = alpha;
}

void WindowAnimator::BeginDrag(POINT mousePos) {
    m_dragging = true;
    m_dragStartPos = mousePos;
    RECT rc = GetWindowRectPlat(m_hwnd);
    m_windowStartPos.x = rc.left;
    m_windowStartPos.y = rc.top;
}

void WindowAnimator::UpdateDrag(POINT mousePos) {
    if (!m_dragging) return;

    int dx = mousePos.x - m_dragStartPos.x;
    int dy = mousePos.y - m_dragStartPos.y;
    int newX = m_windowStartPos.x + dx;
    int newY = m_windowStartPos.y + dy;

    RECT work = GetWorkAreaPlat();
    RECT rc = GetWindowRectPlat(m_hwnd);
    int winW = rc.right - rc.left;
    int winH = rc.bottom - rc.top;

    if (g_enableSnapToEdge.Get()) {
        if (newX <= work.left + g_snapThreshold)               newX = work.left;
        else if (newX + winW >= work.right - g_snapThreshold)   newX = work.right - winW;
        if (newY <= work.top + g_snapThreshold)                 newY = work.top;
        else if (newY + winH >= work.bottom - g_snapThreshold)  newY = work.bottom - winH;
    }

    MoveWindowPlat(m_hwnd, newX, newY);

#if PLATFORM_WINDOWS
    UpdateSide();
    RECT nr = GetWindowRectPlat(m_hwnd);
    m_windowStartPos.x = nr.left;
    m_windowStartPos.y = nr.top;
    m_dragStartPos = mousePos;
#endif
}

void WindowAnimator::EndDrag() {
    if (!m_dragging) return;
    Stop();
    m_dragging = false;
    ReleaseMouseCapture();
    SnapToEdge();
}
