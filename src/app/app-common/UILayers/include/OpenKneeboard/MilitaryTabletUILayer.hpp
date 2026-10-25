// OpenKneeboard
//
// This program is open source; see the LICENSE file in the root of the OpenKneeboard repository.
#pragma once

#include <OpenKneeboard/CursorClickableRegions.hpp>
#include <OpenKneeboard/DXResources.hpp>
#include <OpenKneeboard/Events.hpp>
#include <OpenKneeboard/MilitaryTabletTheme.hpp>
#include <OpenKneeboard/UILayerBase.hpp>

#include <shims/winrt/base.h>

#include <OpenKneeboard/audited_ptr.hpp>

namespace OpenKneeboard {

class KneeboardState;
class MilitaryTabletHomeUILayer;
class MilitaryTabletNotesUILayer;
class MilitaryTabletViewportUILayer;

/** Draws a rugged tablet bezel around every other layer.
 *
 * The next layers fill the screen cut-out of the bezel image, which can be
 * rotated to landscape; the bezel is drawn on top. The hardware keys use the
 * native actions (hide, page, tab) or the other tablet layers (zoom, notes,
 * home); the brightness keys dim the screen only. Other cursor events outside
 * of the screen are not forwarded.
 */
class MilitaryTabletUILayer final : public UILayerBase, private EventReceiver {
 public:
  MilitaryTabletUILayer(
    const audited_ptr<DXResources>& dxr,
    KneeboardState*,
    KneeboardView*,
    MilitaryTabletViewportUILayer*,
    MilitaryTabletHomeUILayer*,
    MilitaryTabletNotesUILayer*);
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
  enum class HardKey {
    Power,
    ZoomIn,
    ZoomOut,
    BrightnessUp,
    BrightnessDown,
    NextPage,
    PreviousPage,
    PreviousTab,
    NextTab,
    Notes,
    Home,
  };
  struct HardKeyRegion {
    Geometry2D::Rect<float> mRect {};
    HardKey mKey {};

    bool operator==(const HardKeyRegion&) const noexcept;
  };

  struct Layout {
    PixelSize mCanvasSize;
    PixelRect mNextArea;
    PixelRect mContentArea;
  };
  Layout GetLayout(const Metrics& nextMetrics) const;
  bool IsLandscape() const;
  bool IsHardKeyEnabled(HardKey) const;
  bool IsHardKeyActive(HardKey) const;
  void OnHardKeyClicked(const HardKeyRegion&);
  bool IsAppOpen() const;
  bool CanTurnPage(bool next) const;
  bool CanSwitchTab(bool next) const;
  float GetBrightness() const;
  void ChangeBrightness(float direction);

  ID2D1Bitmap* GetBezelBitmap(bool landscape);
  void LoadBezelBitmaps();

  audited_ptr<DXResources> mDXResources;
  KneeboardState* mKneeboard {nullptr};
  KneeboardView* mKneeboardView {nullptr};
  MilitaryTabletViewportUILayer* mViewport {nullptr};
  MilitaryTabletHomeUILayer* mHome {nullptr};
  MilitaryTabletNotesUILayer* mNotes {nullptr};
  MilitaryTabletTheme::Brushes mBrushes;
  std::shared_ptr<CursorClickableRegions<HardKeyRegion>> mHardKeys;

  winrt::com_ptr<ID2D1Bitmap1> mPortraitBezel;
  winrt::com_ptr<ID2D1Bitmap1> mLandscapeBezel;
  bool mBezelLoadAttempted {false};
};

}// namespace OpenKneeboard
