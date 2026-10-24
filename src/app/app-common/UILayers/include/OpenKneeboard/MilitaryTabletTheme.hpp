// OpenKneeboard
//
// This program is open source; see the LICENSE file in the root of the OpenKneeboard repository.
#pragma once

#include <shims/winrt/base.h>

#include <d2d1_1.h>

namespace OpenKneeboard::MilitaryTabletTheme {

/// Shared palette of the military tablet theme
struct Brushes {
  winrt::com_ptr<ID2D1SolidColorBrush> mBar;
  winrt::com_ptr<ID2D1SolidColorBrush> mEdge;
  winrt::com_ptr<ID2D1SolidColorBrush> mFace;
  winrt::com_ptr<ID2D1SolidColorBrush> mFaceHover;
  winrt::com_ptr<ID2D1SolidColorBrush> mFacePressed;
  winrt::com_ptr<ID2D1SolidColorBrush> mForeground;
  winrt::com_ptr<ID2D1SolidColorBrush> mAccent;
  winrt::com_ptr<ID2D1SolidColorBrush> mActive;
  winrt::com_ptr<ID2D1SolidColorBrush> mDisabled;
  winrt::com_ptr<ID2D1SolidColorBrush> mOverpaint;
  winrt::com_ptr<ID2D1SolidColorBrush> mMenu;
  winrt::com_ptr<ID2D1SolidColorBrush> mMenuHover;
  winrt::com_ptr<ID2D1SolidColorBrush> mScrollTrack;
  winrt::com_ptr<ID2D1SolidColorBrush> mScrollThumb;
  winrt::com_ptr<ID2D1SolidColorBrush> mScrollThumbHover;
  winrt::com_ptr<ID2D1SolidColorBrush> mHardKeyHover;
  winrt::com_ptr<ID2D1SolidColorBrush> mHardKeyPressed;
  winrt::com_ptr<ID2D1SolidColorBrush> mHardKeyDisabled;

  static Brushes Create(ID2D1DeviceContext* ctx) {
    const auto brush = [ctx](float r, float g, float b, float a = 1.0f) {
      winrt::com_ptr<ID2D1SolidColorBrush> ret;
      winrt::check_hresult(ctx->CreateSolidColorBrush(
        {r, g, b, a}, D2D1::BrushProperties(), ret.put()));
      return ret;
    };
    return {
      .mBar = brush(0.118f, 0.125f, 0.114f),
      .mEdge = brush(0.30f, 0.29f, 0.23f),
      .mFace = brush(0.07f, 0.075f, 0.07f),
      .mFaceHover = brush(0.19f, 0.19f, 0.15f),
      .mFacePressed = brush(0.47f, 0.41f, 0.27f),
      .mForeground = brush(0.88f, 0.86f, 0.78f),
      .mAccent = brush(0.82f, 0.72f, 0.48f),
      .mActive = brush(0.60f, 0.72f, 0.46f),
      .mDisabled = brush(0.50f, 0.50f, 0.46f, 0.55f),
      .mOverpaint = brush(0.0f, 0.0f, 0.0f, 0.45f),
      .mMenu = brush(0.10f, 0.107f, 0.098f, 0.97f),
      .mMenuHover = brush(0.33f, 0.30f, 0.21f),
      .mScrollTrack = brush(0.0f, 0.0f, 0.0f, 0.55f),
      .mScrollThumb = brush(0.82f, 0.72f, 0.48f, 0.80f),
      .mScrollThumbHover = brush(0.93f, 0.83f, 0.58f, 0.95f),
      .mHardKeyHover = brush(1.0f, 1.0f, 1.0f, 0.16f),
      .mHardKeyPressed = brush(0.82f, 0.72f, 0.48f, 0.45f),
      .mHardKeyDisabled = brush(0.0f, 0.0f, 0.0f, 0.45f),
    };
  }
};

}// namespace OpenKneeboard::MilitaryTabletTheme
