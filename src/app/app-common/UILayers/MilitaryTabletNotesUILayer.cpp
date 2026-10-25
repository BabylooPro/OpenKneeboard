// OpenKneeboard
//
// This program is open source; see the LICENSE file in the root of the OpenKneeboard repository.
#include <OpenKneeboard/CursorEvent.hpp>
#include <OpenKneeboard/Filesystem.hpp>
#include <OpenKneeboard/KneeboardView.hpp>
#include <OpenKneeboard/MilitaryTabletNotesUILayer.hpp>

#include <OpenKneeboard/config.hpp>
#include <OpenKneeboard/dprint.hpp>
#include <OpenKneeboard/format/filesystem.hpp>
#include <OpenKneeboard/json.hpp>
#include <OpenKneeboard/tracing.hpp>
#include <OpenKneeboard/utf8.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <utility>

namespace OpenKneeboard {

namespace {
using MilitaryTabletTheme::CreateTextFormat;

// Notes are drawn in the units of this page, whatever the screen size
constexpr Geometry2D::Size<float> PageSize {1000.0f, 1414.0f};
constexpr float LineSpacing = 50.0f;
constexpr float PenWidth = 3.0f;
constexpr float EraserRadius = 18.0f;
constexpr float MinPointDistance = 1.5f;
constexpr auto NotesFileName = "MilitaryTabletNotes.json";

bool Contains(const Geometry2D::Rect<float>& rect, float x, float y) {
  return x >= rect.Left() && x <= rect.Right() && y >= rect.Top()
    && y <= rect.Bottom();
}

float DistanceToSegment(
  const D2D1_POINT_2F& p,
  const D2D1_POINT_2F& a,
  const D2D1_POINT_2F& b) {
  const auto dx = b.x - a.x;
  const auto dy = b.y - a.y;
  const auto lengthSquared = (dx * dx) + (dy * dy);
  const auto t = (lengthSquared > 0)
    ? std::clamp(
        (((p.x - a.x) * dx) + ((p.y - a.y) * dy)) / lengthSquared, 0.0f, 1.0f)
    : 0.0f;
  return std::hypot(p.x - (a.x + (t * dx)), p.y - (a.y + (t * dy)));
}

winrt::com_ptr<ID2D1PathGeometry> CreateStrokeGeometry(
  ID2D1Factory* factory,
  const std::vector<D2D1_POINT_2F>& points) {
  winrt::com_ptr<ID2D1PathGeometry> geometry;
  winrt::check_hresult(factory->CreatePathGeometry(geometry.put()));
  winrt::com_ptr<ID2D1GeometrySink> sink;
  winrt::check_hresult(geometry->Open(sink.put()));
  sink->BeginFigure(points.front(), D2D1_FIGURE_BEGIN_HOLLOW);
  if (points.size() == 1) {
    // A single click: the round caps turn this into a dot
    sink->AddLine({points.front().x + 0.01f, points.front().y});
  } else {
    sink->AddLines(points.data() + 1, static_cast<UINT32>(points.size() - 1));
  }
  sink->EndFigure(D2D1_FIGURE_END_OPEN);
  winrt::check_hresult(sink->Close());
  return geometry;
}
}// namespace

struct MilitaryTabletNotesUILayer::Store {
  struct Stroke {
    std::vector<D2D1_POINT_2F> mPoints;
    winrt::com_ptr<ID2D1PathGeometry> mGeometry;
  };
  using Page = std::vector<Stroke>;

  std::vector<Page> mPages {Page {}};
  Event<> evChangedEvent;

  // One notepad for every view
  static std::shared_ptr<Store> Get() {
    static std::weak_ptr<Store> sStore;
    if (auto store = sStore.lock()) {
      return store;
    }
    auto store = std::make_shared<Store>();
    store->Load();
    sStore = store;
    return store;
  }

  static std::filesystem::path GetPath() {
    return Filesystem::GetLocalAppDataDirectory() / NotesFileName;
  }

