#include "simulation/Climate.h"
#include "renderer/overlay/ProvinceMap.h"

#include <cmath>
#include <algorithm>

namespace wl {

namespace {
constexpr double kPI = 3.14159265358979323846;

// Vent zonal selon la latitude (cellules / jour) : alizes d'est dans les
// tropiques, vents d'ouest aux latitudes moyennes, est aux poles.
float zonalWind(float latN) {
    float a = std::abs(latN);
    float dir = (a < 0.33f) ? -1.0f : (a < 0.66f ? 1.0f : -1.0f);
    return dir * 1.6f;
}

// Temperature normalisee (0 froid .. 1 chaud) selon latitude, saison, altitude.
float temperature(float latN, double season, float elev) {
    float warmth = std::cos(latN * static_cast<float>(kPI) * 0.5f); // 1 equateur
    warmth += 0.15f * static_cast<float>(std::sin(season * 2.0 * kPI)) * latN; // saison
    warmth -= 0.55f * std::max(0.0f, elev); // refroidissement en altitude
    return std::clamp(warmth, 0.0f, 1.0f);
}
} // namespace

void Climate::init(const ProvinceMap& provinces, float seaLevel, int width, int height) {
    m_w = width;
    m_h = height;
    int n = m_w * m_h;
    m_ocean.assign(n, 0);
    m_elev.assign(n, 0.0f);
    m_latN.assign(n, 0.0f);
    m_hum.assign(n, 0.3f);
    m_rain.assign(n, 0.3f);
    m_cloud.assign(n, 0.0f);
    m_tmp.assign(n, 0.0f);

    int P = provinces.provinceCount();
    for (int j = 0; j < m_h; ++j) {
        float lat = ((j + 0.5f) / m_h) * static_cast<float>(kPI) - static_cast<float>(kPI) * 0.5f;
        for (int i = 0; i < m_w; ++i) {
            float lon = ((i + 0.5f) / m_w) * 2.0f * static_cast<float>(kPI) - static_cast<float>(kPI);
            glm::vec3 dir(std::cos(lat) * std::cos(lon), std::sin(lat), std::cos(lat) * std::sin(lon));

            // Province la plus proche -> elevation / ocean.
            int best = 0;
            float bestDot = -2.0f;
            for (int p = 0; p < P; ++p) {
                float d = glm::dot(dir, provinces.provinceDir(p));
                if (d > bestDot) { bestDot = d; best = p; }
            }
            float elev = provinces.provinceElevation(best);
            int idx = index(i, j);
            m_elev[idx] = elev;
            m_ocean[idx] = (elev < seaLevel) ? 1 : 0;
            m_latN[idx] = lat / (static_cast<float>(kPI) * 0.5f);
        }
    }
}

float Climate::sampleBilinear(const std::vector<float>& f, float fi, float fj) const {
    // Longitude periodique, latitude bornee.
    fi = std::fmod(fi, static_cast<float>(m_w));
    if (fi < 0) fi += m_w;
    fj = std::clamp(fj, 0.0f, static_cast<float>(m_h - 1));
    int i0 = static_cast<int>(std::floor(fi));
    int j0 = static_cast<int>(std::floor(fj));
    int i1 = (i0 + 1) % m_w;
    int j1 = std::min(j0 + 1, m_h - 1);
    float tx = fi - i0;
    float ty = fj - j0;
    float a = f[index(i0, j0)], b = f[index(i1, j0)];
    float c = f[index(i0, j1)], d = f[index(i1, j1)];
    return glm::mix(glm::mix(a, b, tx), glm::mix(c, d, tx), ty);
}

void Climate::step(double days, double season) {
    if (m_w == 0) return;
    days = std::min(days, 5.0); // garde-fou
    float fd = static_cast<float>(days);

    // 1. Evaporation : les oceans chauds chargent l'air en humidite.
    for (int idx = 0; idx < m_w * m_h; ++idx) {
        float T = temperature(m_latN[idx], season, m_elev[idx]);
        float evap = (m_ocean[idx] ? 0.11f : 0.012f) * T * fd;
        m_hum[idx] = std::min(2.0f, m_hum[idx] + evap);
    }

    // 2. Advection semi-lagrangienne par les vents zonaux (+ leger mélange).
    m_tmp = m_hum;
    for (int j = 0; j < m_h; ++j) {
        float u = zonalWind(m_latN[index(0, j)]); // cellules/jour
        for (int i = 0; i < m_w; ++i) {
            float srcI = i - u * fd;
            float srcJ = j - 0.1f * fd; // legere derive
            m_hum[index(i, j)] = sampleBilinear(m_tmp, srcI, srcJ);
        }
    }

    // 3. Condensation / precipitation.
    for (int j = 0; j < m_h; ++j) {
        for (int i = 0; i < m_w; ++i) {
            int idx = index(i, j);
            float T = temperature(m_latN[idx], season, m_elev[idx]);
            float cap = 0.20f + 0.80f * T; // l'air chaud retient plus d'eau

            // Soulevement orographique : si l'air monte une pente face au vent.
            float u = zonalWind(m_latN[idx]);
            int upI = (i - (u > 0 ? 1 : -1)) % m_w; if (upI < 0) upI += m_w;
            float slope = m_elev[idx] - m_elev[index(upI, j)];
            float oro = std::max(0.0f, slope) * m_hum[idx] * 3.0f;

            // Ceinture subtropicale seche (~30 deg) : air descendant.
            float a = std::abs(m_latN[idx]);
            float subsid = std::exp(-((a - 0.34f) * (a - 0.34f)) / 0.012f);

            float excess = std::max(0.0f, m_hum[idx] - cap);
            float precip = (excess * 0.5f + oro) * (1.0f - 0.7f * subsid) * fd;
            precip = std::min(precip, m_hum[idx]);
            m_hum[idx] -= precip;

            // Pluie lissee (memoire de quelques jours).
            float wet = std::clamp(precip * 12.0f, 0.0f, 1.0f);
            float blend = std::clamp(0.25f * fd, 0.0f, 1.0f);
            m_rain[idx] = glm::mix(m_rain[idx], wet, blend);

            // Nuages : air proche de la saturation + precipitation active.
            float sat = std::clamp((m_hum[idx] / cap - 0.55f) / 0.45f, 0.0f, 1.0f);
            m_cloud[idx] = std::clamp(sat * 0.7f + wet * 0.6f, 0.0f, 1.0f);
        }
    }
}

float Climate::rainfallAt(const glm::vec3& dir) const {
    if (m_w == 0) return 0.3f;
    float lat = std::asin(std::clamp(dir.y, -1.0f, 1.0f));
    float lon = std::atan2(dir.z, dir.x);
    float fi = ((lon + static_cast<float>(kPI)) / (2.0f * static_cast<float>(kPI))) * m_w - 0.5f;
    float fj = ((lat + static_cast<float>(kPI) * 0.5f) / static_cast<float>(kPI)) * m_h - 0.5f;
    return sampleBilinear(m_rain, fi, fj);
}

} // namespace wl
