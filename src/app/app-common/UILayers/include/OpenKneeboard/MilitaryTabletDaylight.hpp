// OpenKneeboard
//
// This program is open source; see the LICENSE file in the root of the OpenKneeboard repository.
#pragma once

#include <OpenKneeboard/Events.hpp>

#include <shims/winrt/base.h>

#include <chrono>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>

namespace OpenKneeboard {

class KneeboardState;
struct APIEvent;
struct GameInstance;

/** Light falling on the tablet in the current DCS mission.
 *
 * The sun and the moon are placed for the mission date and time over the
 * aircraft, or over the map origin without one. The mission clouds dim them
 * unless the aircraft is above the cloud layers. White without a mission time.
 */
class MilitaryTabletDaylight final : private EventReceiver {
 public:
  /// Multipliers for the red, green and blue channels
  struct Light {
    float mRed {1.0f};
    float mGreen {1.0f};
    float mBlue {1.0f};

    constexpr bool operator==(const Light&) const noexcept = default;
  };

  explicit MilitaryTabletDaylight(KneeboardState*);
  ~MilitaryTabletDaylight();

  Light GetLight() const noexcept;

  Event<> evChangedEvent;

 private:
  struct GeoPoint {
    double mLatitude {};
    double mLongitude {};
  };
  struct Clouds {
    float mCover {};
    float mTop {};
  };
  struct Mission {
    std::chrono::year_month_day mDate;
    std::optional<Clouds> mClouds;
  };

  static std::optional<Mission> LoadMission(std::filesystem::path);

  void OnAPIEvent(const APIEvent&);
  void OnGameChanged(DWORD processID, const std::shared_ptr<GameInstance>&);
  void Update();

  KneeboardState* mKneeboard {nullptr};
  DWORD mCurrentGamePID {};
  std::filesystem::path mMissionPath;
  std::future<std::optional<Mission>> mMissionLoad;
  std::optional<Mission> mMission;
  std::optional<GeoPoint> mAircraft;
  std::optional<double> mAircraftAltitude;
  std::optional<GeoPoint> mOrigin;
  std::optional<double> mLocalSeconds;
  int64_t mUTCOffsetHours {};
  Light mLight;
};

}// namespace OpenKneeboard