  void Load() {
    const auto path = GetPath();
    if (!std::filesystem::exists(path)) {
      return;
    }
    try {
      const auto json
        = nlohmann::json::parse(std::ifstream {path, std::ios::binary});
      std::vector<Page> pages;
      for (const auto& jsonPage: json.at("Pages")) {
        Page page;
        for (const auto& jsonStroke: jsonPage) {
          const auto values = jsonStroke.get<std::vector<float>>();
          Stroke stroke;
          for (std::size_t i = 0; i + 1 < values.size(); i += 2) {
            stroke.mPoints.push_back({
              std::clamp(values.at(i), 0.0f, PageSize.mWidth),
              std::clamp(values.at(i + 1), 0.0f, PageSize.mHeight),
            });
          }
          if (!stroke.mPoints.empty()) {
            page.push_back(std::move(stroke));
          }
        }
        pages.push_back(std::move(page));
      }
      if (!pages.empty()) {
        mPages = std::move(pages);
      }
    } catch (const std::exception& e) {
      dprint.Warning(
        "Failed to load military tablet notes from {}: {}", path, e.what());
      // Keep the unreadable file rather than overwrite it with the next change
      auto unreadable = path;
      unreadable += ".unreadable";
      std::error_code ec;
      std::filesystem::rename(path, unreadable, ec);
    }
  }

  void Save() const {
    // Blank pages after the last note are not kept
    auto count = mPages.size();
    while (count > 1 && mPages.at(count - 1).empty()) {
      --count;
    }
    auto pages = nlohmann::json::array();
    for (std::size_t i = 0; i < count; ++i) {
      auto strokes = nlohmann::json::array();
      for (const auto& stroke: mPages.at(i)) {
        std::vector<float> values;
        values.reserve(stroke.mPoints.size() * 2);
        for (const auto& point: stroke.mPoints) {
          values.push_back(std::round(point.x * 10) / 10);
          values.push_back(std::round(point.y * 10) / 10);
        }
        strokes.push_back(std::move(values));
      }
      pages.push_back(std::move(strokes));
    }

    // Written aside then renamed, so that a crash cannot truncate the notes
    const auto path = GetPath();
    auto temporary = path;
    temporary += ".tmp";
    {
      std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
      file << nlohmann::json {{"Version", 1}, {"Pages", std::move(pages)}}
                .dump();
      if (!file) {
        dprint.Warning("Failed to write military tablet notes to {}", temporary);
        return;
      }
    }
    std::error_code ec;
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
      dprint.Warning(
        "Failed to save military tablet notes to {}: {}", path, ec.message());
    }
  }
};

MilitaryTabletNotesUILayer::MilitaryTabletNotesUILayer(
  const audited_ptr<DXResources>& dxr,
  KneeboardView* kneeboardView)
  : mDXResources(dxr),
    mBrushes(
      MilitaryTabletTheme::Brushes::Create(dxr->mD2DDeviceContext.get())),
    mStore(Store::Get()) {
  const auto strokeProperties = D2D1::StrokeStyleProperties(
    D2D1_CAP_STYLE_ROUND,
    D2D1_CAP_STYLE_ROUND,
    D2D1_CAP_STYLE_ROUND,
    D2D1_LINE_JOIN_ROUND);
  winrt::check_hresult(
    static_cast<ID2D1Factory*>(dxr->mD2DFactory.get())
      ->CreateStrokeStyle(&strokeProperties, nullptr, 0, mStrokeStyle.put()));

  AddEventListener(
    kneeboardView->evCurrentTabChangedEvent, [this](auto) {
      this->SetOpen(false);
    });
  AddEventListener(mStore->evChangedEvent, [this]() {
    if (mOpen) {
      evNeedsRepaintEvent.Emit();
    }
  });
}

MilitaryTabletNotesUILayer::~MilitaryTabletNotesUILayer() {
  this->RemoveAllEventListeners();
}

bool MilitaryTabletNotesUILayer::IsOpen() const noexcept {
  return mOpen;
}

