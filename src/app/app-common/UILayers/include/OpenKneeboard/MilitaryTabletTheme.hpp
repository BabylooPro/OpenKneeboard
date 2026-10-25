// OpenKneeboard
//
// This program is open source; see the LICENSE file in the root of the OpenKneeboard repository.
#pragma once

#include <shims/winrt/base.h>

#include <algorithm>
#include <string_view>

#include <d2d1_1.h>
#include <dwrite.h>

namespace OpenKneeboard::MilitaryTabletTheme {

/// Shared palette of the military tablet theme
struct Brushes {
  winrt::com_ptr<ID2D1SolidColorBrush> mScreen;
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
  winrt::com_ptr<ID2D1SolidColorBrush> mDanger;
  winrt::com_ptr<ID2D1SolidColorBrush> mPaper;
  winrt::com_ptr<ID2D1SolidColorBrush> mPaperLine;
  winrt::com_ptr<ID2D1SolidColorBrush> mInk;
  winrt::com_ptr<ID2D1SolidColorBrush> mDimmer;

  static Brushes Create(ID2D1DeviceContext* ctx) {
    const auto brush = [ctx](float r, float g, float b, float a = 1.0f) {
      winrt::com_ptr<ID2D1SolidColorBrush> ret;
      winrt::check_hresult(ctx->CreateSolidColorBrush(
        {r, g, b, a}, D2D1::BrushProperties(), ret.put()));
      return ret;
    };
    return {
      .mScreen = brush(0.055f, 0.06f, 0.055f),
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
      .mDanger = brush(0.72f, 0.24f, 0.18f),
      .mPaper = brush(0.93f, 0.92f, 0.87f),
      .mPaperLine = brush(0.80f, 0.80f, 0.74f),
      .mInk = brush(0.08f, 0.10f, 0.15f),
      .mDimmer = brush(0.0f, 0.0f, 0.0f),
    };
  }
};

/// Vertically centered text, ellipsized on overflow; the size is in pixels
inline winrt::com_ptr<IDWriteTextFormat> CreateTextFormat(
  IDWriteFactory* dwf,
  const wchar_t* font,
  DWRITE_FONT_WEIGHT weight,
  float pixelSize,
  float dpi,
  DWRITE_TEXT_ALIGNMENT alignment) {
  winrt::com_ptr<IDWriteTextFormat> ret;
  winrt::check_hresult(dwf->CreateTextFormat(
    font,
    nullptr,
    weight,
    DWRITE_FONT_STYLE_NORMAL,
    DWRITE_FONT_STRETCH_NORMAL,
    std::max(1.0f, (pixelSize * 96) / dpi),
    L"",
    ret.put()));
  ret->SetTextAlignment(alignment);
  ret->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
  winrt::com_ptr<IDWriteInlineObject> ellipsis;
  winrt::check_hresult(
    dwf->CreateEllipsisTrimmingSign(ret.get(), ellipsis.put()));
  DWRITE_TRIMMING trimming {.granularity = DWRITE_TRIMMING_GRANULARITY_WORD};
  winrt::check_hresult(ret->SetTrimming(&trimming, ellipsis.get()));
  return ret;
}

/// Recessed toolbar button with its glyph centered
inline void DrawButton(
  ID2D1DeviceContext* d2d,
  const Brushes& brushes,
  const D2D1_RECT_F& rect,
  std::wstring_view glyph,
  IDWriteTextFormat* glyphFormat,
  float strokeWidth,
  bool enabled,
  bool hover,
  bool pressed,
  bool active) {
  ID2D1Brush* face = brushes.mFace.get();
  ID2D1Brush* foreground = brushes.mForeground.get();
  if (!enabled) {
    foreground = brushes.mDisabled.get();
  } else if (pressed) {
    face = brushes.mFacePressed.get();
  } else if (hover) {
    face = brushes.mFaceHover.get();
    foreground = brushes.mAccent.get();
  } else if (active) {
    foreground = brushes.mActive.get();
  }

  const auto radius = (rect.bottom - rect.top) / 5;
  const auto roundedRect = D2D1::RoundedRect(rect, radius, radius);
  d2d->FillRoundedRectangle(roundedRect, face);
  d2d->DrawRoundedRectangle(
    roundedRect,
    active ? brushes.mActive.get() : brushes.mEdge.get(),
    strokeWidth / 2);
  d2d->DrawTextW(
    glyph.data(),
    static_cast<UINT32>(glyph.size()),
    glyphFormat,
    rect,
    foreground);
}

}// namespace OpenKneeboard::MilitaryTabletTheme
