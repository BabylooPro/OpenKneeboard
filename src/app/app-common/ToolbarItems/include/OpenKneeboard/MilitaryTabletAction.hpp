// OpenKneeboard
//
// This program is open source; see the LICENSE file in the root of the OpenKneeboard repository.
#pragma once

#include <OpenKneeboard/ToolbarAction.hpp>

#include <memory>

namespace OpenKneeboard {

class KneeboardState;
class KneeboardView;

/// Rotation and zoom commands of the military tablet theme
class MilitaryTabletAction final : public ToolbarAction, private EventReceiver {
 public:
  enum class Kind {
    Rotate,
    ZoomIn,
    ZoomOut,
    FitToPage,
    FitToWidth,
    ResetZoom,
    RotateDocument,
  };

  MilitaryTabletAction() = delete;
  MilitaryTabletAction(
    KneeboardState*,
    const std::shared_ptr<KneeboardView>&,
    Kind);
  ~MilitaryTabletAction();

  virtual bool IsEnabled() const override;
  [[nodiscard]]
  virtual task<void> Execute() override;

 private:
  KneeboardState* mKneeboardState {nullptr};
  std::weak_ptr<KneeboardView> mKneeboardView;
  Kind mKind;
};

}// namespace OpenKneeboard