void MilitaryTabletNotesUILayer::SetOpen(bool open) {
  if (open == mOpen) {
    return;
  }
  if (!open) {
    this->EndStroke();
  }
  mOpen = open;
  mClearArmed = false;
  mHover = Button::None;
  mPressed = Button::None;
  mTouching = false;
  evNeedsRepaintEvent.Emit();
}

bool MilitaryTabletNotesUILayer::CanGoToPreviousPage() const {
  return mPageIndex > 0;
}

bool MilitaryTabletNotesUILayer::CanGoToNextPage() const {
  const auto& pages = mStore->mPages;
  return (mPageIndex + 1 < pages.size()) || !pages.at(mPageIndex).empty();
}

void MilitaryTabletNotesUILayer::PreviousPage() {
  if (!this->CanGoToPreviousPage()) {
    return;
  }
  this->EndStroke();
  --mPageIndex;
  mClearArmed = false;
  evNeedsRepaintEvent.Emit();
}

void MilitaryTabletNotesUILayer::NextPage() {
  if (!this->CanGoToNextPage()) {
    return;
  }
  this->EndStroke();
  if (mPageIndex + 1 == mStore->mPages.size()) {
    mStore->mPages.emplace_back();
  }
  ++mPageIndex;
  mClearArmed = false;
  evNeedsRepaintEvent.Emit();
}

MilitaryTabletNotesUILayer::Layout MilitaryTabletNotesUILayer::GetLayout(
  const PixelSize& size) const {
  const auto width = size.Width<float>();
  const auto height = size.Height<float>();
  // Same bar height as the header when both bars are shown
  const auto barHeight = std::round(
    height * HeaderPercent
    / static_cast<float>(100 + HeaderPercent + FooterPercent));
  const auto buttonHeight = std::round(barHeight * 0.75f);
  const auto margin = std::round((barHeight - buttonHeight) / 2);

  Layout ret {.mBar = {{0, 0}, {width, barHeight}}};
  auto left = 2 * margin;
  for (const auto button:
       {Button::Back, Button::Pen, Button::Eraser, Button::Undo, Button::Clear}) {
    ret.mButtons.push_back(
      {button, {{left, margin}, {buttonHeight, buttonHeight}}});
    left += buttonHeight + margin;
    if (button == Button::Back) {
      left += 2 * margin;
    }
  }
  auto right = width - (2 * margin);
  for (const auto button: {Button::NextPage, Button::PreviousPage}) {
    right -= buttonHeight;
    ret.mButtons.push_back(
      {button, {{right, margin}, {buttonHeight, buttonHeight}}});
    right -= margin;
  }
  ret.mTitle = {{left, 0}, {std::max(0.0f, right - left), barHeight}};

  const auto padding = std::round(barHeight * 0.3f);
  const FloatRect area {
    {padding, barHeight + padding},
    {width - (2 * padding), height - barHeight - (2 * padding)},
  };
  const auto scale = std::min(
    area.Width() / PageSize.mWidth, area.Height() / PageSize.mHeight);
  const auto pageWidth = PageSize.mWidth * scale;
  const auto pageHeight = PageSize.mHeight * scale;
  ret.mPage = {
    {
      area.Left() + ((area.Width() - pageWidth) / 2),
      area.Top() + ((area.Height() - pageHeight) / 2),
    },
    {pageWidth, pageHeight},
  };
  return ret;
}

MilitaryTabletNotesUILayer::Button MilitaryTabletNotesUILayer::HitTest(
  const Layout& layout,
  const FloatPoint& point) const {
  for (const auto& [button, rect]: layout.mButtons) {
    if (Contains(rect, point.mX, point.mY)) {
      return button;
    }
  }
  return Button::None;
}

bool MilitaryTabletNotesUILayer::IsButtonEnabled(Button button) const {
  const auto& page = mStore->mPages.at(mPageIndex);
  switch (button) {
    case Button::Undo:
    case Button::Clear:
      return !page.empty();
    case Button::PreviousPage:
      return this->CanGoToPreviousPage();
    case Button::NextPage:
      return this->CanGoToNextPage();
    default:
      return true;
  }
}

