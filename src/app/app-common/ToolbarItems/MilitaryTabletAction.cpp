// OpenKneeboard
//
// This program is open source; see the LICENSE file in the root of the OpenKneeboard repository.
#include <OpenKneeboard/KneeboardState.hpp>
#include <OpenKneeboard/KneeboardView.hpp>
#include <OpenKneeboard/MilitaryTabletAction.hpp>
#include <OpenKneeboard/MilitaryTabletViewportUILayer.hpp>

namespace OpenKneeboard {

namespace {
using Kind = MilitaryTabletAction::Kind;
using Fit = MilitaryTabletViewportUILayer::Fit;

std::string GlyphFor(Kind kind) {
  switch (kind) {
    case Kind::Rotate:
      return "\uE7AD";// Rotate
    case Kind::ZoomIn:
      return "\uE8A3";// ZoomIn
    case Kind::ZoomOut:
      return "\uE71F";// ZoomOut
    case Kind::FitToPage:
      return "\uE9A6";// FitPage
    case Kind::FitToWidth:
      return "\uE740";// FullScreen
    case Kind::ResetZoom:
      return "\uE7A7";// Undo
    case Kind::RotateDocument:
      return "\uE7AD";// Rotate
  }
  return {};
}

std::string LabelFor(Kind kind) {
  switch (kind) {
    case Kind::Rotate:
      return _("Rotate tablet");
    case Kind::ZoomIn:
      return _("Zoom in");
    case Kind::ZoomOut:
      return _("Zoom out");
    case Kind::FitToPage:
      return _("Fit to page");
    case Kind::FitToWidth:
      return _("Fit to width");
    case Kind::ResetZoom:
      return _("Reset zoom");
    case Kind::RotateDocument:
      return _("Rotate document");
  }
  return {};
}
}// namespace

MilitaryTabletAction::MilitaryTabletAction(
  KneeboardState* kneeboardState,
  const std::shared_ptr<KneeboardView>& kneeboardView,
  Kind kind)
  : ToolbarAction(GlyphFor(kind), LabelFor(kind)),
    mKneeboardState(kneeboardState),
    mKneeboardView(kneeboardView),
    mKind(kind) {
  if (const auto viewport = kneeboardView->GetMilitaryTabletViewport()) {
    AddEventListener(viewport->evStateChangedEvent, this->evStateChangedEvent);
  }
}

MilitaryTabletAction::~MilitaryTabletAction() {
  this->RemoveAllEventListeners();
}

bool MilitaryTabletAction::IsEnabled() const {
  if (mKind == Kind::Rotate) {
    return true;
  }

  const auto view = mKneeboardView.lock();
  const auto viewport = view ? view->GetMilitaryTabletViewport() : nullptr;
  if (!viewport) {
    return false;
  }

  switch (mKind) {
    case Kind::ZoomIn:
      return viewport->CanZoomIn();
    case Kind::ZoomOut:
      return viewport->CanZoomOut();
    case Kind::FitToPage:
      return viewport->IsZoomed() || viewport->GetFit() != Fit::Page;
    case Kind::FitToWidth:
      return viewport->IsZoomed() || viewport->GetFit() != Fit::Width;
    case Kind::ResetZoom:
      return viewport->IsZoomed();
    case Kind::Rotate:
    case Kind::RotateDocument:
      return true;
  }
  return false;
}

task<void> MilitaryTabletAction::Execute() {
  if (mKind == Kind::Rotate) {
    auto settings = mKneeboardState->GetUISettings();
    auto& landscape = settings.mInGameUI.mMilitaryTabletLandscape;
    landscape = !landscape;
    co_await mKneeboardState->SetUISettings(settings);
    co_return;
  }

  const auto view = mKneeboardView.lock();
  const auto viewport = view ? view->GetMilitaryTabletViewport() : nullptr;
  if (!viewport) {
    co_return;
  }

  switch (mKind) {
    case Kind::ZoomIn:
      viewport->ZoomIn();
      break;
    case Kind::ZoomOut:
      viewport->ZoomOut();
      break;
    case Kind::FitToPage:
      viewport->SetFit(Fit::Page);
      break;
    case Kind::FitToWidth:
      viewport->SetFit(Fit::Width);
      break;
    case Kind::ResetZoom:
      viewport->ResetZoom();
      break;
    case Kind::RotateDocument:
      viewport->RotateDocument();
      break;
    case Kind::Rotate:
      break;
  }
  co_return;
}

}// namespace OpenKneeboard
