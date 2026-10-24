// OpenKneeboard
//
// This program is open source; see the LICENSE file in the root of the OpenKneeboard repository.
#pragma once

#include <OpenKneeboard/DXResources.hpp>
#include <OpenKneeboard/UILayerBase.hpp>

#include <shims/winrt/base.h>

#include <OpenKneeboard/audited_ptr.hpp>

namespace OpenKneeboard {

/** Draws a rugged tablet bezel around every other layer.
 *
 * The next layers are letterboxed, unscaled, into the screen cut-out of the
 * bezel image; the bezel is drawn on top, and cursor events outside of the
 * screen are not forwarded.
 */
class MilitaryTabletUILayer final : public UILayerBase {
 public:
  MilitaryTabletUILayer(const audited_ptr<DXResources>& dxr);
  virtual ~MilitaryTabletUILayer();

  virtual void PostCursorEvent(
    const NextList&,
    const Context&,
    KneeboardViewID,
    const CursorEvent&) override;
  virtual Metrics GetMetrics(const NextList&, const Context&) const override;
  [[nodiscard]] task<void> Render(
    const RenderContext&,
    const NextList&,
    const Context&,
    const PixelRect&) override;

 private:
  struct Layout {
    PixelSize mCanvasSize;
    PixelRect mScreenArea;
    PixelRect mNextArea;
  };
  static Layout GetLayout(const PixelSize& nextSize);

  winrt::com_ptr<ID2D1Bitmap> GetBezelBitmap();

  audited_ptr<DXResources> mDXResources;
  winrt::com_ptr<ID2D1Bitmap> mBezelBitmap;
  bool mBezelBitmapLoadAttempted {false};
};

}// namespace OpenKneeboard