void MilitaryTabletNotesUILayer::OnButtonClicked(Button button) {
  auto& page = mStore->mPages.at(mPageIndex);
  // Clearing a page cannot be undone, so it takes a second click
  if (button == Button::Clear && !mClearArmed) {
    mClearArmed = true;
    return;
  }
  mClearArmed = false;

  switch (button) {
    case Button::Back:
      this->SetOpen(false);
      return;
    case Button::Pen:
      mTool = Tool::Pen;
      return;
    case Button::Eraser:
      mTool = Tool::Eraser;
      return;
    case Button::Undo:
      page.pop_back();
      break;
    case Button::Clear:
      page.clear();
      break;
    case Button::PreviousPage:
      this->PreviousPage();
      return;
    case Button::NextPage:
      this->NextPage();
      return;
    case Button::None:
      return;
  }
  mStore->Save();
  mStore->evChangedEvent.Emit();
}

MilitaryTabletNotesUILayer::FloatPoint MilitaryTabletNotesUILayer::ToPage(
  const Layout& layout,
  const FloatPoint& point) const {
  const auto& page = layout.mPage;
  return {
    std::clamp(
      (point.mX - page.Left()) * PageSize.mWidth / page.Width(),
      0.0f,
      PageSize.mWidth),
    std::clamp(
      (point.mY - page.Top()) * PageSize.mHeight / page.Height(),
      0.0f,
      PageSize.mHeight),
  };
}

void MilitaryTabletNotesUILayer::Erase(const FloatPoint& point) {
  const D2D1_POINT_2F at {point.mX, point.mY};
  auto& page = mStore->mPages.at(mPageIndex);
  const auto erased = std::erase_if(page, [&at](const Store::Stroke& stroke) {
    const auto& points = stroke.mPoints;
    if (points.size() == 1) {
      return DistanceToSegment(at, points.front(), points.front())
        <= EraserRadius;
    }
    for (std::size_t i = 1; i < points.size(); ++i) {
      if (DistanceToSegment(at, points.at(i - 1), points.at(i)) <= EraserRadius) {
        return true;
      }
    }
    return false;
  });
  if (erased) {
    mErased = true;
    mStore->evChangedEvent.Emit();
  }
}

void MilitaryTabletNotesUILayer::EndStroke() {
  if (mDrawing && !mStroke.empty()) {
    mStore->mPages.at(mPageIndex).push_back({std::move(mStroke), nullptr});
    mStroke.clear();
    mStore->Save();
    mStore->evChangedEvent.Emit();
  } else if (mErasing && mErased) {
    mStore->Save();
  }
  mDrawing = false;
  mErasing = false;
  mErased = false;
}

