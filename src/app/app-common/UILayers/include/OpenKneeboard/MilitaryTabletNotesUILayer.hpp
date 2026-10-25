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
#include <utility>
#include <vector>

namespace OpenKneeboard {

/** Notepad of the military tablet: blank pages to draw on with the cursor.
 *
 * While open, it covers the whole tablet screen instead of the next layers.
 * The pages are saved to disk after every change and shared by all views.
 */
class MilitaryTabletNotesUILayer final : public UILayerBase,
                                         private EventReceiver {
 public:
  MilitaryTabletNotesUILayer(
    const audited_ptr<DXResources>& dxr,
    KneeboardView*);
  virtual ~MilitaryTabletNotesUILayer();

  bool IsOpen() const noexcept;
  void SetOpen(bool);

  bool CanGoToPreviousPage() const;
  bool CanGoToNextPage() const;
  void PreviousPage();
  /// Appends a blank page after the last one, unless it is blank already
  void NextPage();

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

  struct Store;

 private:
  using FloatPoint = Geometry2D::Point<float>;
  using FloatRect = Geometry2D::Rect<float>;

  enum class Tool {
    Pen,
    Eraser,
  };
  enum class Button {
    None,
    Back,
    Pen,
    Eraser,
    Undo,
    Clear,
    PreviousPage,
    NextPage,
  };
  struct Layout {
    FloatRect mBar;
    std::vector<std::pair<Button, FloatRect>> mButtons;
    FloatRect mTitle;
    FloatRect mPage;
  };
  Layout GetLayout(const PixelSize&) const;
  Button HitTest(const Layout&, const FloatPoint&) const;
  bool IsButtonEnabled(Button) const;
  void OnButtonClicked(Button);
  FloatPoint ToPage(const Layout&, const FloatPoint&) const;
  void Erase(const FloatPoint&);
  void EndStroke();

  audited_ptr<DXResources> mDXResources;
  MilitaryTabletTheme::Brushes mBrushes;
  winrt::com_ptr<ID2D1StrokeStyle> mStrokeStyle;
  std::shared_ptr<Store> mStore;

  bool mOpen {false};
  std::size_t mPageIndex {0};
  Tool mTool {Tool::Pen};
  bool mClearArmed {false};

  std::optional<PixelSize> mSize;
  Button mHover {Button::None};
  Button mPressed {Button::None};
  bool mTouching {false};
  bool mDrawing {false};
  bool mErasing {false};
  bool mErased {false};
  std::vector<D2D1_POINT_2F> mStroke;
};

}// namespace OpenKneeboard
