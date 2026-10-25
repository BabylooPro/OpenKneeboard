// OpenKneeboard
//
// This program is open source; see the LICENSE file in the root of the OpenKneeboard repository.
#include <OpenKneeboard/CursorEvent.hpp>
#include <OpenKneeboard/KneeboardState.hpp>
#include <OpenKneeboard/KneeboardView.hpp>
#include <OpenKneeboard/MilitaryTabletHomeUILayer.hpp>
#include <OpenKneeboard/TabView.hpp>
#include <OpenKneeboard/TabsList.hpp>

#include <OpenKneeboard/config.hpp>
#include <OpenKneeboard/tracing.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>

namespace OpenKneeboard {

namespace {
using MilitaryTabletTheme::CreateTextFormat;

bool Contains(const Geometry2D::Rect<float>& rect, float x, float y) {
  return x >= rect.Left() && x <= rect.Right() && y >= rect.Top()
    && y <= rect.Bottom();
}
}// namespace

MilitaryTabletHomeUILayer::MilitaryTabletHomeUILayer(
  const audited_ptr<DXResources>& dxr,
  KneeboardState* kneeboard,
  KneeboardView* kneeboardView)
  : mDXResources(dxr),
    mKneeboard(kneeboard),
    mKneeboardView(kneeboardView),
    mBrushes(
      MilitaryTabletTheme::Brushes::Create(dxr->mD2DDeviceContext.get())) {
  AddEventListener(
    kneeboardView->evCurrentTabChangedEvent, [this](auto) {
      this->SetOpen(false);
    });
  if (const auto tabs = kneeboard->GetTabsList()) {
    AddEventListener(tabs->evTabsChangedEvent, [this]() {
      if (mOpen) {
        evNeedsRepaintEvent.Emit();
      }
    });
  }
}

MilitaryTabletHomeUILayer::~MilitaryTabletHomeUILayer() {
  this->RemoveAllEventListeners();
}

bool MilitaryTabletHomeUILayer::IsOpen() const noexcept {
  return mOpen;
}

void MilitaryTabletHomeUILayer::SetOpen(bool open) {
  if (open == mOpen) {
    return;
  }
  mOpen = open;
  mHover = NoTarget;
  mPressed = NoTarget;
  mTouching = false;
  evNeedsRepaintEvent.Emit();
}

MilitaryTabletHomeUILayer::Layout MilitaryTabletHomeUILayer::GetLayout(
  const PixelSize& size) const {
  const auto width = size.Width<float>();
  const auto height = size.Height<float>();
  // Same bar height as the header when both bars are shown
  const auto barHeight = std::round(
    height * HeaderPercent
    / static_cast<float>(100 + HeaderPercent + FooterPercent));
  const auto buttonHeight = std::round(barHeight * 0.75f);
  const auto margin = std::round((barHeight - buttonHeight) / 2);
  const auto titleLeft = (4 * margin) + buttonHeight;

  Layout ret {
    .mBar = {{0, 0}, {width, barHeight}},
    .mBack = {{2 * margin, margin}, {buttonHeight, buttonHeight}},
    .mTitle = {{titleLeft, 0}, {std::max(0.0f, width - titleLeft), barHeight}},
  };

  const auto tabsList = mKneeboard->GetTabsList();
  if (!tabsList) {
    return ret;
  }
  const auto tabs = tabsList->GetTabs();
  if (tabs.empty()) {
    return ret;
  }
  const auto tabView = mKneeboardView->GetCurrentTabView();
  const auto current = tabView ? tabView->GetRootTab().lock() : nullptr;

  const auto gap = std::round(barHeight * 0.3f);
  const FloatRect grid {
    {gap, barHeight + gap},
    {width - (2 * gap), height - barHeight - (2 * gap)},
  };
  const auto count = static_cast<float>(tabs.size());
  // Fewer, larger cards when they fit; more columns only for many tabs
  auto columns = (grid.Width() > grid.Height()) ? 3.0f : 2.0f;
  auto rows = 1.0f;
  auto cardWidth = 0.0f;
  auto cardHeight = 0.0f;
  for (;; columns += 1.0f) {
    rows = std::ceil(count / columns);
    cardWidth = (grid.Width() - (gap * (columns - 1))) / columns;
    cardHeight = std::min(
      cardWidth * 0.8f, (grid.Height() - (gap * (rows - 1))) / rows);
    if (cardHeight >= cardWidth * 0.55f || columns >= 6.0f) {
      break;
    }
  }

  const auto columnCount = static_cast<std::size_t>(columns);
  for (std::size_t i = 0; i < tabs.size(); ++i) {
    const auto& tab = tabs.at(i);
    const auto column = static_cast<float>(i % columnCount);
    const auto row = static_cast<float>(i / columnCount);
    ret.mCards.push_back({
      .mRect = {
        {
          grid.Left() + (column * (cardWidth + gap)),
          grid.Top() + (row * (cardHeight + gap)),
        },
        {cardWidth, cardHeight},
      },
      .mTabID = tab->GetRuntimeID(),
      .mTitle = tab->GetTitle(),
      .mGlyph = tab->GetGlyph(),
      .mPageCount = tab->GetPageCount(),
      .mIsCurrent = current && current->GetRuntimeID() == tab->GetRuntimeID(),
    });
  }
  return ret;
}

int MilitaryTabletHomeUILayer::HitTest(
  const Layout& layout,
  const FloatPoint& point) const {
  if (Contains(layout.mBack, point.mX, point.mY)) {
    return BackTarget;
  }
  for (std::size_t i = 0; i < layout.mCards.size(); ++i) {
    if (Contains(layout.mCards.at(i).mRect, point.mX, point.mY)) {
      return static_cast<int>(i);
    }
  }
  return NoTarget;
}

void MilitaryTabletHomeUILayer::Activate(const Layout& layout, int target) {
  if (target == BackTarget) {
    this->SetOpen(false);
    return;
  }
  if (target < 0 || static_cast<std::size_t>(target) >= layout.mCards.size()) {
    return;
  }
  const auto& card = layout.mCards.at(static_cast<std::size_t>(target));
  if (!card.mIsCurrent) {
    // Closes the home screen through the tab change event
    mKneeboardView->SetCurrentTabByRuntimeID(card.mTabID);
  }
  this->SetOpen(false);
}

void MilitaryTabletHomeUILayer::PostCursorEvent(
  const IUILayer::NextList& next,
  const Context& context,
  KneeboardViewID kneeboardViewID,
  const CursorEvent& cursorEvent) {
  const auto [first, rest] = Split(next);
  // The app window shows the tab itself
  if (!mOpen || cursorEvent.mSource == CursorSource::WindowPointer) {
    first->PostCursorEvent(rest, context, kneeboardViewID, cursorEvent);
    return;
  }
  first->PostCursorEvent(rest, context, kneeboardViewID, {});

  if (cursorEvent.mTouchState == CursorTouchState::NotNearSurface || !mSize) {
    mHover = NoTarget;
    mPressed = NoTarget;
    mTouching = false;
    return;
  }

  const auto layout = this->GetLayout(*mSize);
  const auto target = this->HitTest(
    layout,
    {cursorEvent.mX * mSize->Width<float>(),
     cursorEvent.mY * mSize->Height<float>()});
  mHover = target;

  if (cursorEvent.mTouchState == CursorTouchState::TouchingSurface) {
    if (!mTouching) {
      mTouching = true;
      mPressed = target;
    }
    return;
  }

  if (!std::exchange(mTouching, false)) {
    return;
  }
  const auto pressed = std::exchange(mPressed, NoTarget);
  if (pressed == target && target != NoTarget) {
    this->Activate(layout, target);
  }
}

IUILayer::Metrics MilitaryTabletHomeUILayer::GetMetrics(
  const IUILayer::NextList& next,
  const Context& context) const {
  const auto nextMetrics = next.front()->GetMetrics(next.subspan(1), context);
  return Metrics {
    nextMetrics.mPreferredSize,
    {{}, nextMetrics.mPreferredSize.mPixelSize},
    nextMetrics.mContentArea,
  };
}

task<void> MilitaryTabletHomeUILayer::Render(
  const RenderContext& rc,
  const IUILayer::NextList& next,
  const Context& context,
  const PixelRect& rect) {
  OPENKNEEBOARD_TraceLoggingScope("MilitaryTabletHomeUILayer::Render()");
  const auto [first, rest] = Split(next);
  if (!mOpen) {
    co_await first->Render(rc, rest, context, rect);
    co_return;
  }

  mSize = rect.mSize;
  const auto layout = this->GetLayout(rect.mSize);
  const auto origin = rect.mOffset.StaticCast<float>();
  const auto at = [origin](const FloatRect& area) -> D2D1_RECT_F {
    return FloatRect {origin + area.mOffset, area.mSize};
  };

  auto d2d = rc.d2d();
  d2d->SetTransform(D2D1::Matrix3x2F::Identity());
  d2d->FillRectangle(rect, mBrushes.mScreen.get());

  FLOAT dpix {}, dpiy {};
  d2d->GetDpi(&dpix, &dpiy);
  const auto dwf = mDXResources->mDWriteFactory.get();

  const auto bar = at(layout.mBar);
  d2d->FillRectangle(bar, mBrushes.mBar.get());
  d2d->DrawLine(
    {bar.left, bar.bottom - 0.5f},
    {bar.right, bar.bottom - 0.5f},
    mBrushes.mEdge.get());

  const auto buttonHeight = layout.mBack.Height();
  const auto backGlyphFormat = CreateTextFormat(
    dwf,
    GlyphFont,
    DWRITE_FONT_WEIGHT_EXTRA_BOLD,
    buttonHeight * 0.66f,
    dpiy,
    DWRITE_TEXT_ALIGNMENT_CENTER);
  MilitaryTabletTheme::DrawButton(
    d2d,
    mBrushes,
    at(layout.mBack),
    L"\uE72B",// Back
    backGlyphFormat.get(),
    buttonHeight / 15,
    true,
    mHover == BackTarget,
    mTouching && mPressed == BackTarget,
    false);

  const auto barTitleFormat = CreateTextFormat(
    dwf,
    VariableWidthUIFont,
    DWRITE_FONT_WEIGHT_BOLD,
    layout.mBar.Height() * 0.45f,
    dpiy,
    DWRITE_TEXT_ALIGNMENT_LEADING);
  const std::wstring_view homeTitle {_(L"Home")};
  d2d->DrawTextW(
    homeTitle.data(),
    static_cast<UINT32>(homeTitle.size()),
    barTitleFormat.get(),
    at(layout.mTitle),
    mBrushes.mForeground.get());

  if (layout.mCards.empty()) {
    const std::wstring_view noTabs {_(L"No tabs")};
    const auto noTabsFormat = CreateTextFormat(
      dwf,
      VariableWidthUIFont,
      DWRITE_FONT_WEIGHT_NORMAL,
      layout.mBar.Height() * 0.5f,
      dpiy,
      DWRITE_TEXT_ALIGNMENT_CENTER);
    d2d->DrawTextW(
      noTabs.data(),
      static_cast<UINT32>(noTabs.size()),
      noTabsFormat.get(),
      rect,
      mBrushes.mDisabled.get());
    co_return;
  }

  const auto cardHeight = layout.mCards.front().mRect.Height();
  const auto glyphFormat = CreateTextFormat(
    dwf,
    GlyphFont,
    DWRITE_FONT_WEIGHT_NORMAL,
    cardHeight * 0.30f,
    dpiy,
    DWRITE_TEXT_ALIGNMENT_CENTER);
  const auto titleFormat = CreateTextFormat(
    dwf,
    VariableWidthUIFont,
    DWRITE_FONT_WEIGHT_SEMI_BOLD,
    cardHeight * 0.11f,
    dpiy,
    DWRITE_TEXT_ALIGNMENT_CENTER);
  const auto detailFormat = CreateTextFormat(
    dwf,
    VariableWidthUIFont,
    DWRITE_FONT_WEIGHT_NORMAL,
    cardHeight * 0.085f,
    dpiy,
    DWRITE_TEXT_ALIGNMENT_CENTER);
  const auto stroke = std::max(1.0f, cardHeight / 60);

  for (std::size_t i = 0; i < layout.mCards.size(); ++i) {
    const auto& card = layout.mCards.at(i);
    const bool hover = (mHover == static_cast<int>(i));
    const bool pressed = mTouching && (mPressed == static_cast<int>(i));
    const auto area = at(card.mRect);
    const auto width = area.right - area.left;
    const auto height = area.bottom - area.top;
    const auto radius = std::min(width, height) * 0.08f;
    const auto rounded = D2D1::RoundedRect(area, radius, radius);

    ID2D1Brush* face = mBrushes.mBar.get();
    if (pressed) {
      face = mBrushes.mFacePressed.get();
    } else if (hover) {
      face = mBrushes.mFaceHover.get();
    }
    d2d->FillRoundedRectangle(rounded, face);
    if (card.mIsCurrent) {
      d2d->DrawRoundedRectangle(rounded, mBrushes.mActive.get(), stroke * 2);
    } else {
      d2d->DrawRoundedRectangle(
        rounded,
        hover ? mBrushes.mAccent.get() : mBrushes.mEdge.get(),
        stroke);
    }

    ID2D1Brush* glyphBrush = mBrushes.mForeground.get();
    if (card.mIsCurrent) {
      glyphBrush = mBrushes.mActive.get();
    } else if (hover) {
      glyphBrush = mBrushes.mAccent.get();
    }
    const auto inset = width * 0.06f;
    const auto band = [&](float top, float bottom) {
      return D2D1::RectF(
        area.left + inset,
        area.top + (height * top),
        area.right - inset,
        area.top + (height * bottom));
    };

    const auto glyph = winrt::to_hstring(card.mGlyph);
    d2d->DrawTextW(
      glyph.c_str(),
      static_cast<UINT32>(glyph.size()),
      glyphFormat.get(),
      band(0.10f, 0.56f),
      glyphBrush);

    const auto title = winrt::to_hstring(card.mTitle);
    d2d->DrawTextW(
      title.c_str(),
      static_cast<UINT32>(title.size()),
      titleFormat.get(),
      band(0.56f, 0.84f),
      mBrushes.mForeground.get());

    const auto detail = (card.mPageCount == 1)
      ? std::wstring {_(L"1 page")}
      : std::format(_(L"{} pages"), card.mPageCount);
    d2d->DrawTextW(
      detail.c_str(),
      static_cast<UINT32>(detail.size()),
      detailFormat.get(),
      band(0.83f, 0.97f),
      mBrushes.mDisabled.get());
  }
}

}// namespace OpenKneeboard
