#pragma once
#include <string>

struct SpotifyTrackInfo {
    std::wstring song;
    std::wstring artist;
};

SpotifyTrackInfo GetCurrentSpotifyTrack(const std::string& accessToken);