// OpenKneeboard
//
// This program is open source; see the LICENSE file in the root of the OpenKneeboard repository.
#include <OpenKneeboard/APIEvent.hpp>
#include <OpenKneeboard/DCSExtractedMission.hpp>
#include <OpenKneeboard/DCSWorld.hpp>
#include <OpenKneeboard/KneeboardState.hpp>
#include <OpenKneeboard/Lua.hpp>
#include <OpenKneeboard/MilitaryTabletDaylight.hpp>

#include <shims/nlohmann/json.hpp>

#include <OpenKneeboard/dprint.hpp>
#include <OpenKneeboard/format/filesystem.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <numbers>
#include <span>
#include <string>

namespace OpenKneeboard {

namespace {

constexpr double Rad = std::numbers::pi / 180;
constexpr double Obliquity = Rad * 23.4397;
// Days from the Unix epoch to J2000, 2000-01-01 12:00 UTC
constexpr double UnixDaysAtJ2000 = 10957.5;
// An equinox, if the mission date cannot be read
constexpr std::chrono::year_month_day FallbackDate {
  std::chrono::year {2016},
  std::chrono::March,
  std::chrono::day {20},
};

struct Breakpoint {
  double mX {};
  double mY {};
};

// Horizontal illuminance from the sun, as log10(lux), by solar elevation in
// degrees: about 400 lux at sunset, 3.4 at the end of civil twilight and 0.008
// at the end of nautical twilight
constexpr Breakpoint SunLogLux[] {
  {-18.0, -4.0},
  {-12.0, -2.1},
  {-9.0, -1.0},
  {-6.0, 0.53},
  {-3.0, 1.6},
  {0.0, 2.6},
  {5.0, 3.5},
  {10.0, 4.0},
  {20.0, 4.4},
  {30.0, 4.7},
  {90.0, 5.05},
};
constexpr double FullMoonLux = 0.27;
constexpr double NightSkyLux = 0.0005;
// Eyes adapt: brightness follows the logarithm of the light, and is full from
// a bright day on
constexpr double DaylightLux = 50000.0;
// Color temperature of the light by solar elevation in degrees; below the
// horizon it stands for the blue hour, then for the eye's blue shift at night
constexpr Breakpoint AmbientKelvin[] {
  {-9.0, 12000.0},
  {-6.0, 9000.0},
  {-3.0, 5500.0},
  {0.0, 3200.0},
  {5.0, 4300.0},
  {10.0, 5200.0},
  {20.0, 6500.0},
};
constexpr double NeutralKelvin = 6500.0;
constexpr double TintStrength = 0.5;

// Sky cover, and meters from the base to the top of the cloud layers, from the
// METAR and the layers of each preset in DCS' Config/Effects/clouds.lua
struct CloudPreset {
  float mCover {};
  float mDepth {};
};
constexpr CloudPreset CloudPresets[] {
  {0.35f, 3780},// FEW/SCT
  {0.45f, 5880},// FEW/SCT, SCT
  {0.45f, 5040},// SCT, FEW
  {0.45f, 6041},// SCT, FEW/SCT
  {0.5f, 4620},// SCT, FEW, BKN
  {0.6f, 840},// SCT/BKN
  {0.7f, 5460},// BKN, SCT/BKN, SCT
  {0.6f, 7140},// SCT/BKN, FEW
  {0.7f, 5040},// BKN, SCT
  {0.6f, 5460},// SCT/BKN, FEW
  {0.8f, 5040},// BKN, BKN
  {0.7f, 4200},// BKN, SCT
  {0.8f, 5460},// BKN, BKN
  {0.7f, 3780},// BKN LYR
  {0.8f, 4620},// SCT/BKN, BKN
  {0.8f, 5880},// BKN, BKN
  {0.85f, 8820},// BKN/OVC LYR
  {0.85f, 9240},// BKN/OVC LYR
  {0.95f, 8400},// OVC, BKN/OVC LYR
  {0.85f, 9660},// BKN/OVC, BKN, SCT
  {0.85f, 4200},// BKN/OVC LYR
  {0.75f, 4620},// BKN LYR
  {0.75f, 7980},// BKN LYR, SCT
  {0.85f, 10500},// BKN/OVC, BKN
  {0.95f, 10080},// OVC LYR
  {0.95f, 8400},// OVC, BKN, SCT
  {0.95f, 9240},// OVC, SCT/BKN
};
constexpr CloudPreset RainyCloudPresets[] {
  {1.0f, 7560},// RA OVC
  {0.95f, 8820},// RA BKN/OVC
  {1.0f, 8820},// RA OVC LYR
  {0.8f, 5460},// rain, SCT/BKN
  {0.9f, 8820},// rain, BKN/OVC
  {1.0f, 8400},// rain, OVC
};

std::optional<CloudPreset> GetCloudPreset(std::string_view name) {
  const auto index = [name](
                       std::string_view prefix) -> std::optional<std::size_t> {
    if (!name.starts_with(prefix)) {
      return std::nullopt;
    }
    const auto digits = name.substr(prefix.size());
    std::size_t number {};
    const auto [end, error]
      = std::from_chars(digits.data(), digits.data() + digits.size(), number);
    if (error != std::errc {} || end != digits.data() + digits.size()) {
      return std::nullopt;
    }
    return number - 1;
  };
  if (const auto i = index("RainyPreset");
      i && *i < std::size(RainyCloudPresets)) {
    return RainyCloudPresets[*i];
  }
  if (const auto i = index("Preset"); i && *i < std::size(CloudPresets)) {
    return CloudPresets[*i];
  }
  return std::nullopt;
}

double Interpolate(std::span<const Breakpoint> table, double x) {
  if (x <= table.front().mX) {
    return table.front().mY;
  }
  for (std::size_t i = 1; i < table.size(); ++i) {
    const auto& [x0, y0] = table[i - 1];
    const auto& [x1, y1] = table[i];
    if (x <= x1) {
      return y0 + ((y1 - y0) * (x - x0) / (x1 - x0));
    }
  }
  return table.back().mY;
}

// Low-precision positions from suncalc (Vladimir Agafonkin, BSD-2-Clause):
// well under a degree, far below what shows on the tablet
struct SkyPosition {
  double mRightAscension {};
  double mDeclination {};
  double mDistance {};
};

double RightAscension(double longitude, double latitude) {
  return std::atan2(
    (std::sin(longitude) * std::cos(Obliquity))
      - (std::tan(latitude) * std::sin(Obliquity)),
    std::cos(longitude));
}

double Declination(double longitude, double latitude) {
  return std::asin(
    (std::sin(latitude) * std::cos(Obliquity))
    + (std::cos(latitude) * std::sin(Obliquity) * std::sin(longitude)));
}

SkyPosition SunPosition(double days) {
  const auto anomaly = Rad * (357.5291 + (0.98560028 * days));
  const auto center = Rad
    * ((1.9148 * std::sin(anomaly)) + (0.02 * std::sin(2 * anomaly))
       + (0.0003 * std::sin(3 * anomaly)));
  const auto longitude
    = anomaly + center + (Rad * 102.9372) + std::numbers::pi;
  return {
    RightAscension(longitude, 0),
    Declination(longitude, 0),
    149598000,
  };
}

SkyPosition MoonPosition(double days) {
  const auto meanLongitude = Rad * (218.316 + (13.176396 * days));
  const auto anomaly = Rad * (134.963 + (13.064993 * days));
  const auto meanDistance = Rad * (93.272 + (13.229350 * days));
  const auto longitude = meanLongitude + (Rad * 6.289 * std::sin(anomaly));
  const auto latitude = Rad * 5.128 * std::sin(meanDistance);
  return {
    RightAscension(longitude, latitude),
    Declination(longitude, latitude),
    385001 - (20905 * std::cos(anomaly)),
  };
}

// Degrees above the horizon
double Elevation(
  const SkyPosition& body,
  double days,
  double latitude,
  double longitude) {
  const auto siderealTime
    = (Rad * (280.16 + (360.9856235 * days))) + (Rad * longitude);
  const auto hourAngle = siderealTime - body.mRightAscension;
  const auto phi = Rad * latitude;
  return std::asin(
           (std::sin(phi) * std::sin(body.mDeclination))
           + (std::cos(phi) * std::cos(body.mDeclination) * std::cos(hourAngle)))
    / Rad;
}

double MoonLitFraction(const SkyPosition& sun, const SkyPosition& moon) {
  const auto elongation = std::acos(std::clamp(
    (std::sin(sun.mDeclination) * std::sin(moon.mDeclination))
      + (std::cos(sun.mDeclination) * std::cos(moon.mDeclination)
         * std::cos(sun.mRightAscension - moon.mRightAscension)),
    -1.0,
    1.0));
  const auto phaseAngle = std::atan2(
    sun.mDistance * std::sin(elongation),
    moon.mDistance - (sun.mDistance * std::cos(elongation)));
  return (1 + std::cos(phaseAngle)) / 2;
}

// Tanner Helland's fit of black-body colors, in 0..255
std::array<double, 3> KelvinToRGB(double kelvin) {
  const auto t = kelvin / 100;
  const auto red
    = (t <= 66) ? 255.0 : 329.698727446 * std::pow(t - 60, -0.1332047592);
  const auto green = (t <= 66)
    ? (99.4708025861 * std::log(t)) - 161.1195681661
    : 288.1221695283 * std::pow(t - 60, -0.0755148492);
  auto blue = 255.0;
  if (t <= 19) {
    blue = 0;
  } else if (t < 66) {
    blue = (138.5177312231 * std::log(t - 10)) - 305.0447927307;
  }
  return {
    std::clamp(red, 0.0, 255.0),
    std::clamp(green, 0.0, 255.0),
    std::clamp(blue, 0.0, 255.0),
  };
}

// Relative to neutral white, at the same luminance
std::array<double, 3> Tint(double kelvin) {
  const auto color = KelvinToRGB(kelvin);
  const auto white = KelvinToRGB(NeutralKelvin);
  std::array<double, 3> ret {};
  for (std::size_t i = 0; i < ret.size(); ++i) {
    ret[i] = color[i] / white[i];
  }
  const auto luminance
    = (0.2126 * ret[0]) + (0.7152 * ret[1]) + (0.0722 * ret[2]);
  for (auto& channel: ret) {
    channel = 1 + (TintStrength * ((channel / luminance) - 1));
  }
  return ret;
}

MilitaryTabletDaylight::Light GetSkyLight(
  double days,
  double latitude,
  double longitude,
  double cloudCover) {
  const auto sun = SunPosition(days);
  const auto moon = MoonPosition(days);
  const auto sunElevation = Elevation(sun, days, latitude, longitude);
  const auto moonElevation = Elevation(moon, days, latitude, longitude);

  const auto sunLux = (sunElevation < SunLogLux[0].mX)
    ? 0.0
    : std::pow(10.0, Interpolate(SunLogLux, sunElevation));
  const auto moonLux = FullMoonLux * std::pow(MoonLitFraction(sun, moon), 3)
    * std::max(std::sin(Rad * moonElevation), 0.0);
  // Overcast daylight is about 20% of a clear sky's, under rain about 5%
  const auto transmission = 1 - (0.95 * cloudCover * cloudCover);
  const auto lux = ((sunLux + moonLux) * transmission) + NightSkyLux;

  const auto low = std::log10(NightSkyLux);
  const auto level = std::clamp(
    (std::log10(lux) - low) / (std::log10(DaylightLux) - low), 0.0, 1.0);
  const auto brightness = level * level;

  // Clouds scatter the light to a neutral gray
  const auto tint = Tint(std::lerp(
    Interpolate(AmbientKelvin, sunElevation), NeutralKelvin, cloudCover));
  const auto channel = [brightness](double value) {
    return static_cast<float>(std::min(brightness * value, 1.0));
  };
  return {channel(tint[0]), channel(tint[1]), channel(tint[2])};
}

}// namespace

MilitaryTabletDaylight::MilitaryTabletDaylight(KneeboardState* kneeboard)
  : mKneeboard(kneeboard) {
  AddEventListener(
    kneeboard->evAPIEvent,
    std::bind_front(&MilitaryTabletDaylight::OnAPIEvent, this));
  AddEventListener(
    kneeboard->evGameChangedEvent,
    std::bind_front(&MilitaryTabletDaylight::OnGameChanged, this));
}

MilitaryTabletDaylight::~MilitaryTabletDaylight() {
  this->RemoveAllEventListeners();
}

MilitaryTabletDaylight::Light MilitaryTabletDaylight::GetLight() const noexcept {
  return mLight;
}

// Runs on a background thread: big missions take a while to extract and parse
std::optional<MilitaryTabletDaylight::Mission>
MilitaryTabletDaylight::LoadMission(std::filesystem::path zip) {
  try {
    const auto extracted = DCSExtractedMission::Get(zip);
    const auto file = extracted->GetExtractedPath() / "mission";
    if (!std::filesystem::exists(file)) {
      return std::nullopt;
    }
    LuaState lua;
    lua.DoFile(file);
    const auto mission = lua.GetGlobal("mission");

    const auto date = mission["date"];
    Mission ret {
      .mDate = std::chrono::year_month_day {
        std::chrono::year {date["Year"].Get<int>()},
        std::chrono::month {date["Month"].Get<unsigned int>()},
        std::chrono::day {date["Day"].Get<unsigned int>()},
      },
    };
    if (!ret.mDate.ok()) {
      ret.mDate = FallbackDate;
    }

    try {
      const auto clouds = mission["weather"]["clouds"];
      CloudPreset layers {
        static_cast<float>(clouds["density"].Get<double>() / 10),
        static_cast<float>(clouds["thickness"].Get<double>()),
      };
      if (clouds.contains("preset")) {
        layers = GetCloudPreset(clouds["preset"].Get<std::string>())
                   .value_or(layers);
      }
      if (clouds.contains("iprecptns") && clouds["iprecptns"].Get<int>() > 0) {
        layers.mCover = 1.0f;
      }
      ret.mClouds = Clouds {
        layers.mCover,
        static_cast<float>(clouds["base"].Get<double>()) + layers.mDepth,
      };
    } catch (const LuaError& e) {
      dprint("Military tablet: no clouds in {}: {}", zip, e.what());
    }
    return ret;
  } catch (const std::exception& e) {
    dprint.Warning("Military tablet: failed to read {}: {}", zip, e.what());
    return std::nullopt;
  }
}

void MilitaryTabletDaylight::OnAPIEvent(const APIEvent& ev) {
  // The default theme does not read the mission
  if (!mKneeboard->GetUISettings().mInGameUI.mMilitaryTabletThemeEnabled) {
    return;
  }

  if (ev.name == DCSWorld::EVT_MISSION_TIME) {
    const auto times = ev.TryParsedValue<DCSWorld::MissionTimeEvent>();
    if (!times) {
      return;
    }
    mLocalSeconds = times->currentTime;
    mUTCOffsetHours = times->utcOffset;
    this->Update();
    return;
  }

  if (ev.name == DCSWorld::EVT_MISSION) {
    // Same path as the DCS tabs, so the extracted mission is shared
    std::error_code ec;
    const auto path = std::filesystem::canonical(ev.value, ec);
    if (ec || path == mMissionPath) {
      return;
    }
    mMissionPath = path;
    mMission.reset();
    mMissionLoad = std::async(
      std::launch::async, &MilitaryTabletDaylight::LoadMission, path);
    return;
  }

  try {
    if (ev.name == DCSWorld::EVT_SELF_DATA) {
      const auto position = nlohmann::json::parse(ev.value).at("LatLongAlt");
      mAircraft = GeoPoint {
        position.at("Lat").get<double>(),
        position.at("Long").get<double>(),
      };
      mAircraftAltitude = position.at("Alt").get<double>();
    } else if (ev.name == DCSWorld::EVT_ORIGIN) {
      const auto origin = nlohmann::json::parse(ev.value);
      mOrigin = GeoPoint {
        origin.at("latitude").get<double>(),
        origin.at("longitude").get<double>(),
      };
    }
  } catch (const nlohmann::json::exception&) {
  }
}

void MilitaryTabletDaylight::OnGameChanged(
  DWORD processID,
  const std::shared_ptr<GameInstance>&) {
  if (processID == mCurrentGamePID) {
    return;
  }
  mCurrentGamePID = processID;
  mLocalSeconds.reset();
  mAircraft.reset();
  mAircraftAltitude.reset();
  mOrigin.reset();
  if (mLight != Light {}) {
    mLight = {};
    evChangedEvent.Emit();
  }
}

void MilitaryTabletDaylight::Update() {
  if (
    mMissionLoad.valid()
    && mMissionLoad.wait_for(std::chrono::seconds::zero())
      == std::future_status::ready) {
    mMission = mMissionLoad.get();
  }

  const auto place = mAircraft ? mAircraft : mOrigin;
  if (!(mLocalSeconds && place)) {
    return;
  }

  const auto date = mMission ? mMission->mDate : FallbackDate;
  const auto utcSeconds = *mLocalSeconds - (mUTCOffsetHours * 3600.0);
  const auto days = std::chrono::sys_days {date}.time_since_epoch().count()
    + (utcSeconds / 86400) - UnixDaysAtJ2000;

  double cloudCover = 0;
  if (mMission && mMission->mClouds) {
    const bool aboveClouds = mAircraft && mAircraftAltitude
      && *mAircraftAltitude > mMission->mClouds->mTop;
    cloudCover = aboveClouds ? 0 : mMission->mClouds->mCover;
  }

  const auto light
    = GetSkyLight(days, place->mLatitude, place->mLongitude, cloudCover);
  const auto changed = [](float a, float b) { return std::abs(a - b) > 0.002f; };
  if (
    changed(light.mRed, mLight.mRed) || changed(light.mGreen, mLight.mGreen)
    || changed(light.mBlue, mLight.mBlue)) {
    mLight = light;
    evChangedEvent.Emit();
  }
}

}// namespace OpenKneeboard
