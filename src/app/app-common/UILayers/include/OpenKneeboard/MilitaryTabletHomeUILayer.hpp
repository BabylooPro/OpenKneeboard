// OpenKneeboard
//
// This program is open source; see the LICENSE file in the root of the OpenKneeboard repository.
#pragma once

#include <OpenKneeboard/DXResources.hpp>
#include <OpenKneeboard/Events.hpp>
#include <OpenKneeboard/ITab.hpp>
#include <OpenKneeboard/MilitaryTabletTheme.hpp>
#include <OpenKneeboard/UILayerBase.hpp>

#include <OpenKneeboard/audited_ptr.hpp>

#include <optional>
#include <vector>

namespace OpenKneeboard {

class KneeboardState;

/** Home screen of the military tablet: a grid of cards, one per tab.
 *
 * While open, it covers the whole tablet screen instead of the next layers;
 * choosing a card switches to that tab, which keeps its page.
 */
class MilitaryTabletHomeUILayer final : public UILayerBase,
                                        private EventReceiver {
 public:
  MilitaryTabletHomeUILayer(
    const audited_ptr<DXResources>& dxr,
    KneeboardState*,
    KneeboardView*);
  virtual ~MilitaryTabletHomeUILayer();

  bool IsOpen() const noexcept;
  void SetOpen(bool);

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
  using FloatPoint = Geometry2D::Point<float>;
  using FloatRect = Geometry2D::Rect<float>;

  // Back button, or the card of a tab
  static constexpr int BackTarget = -1;
  static constexpr int NoTarget = -2;

  struct Card {
    FloatRect mRect;
    ITab::RuntimeID mTabID;
    std::string mTitle;
    std::string mGlyph;
    PageIndex mPageCount {};
    bool mIsCurrent {false};
  };
  struct Layout {
    FloatRect mBar;
    FloatRect mBack;
    FloatRect mTitle;
    std::vector<Card> mCards;
  };
  Layout GetLayout(const PixelSize&) const;
  int HitTest(const Layout&, const FloatPoint&) const;
  void Activate(const Layout&, int target);

  audited_ptr<DXResources> mDXResources;
  KneeboardState* mKneeboard {nullptr};
  KneeboardView* mKneeboardView {nullptr};
  MilitaryTabletTheme::Brushes mBrushes;

  bool mOpen {false};
  std::optional<PixelSize> mSize;
  int mHover {NoTarget};
  int mPressed {NoTarget};
  bool mTouching {false};
};

}// namespace OpenKneeboard
