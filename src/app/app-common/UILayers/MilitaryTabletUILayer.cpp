// OpenKneeboard
//
// This program is open source; see the LICENSE file in the root of the OpenKneeboard repository.
#include <OpenKneeboard/CursorEvent.hpp>
#include <OpenKneeboard/Filesystem.hpp>
#include <OpenKneeboard/InterprocessRenderer.hpp>
#include <OpenKneeboard/KneeboardState.hpp>
#include <OpenKneeboard/MilitaryTabletUILayer.hpp>
#include <OpenKneeboard/MilitaryTabletViewportUILayer.hpp>
#include <OpenKneeboard/UserAction.hpp>

#include <OpenKneeboard/dprint.hpp>
#include <OpenKneeboard/format/filesystem.hpp>
#include <OpenKneeboard/tracing.hpp>

#include <algorithm>
#include <cstring>
#include <vector>

namespace OpenKneeboard {

namespace {
constexpr auto BezelFileName = "military-tablet-bezel.png";

// Pixel geometry of the portrait bezel image; the screen area includes the
// inner bevel shadow, while the content area is the fully-transparent part of it
constexpr Geometry2D::Size<float> BezelSize {992.0f, 1586.0f};
constexpr Geometry2D::Rect<float> ScreenRect {
  {144.0f, 174.0f},
  {709.0f, 1202.0f},
};
constexpr Geometry2D::Rect<float> ContentRect {
  {145.0f, 175.0f},
  {690.0f, 1180.0f},
};
constexpr Geometry2D::Rect<float> PowerKeyRect {
  {915.0f, 301.0f},
  {49.0f, 87.0f},
};
constexpr Geometry2D::Rect<float> ZoomInKeyRect {
  {911.0f, 477.0f},
  {47.0f, 80.0f},
};
constexpr Geometry2D::Rect<float> ZoomOutKeyRect {
  {911.0f, 558.0f},
  {47.0f, 78.0f},
};

// Landscape is the portrait image rotated 90 degrees counter-clockwise, which
// puts the hardware keys on the top edge
constexpr Geometry2D::Size<float> Oriented(
  const Geometry2D::Size<float>& size,
  bool landscape) {
  if (!landscape) {
    return size;
  }
  return {size.mHeight, size.mWidth};
}

constexpr Geometry2D::Rect<float> Oriented(
  const Geometry2D::Rect<float>& rect,
  bool landscape) {
  if (!landscape) {
    return rect;
  }
  return {
    {rect.Top(), BezelSize.mWidth - rect.Right()},
    {rect.Height(), rect.Width()},
  };
}
}// namespace

MilitaryTabletUILayer::MilitaryTabletUILayer(
  const audited_ptr<DXResources>& dxr,
  KneeboardState* kneeboard,
  MilitaryTabletViewportUILayer* viewport)
  : mDXResources(dxr),
    mKneeboard(kneeboard),
    mViewport(viewport),
    mBrushes(
      MilitaryTabletTheme::Brushes::Create(dxr->mD2DDeviceContext.get())) {
  mHardKeys = CursorClickableRegions<HardKeyRegion>::Create({
    {PowerKeyRect, HardKey::Power},
    {ZoomInKeyRect, HardKey::ZoomIn},
    {ZoomOutKeyRect, HardKey::ZoomOut},
  });
  AddEventListener(
    mHardKeys->evClicked,
    [this](KneeboardViewID, const HardKeyRegion& region) {
      this->OnHardKeyClicked(region);
    });
}

MilitaryTabletUILayer::~MilitaryTabletUILayer() {
  this->RemoveAllEventListeners();
}

bool MilitaryTabletUILayer::HardKeyRegion::operator==(
  const HardKeyRegion& other) const noexcept {
  return mKey == other.mKey;
}

bool MilitaryTabletUILayer::IsLandscape() const {
  return mKneeboard->GetUISettings().mInGameUI.mMilitaryTabletLandscape;
}

bool MilitaryTabletUILayer::IsHardKeyEnabled(HardKey key) const {
  switch (key) {
    case HardKey::Power:
      return true;
    case HardKey::ZoomIn:
      return mViewport->CanZoomIn();
    case HardKey::ZoomOut:
      return mViewport->CanZoomOut();
  }
  return false;
}

void MilitaryTabletUILayer::OnHardKeyClicked(const HardKeyRegion& region) {
  switch (region.mKey) {
    case HardKey::Power:
      fire_and_forget::wrap(
        &KneeboardState::PostUserAction, mKneeboard, UserAction::HIDE);
      return;
    case HardKey::ZoomIn:
      mViewport->ZoomIn();
      return;
    case HardKey::ZoomOut:
      mViewport->ZoomOut();
      return;
  }
}

void MilitaryTabletUILayer::PostCursorEvent(
  const IUILayer::NextList& next,
  const Context& context,
  KneeboardViewID kneeboardViewID,
  const CursorEvent& cursorEvent) {
  const auto [first, rest] = Split(next);
  if (cursorEvent.mTouchState == CursorTouchState::NotNearSurface) {
    mHardKeys->PostCursorEvent(kneeboardViewID, {});
    first->PostCursorEvent(rest, context, kneeboardViewID, {});
    return;
  }

  const auto nextMetrics = first->GetMetrics(rest, context);
  const auto layout = this->GetLayout(nextMetrics);
  const Geometry2D::Point<float> canvasPoint {
    cursorEvent.mX * layout.mCanvasSize.mWidth,
    cursorEvent.mY * layout.mCanvasSize.mHeight,
  };

  if (cursorEvent.mSource == CursorSource::WindowPointer) {
    // The app window shows the unzoomed content: map back to that content,
    // then to the coordinates the next layers expect from their own metrics
    const auto& content = layout.mContentArea;
    const auto x = (canvasPoint.mX - content.Left()) / content.Width();
    const auto y = (canvasPoint.mY - content.Top()) / content.Height();
    if (x < 0 || x > 1 || y < 0 || y > 1) {
      first->PostCursorEvent(rest, context, kneeboardViewID, {});
      return;
    }
    const auto& nextContent = nextMetrics.mContentArea;
    const auto& nextSize = nextMetrics.mPreferredSize.mPixelSize;
    CursorEvent nextEvent {cursorEvent};
    nextEvent.mX
      = (nextContent.Left() + (x * nextContent.Width())) / nextSize.mWidth;
    nextEvent.mY
      = (nextContent.Top() + (y * nextContent.Height())) / nextSize.mHeight;
    first->PostCursorEvent(rest, context, kneeboardViewID, nextEvent);
    return;
  }

  // Hidden with the power key or a binding: nothing on screen takes clicks
  const auto renderer = mKneeboard->GetInterprocessRenderer();
  if (renderer && !renderer->IsVisible()) {
    mHardKeys->PostCursorEvent(kneeboardViewID, {});
    first->PostCursorEvent(rest, context, kneeboardViewID, {});
    return;
  }

  const bool landscape = this->IsLandscape();
  const auto toBezel
    = Oriented(BezelSize, landscape).mWidth / layout.mCanvasSize.mWidth;
  const auto x = canvasPoint.mX * toBezel;
  const auto y = canvasPoint.mY * toBezel;
  CursorEvent bezelEvent {cursorEvent};
  bezelEvent.mX = landscape ? (BezelSize.mWidth - y) : x;
  bezelEvent.mY = landscape ? x : y;
  mHardKeys->PostCursorEvent(kneeboardViewID, bezelEvent);

  this->PostNextCursorEvent(next, context, kneeboardViewID, cursorEvent);
}

MilitaryTabletUILayer::Layout MilitaryTabletUILayer::GetLayout(
  const Metrics& nextMetrics) const {
  const bool landscape = this->IsLandscape();
  const auto nextSize
    = nextMetrics.mPreferredSize.mPixelSize.StaticCast<float>();
  const auto screenContent = Oriented(ContentRect, landscape);
  const auto scale = std::max(
    nextSize.mWidth / screenContent.Width(),
    nextSize.mHeight / screenContent.Height());
  const auto nextArea = screenContent * scale;

  // The next layers stretch to fill the screen; this is where the unzoomed
  // content ends up inside it
  const auto nextContent = nextMetrics.mContentArea.StaticCast<float>();
  const auto toNextArea = [&](float value, float area, float size) {
    return value * area / size;
  };
  const Geometry2D::Rect<float> viewport {
    {
      nextArea.Left()
        + toNextArea(nextContent.Left(), nextArea.Width(), nextSize.mWidth),
      nextArea.Top()
        + toNextArea(nextContent.Top(), nextArea.Height(), nextSize.mHeight),
    },
    {
      toNextArea(nextContent.Width(), nextArea.Width(), nextSize.mWidth),
      toNextArea(nextContent.Height(), nextArea.Height(), nextSize.mHeight),
    },
  };
  const auto fit = std::min(
    viewport.Width() / nextContent.Width(),
    viewport.Height() / nextContent.Height());
  const auto fitted = nextContent.mSize * fit;
  const Geometry2D::Rect<float> contentArea {
    {
      viewport.Left() + ((viewport.Width() - fitted.mWidth) / 2),
      viewport.Top() + ((viewport.Height() - fitted.mHeight) / 2),
    },
    fitted,
  };

  return {
    (Oriented(BezelSize, landscape) * scale).Rounded<uint32_t>(),
    (Oriented(ScreenRect, landscape) * scale).Rounded<uint32_t>(),
    nextArea.Rounded<uint32_t>(),
    contentArea.Rounded<uint32_t>(),
  };
}

IUILayer::Metrics MilitaryTabletUILayer::GetMetrics(
  const IUILayer::NextList& next,
  const Context& context) const {
  const auto nextMetrics = next.front()->GetMetrics(next.subspan(1), context);
  const auto& nextSize = nextMetrics.mPreferredSize.mPixelSize;
  const auto layout = this->GetLayout(nextMetrics);

  return Metrics {
    nextMetrics.mPreferredSize.Extended({
      layout.mCanvasSize.mWidth - nextSize.mWidth,
      layout.mCanvasSize.mHeight - nextSize.mHeight,
    }),
    layout.mNextArea,
    layout.mContentArea,
  };
}

task<void> MilitaryTabletUILayer::Render(
  const RenderContext& rc,
  const IUILayer::NextList& next,
  const Context& context,
  const PixelRect& rect) {
  OPENKNEEBOARD_TraceLoggingScope("MilitaryTabletUILayer::Render()");
  const auto [first, rest] = Split(next);
  const bool landscape = this->IsLandscape();
  const auto layout = this->GetLayout(first->GetMetrics(rest, context));

  const auto scale = rect.Width<float>() / layout.mCanvasSize.Width<float>();
  const auto origin = rect.mOffset.StaticCast<float>();
  const auto toRenderRect = [origin, scale](const PixelRect& area) {
    return PixelRect {
      (origin + (area.mOffset.StaticCast<float>() * scale))
        .Rounded<uint32_t>(),
      (area.mSize.StaticCast<float>() * scale).Rounded<uint32_t>(),
    };
  };

  {
    auto d2d = rc.d2d();
    d2d->SetTransform(D2D1::Matrix3x2F::Identity());
    d2d->FillRectangle(
      toRenderRect(layout.mScreenArea), mDXResources->mBlackBrush.get());
  }

  co_await first->Render(rc, rest, context, toRenderRect(layout.mNextArea));

  const auto bezel = this->GetBezelBitmap(landscape);
  if (!bezel) {
    co_return;
  }

  auto d2d = rc.d2d();
  d2d->SetTransform(D2D1::Matrix3x2F::Identity());
  d2d->DrawBitmap(
    bezel, rect, 1.0f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);

  const auto [hoverKey, keys] = mHardKeys->GetState();
  const auto pressedKey = mHardKeys->GetPressedButton();
  const auto toRender
    = rect.Width<float>() / Oriented(BezelSize, landscape).mWidth;
  for (const auto& key: keys) {
    ID2D1Brush* brush = nullptr;
    if (!this->IsHardKeyEnabled(key.mKey)) {
      brush = mBrushes.mHardKeyDisabled.get();
    } else if (pressedKey == key) {
      brush = mBrushes.mHardKeyPressed.get();
    } else if (hoverKey == key) {
      brush = mBrushes.mHardKeyHover.get();
    } else {
      continue;
    }

    const auto area = Oriented(key.mRect, landscape) * toRender;
    const auto radius = std::min(area.Width(), area.Height()) * 0.2f;
    const auto keyRect = D2D1::RoundedRect(
      Geometry2D::Rect<float> {origin + area.mOffset, area.mSize},
      radius,
      radius);
    d2d->FillRoundedRectangle(keyRect, brush);
    if (brush != mBrushes.mHardKeyDisabled.get()) {
      d2d->DrawRoundedRectangle(
        keyRect, mBrushes.mAccent.get(), std::max(1.0f, 2.0f * toRender));
    }
  }
}

ID2D1Bitmap* MilitaryTabletUILayer::GetBezelBitmap(bool landscape) {
  if (!mBezelLoadAttempted) {
    this->LoadBezelBitmaps();
  }
  return landscape ? mLandscapeBezel.get() : mPortraitBezel.get();
}

void MilitaryTabletUILayer::LoadBezelBitmaps() {
  mBezelLoadAttempted = true;

  const auto path = Filesystem::GetImmutableDataDirectory() / BezelFileName;
  auto wic = mDXResources->mWIC.get();

  winrt::com_ptr<IWICBitmapDecoder> decoder;
  wic->CreateDecoderFromFilename(
    path.c_str(),
    nullptr,
    GENERIC_READ,
    WICDecodeMetadataCacheOnLoad,
    decoder.put());
  if (!decoder) {
    dprint.Warning("Failed to load military tablet bezel from {}", path);
    return;
  }

  winrt::com_ptr<IWICBitmapFrameDecode> frame;
  decoder->GetFrame(0, frame.put());
  if (!frame) {
    return;
  }

  winrt::com_ptr<IWICFormatConverter> converter;
  wic->CreateFormatConverter(converter.put());
  if (!converter) {
    return;
  }
  converter->Initialize(
    frame.get(),
    GUID_WICPixelFormat32bppPBGRA,
    WICBitmapDitherTypeNone,
    nullptr,
    0.0f,
    WICBitmapPaletteTypeMedianCut);

  UINT width {};
  UINT height {};
  converter->GetSize(&width, &height);
  if (width == 0 || height == 0) {
    return;
  }
  std::vector<uint8_t> portrait(width * height * 4);
  if (FAILED(converter->CopyPixels(
        nullptr,
        width * 4,
        static_cast<UINT>(portrait.size()),
        portrait.data()))) {
    return;
  }

  // The case itself is slightly translucent in the image, so the game would
  // show through it; make it opaque as if drawn over black, keeping only the
  // soft outer edge. Lower-alpha pixels inside the case are all in the bevel
  // around the screen, which is drawn over black anyway.
  for (std::size_t i = 3; i < portrait.size(); i += 4) {
    if (portrait[i] >= 128) {
      portrait[i] = 255;
    }
  }

  std::vector<uint8_t> landscape(portrait.size());
  for (UINT y = 0; y < width; ++y) {
    for (UINT x = 0; x < height; ++x) {
      std::memcpy(
        &landscape[((y * height) + x) * 4],
        &portrait[((x * width) + (width - 1 - y)) * 4],
        4);
    }
  }

  const auto properties = D2D1::BitmapProperties1(
    D2D1_BITMAP_OPTIONS_NONE,
    D2D1::PixelFormat(
      DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
  auto ctx = mDXResources->mD2DDeviceContext.get();
  winrt::check_hresult(ctx->CreateBitmap(
    {width, height},
    portrait.data(),
    width * 4,
    properties,
    mPortraitBezel.put()));
  winrt::check_hresult(ctx->CreateBitmap(
    {height, width},
    landscape.data(),
    height * 4,
    properties,
    mLandscapeBezel.put()));
}

}// namespace OpenKneeboard
