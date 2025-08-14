#include "AzgaarImporter.h"
#include <glm/vec2.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <unordered_map>
#include <unordered_set>
#include <random>
#include <spdlog/spdlog.h>
#include <cctype>
#include <limits>

using json = nlohmann::json;

static uint64_t hash64(uint64_t x){ x += 0x9e3779b97f4a7c15ULL; x = (x^(x>>30))*0xbf58476d1ce4e5b9ULL; x = (x^(x>>27))*0x94d049bb133111ebULL; return x^(x>>31); }

static std::string gen_name(uint64_t seed, const std::string& culture) {
    static const char* sylA[] = {"an","dor","fal","gal","har","ith","kal","lor","mor","nar","or","pel","quel","run","sar","thal","ur","val","wyr","zan"};
    static const char* sylB[] = {"a","e","i","o","u","ae","ia","ou"};
    static const char* sylC[] = {"d","g","l","n","r","s","th","v","z","dr","gr","kr","st","sh","kh"};
    std::mt19937_64 rng(hash64(seed) ^ std::hash<std::string>{}(culture));
    auto pick = [&](auto&& arr){ std::uniform_int_distribution<size_t> d(0, std::size(arr)-1); return arr[d(rng)]; };
    std::string n; n += pick(sylA); n[0] = (char)toupper(n[0]); n += pick(sylC); n += pick(sylB); if ((rng()&1)==0){ n += pick(sylC); n += pick(sylB);} return n;
}

struct SurrogatePreprocessStats { int isolatedHigh=0; int isolatedLow=0; int preservedPairs=0; };

// Préprocesse JSON string-level pour neutraliser surrogates invalides sans les perdre:
// - Paires valides \uD8xx\uDCxx conservées.
// - High isolé : transformé en \\uD8xx (double backslash) => devient texte littéral "\uD8xx" dans la chaîne résultante.
// - Low isolé idem.
// Une seule passe, O(n), pas d'allocation intermédiaire excessive.
static std::string preprocess_invalid_surrogates(const std::string& in, SurrogatePreprocessStats& stats) {
    std::string out; out.reserve(in.size()+64);
    bool inString=false; bool escape=false;
    size_t i=0, n=in.size();
    while(i<n) {
        char c = in[i];
        if (!inString) {
            if (c=='"') { inString=true; out.push_back(c); i++; continue; }
            out.push_back(c); i++; continue;
        } else { // inside string
            if (escape) { out.push_back(c); escape=false; i++; continue; }
            if (c=='\\') {
                // possible escape sequence
                if (i+1 < n && in[i+1]=='u' && i+6<=n) {
                    // candidate \uXXXX
                    bool hexOK=true; for(int k=0;k<4;k++){ unsigned char hc=in[i+2+k]; if(!std::isxdigit(hc)){hexOK=false;break;} }
                    if (hexOK) {
                        unsigned val = (unsigned)std::strtoul(in.substr(i+2,4).c_str(), nullptr, 16);
                        bool isHigh = (val>=0xD800 && val<=0xDBFF);
                        bool isLow  = (val>=0xDC00 && val<=0xDFFF);
                        if (isHigh) {
                            // check pair
                            if (i+12<=n && in[i+6]=='\\' && in[i+7]=='u') {
                                bool hex2=true; for(int k=0;k<4;k++){ unsigned char hc=in[i+8+k]; if(!std::isxdigit(hc)){hex2=false;break;} }
                                if (hex2) {
                                    unsigned val2 = (unsigned)std::strtoul(in.substr(i+8,4).c_str(), nullptr, 16);
                                    if (val2>=0xDC00 && val2<=0xDFFF) {
                                        // valid pair
                                        out.append(in, i, 12); i+=12; stats.preservedPairs++; continue;
                                    }
                                }
                            }
                            // isolate high -> textualize by duplicating leading backslash
                            out.push_back('\\'); // extra backslash
                            out.append(in, i, 6); // original sequence
                            i+=6; stats.isolatedHigh++; continue;
                        } else if (isLow) {
                            // low isolé
                            out.push_back('\\');
                            out.append(in, i, 6);
                            i+=6; stats.isolatedLow++; continue;
                        } else {
                            // normal escape \uXXXX
                            out.append(in, i, 6); i+=6; continue;
                        }
                    }
                }
                // generic escape start
                out.push_back('\\'); escape=true; i++; continue;
            }
            if (c=='"') { inString=false; out.push_back(c); i++; continue; }
            out.push_back(c); i++; continue;
        }
    }
    return out;
}

