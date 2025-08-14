#include "BiomeMapImporter.h"
#include "../../../external/stb/stb_image.h"
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <random>
#include <fstream>

using json = nlohmann::json;

static uint32_t pack(uint8_t r,uint8_t g,uint8_t b){return (r<<16)|(g<<8)|b;}

static void load_png_rgb(const std::string& path, int& w, int& h, std::vector<uint8_t>& rgb) {
    int n=0; unsigned char* data = stbi_load(path.c_str(), &w, &h, &n, 3);
    if (!data) { w=h=0; rgb.clear(); return; }
    rgb.assign(data, data + (w*h*3));
    stbi_image_free(data);
}

static uint64_t hash64(uint64_t x){ x += 0x9e3779b97f4a7c15ULL; x = (x^(x>>30))*0xbf58476d1ce4e5b9ULL; x = (x^(x>>27))*0x94d049bb133111ebULL; return x^(x>>31); }

static std::string gen_name(uint64_t seed, const std::string& culture) {
    static const char* sylA[] = {"an","ar","bel","cal","dor","el","fae","gal","har","ith","jor","kal","lor","mor","nar","or","pel","qua","ror","sar","thor","ur","val","wyr","yor","zan"};
    static const char* sylB[] = {"a","e","i","o","u","ae","ia","ou"};
    static const char* sylC[] = {"d","g","l","n","r","s","th","v","z","dr","gr","kr","st","mn","rl","sh","kh"};
    std::mt19937_64 rng(hash64(seed) ^ std::hash<std::string>{}(culture));
    auto pick = [&](auto&& arr){ std::uniform_int_distribution<size_t> d(0, std::size(arr)-1); return arr[d(rng)]; };
    std::string n;
    n += pick(sylA);
    n[0] = (char)toupper(n[0]);
    n += pick(sylC);
    n += pick(sylB);
    if ((rng()&1)==0) { n += pick(sylC); n += pick(sylB); }
    return n;
}

bool WorldGenerator::Generate(const std::string& biomePng,
                              const std::vector<ColorToTile>& biomePalette,
                              const std::string& countriesPng,
                              const std::vector<ColorToCountry>& countryPalette,
                              const std::string& citiesPng,
                              const std::vector<ColorToPlaceType>& placePalette,
                              uint64_t worldSeed,
                              const std::string& atlasImage,
                              ImportResult* out) {
    int wb=0,hb=0; std::vector<uint8_t> imgB; load_png_rgb(biomePng, wb, hb, imgB);
    if (wb==0||hb==0) return false;
    int wc=0,hc=0; std::vector<uint8_t> imgC; load_png_rgb(countriesPng, wc, hc, imgC);
    if (wc!=wb||hc!=hb) return false;
    int wl=0,hl=0; std::vector<uint8_t> imgL; load_png_rgb(citiesPng, wl, hl, imgL);
    if (wl!=wb||hl!=hb) return false;

    out->map.width = wb; out->map.height = hb; out->map.tileSize = 32; out->map.atlasImagePath = atlasImage;
    out->map.tiles.assign(wb*hb, 0);
    out->map.countries.assign(wb*hb, 0);

    std::unordered_map<uint32_t,uint16_t> lutBiome; for (auto& e: biomePalette) lutBiome[pack(e.r,e.g,e.b)] = e.tileId;
    std::unordered_map<uint32_t,uint16_t> lutCountry; for (auto& e: countryPalette) lutCountry[pack(e.r,e.g,e.b)] = e.countryId;
    std::unordered_map<uint32_t,std::string> lutPlace; for (auto& e: placePalette) lutPlace[pack(e.r,e.g,e.b)] = e.type;

    // Biomes and countries per tile
    for (int y=0; y<hb; ++y) {
        for (int x=0; x<wb; ++x) {
            int i = (y*wb + x)*3;
            uint32_t kb = pack(imgB[i], imgB[i+1], imgB[i+2]);
            uint32_t kc = pack(imgC[i], imgC[i+1], imgC[i+2]);
            auto ib = lutBiome.find(kb);
            auto ic = lutCountry.find(kc);
            out->map.tiles[y*wb + x] = (ib==lutBiome.end()?0:ib->second);
            out->map.countries[y*wb + x] = (ic==lutCountry.end()?0:ic->second);
        }
    }

    // Places from cities layer with deterministic names
    out->map.places.clear();
    for (int y=0; y<hb; ++y) {
        for (int x=0; x<wb; ++x) {
            int i = (y*wb + x)*3;
            uint32_t kl = pack(imgL[i], imgL[i+1], imgL[i+2]);
            auto it = lutPlace.find(kl);
            if (it == lutPlace.end()) continue; // background/no place
            // Deduplicate: only take pixel if it’s a local maximum of intensity to avoid clusters (simple heuristic)
            if ((x%3)!=(y%3)) continue; // crude thinning
            Place p; p.type = it->second; p.x = x; p.y = y;
            uint16_t cid = out->map.countries[y*wb + x];
            std::string culture = "human"; // placeholder; derive from country later
            uint64_t seed = worldSeed ^ ((uint64_t)x<<32) ^ (uint64_t)y ^ ((uint64_t)cid<<16) ^ std::hash<std::string>{}(p.type);
            p.name = gen_name(seed, culture);
            out->map.places.push_back(std::move(p));
        }
    }

    return true;
}
