// OpenKneeboard
//
// This program is open source; see the LICENSE file in the root of the OpenKneeboard repository.
#pragma once

#include <OpenKneeboard/DXResources.hpp>
#include <OpenKneeboard/Events.hpp>
#include <OpenKneeboard/MilitaryTabletTheme.hpp>
#include <OpenKneeboard/UILayerBase.hpp>

#include <shims/winrt/base.h>

#include <OpenKneeboard/audited_ptr.hpp>

#include <memory>
#include <optional>

namespace OpenKneeboard {

class KneeboardView;
class RenderTarget;

/** Fits, zooms and scrolls the tab content inside the military tablet screen.
 *
 * The next layer is always rendered at the size it is displayed at; when that
 * does not fit, it is rendered to an offscreen texture and only the visible
 * part is copied, with scroll bars on the right and bottom edges.
 */
class MilitaryTabletViewportUILayer final : public UILayerBase,
                                            private EventReceiver {
 public:
  enum class Fit {
    Page,
    Width,
  };

  MilitaryTabletViewportUILayer(
    const audited_ptr<DXResources>& dxr,
    KneeboardView*);
  virtual ~MilitaryTabletViewportUILayer();

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

  Fit GetFit() const noexcept;
  bool IsZoomed() const noexcept;
  bool CanZoomIn() const noexcept;
  bool CanZoomOut() const noexcept;

  void SetFit(Fit);
  void ZoomIn();
  void ZoomOut();
  void ResetZoom();

  Event<> evStateChangedEvent;

 private:
  using FloatPoint = Geometry2D::Point<float>;
  using FloatSize = Geometry2D::Size<float>;
  using FloatRect = Geometry2D::Rect<float>;

  enum class Target {
    None,
    Content,
    HorizontalBar,
    VerticalBar,
  };

  struct Layout {
    FloatSize mContentSize;
    FloatPoint mOffset;
    FloatRect mDestination;
    bool mScrollX {false};
    bool mScrollY {false};
    FloatRect mHorizontalTrack;
    FloatRect mHorizontalThumb;
    FloatRect mVerticalTrack;
    FloatRect mVerticalThumb;
  };
  Layout GetLayout(const PixelSize& viewport, const PixelSize& content) const;
  Target HitTest(const Layout&, const FloatPoint&) const;
  FloatPoint GetVisibleCenter() const;

  void SetZoom(std::size_t index, Fit, const FloatPoint& center);
  void ScrollTo(const Layout&, const FloatPoint& center);
  void ResizeOffscreen(const PixelSize&);

  audited_ptr<DXResources> mDXResources;
  MilitaryTabletTheme::Brushes mBrushes;

  Fit mFit {Fit::Page};
  std::size_t mZoomIndex {0};
  FloatPoint mCenter {0.5f, 0.0f};

  std::optional<PixelSize> mViewportSize;
  std::optional<PixelSize> mContentSize;
  std::optional<PageID> mPageID;

  Target mPressed {Target::None};
  Target mHover {Target::None};
  bool mDragging {false};
  float mDragOrigin {};
  FloatPoint mDragCenter {};

  std::shared_ptr<RenderTarget> mOffscreen;
  winrt::com_ptr<ID3D11Texture2D> mOffscreenTexture;
  winrt::com_ptr<ID3D11ShaderResourceView> mOffscreenSRV;
  PixelSize mOffscreenSize {};
};

}// namespace OpenKneeboard