void MilitaryTabletNotesUILayer::PostCursorEvent(
  const IUILayer::NextList& next,
  const Context& context,
  KneeboardViewID kneeboardViewID,
  const CursorEvent& cursorEvent) {
  const auto [first, rest] = Split(next);
  // The app window shows the tab itself
  if (!mOpen || cursorEvent.mSource == CursorSource::WindowPointer) {
    first->PostCursorEvent(rest, context, kneeboardViewID, cursorEvent);
    return;
  }
  first->PostCursorEvent(rest, context, kneeboardViewID, {});

  if (cursorEvent.mTouchState == CursorTouchState::NotNearSurface || !mSize) {
    this->EndStroke();
    mHover = Button::None;
    mPressed = Button::None;
    mTouching = false;
    return;
  }

  mPageIndex = std::min(mPageIndex, mStore->mPages.size() - 1);
  const auto layout = this->GetLayout(*mSize);
  const FloatPoint point {
    cursorEvent.mX * mSize->Width<float>(),
    cursorEvent.mY * mSize->Height<float>(),
  };
  const auto button = this->HitTest(layout, point);
  mHover = button;

  if (cursorEvent.mTouchState == CursorTouchState::TouchingSurface) {
    if (!mTouching) {
      mTouching = true;
      mPressed = button;
      if (
        button == Button::None
        && Contains(layout.mPage, point.mX, point.mY)) {
        mClearArmed = false;
        // As for notes on pages: the tip draws, any other pen button erases
        mErasing = (mTool == Tool::Eraser) || (cursorEvent.mButtons & ~1u);
        mDrawing = !mErasing;
        if (mDrawing) {
          mStroke.clear();
          mStroke.push_back(this->ToPage(layout, point));
        } else {
          this->Erase(this->ToPage(layout, point));
        }
      }
    } else if (mDrawing) {
      const D2D1_POINT_2F at = this->ToPage(layout, point);
      const auto& last = mStroke.back();
      if (std::hypot(at.x - last.x, at.y - last.y) >= MinPointDistance) {
        mStroke.push_back(at);
      }
    } else if (mErasing) {
      this->Erase(this->ToPage(layout, point));
    }
    evNeedsRepaintEvent.Emit();
    return;
  }

  if (!std::exchange(mTouching, false)) {
    return;
  }
  this->EndStroke();
  const auto pressed = std::exchange(mPressed, Button::None);
  if (
    pressed != Button::None && pressed == button
    && this->IsButtonEnabled(button)) {
    this->OnButtonClicked(button);
  }
  evNeedsRepaintEvent.Emit();
}

IUILayer::Metrics MilitaryTabletNotesUILayer::GetMetrics(
  const IUILayer::NextList& next,
  const Context& context) const {
  const auto nextMetrics = next.front()->GetMetrics(next.subspan(1), context);
  return Metrics {
    nextMetrics.mPreferredSize,
    {{}, nextMetrics.mPreferredSize.mPixelSize},
    nextMetrics.mContentArea,
  };
}

