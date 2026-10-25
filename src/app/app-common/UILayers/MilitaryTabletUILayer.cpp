// OpenKneeboard
//
// This program is open source; see the LICENSE file in the root of the OpenKneeboard repository.
#include <OpenKneeboard/CursorEvent.hpp>
#include <OpenKneeboard/Filesystem.hpp>
#include <OpenKneeboard/InterprocessRenderer.hpp>
#include <OpenKneeboard/KneeboardState.hpp>
#include <OpenKneeboard/KneeboardView.hpp>
#include <OpenKneeboard/MilitaryTabletHomeUILayer.hpp>
#include <OpenKneeboard/MilitaryTabletNotesUILayer.hpp>
#include <OpenKneeboard/MilitaryTabletUILayer.hpp>
#include <OpenKneeboard/MilitaryTabletViewportUILayer.hpp>
#include <OpenKneeboard/TabView.hpp>
#include <OpenKneeboard/TabsList.hpp>
#include <OpenKneeboard/UserAction.hpp>

#include <OpenKneeboard/dprint.hpp>
#include <OpenKneeboard/format/filesystem.hpp>
#include <OpenKneeboard/tracing.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace OpenKneeboard {

namespace {
constexpr auto BezelFileName = "military-tablet-bezel.png";

// Pixel geometry of the portrait bezel image; the screen is the dark glass
// inside the inner rim, rounded corners included
constexpr Geometry2D::Size<float> BezelSize {1024.0f, 1536.0f};
constexpr Geometry2D::Rect<float> ScreenRect {
  {156.0f, 170.0f},
  {713.0f, 1171.0f},
};
constexpr float ScreenCornerRadius = 16.0f;
// Below this alpha, the image has a faint matte around its outline
constexpr uint8_t HaloMaxAlpha = 16;
// The sun keys stop short of black
constexpr float MinBrightness = 0.1f;
constexpr float BrightnessStep = 0.1f;
// The bezel is 2:3, so scaling it in steps of 1/512 gives the canvas the exact
// same aspect ratio whatever the tab: the game overlay does not shift
constexpr float ScaleStep = 1.0f / 512;

constexpr Geometry2D::Rect<float> Key(float x, float y, float w, float h) {
  return {{x, y}, {w, h}};
}

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
  KneeboardView* kneeboardView,
  MilitaryTabletViewportUILayer* viewport,
  MilitaryTabletHomeUILayer* home,
  MilitaryTabletNotesUILayer* notes)
  : mDXResources(dxr),
    mKneeboard(kneeboard),
    mKneeboardView(kneeboardView),
    mViewport(viewport),
    mHome(home),
    mNotes(notes),
    mBrushes(
      MilitaryTabletTheme::Brushes::Create(dxr->mD2DDeviceContext.get())) {
  // Measured on the portrait image
  mHardKeys = CursorClickableRegions<HardKeyRegion>::Create({
    {Key(62, 316, 53, 89), HardKey::NextPage},
    {Key(62, 405, 53, 86), HardKey::PreviousPage},
    {Key(62, 614, 53, 84), HardKey::PreviousTab},
    {Key(62, 698, 53, 89), HardKey::NextTab},
    {Key(62, 920, 53, 107), HardKey::Notes},
    {Key(918, 306, 55, 78), HardKey::Power},
    {Key(918, 469, 55, 81), HardKey::ZoomIn},
    {Key(918, 550, 55, 80), HardKey::ZoomOut},
    {Key(918, 842, 55, 104), HardKey::BrightnessUp},
    {Key(918, 946, 55, 98), HardKey::BrightnessDown},
    {Key(451, 1388, 122, 63), HardKey::Home},
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

bool MilitaryTabletUILayer::IsAppOpen() const {
  return mHome->IsOpen() || mNotes->IsOpen();
}

bool MilitaryTabletUILayer::CanTurnPage(bool next) const {
  if (mNotes->IsOpen()) {
    return next ? mNotes->CanGoToNextPage() : mNotes->CanGoToPreviousPage();
  }
  if (mHome->IsOpen()) {
    return false;
  }
  // Same rules as the header page buttons
  const auto tabView = mKneeboardView->GetCurrentTabView();
  if (!tabView) {
    return false;
  }
  const auto pages = tabView->GetPageIDs();
  if (pages.size() < 2) {
    return false;
  }
  if (mKneeboard->GetUISettings().mLoopPages) {
    return true;
  }
  return tabView->GetPageID() != (next ? pages.back() : pages.front());
}

bool MilitaryTabletUILayer::CanSwitchTab(bool next) const {
  // Same rules as the header tab buttons
  const auto tabs = mKneeboard->GetTabsList();
  const auto count = tabs ? tabs->GetTabs().size() : 0;
  if (count < 2) {
    return false;
  }
  if (mKneeboard->GetUISettings().mLoopTabs) {
    return true;
  }
  const auto index = mKneeboardView->GetTabIndex();
  return next ? (index + 1 < count) : (index > 0);
}

// Not the native tint: the game overlay applies it to the bezel too
float MilitaryTabletUILayer::GetBrightness() const {
  return mKneeboard->GetUISettings().mInGameUI.mMilitaryTabletBrightness;
}

void MilitaryTabletUILayer::ChangeBrightness(float direction) {
  auto settings = mKneeboard->GetUISettings();
  auto& brightness = settings.mInGameUI.mMilitaryTabletBrightness;
  brightness = std::clamp(
    std::round((brightness / BrightnessStep) + direction) * BrightnessStep,
    MinBrightness,
    1.0f);
  fire_and_forget::wrap(&KneeboardState::SetUISettings, mKneeboard, settings);
}

bool MilitaryTabletUILayer::IsHardKeyEnabled(HardKey key) const {
  switch (key) {
    case HardKey::Power:
    case HardKey::Notes:
    case HardKey::Home:
      return true;
    case HardKey::ZoomIn:
      return !this->IsAppOpen() && mViewport->CanZoomIn();
    case HardKey::ZoomOut:
      return !this->IsAppOpen() && mViewport->CanZoomOut();
    case HardKey::BrightnessUp:
      return this->GetBrightness() < 0.999f;
    case HardKey::BrightnessDown:
      return this->GetBrightness() - BrightnessStep > MinBrightness - 0.001f;
    case HardKey::NextPage:
      return this->CanTurnPage(true);
    case HardKey::PreviousPage:
      return this->CanTurnPage(false);
    case HardKey::NextTab:
      return this->CanSwitchTab(true);
    case HardKey::PreviousTab:
      return this->CanSwitchTab(false);
  }
  return false;
}

bool MilitaryTabletUILayer::IsHardKeyActive(HardKey key) const {
  return (key == HardKey::Home && mHome->IsOpen())
    || (key == HardKey::Notes && mNotes->IsOpen());
}

void MilitaryTabletUILayer::OnHardKeyClicked(const HardKeyRegion& region) {
  if (!this->IsHardKeyEnabled(region.mKey)) {
    return;
  }
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
    case HardKey::BrightnessUp:
      this->ChangeBrightness(1.0f);
      return;
    case HardKey::BrightnessDown:
      this->ChangeBrightness(-1.0f);
      return;
    case HardKey::NextPage:
    case HardKey::PreviousPage: {
      const bool next = (region.mKey == HardKey::NextPage);
      if (mNotes->IsOpen()) {
        next ? mNotes->NextPage() : mNotes->PreviousPage();
        return;
      }
      fire_and_forget::wrap(
        &KneeboardView::PostUserAction,
        mKneeboardView,
        next ? UserAction::NEXT_PAGE : UserAction::PREVIOUS_PAGE);
      return;
    }
    // Switching tabs also closes the home screen and the notes
    case HardKey::NextTab:
      mKneeboardView->NextTab();
      return;
    case HardKey::PreviousTab:
      mKneeboardView->PreviousTab();
      return;
    case HardKey::Notes: {
      const bool open = !mNotes->IsOpen();
      mHome->SetOpen(false);
      mNotes->SetOpen(open);
      return;
    }
    case HardKey::Home: {
      const bool open = !mHome->IsOpen();
      mNotes->SetOpen(false);
      mHome->SetOpen(open);
      return;
    }
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
  const auto screen = Oriented(ScreenRect, landscape);
  const auto fitScale = std::max(
    nextSize.mWidth / screen.Width(), nextSize.mHeight / screen.Height());
  const auto scale = std::ceil(fitScale / ScaleStep) * ScaleStep;
  const auto nextArea = screen * scale;

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
      toRenderRect(layout.mNextArea), mDXResources->mBlackBrush.get());
  }

  co_await first->Render(rc, rest, context, toRenderRect(layout.mNextArea));

  if (const auto brightness = this->GetBrightness(); brightness < 1.0f) {
    auto d2d = rc.d2d();
    d2d->SetTransform(D2D1::Matrix3x2F::Identity());
    mBrushes.mDimmer->SetOpacity(1.0f - brightness);
    d2d->FillRectangle(
      toRenderRect(layout.mNextArea), mBrushes.mDimmer.get());
  }

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
    } else if (pressedKey == key || this->IsHardKeyActive(key.mKey)) {
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

  // The dark glass is cleared, so the header, footer and content can reach the
  // inner rim, which stays on top of them; the rounded corners are
  // antialiased. The case itself is slightly translucent in the image, so the
  // game would show through it: make it opaque as if drawn over black, keeping
  // the soft outer edge but not the faint matte around it.
  const auto glassCoverage = [](float x, float y) {
    const auto& screen = ScreenRect;
    if (
      x <= screen.Left() || x >= screen.Right() || y <= screen.Top()
      || y >= screen.Bottom()) {
      return 0.0f;
    }
    const auto cornerX = std::clamp(
      x,
      screen.Left() + ScreenCornerRadius,
      screen.Right() - ScreenCornerRadius);
    const auto cornerY = std::clamp(
      y,
      screen.Top() + ScreenCornerRadius,
      screen.Bottom() - ScreenCornerRadius);
    const auto distance = std::hypot(x - cornerX, y - cornerY);
    return std::clamp(ScreenCornerRadius - distance + 0.5f, 0.0f, 1.0f);
  };
  for (UINT y = 0; y < height; ++y) {
    for (UINT x = 0; x < width; ++x) {
      const auto pixel = &portrait[((y * width) + x) * 4];
      const auto glass = glassCoverage(
        static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f);
      if (glass > 0) {
        for (int channel = 0; channel < 4; ++channel) {
          pixel[channel]
            = static_cast<uint8_t>(pixel[channel] * (1.0f - glass));
        }
      } else if (pixel[3] >= 128) {
        pixel[3] = 255;
      } else if (pixel[3] < HaloMaxAlpha) {
        std::memset(pixel, 0, 4);
      }
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
