// OpenKneeboard
//
// This program is open source; see the LICENSE file in the root of the OpenKneeboard repository.
#include <OpenKneeboard/Filesystem.hpp>
#include <OpenKneeboard/MilitaryTabletUILayer.hpp>

#include <OpenKneeboard/dprint.hpp>
#include <OpenKneeboard/tracing.hpp>

#include <algorithm>

namespace OpenKneeboard {

namespace {
constexpr auto BezelFileName = "military-tablet-bezel.png";

// Pixel geometry of the bezel image; the screen area includes the inner
// bevel shadow, while the content area is the fully-transparent part of it
constexpr Geometry2D::Size<float> BezelSize {992.0f, 1586.0f};
constexpr Geometry2D::Rect<float> ScreenRect {
  {144.0f, 174.0f},
  {709.0f, 1202.0f},
};
constexpr Geometry2D::Rect<float> ContentRect {
  {145.0f, 175.0f},
  {690.0f, 1180.0f},
};
}// namespace

MilitaryTabletUILayer::MilitaryTabletUILayer(
  const audited_ptr<DXResources>& dxr)
  : mDXResources(dxr) {
}

MilitaryTabletUILayer::~MilitaryTabletUILayer() = default;

void MilitaryTabletUILayer::PostCursorEvent(
  const IUILayer::NextList& next,
  const Context& context,
  KneeboardViewID kneeboardViewID,
  const CursorEvent& cursorEvent) {
  this->PostNextCursorEvent(next, context, kneeboardViewID, cursorEvent);
}

MilitaryTabletUILayer::Layout MilitaryTabletUILayer::GetLayout(
  const PixelSize& nextSize) {
  const auto scale = std::max(
    nextSize.Width<float>() / ContentRect.Width(),
    nextSize.Height<float>() / ContentRect.Height());
  const auto content = ContentRect * scale;
  const Geometry2D::Point<float> nextOffset {
    content.Left() + ((content.Width() - nextSize.Width<float>()) / 2),
    content.Top() + ((content.Height() - nextSize.Height<float>()) / 2),
  };

  return {
    (BezelSize * scale).Rounded<uint32_t>(),
    (ScreenRect * scale).Rounded<uint32_t>(),
    {nextOffset.Rounded<uint32_t>(), nextSize},
  };
}

IUILayer::Metrics MilitaryTabletUILayer::GetMetrics(
  const IUILayer::NextList& next,
  const Context& context) const {
  const auto nextMetrics = next.front()->GetMetrics(next.subspan(1), context);
  const auto nextSize = nextMetrics.mPreferredSize.mPixelSize;
  const auto layout = GetLayout(nextSize);

  return Metrics {
    nextMetrics.mPreferredSize.Extended({
      layout.mCanvasSize.mWidth - nextSize.mWidth,
      layout.mCanvasSize.mHeight - nextSize.mHeight,
    }),
    layout.mNextArea,
    {
      layout.mNextArea.mOffset + nextMetrics.mContentArea.mOffset,
      nextMetrics.mContentArea.mSize,
    },
  };
}

task<void> MilitaryTabletUILayer::Render(
  const RenderContext& rc,
  const IUILayer::NextList& next,
  const Context& context,
  const PixelRect& rect) {
  OPENKNEEBOARD_TraceLoggingScope("MilitaryTabletUILayer::Render()");
  const auto [first, rest] = Split(next);
  const auto layout
    = GetLayout(first->GetMetrics(rest, context).mPreferredSize.mPixelSize);

  const auto scale = rect.Width<float>() / layout.mCanvasSize.Width<float>();
  const auto toRenderRect = [&rect, scale](const PixelRect& area) {
    return PixelRect {
      (rect.mOffset.StaticCast<float>()
       + (area.mOffset.StaticCast<float>() * scale))
        .Rounded<uint32_t>(),
      (area.mSize.StaticCast<float>() * scale).Rounded<uint32_t>(),
    };
  };

  const auto bezel = this->GetBezelBitmap();

  {
    auto d2d = rc.d2d();
    d2d->SetTransform(D2D1::Matrix3x2F::Identity());
    d2d->FillRectangle(
      toRenderRect(layout.mScreenArea), mDXResources->mBlackBrush.get());
  }

  co_await first->Render(rc, rest, context, toRenderRect(layout.mNextArea));

  if (!bezel) {
    co_return;
  }

  auto d2d = rc.d2d();
  d2d->SetTransform(D2D1::Matrix3x2F::Identity());
  d2d->DrawBitmap(
    bezel.get(), rect, 1.0f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
}

winrt::com_ptr<ID2D1Bitmap> MilitaryTabletUILayer::GetBezelBitmap() {
  if (mBezelBitmapLoadAttempted) {
    return mBezelBitmap;
  }
  mBezelBitmapLoadAttempted = true;

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
    return {};
  }

  winrt::com_ptr<IWICBitmapFrameDecode> frame;
  decoder->GetFrame(0, frame.put());
  if (!frame) {
    return {};
  }

  winrt::com_ptr<IWICFormatConverter> converter;
  wic->CreateFormatConverter(converter.put());
  if (!converter) {
    return {};
  }
  converter->Initialize(
    frame.get(),
    GUID_WICPixelFormat32bppPBGRA,
    WICBitmapDitherTypeNone,
    nullptr,
    0.0f,
    WICBitmapPaletteTypeMedianCut);

  mDXResources->mD2DDeviceContext->CreateBitmapFromWicBitmap(
    converter.get(), mBezelBitmap.put());
  return mBezelBitmap;
}

}// namespace OpenKneeboard