task<void> MilitaryTabletNotesUILayer::Render(
  const RenderContext& rc,
  const IUILayer::NextList& next,
  const Context& context,
  const PixelRect& rect) {
  OPENKNEEBOARD_TraceLoggingScope("MilitaryTabletNotesUILayer::Render()");
  const auto [first, rest] = Split(next);
  if (!mOpen) {
    co_await first->Render(rc, rest, context, rect);
    co_return;
  }

  mSize = rect.mSize;
  auto& pages = mStore->mPages;
  mPageIndex = std::min(mPageIndex, pages.size() - 1);
  auto& page = pages.at(mPageIndex);
  const auto layout = this->GetLayout(rect.mSize);
  const auto origin = rect.mOffset.StaticCast<float>();
  const auto at = [origin](const FloatRect& area) -> D2D1_RECT_F {
    return FloatRect {origin + area.mOffset, area.mSize};
  };

  auto d2d = rc.d2d();
  d2d->SetTransform(D2D1::Matrix3x2F::Identity());
  d2d->FillRectangle(rect, mBrushes.mScreen.get());

  const auto bar = at(layout.mBar);
  d2d->FillRectangle(bar, mBrushes.mBar.get());
  d2d->DrawLine(
    {bar.left, bar.bottom - 0.5f},
    {bar.right, bar.bottom - 0.5f},
    mBrushes.mEdge.get());

  FLOAT dpix {}, dpiy {};
  d2d->GetDpi(&dpix, &dpiy);
  const auto dwf = mDXResources->mDWriteFactory.get();
  const auto buttonHeight = layout.mButtons.front().second.Height();
  const auto glyphFormat = CreateTextFormat(
    dwf,
    GlyphFont,
    DWRITE_FONT_WEIGHT_EXTRA_BOLD,
    buttonHeight * 0.66f,
    dpiy,
    DWRITE_TEXT_ALIGNMENT_CENTER);

  for (const auto& [button, area]: layout.mButtons) {
    std::wstring_view glyph;
    switch (button) {
      case Button::Back:
        glyph = L"\uE72B";// Back
        break;
      case Button::Pen:
        glyph = L"\uE70F";// Edit
        break;
      case Button::Eraser:
        glyph = L"\uE75C";// EraseTool
        break;
      case Button::Undo:
        glyph = L"\uE7A7";// Undo
        break;
      case Button::Clear:
        glyph = L"\uE74D";// Delete
        break;
      case Button::PreviousPage:
        glyph = L"\uE760";// PageLeft
        break;
      case Button::NextPage:
        glyph = (mPageIndex + 1 < pages.size()) ? L"\uE761"// PageRight
                                                 : L"\uE710";// Add
        break;
      case Button::None:
        break;
    }
    const auto buttonRect = at(area);
    if (button == Button::Clear && mClearArmed) {
      const auto radius = buttonHeight / 5;
      const auto rounded = D2D1::RoundedRect(buttonRect, radius, radius);
      d2d->FillRoundedRectangle(rounded, mBrushes.mDanger.get());
      d2d->DrawRoundedRectangle(
        rounded, mBrushes.mAccent.get(), buttonHeight / 30);
      d2d->DrawTextW(
        glyph.data(),
        static_cast<UINT32>(glyph.size()),
        glyphFormat.get(),
        buttonRect,
        mBrushes.mForeground.get());
      continue;
    }
    MilitaryTabletTheme::DrawButton(
      d2d,
      mBrushes,
      buttonRect,
      glyph,
      glyphFormat.get(),
      buttonHeight / 15,
      this->IsButtonEnabled(button),
      mHover == button && !(mDrawing || mErasing),
      mTouching && mPressed == button,
      (button == Button::Pen && mTool == Tool::Pen)
        || (button == Button::Eraser && mTool == Tool::Eraser));
  }

  const auto title = mClearArmed
    ? std::wstring {_(L"Click the bin again to clear this page")}
    : std::format(_(L"Notes   {} / {}"), mPageIndex + 1, pages.size());
  const auto titleFormat = CreateTextFormat(
    dwf,
    VariableWidthUIFont,
    DWRITE_FONT_WEIGHT_BOLD,
    layout.mBar.Height() * (mClearArmed ? 0.34f : 0.42f),
    dpiy,
    DWRITE_TEXT_ALIGNMENT_CENTER);
  d2d->DrawTextW(
    title.c_str(),
    static_cast<UINT32>(title.size()),
    titleFormat.get(),
    at(layout.mTitle),
    mClearArmed ? mBrushes.mAccent.get() : mBrushes.mForeground.get());

  const auto paper = at(layout.mPage);
  const auto scale = layout.mPage.Width() / PageSize.mWidth;
  d2d->FillRectangle(paper, mBrushes.mPaper.get());
  for (auto y = LineSpacing; y < PageSize.mHeight; y += LineSpacing) {
    const auto lineY = paper.top + (y * scale);
    d2d->DrawLine(
      {paper.left, lineY}, {paper.right, lineY}, mBrushes.mPaperLine.get());
  }

  const auto factory = static_cast<ID2D1Factory*>(mDXResources->mD2DFactory.get());
  d2d->PushAxisAlignedClip(paper, D2D1_ANTIALIAS_MODE_ALIASED);
  d2d->SetTransform(
    D2D1::Matrix3x2F::Scale(scale, scale)
    * D2D1::Matrix3x2F::Translation(paper.left, paper.top));
  for (auto& stroke: page) {
    if (!stroke.mGeometry) {
      stroke.mGeometry = CreateStrokeGeometry(factory, stroke.mPoints);
    }
    d2d->DrawGeometry(
      stroke.mGeometry.get(),
      mBrushes.mInk.get(),
      PenWidth,
      mStrokeStyle.get());
  }
  if (mDrawing && !mStroke.empty()) {
    const auto geometry = CreateStrokeGeometry(factory, mStroke);
    d2d->DrawGeometry(
      geometry.get(), mBrushes.mInk.get(), PenWidth, mStrokeStyle.get());
  }
  d2d->SetTransform(D2D1::Matrix3x2F::Identity());
  d2d->PopAxisAlignedClip();
}

}// namespace OpenKneeboard
