#pragma once
#include <platform/platform.h>

class WindowAnimator {
public:
    static WindowAnimator& Get();

    void Init(WindowHandle hwnd);
    void Reset();                                   // 恢复初始状态

    // 查询
    bool IsAnimating() const { return m_animating; }
    bool IsHidden()    const { return m_hidden; }
    BYTE Alpha()       const { return m_alpha; }
    int  Side()        const { return m_side; }

    // 动作
    void UpdateSide();
    void HideShow(bool hide);                       // 原 StartHideShowAnimation
    void Toggle() { HideShow(!m_hidden); }
    void Stop();                                    // 原 StopAnimation
    void SnapToEdge();                              // 原 SnapToEdgeWithAnimation
    void OnTick();                                  // 原 OnAnimationTick

    // 拖拽
    void BeginDrag(POINT mousePos);
    void UpdateDrag(POINT mousePos);
    void EndDrag();                                 // 原 OnDragEnd
    bool IsDragging() const { return m_dragging; }

private:
    WindowAnimator() = default;

    WindowHandle m_hwnd = nullptr;

    bool     m_animating = false;
    bool     m_targetHidden = false;
    bool     m_hidden = false;
    int      m_animStep = 0;
    int      m_animTotalSteps = 15;

    RECT     m_animStartRect{};
    RECT     m_animTargetRect{};
    BYTE     m_animStartAlpha = 255;
    BYTE     m_animTargetAlpha = 255;
    UINT_PTR m_animTimer = 0;

    int      m_animStartW = 0, m_animTargetW = 0;
    int      m_animStartH = 0, m_animTargetH = 0;

    int      m_displayX = 0, m_displayY = 0;
    int      m_side = 0;
    BYTE     m_alpha = 255;

    bool     m_dragging = false;
    POINT    m_dragStartPos{};
    POINT    m_windowStartPos{};
};
