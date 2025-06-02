#include "SpotifyApi.h"
#include <curl/curl.h>
#include <nlohmann/json.hpp>

size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

SpotifyTrackInfo GetCurrentSpotifyTrack(const std::string& accessToken) {
    CURL* curl = curl_easy_init();
    std::string readBuffer;
    SpotifyTrackInfo info;

    if (curl) {
        struct curl_slist* headers = NULL;
        std::string auth = "Authorization: Bearer " + accessToken;
        headers = curl_slist_append(headers, auth.c_str());

        curl_easy_setopt(curl, CURLOPT_URL, "https://api.spotify.com/v1/me/player/currently-playing");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

        CURLcode res = curl_easy_perform(curl);
        if (res == CURLE_OK) {
            auto json = nlohmann::json::parse(readBuffer, nullptr, false);
            if (!json.is_discarded() && json.contains("item")) {
                info.song = std::wstring(json["item"]["name"].get<std::string>().begin(), json["item"]["name"].get<std::string>().end());
                if (json["item"]["artists"].size() > 0) {
                    info.artist = std::wstring(json["item"]["artists"][0]["name"].get<std::string>().begin(), json["item"]["artists"][0]["name"].get<std::string>().end());
                }
            }
        }
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
    }
    return info;
}