namespace AzgaarImporter {

bool Load(const std::string& jsonPath,
          const AzgaarImportConfig& cfg,
          const std::string& atlasImage,
          uint64_t worldSeed,
          AzgaarImportResult& out) {
    SPDLOG_INFO("[Azgaar] Ouverture fichier: {}", jsonPath);
    std::ifstream f(jsonPath, std::ios::binary);
    if (!f.is_open()) { SPDLOG_ERROR("[Azgaar] Echec ouverture fichier"); return false; }

    f.seekg(0, std::ios::end); auto fileSize = f.tellg(); f.seekg(0, std::ios::beg);
    SPDLOG_INFO("[Azgaar] Taille fichier: {} octets", (long long)fileSize);
    std::string content; content.resize((size_t)fileSize);
    if (fileSize>0) f.read(content.data(), fileSize);
    if (!f && (size_t)fileSize != content.size()) { SPDLOG_ERROR("[Azgaar] Lecture fichier incomplète"); return false; }

    // Préprocess surrogates invalides (préserve l'information textuelle)
    SurrogatePreprocessStats preStats; std::string prepared = preprocess_invalid_surrogates(content, preStats);
    if (preStats.isolatedHigh || preStats.isolatedLow) {
        SPDLOG_WARN("[Azgaar] Surrogates neutralisés: highIsolated={} lowIsolated={} (paires valides={})", preStats.isolatedHigh, preStats.isolatedLow, preStats.preservedPairs);
    }

    json j;
    try { j = json::parse(prepared); }
    catch (const std::exception& e) {
        SPDLOG_ERROR("[Azgaar] Parse échoué malgré préprocess: {}", e.what());
        return false;
    }

    size_t rootKeys = j.is_object() ? j.size() : 0;
    SPDLOG_INFO("[Azgaar] JSON chargé (éléments racine: {})", rootKeys);

    // Récupération robuste des tableaux (top-level ou dans 'pack')
    auto getArray = [&](const char* key) -> const json& {
        if (j.contains(key) && j[key].is_array()) return j[key];
        if (j.contains("pack") && j["pack"].is_object()) {
            const auto& pk = j["pack"]; if (pk.contains(key) && pk[key].is_array()) return pk[key];
        }
        static json empty = json::array(); return empty;
    };
    const auto& cells = getArray("cells");
    const auto& states = getArray("states");
    const auto& biomes = getArray("biomes");
    const auto& burgs = getArray("burgs");
    // Fallback: certains exports Azgaar utilisent 'biomesData' (tableaux parallèles) au lieu de 'biomes'
    const json* biomesPtr = &biomes;
    json biomesSynth; // restera local si besoin
    if (biomes.empty()) {
        const json* packObj = (j.contains("pack") && j["pack"].is_object()) ? &j["pack"] : &j;
        if (packObj->contains("biomesData") && (*packObj)["biomesData"].is_object()) {
            const json &bd = (*packObj)["biomesData"];
            if (bd.contains("i") && bd.contains("name") && bd.contains("color") && bd["i"].is_array() && bd["name"].is_array() && bd["color"].is_array()) {
                const auto &ids = bd["i"]; const auto &names = bd["name"]; const auto &colors = bd["color"]; size_t n = std::min({ids.size(), names.size(), colors.size()});
                biomesSynth = json::array();
                // reserve capacity on underlying vector (avoid invalid basic_json::reserve usage)
                auto &arrVec = biomesSynth.get_ref<json::array_t&>();
                arrVec.reserve(n);
                for (size_t k=0; k<n; ++k) {
                    json obj = json::object();
                    obj["i"] = ids[k];
                    obj["name"] = names[k];
                    obj["color"] = colors[k];
                    biomesSynth.push_back(std::move(obj));
                }
                biomesPtr = &biomesSynth;
                SPDLOG_INFO("[Azgaar] biomesData converti -> {} entrées", biomesSynth.size());
            }
        }
    }
    const json &biomesArr = *biomesPtr;
    // Préparer stockage des noms de biomes pour debug (prendre en compte ids biomes ET indices utilisés dans les cellules)
    size_t maxBiomeId = 0;
    for (size_t i=0; i<biomesArr.size(); ++i) {
        const auto &b = biomesArr[i]; if (!b.is_object()) continue;
        int bid = -1;
        if (b.contains("i") && b["i"].is_number_integer()) bid = b["i"].get<int>();
        else if (b.contains("id") && b["id"].is_number_integer()) bid = b["id"].get<int>();
        else bid = (int)i; // fallback index
        if (bid >= 0 && (size_t)bid > maxBiomeId) maxBiomeId = (size_t)bid;
    }
    // Inclure les indices référencés par les cellules (sinon palette->biomeName hors plage)
    for (auto &c : cells) { if (c.is_object()) { int cb = c.value("biome", -1); if (cb >= 0 && (size_t)cb > maxBiomeId) maxBiomeId = (size_t)cb; } }
    out.map.biomeNames.assign(maxBiomeId+1, std::string());
    out.map.biomeColorsRGB.assign(maxBiomeId+1, 0x707070u);
    out.map.countryInfos.clear(); // reset pays
    // Build water biome mask (names containing various water-related keywords)
    std::unordered_set<int> waterBiomes; waterBiomes.reserve(biomesArr.size());
    for (size_t i=0; i<biomesArr.size(); ++i) {
        const auto &b = biomesArr[i]; if (!b.is_object()) continue;
        int bid = -1;
        if (b.contains("i") && b["i"].is_number_integer()) bid = b["i"].get<int>();
        else if (b.contains("id") && b["id"].is_number_integer()) bid = b["id"].get<int>();
        else bid = (int)i; // align with fallback used in cells
        if (bid < 0) continue; if ((size_t)bid >= out.map.biomeNames.size()) continue; // ignore ids aberrants
        std::string n = b.value("name", std::string());
        out.map.biomeNames[bid] = n;
        if (b.contains("color")) {
            const auto &col = b["color"]; 
            if (col.is_string()) {
                std::string cHex = col.get<std::string>();
                if (cHex.size()==7 && cHex[0]=='#') { try { out.map.biomeColorsRGB[bid] = (uint32_t)std::stoul(cHex.substr(1), nullptr, 16)&0xFFFFFFu; } catch(...){} }
            } else if (col.is_number_unsigned()) {
                out.map.biomeColorsRGB[bid] = (uint32_t)col.get<unsigned long long>() & 0xFFFFFFu;
            }
        }
        std::string low; low.reserve(n.size()); for(char ch: n){ low.push_back((char)tolower(ch)); }
        if (low.find("ocean")!=std::string::npos || low.find("sea")!=std::string::npos || low.find("lake")!=std::string::npos ||
            low.find("water")!=std::string::npos || low.find("coast")!=std::string::npos || low.find("shore")!=std::string::npos ||
            low.find("shallows")!=std::string::npos || low.find("shoal")!=std::string::npos || low.find("reef")!=std::string::npos ||
            low.find("bay")!=std::string::npos || low.find("lagoon")!=std::string::npos || low.find("gulf")!=std::string::npos ||
            low.find("channel")!=std::string::npos || low.find("strait")!=std::string::npos || low.find("fjord")!=std::string::npos ||
            low.find("sound")!=std::string::npos || low.find("estuary")!=std::string::npos || low.find("delta")!=std::string::npos ||
            low.find("marsh")!=std::string::npos || low.find("swamp")!=std::string::npos || low.find("bog")!=std::string::npos ||
            low.find("wetland")!=std::string::npos || low.find("river")!=std::string::npos ) {
            waterBiomes.insert(bid);
        }
    }
    SPDLOG_INFO("[Azgaar] Biomes: entries=%zu maxBiomeId=%zu eauDetec=%zu (fallbackData=%s)", biomesArr.size(), maxBiomeId, waterBiomes.size(), biomes.empty()?"oui":"non");

    // Fallback: si certains noms sont vides, injecter les noms standards FMG (ordre canonique)
    {
        static const char* kDefaultBiomeNames[] = {
            "Marine",                // 0
            "Hot Desert",            // 1
            "Cold Desert",           // 2
            "Savanna",               // 3
            "Grassland",             // 4
            "Tropical Seasonal Forest", // 5
            "Temperate Deciduous Forest", // 6
            "Tropical Rainforest",   // 7
            "Temperate Rainforest",  // 8
            "Taiga",                 // 9
            "Tundra",                // 10
            "Glacier",               // 11
            "Wetland"                // 12
        };
        constexpr size_t kDefaultCount = sizeof(kDefaultBiomeNames)/sizeof(kDefaultBiomeNames[0]);
        size_t filled = 0; size_t missing = 0;
        for (size_t i=0; i<kDefaultCount && i<out.map.biomeNames.size(); ++i) {
            if (out.map.biomeNames[i].empty()) { out.map.biomeNames[i] = kDefaultBiomeNames[i]; filled++; }
        }
        for (size_t i=0; i<out.map.biomeNames.size(); ++i) if (out.map.biomeNames[i].empty()) missing++;
        if (filled>0) SPDLOG_INFO("[Azgaar] Biome noms défaut appliqués: remplis=%zu restants vides=%zu", filled, missing);
    }

    // Fallback couleurs : remplacer les 0x707070 restants par des couleurs HSV distinctes
    {
        size_t replaced = 0; 
        auto hsv2rgb = [](float h, float s, float v)->uint32_t {
            h = h - std::floor(h); // wrap [0,1)
            float c = v * s;
            float x = c * (1.0f - fabsf(fmodf(h*6.0f, 2.0f) - 1.0f));
            float m = v - c; float r=0,g=0,b=0; int hi = (int)floorf(h*6.0f);
            switch(hi%6){
                case 0: r=c; g=x; b=0; break; case 1: r=x; g=c; b=0; break; case 2: r=0; g=c; b=x; break; case 3: r=0; g=x; b=c; break; case 4: r=x; g=0; b=c; break; case 5: r=c; g=0; b=x; break; }
            uint8_t R=(uint8_t)std::round((r+m)*255.0f);
            uint8_t G=(uint8_t)std::round((g+m)*255.0f);
            uint8_t B=(uint8_t)std::round((b+m)*255.0f);
            return (uint32_t(R)<<16)|(uint32_t(G)<<8)|uint32_t(B);
        };
        // Hue stepping with golden ratio for distinctness
        for (size_t i=0;i<out.map.biomeColorsRGB.size();++i){
            if (out.map.biomeColorsRGB[i]==0x707070u){
                float h = fmodf(0.123f + i * 0.61803398875f, 1.0f);
                float s = 0.55f; float v = 0.85f;
                // Réserver une tonalité bleutée plus sombre pour l'index 0 si jamais utilisé (Marine) mais eau gérée ailleurs
                if (i==0){ h=0.58f; s=0.50f; v=0.55f; }
                out.map.biomeColorsRGB[i] = hsv2rgb(h,s,v); replaced++;
            }
        }
        if (replaced>0) SPDLOG_INFO("[Azgaar] Biome couleurs fallback générées: {}", replaced);
    }

    double maxX=0, maxY=0;
    for (auto& c : cells) {
        if (!c.is_object()) continue; double cx=0,cy=0;
        if (c.contains("x")) { cx = c.value("x",0.0); cy = c.value("y",0.0); }
        else if (c.contains("p") && c["p"].is_array() && c["p"].size()>=2) { cx = c["p"][0].get<double>(); cy = c["p"][1].get<double>(); }
        if (cx>maxX) maxX=cx; if (cy>maxY) maxY=cy;
    }
    // After computing maxX/maxY store in map
    out.map.worldMaxX = (float)maxX; out.map.worldMaxY = (float)maxY;
    if (cfg.worldKmWidth > 0.f && maxX>0) out.map.kmPerUnit = cfg.worldKmWidth / (float)maxX; else out.map.kmPerUnit = 1.f;
    if (maxX <= 0 || maxY <= 0) { SPDLOG_ERROR("[Azgaar] Etendue invalide maxX={} maxY={}", maxX, maxY); return false; }
    SPDLOG_INFO("[Azgaar] Etendue carte source: maxX={} maxY={}", maxX, maxY);

    out.map.width = cfg.targetWidth; out.map.height = cfg.targetHeight; out.map.tileSize=1; out.map.atlasImagePath = atlasImage;
    out.map.tiles.assign(out.map.width * out.map.height, 0);
    out.map.countries.assign(out.map.width * out.map.height, 0);
    out.map.tileHeights.assign(out.map.width * out.map.height, 0.f);
    out.map.paletteIndices.assign(out.map.width * out.map.height, 0u);
    out.sourceCellCount = (int)cells.size();

    auto biomeToTile = [&](int b)->uint16_t { return b<0?0:(uint16_t)b; };
    std::unordered_map<int,int> stateIdMap; int nextCountry=1; int mappedStates=0;
    for (auto& st : states) {
        if (!st.is_object()) continue; int sid = st.value("i", -1); if (sid<0 && st.contains("id")) sid = st["id"].get<int>();
        if (sid>=0) {
            stateIdMap[sid]=nextCountry; uint32_t rgb=0x505050; std::string sname = st.value("name", std::string());
            if (st.contains("color") && st["color"].is_string()) {
                std::string c = st["color"].get<std::string>(); if (c.size()==7 && c[0]=='#') { try { rgb = (uint32_t)std::stoul(c.substr(1), nullptr, 16)&0xFFFFFFu; } catch(...) {} }
            } else if (st.contains("color") && st["color"].is_number_unsigned()) {
                rgb = (uint32_t)st["color"].get<unsigned long long>() & 0xFFFFFFu;
            }
            if (out.map.countryColorsRGB.size() <= (size_t)nextCountry) out.map.countryColorsRGB.resize(nextCountry+1,0x303030);
            out.map.countryColorsRGB[nextCountry]=rgb;
            // Ajouter placeholder CountryInfo (position à calculer après parsing des cellules)
            out.map.countryInfos.push_back({ nextCountry, sname, 0.f, 0.f });
            nextCountry++; mappedStates++;
        }
    }
    if (out.map.countryColorsRGB.empty()) out.map.countryColorsRGB.resize(1,0x101010);
    SPDLOG_INFO("[Azgaar] Etats mappés: {} (countryIds 1..{})", mappedStates, nextCountry-1);

    // Parse raw vertices (Azgaar global vertex list) if present
    const json* verticesArray = nullptr;
    if (j.contains("vertices") && j["vertices"].is_array()) verticesArray = &j["vertices"];
    else if (j.contains("pack") && j["pack"].is_object() && j["pack"].contains("vertices") && j["pack"]["vertices"].is_array()) verticesArray = &j["pack"]["vertices"];
    std::vector<glm::vec2> rawVerts; rawVerts.reserve(verticesArray? verticesArray->size():0);
    if (verticesArray) {
        for (auto& v : *verticesArray) {
            if (!v.is_object()) continue; double px=0,py=0; if (v.contains("p") && v["p"].is_array() && v["p"].size()>=2){ px=v["p"][0].get<double>(); py=v["p"][1].get<double>(); }
            rawVerts.emplace_back((float)px,(float)py);
        }
        SPDLOG_INFO("[Azgaar] Vertices bruts: {}", rawVerts.size());
    } else {
        SPDLOG_WARN("[Azgaar] Aucun tableau 'vertices' trouvé (polygones non disponibles)");
    }

    // Prepare polygon containers
    out.map.polygonVertices.clear();
    out.map.cellPolys.clear();
    std::vector<uint16_t> triCountryIds; triCountryIds.reserve(cells.size()*4); // pays par triangle

    // Precompute land/water flags per cell using biome (needs to be before polygon loop)
    std::vector<uint8_t> cellIsWater; cellIsWater.reserve(cells.size());
    std::vector<float>   cellHeights; cellHeights.reserve(cells.size()); // hauteur brute (h) Azgaar
    std::vector<float>   waterHeights; waterHeights.reserve(cells.size());
    for (auto &cc : cells) {
        if (!cc.is_object()) { cellIsWater.push_back(0); cellHeights.push_back(0.f); continue; }
        int biome = cc.value("biome", -1);
        int stVal = cc.value("state", -1);
        bool water = (stVal < 0) || (biome>=0 && waterBiomes.count(biome)); // état <0 => eau
        float h = 0.f;
        if (cc.contains("h")) { try { h = (float)cc.value("h", 0.0); } catch(...) {} }
        else if (cc.contains("height")) { try { h = (float)cc.value("height", 0.0); } catch(...) {} }
        cellIsWater.push_back(water?1:0);
        cellHeights.push_back(h);
        if (water) waterHeights.push_back(h);
    }
    // Détecte un seuil de profondeur via distribution des hauteurs d'eau
    float shallowThreshold = 0.f; // h >= threshold => eau peu profonde
    if (!waterHeights.empty()) {
        float minW = std::numeric_limits<float>::max();
        float maxW = -std::numeric_limits<float>::max();
        double sum=0.0;
        for (float h : waterHeights){ if (h<minW) minW=h; if (h>maxW) maxW=h; sum += h; }
        float avg = (float)(sum / (double)waterHeights.size());
        // Heuristique: seuil près de la partie supérieure (proche du niveau marin / côte).
        // On prend interpolation vers le haut: shallow = top 40% de la colonne d'eau.
        // Si distribution triviale (min==max) tout reste deep.
        if (maxW > minW) shallowThreshold = minW + (maxW - minW) * 0.60f; // top 40% => peu profond
        else shallowThreshold = maxW + 1.f; // impossible => aucune eau peu profonde
        SPDLOG_INFO("[Azgaar] Eau depth stats: minH={} maxH={} avgH={} shallowThreshold={}", minW, maxW, avg, shallowThreshold);
        out.map.waterMinHeight=minW; out.map.waterMaxHeight=maxW;
    } else {
        SPDLOG_WARN("[Azgaar] Aucune hauteur d'eau disponible pour classifier profonde/peu profonde");
    }
    // stats terres
    {
        float minL=std::numeric_limits<float>::max(), maxL=-std::numeric_limits<float>::max(); bool any=false;
        for (size_t i=0;i<cellHeights.size();++i){ if(!cellIsWater[i]){ any=true; float h=cellHeights[i]; if(h<minL)minL=h; if(h>maxL)maxL=h; }}
        if(any){ out.map.landMinHeight=minL; out.map.landMaxHeight=maxL; }
    }

    // (Re)define triangulation lambda (was lost during edits)
    auto addCellFan = [&](const std::vector<int>& vids, uint16_t paletteIndex, uint16_t countryId){
        if (vids.size() < 3) return;
        uint32_t first = (uint32_t)out.map.polygonVertices.size();
        uint16_t triVertCount = 0;
        glm::vec2 v0 = rawVerts[vids[0]];
        for (size_t i=1; i+1<vids.size(); ++i) {
            glm::vec2 v1 = rawVerts[vids[i]];
            glm::vec2 v2 = rawVerts[vids[i+1]];
            out.map.polygonVertices.push_back({v0.x, v0.y, paletteIndex});
            out.map.polygonVertices.push_back({v1.x, v1.y, paletteIndex});
            out.map.polygonVertices.push_back({v2.x, v2.y, paletteIndex});
            triVertCount += 3;
            triCountryIds.push_back(countryId);
        }
        out.map.cellPolys.push_back({first, triVertCount, paletteIndex});
    };

    // Iterate cells again to build polygons if we have raw vertices
    if (verticesArray && !rawVerts.empty()) {
        size_t cellsWithPoly=0; size_t cellIdx=0;
        for (auto& c : cells) {
            if (!c.is_object()) { cellIdx++; continue; }
            if (!c.contains("v") || !c["v"].is_array()) { cellIdx++; continue; }
            int state = c.value("state", -1);
            int biome = c.value("biome", -1);
            bool isWater = (state < 0) || (biome>=0 && waterBiomes.count(biome));
            uint16_t mappedCountry = 0; if (!isWater && state>=0){ auto it=stateIdMap.find(state); if (it!=stateIdMap.end()) mappedCountry=(uint16_t)it->second; }
            uint16_t paletteIndex = 0;
            if (isWater) {
                float h = 0.f; if (cellIdx < cellHeights.size()) h = cellHeights[cellIdx]; bool shallow = (h >= shallowThreshold); paletteIndex = shallow ? 1 : 0;
            } else {
                paletteIndex = (uint16_t)( (biome >= 0 ? biome : 0) + 2 );
            }
            std::vector<int> vids; vids.reserve(c["v"].size());
            for (auto& vi : c["v"]) { if (!vi.is_number_integer()) continue; int idxV=vi.get<int>(); if (idxV>=0 && idxV < (int)rawVerts.size()) vids.push_back(idxV); }
            if (vids.size()>=3) { addCellFan(vids,paletteIndex,mappedCountry); cellsWithPoly++; }
            cellIdx++;
        }
        SPDLOG_INFO("[Azgaar] Cellules polygonisées: {} (tri vertices total={})", cellsWithPoly, out.map.polygonVertices.size());

        // Scanline fill des pays par cellule (robuste concave) -> remplit out.map.countries
        // On le fait seulement si nous avons peu de coverage (heuristique) ou toujours (pour être sûr)
        {
            size_t nonZeroBefore = 0; for (auto v : out.map.countries) if (v>0) nonZeroBefore++;
            float sx = (out.map.width  > 1 && maxX>0)? (float)(out.map.width  - 1) / (float)maxX : 1.f;
            float sy = (out.map.height > 1 && maxY>0)? (float)(out.map.height - 1) / (float)maxY : 1.f;
            size_t cellI = 0; size_t filledPix=0;
            std::vector<float> xints; xints.reserve(64);
            for (auto &c : cells){
                if (!c.is_object()){ cellI++; continue; }
                if (!c.contains("v") || !c["v"].is_array()) { cellI++; continue; }
                int state = c.value("state", -1); int biome = c.value("biome", -1);
                bool isWater = (state < 0) || (biome>=0 && waterBiomes.count(biome)); if (isWater){ cellI++; continue; }
                auto itS = stateIdMap.find(state); if (itS==stateIdMap.end()) { cellI++; continue; }
                uint16_t cid = (uint16_t)itS->second; if (cid==0) { cellI++; continue; }
                // Collect polygon
                std::vector<glm::vec2> poly; poly.reserve(c["v"].size());
                float minx=1e9f,miny=1e9f,maxx=-1e9f,maxy=1e9f;
                for (auto &vi : c["v"]) {
                    if (!vi.is_number_integer()) continue; int idv = vi.get<int>(); if (idv<0 || idv>=(int)rawVerts.size()) continue;
                    glm::vec2 w = rawVerts[idv]; glm::vec2 g(w.x * sx, w.y * sy);
                    poly.push_back(g);
                    if (g.x<minx)minx=g.x; if (g.x>maxx)maxx=g.x; if (g.y<miny)miny=g.y; if (g.y>maxy)maxy=g.y;
                }
                if (poly.size()<3){ cellI++; continue; }
                int ix0 = (int)std::max(0.f, std::floor(minx));
                int ix1 = (int)std::min((float)(out.map.width-1), std::ceil (maxx));
                int iy0 = (int)std::max(0.f, std::floor(miny));
                int iy1 = (int)std::min((float)(out.map.height-1), std::ceil (maxy));
                if (ix0>ix1 || iy0>iy1){ cellI++; continue; }
                // Scanline
                for (int y=iy0; y<=iy1; ++y){
                    float scanY = (float)y + 0.5f; xints.clear();
                    // Edges
                    for (size_t i=0;i<poly.size();++i){
                        const glm::vec2 &a = poly[i]; const glm::vec2 &b = poly[(i+1)%poly.size()];
                        // Ignore horizontals
                        if (a.y==b.y) continue;
                        bool yIn = ( (scanY>=std::min(a.y,b.y)) && (scanY < std::max(a.y,b.y)) ); // half-open to avoid double count
                        if (!yIn) continue;
                        float t = (scanY - a.y) / (b.y - a.y);
                        float xh = a.x + (b.x - a.x)*t;
                        xints.push_back(xh);
                    }
                    if (xints.size()<2) continue;
                    std::sort(xints.begin(), xints.end());
                    for (size_t k=0;k+1<xints.size();k+=2){
                        float x0 = xints[k]; float x1 = xints[k+1]; if (x1 < x0) std::swap(x0,x1);
                        int fx0 = (int)std::max(0.f, std::ceil (x0));
                        int fx1 = (int)std::min((float)(out.map.width-1), std::floor(x1));
                        for (int x=fx0; x<=fx1; ++x){ size_t idx = (size_t)y*out.map.width + x; if (idx < out.map.countries.size() && out.map.countries[idx]==0){ out.map.countries[idx]=cid; filledPix++; } }
                    }
                }
                cellI++;
            }
            size_t nonZeroAfter=0; for (auto v : out.map.countries) if (v>0) nonZeroAfter++;
            SPDLOG_INFO("[Azgaar] Country scanline fill: pixelsAvant={} pixelsApres={} ajout={}", nonZeroBefore, nonZeroAfter, (nonZeroAfter>nonZeroBefore?nonZeroAfter-nonZeroBefore:0));
        }
    }

    size_t filledCells=0;
    size_t cellIdx2 = 0; // index parallèle aux vecteurs cells / cellHeights pour le placement des hauteurs
    for (auto& c : cells) {
        if (!c.is_object()) { cellIdx2++; continue; }
        double cx=0,cy=0; if (c.contains("x")) { cx=c.value("x",0.0); cy=c.value("y",0.0);} else if (c.contains("p") && c["p"].is_array() && c["p"].size()>=2){ cx=c["p"][0].get<double>(); cy=c["p"][1].get<double>(); }
        int biome = c.value("biome",0); int state = c.value("state", -1);
        bool isWater = (state < 0) || (biome>=0 && waterBiomes.count(biome));
        int gx = (int)(cx / maxX * (out.map.width -1)); int gy = (int)(cy / maxY * (out.map.height -1));
        if (gx<0||gy<0||gx>=out.map.width||gy>=out.map.height) { out.skippedCells++; cellIdx2++; continue; }
        size_t idx = gy*out.map.width + gx; out.map.tiles[idx]=biomeToTile(biome);
        if (idx < out.map.tileHeights.size() && cellIdx2 < cellHeights.size()) {
            out.map.tileHeights[idx] = cellHeights[cellIdx2];
        }
        // Nouveau: écrire l'ID pays discret
        if (!isWater && state>=0) {
            auto it = stateIdMap.find(state);
            if (it != stateIdMap.end()) out.map.countries[idx] = (uint16_t)it->second;
        }
        filledCells++;
        cellIdx2++;
    }
    SPDLOG_INFO("[Azgaar] Cells placées: {} / {} (skipped={})", filledCells, cells.size(), out.skippedCells);

    out.map.places.clear(); out.placedBurgs=0;
    for (auto& b : burgs) {
        if (!b.is_object()) continue; double bx=0,by=0; if (b.contains("x")) { bx=b.value("x",0.0); by=b.value("y",0.0);} else if (b.contains("p") && b["p"].is_array() && b["p"].size()>=2){ bx=b["p"][0].get<double>(); by=b["p"][1].get<double>(); }
        int gx = (int)(bx / maxX * (out.map.width -1)); int gy = (int)(by / maxY * (out.map.height -1));
        if (gx<0||gy<0||gx>=out.map.width||gy>=out.map.height) continue;
        if (gx==0 && gy==0) continue; // ignore artefact (0,0)
        size_t tileIndex = (size_t)gy * out.map.width + gx;
        if (tileIndex < out.map.countries.size() && out.map.countries[tileIndex]==0) continue; // skip water
        Place p; p.x=gx; p.y=gy; p.type="city"; std::string azName = b.value("name", std::string(""));
        if (cfg.keepAzgaarNames && !azName.empty()) p.name=azName; else { uint64_t seed = worldSeed ^ ((uint64_t)gx<<32) ^ (uint64_t)gy; p.name = gen_name(seed, "culture"); }
        out.map.places.push_back(std::move(p)); out.placedBurgs++;
    }
    SPDLOG_INFO("[Azgaar] Burgs placés: {}", out.placedBurgs);

    size_t roadsAddedBefore = out.map.roads.size();
    if (j.contains("roads")) {
        for (auto& rr : j["roads"]) {
            if (!rr.is_object()) continue; Road road;
            if (rr.contains("points")) {
                for (auto& p : rr["points"]) {
                    double rx = p.value("x",0.0); double ry=p.value("y",0.0); int gx=(int)(rx/maxX*(out.map.width-1)); int gy=(int)(ry/maxY*(out.map.height-1)); if (gx<0||gy<0||gx>=out.map.width||gy>=out.map.height) continue; road.points.push_back({gx,gy});
                }
            } else if (rr.contains("coords")) {
                for (auto& p : rr["coords"]) {
                    if (!p.is_array() || p.size()<2) continue; double rx=p[0].get<double>(); double ry=p[1].get<double>(); int gx=(int)(rx/maxX*(out.map.width-1)); int gy=(int)(ry/maxY*(out.map.height-1)); if (gx<0||gy<0||gx>=out.map.width||gy>=out.map.height) continue; road.points.push_back({gx,gy});
                }
            }
            if (!road.points.empty()) out.map.roads.push_back(std::move(road));
        }
    }
    SPDLOG_INFO("[Azgaar] Routes importées: {}", out.map.roads.size()-roadsAddedBefore);

    // RASTERISATION COMPLETE DES POLYGONES POUR GRILLE POLITIQUE ADAPTATIVE (palette indices directs)
    std::vector<uint16_t> paletteGrid; // même dimensions que countries, mais contient indices palette (0/1 eau, land décalé +2)
    if (!out.map.polygonVertices.empty()) {
        paletteGrid.assign(out.map.width * out.map.height, 0u);
        // Pré-calcul des facteurs de conversion monde -> grille
        float sx = (out.map.width  > 1 && maxX>0)? (float)(out.map.width  - 1) / (float)maxX : 1.f;
        float sy = (out.map.height > 1 && maxY>0)? (float)(out.map.height - 1) / (float)maxY : 1.f;
        struct PV { float x,y; uint16_t c; };
        // Accès direct au buffer triangulé
        const auto &pv = out.map.polygonVertices; // chaque 3 = un triangle
        auto edge = [](const glm::vec2& a, const glm::vec2& b, const glm::vec2& p){ return (p.x - a.x)*(b.y - a.y) - (p.y - a.y)*(b.x - a.x); };
        size_t triCount = pv.size()/3;
        for (size_t t=0; t<triCount; ++t) {
             const auto &v0 = pv[t*3+0];
             const auto &v1 = pv[t*3+1];
             const auto &v2 = pv[t*3+2];
             uint16_t pal = v0.country; // même palette pour les 3
             uint16_t triCountry = (t < triCountryIds.size()? triCountryIds[t] : 0);
             // Convertit en coords grille flottantes
             glm::vec2 g0(v0.x * sx, v0.y * sy);
             glm::vec2 g1(v1.x * sx, v1.y * sy);
             glm::vec2 g2(v2.x * sx, v2.y * sy);
             float minx = std::floor(std::min({g0.x,g1.x,g2.x}));
             float maxx = std::ceil (std::max({g0.x,g1.x,g2.x}));
             float miny = std::floor(std::min({g0.y,g1.y,g2.y}));
             float maxy = std::ceil (std::max({g0.y,g1.y,g2.y}));
             int ix0 = (int)std::max(0.f, minx); int ix1 = (int)std::min((float)(out.map.width-1), maxx);
             int iy0 = (int)std::max(0.f, miny); int iy1 = (int)std::min((float)(out.map.height-1), maxy);
             float areaSign = edge(g0,g1,g2);
             if (areaSign == 0) continue; // triangle dégénéré
             for (int gy=iy0; gy<=iy1; ++gy) {
                 for (int gx=ix0; gx<=ix1; ++gx) {
                     glm::vec2 p((float)gx+0.5f,(float)gy+0.5f); // centre pixel
                     float e0 = edge(g0,g1,p);
                     float e1 = edge(g1,g2,p);
                     float e2 = edge(g2,g0,p);
                     bool inside = (areaSign>0)? (e0>=0 && e1>=0 && e2>=0) : (e0<=0 && e1<=0 && e2<=0);
                     if (!inside) continue;
                     size_t idx = (size_t)gy * out.map.width + gx;
                     paletteGrid[idx] = pal; // écrit palette index directement
                     if (triCountry>0 && idx < out.map.countries.size() && out.map.countries[idx]==0) out.map.countries[idx]=triCountry;
                 }
             }
         }
        SPDLOG_INFO("[Azgaar][Raster] Remplissage paletteGrid triangles: {} (pixels={})", triCount, paletteGrid.size());
        // Copie paletteGrid vers map.paletteIndices pour debug
        out.map.paletteIndices = paletteGrid;
    }

    // Build adaptive political grid (world reference absolue)
    out.map.adaptiveCells.clear();
    float worldW = out.map.worldMaxX; // portée étendue pour meanHeight
    float worldH = out.map.worldMaxY;
    if (out.map.worldMaxX > 0 && out.map.worldMaxY > 0) {
        // worldW/worldH déjà définis
        bool usePaletteGrid = !paletteGrid.empty();
        auto getPalette = [&](int gx, int gy)->uint16_t {
            if (gx<0||gy<0||gx>=out.map.width||gy>=out.map.height) return 0;
            if (usePaletteGrid) return paletteGrid[gy*out.map.width+gx];
            // fallback sparse (ancienne logique) -> convert raw country id en palette
            uint16_t raw = out.map.countries[gy*out.map.width+gx];
            if (raw==0) return 0; return (uint16_t)(raw + 2);
        };
        struct Node { int x,y,w,h; };
        std::vector<Node> stack; stack.push_back({0,0,out.map.width,out.map.height});
        out.map.adaptiveCells.reserve(4096);
        const float majorityThreshold = 0.99f; // accepte si >=99% d'un seul index
        while(!stack.empty()) {
            Node n = stack.back(); stack.pop_back();
            int area = n.w * n.h;
            // Boyer-Moore majority pass
            uint16_t cand = getPalette(n.x, n.y); int cnt = 1;
            for (int yy=n.y; yy<n.y+n.h; ++yy) {
                for (int xx=n.x; xx<n.x+n.w; ++xx) {
                    if (yy==n.y && xx==n.x) continue; // premier déjà pris
                    uint16_t p = getPalette(xx, yy);
                    if (p==cand) cnt++; else { cnt--; if (cnt==0){ cand=p; cnt=1; } }
                }
            }
            // Validation du candidat + détection coexistence eau profonde / peu profonde
            int occ = 0; bool hasDeep=false, hasShallow=false;
            for (int yy=n.y; yy<n.y+n.h; ++yy) {
                for (int xx=n.x; xx<n.x+n.w; ++xx) {
                    uint16_t pv = getPalette(xx,yy);
                    if (pv==0) hasDeep=true; else if (pv==1) hasShallow=true;
                    if (pv==cand) occ++;
                }
            }
            float frac = (float)occ / (float)area;
            bool waterMix = (hasDeep && hasShallow && area>1); // mélange 0 & 1 dans la même boîte
            bool accept = ((frac >= majorityThreshold) || n.w==1 || n.h==1);
            if (waterMix && (cand==0 || cand==1) && area>4) accept=false; // force subdivision pour conserver bande littorale
            if (accept) {
                float cellSizeX = worldW / (float)out.map.width;
                float cellSizeY = worldH / (float)out.map.height;
                out.map.adaptiveCells.push_back({ n.x * cellSizeX, n.y * cellSizeY, n.w * cellSizeX, n.h * cellSizeY, cand, 0.f });
            } else {
                if (n.w >= n.h) {
                    int w1 = n.w/2; if (w1<1) w1=1; int w2 = n.w - w1; if (w2<1) w2=1;
                    stack.push_back({n.x+w1, n.y, w2, n.h});
                    stack.push_back({n.x, n.y, w1, n.h});
                } else {
                    int h1 = n.h/2; if (h1<1) h1=1; int h2 = n.h - h1; if (h2<1) h2=1;
                    stack.push_back({n.x, n.y+h1, n.w, h2});
                    stack.push_back({n.x, n.y, n.w, h1});
                }
            }
            if (out.map.adaptiveCells.size() > 200000) {
                SPDLOG_WARN("[Azgaar][Adaptive] Limite cellules atteinte, arrêt subdivision");
                break;
            }
        }
        SPDLOG_INFO("[Azgaar][Adaptive] Cellules adaptatives: {} (majorité {:.2f}%)", out.map.adaptiveCells.size(), majorityThreshold*100.f);
    }

    // calcul moyenne hauteur par cellule adaptative (après construction)
    if(!out.map.adaptiveCells.empty() && worldW>0 && worldH>0){
        // Remplacer l'échantillon centre par une vraie moyenne des tuiles couvertes
        for(auto &ac : out.map.adaptiveCells){
            int gx0 = (int)std::floor( (ac.x) / worldW * (out.map.width -1) );
            int gx1 = (int)std::ceil ( (ac.x + ac.w) / worldW * (out.map.width -1) );
            int gy0 = (int)std::floor( (ac.y) / worldH * (out.map.height-1) );
            int gy1 = (int)std::ceil ( (ac.y + ac.h) / worldH * (out.map.height -1) );
            if (gx0<0) gx0=0; if (gy0<0) gy0=0; if (gx1>=out.map.width) gx1=out.map.width-1; if (gy1>=out.map.height) gy1=out.map.height-1;
            double sum = 0.0; int count = 0;
            for(int gy=gy0; gy<=gy1; ++gy){
                for(int gx=gx0; gx<=gx1; ++gx){ size_t idx = (size_t)gy*out.map.width + gx; if (idx < out.map.tileHeights.size()) { sum += out.map.tileHeights[idx]; count++; } }
            }
            if (count>0) ac.meanHeight = (float)(sum / (double)count); else ac.meanHeight = 0.f;
        }
    }

    // Après placement des cells, calculer centroïdes par pays
    if (!out.map.countryInfos.empty()) {
        struct Acc { double sx=0, sy=0; int count=0; };
        std::vector<Acc> acc(out.map.countryColorsRGB.size());
        for (auto& c : cells) {
            if (!c.is_object()) continue; int state = c.value("state", -1); if (state<0) continue;
            auto it = stateIdMap.find(state); if (it==stateIdMap.end()) continue; int cid = it->second; if (cid<=0 || cid>=(int)acc.size()) continue;
            double cx=0,cy=0; if (c.contains("x")) { cx=c.value("x",0.0); cy=c.value("y",0.0);} else if (c.contains("p") && c["p"].is_array() && c["p"].size()>=2){ cx=c["p"][0].get<double>(); cy=c["p"][1].get<double>(); }
            acc[cid].sx += cx; acc[cid].sy += cy; acc[cid].count++;
        }
        for (auto &ci : out.map.countryInfos) {
            if (ci.id>0 && ci.id < (int)acc.size() && acc[ci.id].count>0) {
                ci.x = (float)(acc[ci.id].sx / acc[ci.id].count);
                ci.y = (float)(acc[ci.id].sy / acc[ci.id].count);
            }
        }
        SPDLOG_INFO("[Azgaar] CountryInfos: {}", out.map.countryInfos.size());
    }

    SPDLOG_INFO("[Azgaar] Import terminé: map {}x{} places={} roads={} colors={}", out.map.width, out.map.height, out.map.places.size(), out.map.roads.size(), out.map.countryColorsRGB.size());
    return true;
}

} // namespace AzgaarImporter
