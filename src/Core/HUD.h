#pragma once
#include <cstdint>

class FarMapRenderer;
class Input;
struct TileMap;

// Map / world display HUD (legend + toggles)
class MapHUD {
public:
    void handleInput(Input* input);
    void draw(FarMapRenderer* renderer, TileMap& map);

    bool showCountryNames() const { return showCountryNames_; }
    bool showCityNames()   const { return showCityNames_; }

private:
    bool hudVisible_ = true;      // toggle global (L)
    bool hudCompact_ = false;     // compact mode
    bool showCountryNames_ = true;
    bool showCityNames_ = true;
};
