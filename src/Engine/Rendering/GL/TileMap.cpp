#include "TileMap.h"
#include <nlohmann/json.hpp>
#include <fstream>

using json = nlohmann::json;

bool TileMap::loadFromFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    json j; f >> j;
    width = j.value("width", 0);
    height = j.value("height", 0);
    tileSize = j.value("tileSize", 32);
    atlasImagePath = j.value("atlasImage", std::string(""));
    if (j.contains("tiles")) tiles = j["tiles"].get<std::vector<uint16_t>>();

    places.clear();
    if (j.contains("places")) {
        for (auto& p : j["places"]) {
            Place pl;
            pl.name = p.value("name", std::string(""));
            pl.type = p.value("type", std::string(""));
            pl.x = p.value("x", 0);
            pl.y = p.value("y", 0);
            places.push_back(std::move(pl));
        }
    }

    countries.clear();
    if (j.contains("countries")) countries = j["countries"].get<std::vector<uint16_t>>();

    countryColorsRGB.clear();
    if (j.contains("countryColors")) {
        auto arr = j["countryColors"]; // array of {"id":n,"color":16777215}
        if (arr.is_array()) {
            // find max id
            uint16_t maxId=0; for (auto& c : arr) { uint16_t id = (uint16_t)c.value("id",0); if (id>maxId) maxId=id; }
            countryColorsRGB.assign(maxId+1, 0x202020);
            countryColorsRGB[0]=0x101010;
            for (auto& c : arr) {
                if (!c.is_object()) continue;
                uint16_t id = (uint16_t)c.value("id",0);
                uint32_t col = (uint32_t)c.value("color",0x808080);
                if (id < countryColorsRGB.size()) countryColorsRGB[id]=col & 0xFFFFFFu;
            }
        }
    }

    roads.clear();
    if (j.contains("roads")) {
        for (auto& r : j["roads"]) {
            Road rd; 
            if (r.contains("points")) {
                for (auto& pt : r["points"]) {
                    RoadSegment seg{ pt.value("x",0), pt.value("y",0) }; 
                    rd.points.push_back(seg);
                }
            }
            roads.push_back(std::move(rd));
        }
    }
    return true;
}
