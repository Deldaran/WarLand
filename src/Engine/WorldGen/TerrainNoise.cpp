#include "TerrainNoise.h"
#include "../Rendering/GL/TileMap.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace { 
static inline float Hash01(uint64_t v){ v^=v>>33; v*=0xff51afd7ed558ccdull; v^=v>>33; v*=0xc4ceb9fe1a85ec53ull; v^=v>>33; return (float)(v & 0xFFFFFFFFull)/4294967295.0f; }
static inline uint64_t Mix(uint64_t a,int bx,int by){ uint64_t h=a; h^=(uint64_t)bx*0x9E3779B185EBCA87ull; h^=(uint64_t)by*0xC2B2AE3D27D4EB4Full; return h; }
static float ValueNoise(float x,float y,uint64_t seed){ int ix=(int)floor(x); int iy=(int)floor(y); float fx=x-ix; float fy=y-iy; float v00=Hash01(Mix(seed,ix,iy)); float v10=Hash01(Mix(seed,ix+1,iy)); float v01=Hash01(Mix(seed,ix,iy+1)); float v11=Hash01(Mix(seed,ix+1,iy+1)); float sx=fx*fx*(3.f-2.f*fx); float sy=fy*fy*(3.f-2.f*fy); float ix0=v00+(v10-v00)*sx; float ix1=v01+(v11-v01)*sx; return ix0+(ix1-ix0)*sy; }
}

namespace TerrainNoise {
void Generate(TileMap& map, uint64_t seed, const TerrainNoiseConfig& cfg){
    if(map.width<=0||map.height<=0) return; size_t total=(size_t)map.width*map.height; map.tileHeights.assign(total,0.f);
    map.landMinHeight=1e9f; map.landMaxHeight=-1e9f; map.waterMinHeight=1e9f; map.waterMaxHeight=-1e9f;
    // Générateur classique: FBM simple (0..1), puis clamp niveau de la mer et options de lissage / pente globale
    // 1) Bruit brut (0..1)
    std::vector<float> raw(total, 0.f);
    for(int y=0;y<map.height;++y){
        for(int x=0;x<map.width;++x){
            size_t idx=(size_t)y*map.width+x;
            float wx=(map.worldMaxX>0)? ((float)x/(float)(map.width-1))*map.worldMaxX : (float)x;
            float wy=(map.worldMaxY>0)? ((float)y/(float)(map.height-1))*map.worldMaxY : (float)y;
            float amp=1.f; float freq=cfg.baseFrequency; float sum=0.f; float norm=0.f;
            for(int o=0;o<cfg.octaves;++o){
                float n=ValueNoise(wx*freq, wy*freq, seed+(uint64_t)o*0x9E37ull);
                sum+=n*amp; norm+=amp; amp*=cfg.gain; freq*=cfg.lacunarity;
            }
            float fbm=(norm>0)?(sum/norm):0.f; // déjà 0..1
            float h = fbm; // pas de modulation par continents/biomes/crêtes
            // pente globale (après fbm): centre (0.5,0.5)
            float nx=((float)x/(float)(map.width-1)) - 0.5f;
            float ny=((float)y/(float)(map.height-1)) - 0.5f;
            h += nx*cfg.slopeX*0.10f + ny*cfg.slopeY*0.10f;
            raw[idx]=std::clamp(h,0.f,1.f);
        }
    }
    // 2) Blur passes (box 3x3)
    if(cfg.blurPasses>0){
        std::vector<float> tmp(total,0.f);
        for(int p=0;p<cfg.blurPasses;++p){
            for(int y=0;y<map.height;++y){
                for(int x=0;x<map.width;++x){
                    float acc=0.f; int cnt=0;
                    for(int dy=-1;dy<=1;++dy){ int yy=y+dy; if(yy<0||yy>=map.height) continue;
                        for(int dx=-1;dx<=1;++dx){ int xx=x+dx; if(xx<0||xx>=map.width) continue; acc+=raw[(size_t)yy*map.width+xx]; ++cnt; }
                    }
                    tmp[(size_t)y*map.width+x] = acc / (float)cnt;
                }
            }
            raw.swap(tmp);
        }
    }
    // 3) Application sea level -> hauteur relative (0 = mer/plat, >0 = terre)
    map.landMinHeight=1e9f; map.landMaxHeight=-1e9f; map.waterMinHeight=0.f; map.waterMaxHeight=0.f;
    for(size_t i=0;i<total;++i){
        float n=raw[i];
        float h = (n <= cfg.seaLevel) ? 0.f : ((n - cfg.seaLevel)/(1.f - cfg.seaLevel))*cfg.globalAmplitude; // 0..globalAmplitude
        map.tileHeights[i]=h;
        if(h>0.f){ if(h<map.landMinHeight) map.landMinHeight=h; if(h>map.landMaxHeight) map.landMaxHeight=h; }
    }
    if(map.landMinHeight>map.landMaxHeight){ map.landMinHeight=0.f; map.landMaxHeight=0.f; }
}
}
