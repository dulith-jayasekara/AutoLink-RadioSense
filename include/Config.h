#pragma once

#include "Library_Headers.h"

//#define NODE_NO "001"
#define NODE_NO "002"
//define NODE_NO "003"
//define NODE_NO "004"

#define SERIAL_BAUD_RATE 115200
#define START_DELAY 5000
#define CAPTIVE_PORTAL_PORT 80
#define CAPTIVE_PORTAL_HTTPS_PORT 443
#define DNS_PORT 53

// GPS limits and freshness parameters are provisional; validate with field data.
constexpr double GPS_MIN_SPEED_MPS = 0.0;
constexpr double GPS_MAX_SPEED_KMPH = 100.0;
constexpr double GPS_MAX_SPEED_MPS = GPS_MAX_SPEED_KMPH / 3.6;
constexpr double GPS_MAX_ACCEPTABLE_ACCURACY_M = 100.0;
constexpr uint32_t GPS_FUTURE_TIMESTAMP_TOLERANCE_MS = 5000;
constexpr uint32_t GPS_TIMESTAMP_AGE_UNCERTAINTY_MS = 250;

constexpr double GPS_POSITION_FRESH_DISTANCE_M = 25.0;
constexpr double GPS_POSITION_EXPIRE_DISTANCE_M = 50.0;
constexpr uint32_t GPS_POSITION_MIN_FRESH_MS = 500;
constexpr uint32_t GPS_POSITION_MAX_FRESH_MS = 3000;
constexpr uint32_t GPS_POSITION_MIN_EXPIRE_MS = 1500;
constexpr uint32_t GPS_POSITION_MAX_EXPIRE_MS = 10000;

constexpr uint32_t GPS_SPEED_FRESH_MS = 1000;
constexpr uint32_t GPS_SPEED_EXPIRE_MS = 3000;
constexpr uint32_t GPS_ZERO_SPEED_EXPIRE_MS = 10000;
constexpr uint32_t GPS_HEADING_FRESH_MS = 1000;
constexpr uint32_t GPS_HEADING_EXPIRE_MS = 2000;

constexpr double GPS_POSITION_FALLBACK_SPEED_MPS = GPS_MAX_SPEED_MPS;

// Shared on-wire values for GPS data that is unavailable or expired.
constexpr int32_t GPS_LATITUDE_UNAVAILABLE = INT32_MIN;
constexpr int32_t GPS_LONGITUDE_UNAVAILABLE = INT32_MIN;
constexpr uint16_t GPS_SPEED_UNAVAILABLE = UINT16_MAX;
constexpr uint16_t GPS_HEADING_UNAVAILABLE = UINT16_MAX;
constexpr uint32_t GPS_TIMESTAMP_UNAVAILABLE = UINT32_MAX;

static_assert(GPS_MAX_ACCEPTABLE_ACCURACY_M >= 0.0,
              "GPS accuracy limit must be nonnegative");
static_assert(GPS_POSITION_FRESH_DISTANCE_M > 0.0 &&
                  GPS_POSITION_FRESH_DISTANCE_M < GPS_POSITION_EXPIRE_DISTANCE_M,
              "GPS position freshness distance must be positive and below expiration distance");
static_assert(GPS_POSITION_MIN_FRESH_MS > 0 &&
                  GPS_POSITION_MIN_FRESH_MS <= GPS_POSITION_MAX_FRESH_MS,
              "GPS position freshness thresholds are invalid");
static_assert(GPS_POSITION_MIN_EXPIRE_MS > GPS_POSITION_MIN_FRESH_MS &&
                  GPS_POSITION_MIN_EXPIRE_MS <= GPS_POSITION_MAX_EXPIRE_MS,
              "GPS position expiration thresholds are invalid");
static_assert(GPS_SPEED_FRESH_MS > 0 &&
                  GPS_SPEED_FRESH_MS < GPS_SPEED_EXPIRE_MS,
              "GPS speed thresholds are invalid");
static_assert(GPS_SPEED_FRESH_MS < GPS_ZERO_SPEED_EXPIRE_MS,
              "GPS zero-speed expiration must exceed its freshness threshold");
static_assert(GPS_HEADING_FRESH_MS > 0 &&
                  GPS_HEADING_FRESH_MS < GPS_HEADING_EXPIRE_MS,
              "GPS heading thresholds are invalid");
static_assert(GPS_POSITION_FALLBACK_SPEED_MPS > 0.0,
              "GPS position fallback speed must be positive");

extern String NODE_ID;
