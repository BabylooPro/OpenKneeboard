// OpenKneeboard
//
// This program is open source; see the LICENSE file in the root of the OpenKneeboard repository.
#include <OpenKneeboard/CursorEvent.hpp>
#include <OpenKneeboard/KneeboardView.hpp>
#include <OpenKneeboard/MilitaryTabletViewportUILayer.hpp>
#include <OpenKneeboard/RenderTarget.hpp>
#include <OpenKneeboard/SHM.hpp>
#include <OpenKneeboard/TabView.hpp>

#include <OpenKneeboard/tracing.hpp>

#include <DirectXColors.h>

#include <algorithm>
#include <array>
#include <utility>

namespace OpenKneeboard {

namespace {
constexpr std::array ZoomLevels {1.0f, 1.25f, 1.5f, 2.0f, 2.5f, 3.0f, 4.0f};

// Bounds the offscreen texture when zoomed in; beyond this, the visible part
// is upscaled
constexpr float MaxOffscreenSize = 4096.0f;

bool Contains(const Geometry2D::Rect<float>& rect, float x, float y) {
  return rect.Width() > 0 && rect.Height() > 0 && x >= rect.Left()
    && x <= rect.Right() && y >= rect.Top() && y <= rect.Bottom();
}
}// namespace

MilitaryTabletViewportUILayer::MilitaryTabletViewportUILayer(
  const audited_ptr<DXResources>& dxr,
  KneeboardView* kneeboardView)
  : mDXResources(dxr),
    mKneeboardView(kneeboardView),
    mBrushes(
      MilitaryTabletTheme::Brushes::Create(dxr->mD2DDeviceContext.get())) {
  AddEventListener(
    kneeboardView->evCurrentTabChangedEvent, [this](auto) {
      this->OnTabChanged();
    });
}

MilitaryTabletViewportUILayer::~MilitaryTabletViewportUILayer() {
  this->RemoveAllEventListeners();
}

void MilitaryTabletViewportUILayer::OnTabChanged() {
  if (mTabID) {
    mTabStates[*mTabID] = {mFit, mZoomIndex, mCenter, mQuarterTurns};
  }
  mPageID.reset();

  const auto tabView = mKneeboardView->GetCurrentTabView();
  const auto tab = tabView ? tabView->GetRootTab().lock() : nullptr;
  mTabID.reset();
  if (tab) {
    mTabID = tab->GetRuntimeID();
  }

  // A tab shown for the first time keeps the fit mode in use
  TabState state {.mFit = mFit};
  if (mTabID) {
    if (const auto it = mTabStates.find(*mTabID); it != mTabStates.end()) {
      state = it->second;
    }
  }
  mFit = state.mFit;
  mZoomIndex = state.mZoomIndex;
  mCenter = state.mCenter;
  mQuarterTurns = state.mQuarterTurns;
  evNeedsRepaintEvent.Emit();
  evStateChangedEvent.Emit();
}

PixelSize MilitaryTabletViewportUILayer::Rotated(const PixelSize& size) const {
  if (mQuarterTurns % 2) {
    return {size.mHeight, size.mWidth};
  }
  return size;
}

// Maps a normalized point of the rotated document back to the document
MilitaryTabletViewportUILayer::FloatPoint
MilitaryTabletViewportUILayer::Unrotated(const FloatPoint& point) const {
  switch (mQuarterTurns) {
    case 1:
      return {point.mY, 1.0f - point.mX};
    case 2:
      return {1.0f - point.mX, 1.0f - point.mY};
    case 3:
      return {1.0f - point.mY, point.mX};
    default:
      return point;
  }
}

void MilitaryTabletViewportUILayer::RotateDocument() {
  // Clockwise; the visible center stays on the same part of the document
  const auto center = this->Unrotated(this->GetVisibleCenter());
  mQuarterTurns = (mQuarterTurns + 1) % 4;
  switch (mQuarterTurns) {
    case 1:
      mCenter = {1.0f - center.mY, center.mX};
      break;
    case 2:
      mCenter = {1.0f - center.mX, 1.0f - center.mY};
      break;
    case 3:
      mCenter = {center.mY, 1.0f - center.mX};
      break;
    default:
      mCenter = center;
      break;
  }
  evNeedsRepaintEvent.Emit();
  evStateChangedEvent.Emit();
}

MilitaryTabletViewportUILayer::Fit MilitaryTabletViewportUILayer::GetFit()
  const noexcept {
  return mFit;
}

bool MilitaryTabletViewportUILayer::IsZoomed() const noexcept {
  return mZoomIndex > 0;
}

bool MilitaryTabletViewportUILayer::CanZoomIn() const noexcept {
  return mZoomIndex + 1 < ZoomLevels.size();
}

bool MilitaryTabletViewportUILayer::CanZoomOut() const noexcept {
  return mZoomIndex > 0 || mFit == Fit::Width;
}

void MilitaryTabletViewportUILayer::SetFit(Fit fit) {
  this->SetZoom(0, fit, {0.5f, 0.0f});
}

void MilitaryTabletViewportUILayer::ZoomIn() {
  if (this->CanZoomIn()) {
    this->SetZoom(mZoomIndex + 1, mFit, this->GetVisibleCenter());
  }
}

void MilitaryTabletViewportUILayer::ZoomOut() {
  if (mZoomIndex > 0) {
    this->SetZoom(mZoomIndex - 1, mFit, this->GetVisibleCenter());
    return;
  }
  // Zooming out of a page fitted to its width shows the whole page
  if (mFit == Fit::Width) {
    this->SetFit(Fit::Page);
  }
}

void MilitaryTabletViewportUILayer::ResetZoom() {
  this->SetZoom(0, mFit, {0.5f, 0.0f});
}

void MilitaryTabletViewportUILayer::SetZoom(
  std::size_t index,
  Fit fit,
  const FloatPoint& center) {
  mZoomIndex = index;
  mFit = fit;
  mCenter = center;
  evNeedsRepaintEvent.Emit();
  evStateChangedEvent.Emit();
}

void MilitaryTabletViewportUILayer::ScrollTo(
  const Layout& layout,
  const FloatPoint& center) {
  const auto halfWidth
    = layout.mDestination.Width() / (2 * layout.mContentSize.mWidth);
  const auto halfHeight
    = layout.mDestination.Height() / (2 * layout.mContentSize.mHeight);
  mCenter = {
    std::clamp(center.mX, halfWidth, 1.0f - halfWidth),
    std::clamp(center.mY, halfHeight, 1.0f - halfHeight),
  };
  evNeedsRepaintEvent.Emit();
}

MilitaryTabletViewportUILayer::FloatPoint
MilitaryTabletViewportUILayer::GetVisibleCenter() const {
  if (!(mViewportSize && mContentSize)) {
    return mCenter;
  }
  const auto layout
    = this->GetLayout(*mViewportSize, this->Rotated(*mContentSize));
  return {
    (layout.mOffset.mX + (layout.mDestination.Width() / 2))
      / layout.mContentSize.mWidth,
    (layout.mOffset.mY + (layout.mDestination.Height() / 2))
      / layout.mContentSize.mHeight,
  };
}

MilitaryTabletViewportUILayer::Layout MilitaryTabletViewportUILayer::GetLayout(
  const PixelSize& viewport,
  const PixelSize& content) const {
  const auto contentSize = content.StaticCast<float>();
  const auto zoom = ZoomLevels.at(mZoomIndex);
  const auto displayedSize = [&](const FloatSize& available) {
    const auto toWidth = available.mWidth / contentSize.mWidth;
    const auto toPage
      = std::min(toWidth, available.mHeight / contentSize.mHeight);
    return contentSize * (((mFit == Fit::Width) ? toWidth : toPage) * zoom);
  };

  const auto full = viewport.StaticCast<float>();
  auto size = displayedSize(full);
  const bool reserveX = size.mWidth > full.mWidth + 0.5f;
  const bool reserveY = size.mHeight > full.mHeight + 0.5f;
  const auto barSize = std::max(
    8.0f, std::round(std::min(full.mWidth, full.mHeight) * 0.035f));
  const FloatSize available {
    full.mWidth - (reserveY ? barSize : 0.0f),
    full.mHeight - (reserveX ? barSize : 0.0f),
  };
  if (reserveX || reserveY) {
    size = displayedSize(available);
  }

  const FloatSize visible {
    std::min(size.mWidth, available.mWidth),
    std::min(size.mHeight, available.mHeight),
  };
  const FloatPoint offset {
    std::clamp(
      (mCenter.mX * size.mWidth) - (visible.mWidth / 2),
      0.0f,
      size.mWidth - visible.mWidth),
    std::clamp(
      (mCenter.mY * size.mHeight) - (visible.mHeight / 2),
      0.0f,
      size.mHeight - visible.mHeight),
  };

  Layout ret {
    .mContentSize = size,
    .mOffset = offset,
    .mDestination = {
      {
        (available.mWidth - visible.mWidth) / 2,
        (available.mHeight - visible.mHeight) / 2,
      },
      visible,
    },
    .mScrollX = size.mWidth > available.mWidth + 0.5f,
    .mScrollY = size.mHeight > available.mHeight + 0.5f,
  };

  const auto thumb = [barSize](float track, float shown, float total, float at) {
    const auto length = std::max(barSize * 1.5f, track * (shown / total));
    const auto position = (track - length) * (at / (total - shown));
    return std::pair {position, length};
  };
  if (ret.mScrollY) {
    ret.mVerticalTrack = {{available.mWidth, 0.0f}, {barSize, available.mHeight}};
    const auto [position, length]
      = thumb(available.mHeight, visible.mHeight, size.mHeight, offset.mY);
    ret.mVerticalThumb = {{available.mWidth, position}, {barSize, length}};
  }
  if (ret.mScrollX) {
    ret.mHorizontalTrack
      = {{0.0f, available.mHeight}, {available.mWidth, barSize}};
    const auto [position, length]
      = thumb(available.mWidth, visible.mWidth, size.mWidth, offset.mX);
    ret.mHorizontalThumb = {{position, available.mHeight}, {length, barSize}};
  }
  return ret;
}

MilitaryTabletViewportUILayer::Target MilitaryTabletViewportUILayer::HitTest(
  const Layout& layout,
  const FloatPoint& point) const {
  if (Contains(layout.mVerticalTrack, point.mX, point.mY)) {
    return Target::VerticalBar;
  }
  if (Contains(layout.mHorizontalTrack, point.mX, point.mY)) {
    return Target::HorizontalBar;
  }
  return Target::Content;
}

void MilitaryTabletViewportUILayer::PostCursorEvent(
  const IUILayer::NextList& next,
  const Context& context,
  KneeboardViewID kneeboardViewID,
  const CursorEvent& cursorEvent) {
  const auto first = next.front();
  const auto rest = next.subspan(1);

  // App window events are already mapped to the unzoomed content by the bezel
  if (cursorEvent.mSource == CursorSource::WindowPointer) {
    first->PostCursorEvent(rest, context, kneeboardViewID, cursorEvent);
    return;
  }

  if (
    cursorEvent.mTouchState == CursorTouchState::NotNearSurface
    || !(mViewportSize && mContentSize)) {
    mPressed = Target::None;
    mHover = Target::None;
    mDragging = false;
    first->PostCursorEvent(rest, context, kneeboardViewID, {});
    return;
  }

  const auto layout
    = this->GetLayout(*mViewportSize, this->Rotated(*mContentSize));
  const FloatPoint point {
    cursorEvent.mX * mViewportSize->mWidth,
    cursorEvent.mY * mViewportSize->mHeight,
  };

  const auto postToContent = [&]() {
    const auto& destination = layout.mDestination;
    const auto x = point.mX - destination.Left();
    const auto y = point.mY - destination.Top();
    if (x < 0 || y < 0 || x > destination.Width() || y > destination.Height()) {
      first->PostCursorEvent(rest, context, kneeboardViewID, {});
      return;
    }
    const auto contentPoint = this->Unrotated({
      (layout.mOffset.mX + x) / layout.mContentSize.mWidth,
      (layout.mOffset.mY + y) / layout.mContentSize.mHeight,
    });
    CursorEvent contentEvent {cursorEvent};
    contentEvent.mX = contentPoint.mX;
    contentEvent.mY = contentPoint.mY;
    first->PostCursorEvent(rest, context, kneeboardViewID, contentEvent);
  };

  if (cursorEvent.mTouchState != CursorTouchState::TouchingSurface) {
    mDragging = false;
    if (std::exchange(mPressed, Target::None) == Target::Content) {
      postToContent();
      return;
    }
    mHover = this->HitTest(layout, point);
    if (mHover == Target::Content) {
      postToContent();
    } else {
      first->PostCursorEvent(rest, context, kneeboardViewID, {});
    }
    return;
  }

  // A press goes to whatever it started on until it is released, so that
  // dragging a scroll bar or drawing on the page is not interrupted
  const bool vertical = (mPressed == Target::VerticalBar);
  if (mPressed == Target::None) {
    mPressed = this->HitTest(layout, point);
    if (mPressed == Target::Content) {
      postToContent();
      return;
    }
    const bool pressedVertical = (mPressed == Target::VerticalBar);
    const auto& thumb
      = pressedVertical ? layout.mVerticalThumb : layout.mHorizontalThumb;
    const auto center = this->GetVisibleCenter();
    if (Contains(thumb, point.mX, point.mY)) {
      mDragging = true;
      mDragOrigin = pressedVertical ? point.mY : point.mX;
      mDragCenter = center;
    } else if (pressedVertical) {
      const auto step = 0.9f * layout.mDestination.Height()
        / layout.mContentSize.mHeight;
      this->ScrollTo(
        layout,
        {center.mX, center.mY + ((point.mY < thumb.Top()) ? -step : step)});
    } else {
      const auto step
        = 0.9f * layout.mDestination.Width() / layout.mContentSize.mWidth;
      this->ScrollTo(
        layout,
        {center.mX + ((point.mX < thumb.Left()) ? -step : step), center.mY});
    }
    first->PostCursorEvent(rest, context, kneeboardViewID, {});
    return;
  }

  if (mPressed == Target::Content) {
    postToContent();
    return;
  }

  if (mDragging) {
    const auto& track = vertical ? layout.mVerticalTrack : layout.mHorizontalTrack;
    const auto& thumb = vertical ? layout.mVerticalThumb : layout.mHorizontalThumb;
    const auto travel = vertical ? (track.Height() - thumb.Height())
                                 : (track.Width() - thumb.Width());
    if (travel > 0) {
      const auto moved = (vertical ? point.mY : point.mX) - mDragOrigin;
      const auto shown = vertical
        ? (layout.mDestination.Height() / layout.mContentSize.mHeight)
        : (layout.mDestination.Width() / layout.mContentSize.mWidth);
      const auto delta = (moved / travel) * (1.0f - shown);
      this->ScrollTo(
        layout,
        vertical ? FloatPoint {mDragCenter.mX, mDragCenter.mY + delta}
                 : FloatPoint {mDragCenter.mX + delta, mDragCenter.mY});
    }
  }
  first->PostCursorEvent(rest, context, kneeboardViewID, {});
}

IUILayer::Metrics MilitaryTabletViewportUILayer::GetMetrics(
  const IUILayer::NextList& next,
  const Context& context) const {
  const auto nextMetrics = next.front()->GetMetrics(next.subspan(1), context);
  return Metrics {
    nextMetrics.mPreferredSize,
    {{}, nextMetrics.mPreferredSize.mPixelSize},
    nextMetrics.mContentArea,
  };
}

void MilitaryTabletViewportUILayer::ResizeOffscreen(const PixelSize& size) {
  if (mOffscreenTexture && mOffscreenSize == size) {
    return;
  }

  mOffscreenBitmap = nullptr;
  mOffscreenSRV = nullptr;
  mOffscreenTexture = nullptr;
  D3D11_TEXTURE2D_DESC textureDesc {
    .Width = size.mWidth,
    .Height = size.mHeight,
    .MipLevels = 1,
    .ArraySize = 1,
    .Format = SHM::SHARED_TEXTURE_PIXEL_FORMAT,
    .SampleDesc = {1, 0},
    .Usage = D3D11_USAGE_DEFAULT,
    .BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET,
  };
  winrt::check_hresult(mDXResources->mD3D11Device->CreateTexture2D(
    &textureDesc, nullptr, mOffscreenTexture.put()));
  winrt::check_hresult(mDXResources->mD3D11Device->CreateShaderResourceView(
    mOffscreenTexture.get(), nullptr, mOffscreenSRV.put()));

  // Reusing the render target keeps its ID, which page sources use as a
  // cache key
  if (!mOffscreen) {
    mOffscreen = RenderTarget::Create(mDXResources, nullptr);
  }
  mOffscreen->SetD3DTexture(mOffscreenTexture);
  mOffscreenSize = size;
}

task<void> MilitaryTabletViewportUILayer::Render(
  const RenderContext& rc,
  const IUILayer::NextList& next,
  const Context& context,
  const PixelRect& rect) {
  OPENKNEEBOARD_TraceLoggingScope("MilitaryTabletViewportUILayer::Render()");
  const auto [first, rest] = Split(next);
  const auto content = first->GetMetrics(rest, context).mPreferredSize.mPixelSize;

  if (context.mTabView) {
    const auto pageID = context.mTabView->GetPageID();
    if (mPageID && *mPageID != pageID) {
      mCenter.mY = 0.0f;
    }
    mPageID = pageID;
    if (!mTabID) {
      if (const auto tab = context.mTabView->GetRootTab().lock()) {
        mTabID = tab->GetRuntimeID();
      }
    }
  }
  mViewportSize = rect.mSize;
  mContentSize = content;

  const auto layout = this->GetLayout(rect.mSize, this->Rotated(content));
  const auto origin = rect.mOffset.StaticCast<float>();
  const auto destination = FloatRect {
    origin + layout.mDestination.mOffset,
    layout.mDestination.mSize,
  }.Rounded<uint32_t>();

  if (!(layout.mScrollX || layout.mScrollY || mQuarterTurns)) {
    co_await first->Render(rc, rest, context, destination);
    co_return;
  }

  const auto textureScale = std::min({
    1.0f,
    MaxOffscreenSize / layout.mContentSize.mWidth,
    MaxOffscreenSize / layout.mContentSize.mHeight,
  });
  const auto rotatedTextureSize = layout.mContentSize * textureScale;
  const auto textureSize = ((mQuarterTurns % 2)
                              ? FloatSize {rotatedTextureSize.mHeight,
                                           rotatedTextureSize.mWidth}
                              : rotatedTextureSize)
                             .Rounded<uint32_t>();
  this->ResizeOffscreen(textureSize);
  {
    auto d3d = mOffscreen->d3d();
    mDXResources->mD3D11ImmediateContext->ClearRenderTargetView(
      d3d.rtv(), DirectX::Colors::Transparent);
  }

  const RenderContext offscreenContext {
    mOffscreen.get(), rc.GetKneeboardView()};
  co_await first->Render(
    offscreenContext, rest, context, PixelRect {{0, 0}, textureSize});

  if (mQuarterTurns == 0) {
    const auto source = FloatRect {
      layout.mOffset * textureScale,
      layout.mDestination.mSize * textureScale,
    }.Rounded<uint32_t>().Clamped(textureSize);
    auto d3d = rc.d3d();
    auto sb = mDXResources->mSpriteBatch.get();
    sb->Begin(d3d.rtv(), rc.GetRenderTarget()->GetDimensions());
    sb->Draw(mOffscreenSRV.get(), source, destination);
    sb->End();
  }

  auto d2d = rc.d2d();
  if (mQuarterTurns != 0) {
    if (!mOffscreenBitmap) {
      winrt::check_hresult(
        mDXResources->mD2DDeviceContext->CreateBitmapFromDxgiSurface(
          mOffscreenTexture.as<IDXGISurface>().get(),
          D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_NONE,
            D2D1::PixelFormat(
              SHM::SHARED_TEXTURE_PIXEL_FORMAT, D2D1_ALPHA_MODE_PREMULTIPLIED)),
          mOffscreenBitmap.put()));
    }
    const auto width = textureSize.Width<float>();
    const auto height = textureSize.Height<float>();
    // Texture pixels to the rotated document, clockwise
    auto turn = D2D1::Matrix3x2F::Identity();
    switch (mQuarterTurns) {
      case 1:
        turn = D2D1::Matrix3x2F(0, 1, -1, 0, height, 0);
        break;
      case 2:
        turn = D2D1::Matrix3x2F(-1, 0, 0, -1, width, height);
        break;
      case 3:
        turn = D2D1::Matrix3x2F(0, -1, 1, 0, 0, width);
        break;
    }
    const bool sideways = (mQuarterTurns % 2) != 0;
    const auto toDisplayed = D2D1::Matrix3x2F::Scale(
      layout.mContentSize.mWidth / (sideways ? height : width),
      layout.mContentSize.mHeight / (sideways ? width : height));
    const auto toScreen = D2D1::Matrix3x2F::Translation(
      origin.mX + layout.mDestination.Left() - layout.mOffset.mX,
      origin.mY + layout.mDestination.Top() - layout.mOffset.mY);

    d2d->PushAxisAlignedClip(destination, D2D1_ANTIALIAS_MODE_ALIASED);
    d2d->SetTransform(turn * toDisplayed * toScreen);
    d2d->DrawBitmap(
      mOffscreenBitmap.get(),
      D2D1::RectF(0, 0, width, height),
      1.0f,
      D2D1_INTERPOLATION_MODE_LINEAR);
    d2d->SetTransform(D2D1::Matrix3x2F::Identity());
    d2d->PopAxisAlignedClip();
  }
  d2d->SetTransform(D2D1::Matrix3x2F::Identity());
  const auto drawBar
    = [&](const FloatRect& track, const FloatRect& thumb, Target target) {
        d2d->FillRectangle(
          FloatRect {origin + track.mOffset, track.mSize},
          mBrushes.mScrollTrack.get());
        const auto inset = std::min(track.Width(), track.Height()) * 0.2f;
        const FloatRect thumbRect {
          origin + thumb.mOffset + FloatPoint {inset, inset},
          {thumb.Width() - (2 * inset), thumb.Height() - (2 * inset)},
        };
        const auto radius = std::min(thumbRect.Width(), thumbRect.Height()) / 2;
        const bool highlighted = (mHover == target) || (mPressed == target);
        d2d->FillRoundedRectangle(
          D2D1::RoundedRect(thumbRect, radius, radius),
          highlighted ? mBrushes.mScrollThumbHover.get()
                      : mBrushes.mScrollThumb.get());
      };
  if (layout.mScrollY) {
    drawBar(layout.mVerticalTrack, layout.mVerticalThumb, Target::VerticalBar);
  }
  if (layout.mScrollX) {
    drawBar(
      layout.mHorizontalTrack, layout.mHorizontalThumb, Target::HorizontalBar);
  }
}

}// namespace OpenKneeboard
