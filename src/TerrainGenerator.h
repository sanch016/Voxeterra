#pragma once

#include "Block.h"
#include <FastNoiseLite.h>
#include <algorithm>

inline float smoothstep(float edge0, float edge1, float x) {
    float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// ── Biome system (surface colouring only) ───────────────────────────────

enum class BiomeType : int {
    DeepOcean, Ocean, Beach,
    Plains, Desert, Forest, DarkForest,
    Taiga, SnowyPlains, Mountains, Swamp, Savanna,
    Badlands,
    COUNT
};

struct BiomeDef {
    BiomeType type;
    BlockType surfaceBlock;
    BlockType subBlock;
    BlockType shoreBlock;
    int    shoreWidth;
    float  snowChance;
};

inline const BiomeDef BIOME_DEFS[] = {
    {BiomeType::DeepOcean,   BlockType::Gravel, BlockType::Gravel, BlockType::Gravel, 0, 0.0f},
    {BiomeType::Ocean,       BlockType::Gravel, BlockType::Gravel, BlockType::Sand,   5, 0.0f},
    {BiomeType::Beach,       BlockType::Sand,   BlockType::Sand,   BlockType::Sand,   3, 0.0f},
    {BiomeType::Plains,      BlockType::Grass,  BlockType::Dirt,   BlockType::Sand,   3, 0.0f},
    {BiomeType::Desert,      BlockType::Sand,   BlockType::Sand,   BlockType::Sand,   2, 0.0f},
    {BiomeType::Forest,      BlockType::Grass,  BlockType::Dirt,   BlockType::Sand,   2, 0.0f},
    {BiomeType::DarkForest,  BlockType::Grass,  BlockType::Dirt,   BlockType::Sand,   2, 0.0f},
    {BiomeType::Taiga,       BlockType::Grass,  BlockType::Dirt,   BlockType::Snow,   1, 0.4f},
    {BiomeType::SnowyPlains, BlockType::Snow,   BlockType::Dirt,   BlockType::Snow,   2, 0.7f},
    {BiomeType::Mountains,   BlockType::Stone,  BlockType::Stone,  BlockType::Gravel, 4, 0.6f},
    {BiomeType::Swamp,       BlockType::Grass,  BlockType::Dirt,   BlockType::Clay,   4, 0.0f},
    {BiomeType::Savanna,     BlockType::Grass,  BlockType::Dirt,   BlockType::Sand,   2, 0.0f},
    {BiomeType::Badlands,    BlockType::Gravel, BlockType::Sand,   BlockType::Gravel, 6, 0.0f},
};

static_assert(sizeof(BIOME_DEFS) / sizeof(BIOME_DEFS[0]) == (int)BiomeType::COUNT,
    "BIOME_DEFS size mismatch");

// ── Terrain parameters ─────────────────────────────────────────────────

struct TerrainParams {
    int   seed            = 12345;
    int   seaLevel        = 24;
    int   bedrockDepth    = 2;
    int   dirtDepth       = 5;

    float continentalFreq = 0.0006f;
    int   continentalOctaves = 5;

    float erosionFreq    = 0.0012f;
    int   erosionOctaves = 4;

    float peaksFreq      = 0.003f;
    int   peaksOctaves   = 4;

    float tempFreq       = 0.0008f;
    float moistFreq      = 0.0009f;

    float detailFreq     = 0.02f;
    float detailAmp      = 1.0f;

    float caveFreq       = 0.014f;
    float caveThreshold  = 0.55f;
    bool  cavesEnabled   = true;

    float oreFreq        = 0.07f;
};

// ── Terrain generator ──────────────────────────────────────────────────

class TerrainGenerator {
public:
    explicit TerrainGenerator(const TerrainParams& params = {})
        : m_params(params) { rebuild(); }

    void setParams(const TerrainParams& params) { m_params = params; rebuild(); }
    const TerrainParams& getParams() const { return m_params; }

    struct ColumnResult {
        int   surfaceHeight;
        float temperature;
        float moisture;
        float continentalness;
        float erosion;
    };

    ColumnResult sampleColumn(float wx, float wz) const {
        float cn  = continentalness(wx, wz);
        float er  = erosion(wx, wz);
        float pk  = peaks(wx, wz);
        float tmp = temperature(wx, wz);
        float moi = moisture(wx, wz);
        float det = detail(wx, wz);

        float bh = baseHeight(cn, er);
        float hs = hillScale(cn, er);
        float h  = bh + hs * pk + det * m_params.detailAmp;

        int height = std::max(1, static_cast<int>(std::round(h)));
        return { height, tmp, moi, cn, er };
    }

    BlockType getBlock(float wx, float wy, float wz) const {
        ColumnResult col = sampleColumn(wx, wz);
        int surface = col.surfaceHeight;
        int sea = m_params.seaLevel;

        if (wy > surface) {
            if (wy <= sea) return BlockType::Water;
            return BlockType::Air;
        }

        if (wy <= m_params.bedrockDepth) return BlockType::Bedrock;

        if (m_params.cavesEnabled && wy > m_params.bedrockDepth && wy < surface - 3) {
            float cv = caveNoise(wx, wy, wz);
            if (cv > m_params.caveThreshold) return BlockType::Air;
        }

        float tmp = col.temperature;
        float moi = col.moisture;
        float cn  = col.continentalness;
        BiomeType biome = getBiomeAt(tmp, moi, cn);
        const BiomeDef& def = BIOME_DEFS[(int)biome];

        int shoreW = def.shoreWidth;
        bool nearWater = surface <= sea + shoreW;
        int depthFromSurface = surface - static_cast<int>(wy);

        if (depthFromSurface == 0) {
            if (def.snowChance > 0.0f && wy >= sea + 8 && tmp < 0.35f)
                return BlockType::Snow;
            if (nearWater && !isBlockSolid(def.shoreBlock))
                return def.shoreBlock;
            if (def.shoreBlock != def.surfaceBlock && surface <= sea + shoreW)
                return def.shoreBlock;
            return def.surfaceBlock;
        }

        if (depthFromSurface <= m_params.dirtDepth) {
            return def.subBlock;
        }

        return deepBlock(wx, wy, wz);
    }

    BiomeType getBiomeAt(float temp, float moist, float cont) const {
        if (cont < 0.35f) {
            if (cont < 0.15f) return BiomeType::DeepOcean;
            return BiomeType::Ocean;
        }
        if (cont < 0.40f) return BiomeType::Beach;

        if (temp < 0.25f) {
            return (cont > 0.70f) ? BiomeType::Mountains : BiomeType::SnowyPlains;
        }
        if (temp < 0.45f) {
            if (moist > 0.5f) return BiomeType::Taiga;
            if (cont > 0.65f) return BiomeType::Mountains;
            return BiomeType::Plains;
        }
        if (temp < 0.60f) {
            if (moist > 0.55f) return BiomeType::DarkForest;
            if (moist > 0.35f) return BiomeType::Forest;
            if (cont > 0.60f) return BiomeType::Mountains;
            return BiomeType::Plains;
        }
        if (temp < 0.80f) {
            if (moist > 0.65f) return BiomeType::Swamp;
            if (moist < 0.25f) return BiomeType::Savanna;
            return BiomeType::Plains;
        }
        if (moist < 0.2f) return BiomeType::Desert;
        if (moist > 0.6f) return BiomeType::Swamp;
        if (cont > 0.55f) return BiomeType::Badlands;
        return BiomeType::Desert;
    }

private:
    TerrainParams m_params;

    FastNoiseLite m_continentalNoise;
    FastNoiseLite m_erosionNoise;
    FastNoiseLite m_peaksNoise;
    FastNoiseLite m_temperatureNoise;
    FastNoiseLite m_moistureNoise;
    FastNoiseLite m_detailNoise;
    FastNoiseLite m_caveNoise;
    FastNoiseLite m_caveNoise2;
    FastNoiseLite m_oreNoise;

    void rebuild() {
        setupNoise(m_continentalNoise, m_params.seed + 100,
                   m_params.continentalFreq, m_params.continentalOctaves,
                   FastNoiseLite::FractalType_FBm);
        setupNoise(m_erosionNoise, m_params.seed + 200,
                   m_params.erosionFreq, m_params.erosionOctaves,
                   FastNoiseLite::FractalType_FBm);
        setupNoise(m_peaksNoise, m_params.seed + 300,
                   m_params.peaksFreq, m_params.peaksOctaves,
                   FastNoiseLite::FractalType_FBm);
        setupNoise(m_temperatureNoise, m_params.seed + 400,
                   m_params.tempFreq, 4,
                   FastNoiseLite::FractalType_FBm);
        setupNoise(m_moistureNoise, m_params.seed + 500,
                   m_params.moistFreq, 4,
                   FastNoiseLite::FractalType_FBm);
        setupNoise(m_detailNoise, m_params.seed + 550,
                   m_params.detailFreq, 3,
                   FastNoiseLite::FractalType_FBm);
        setupNoise(m_caveNoise, m_params.seed + 600,
                   m_params.caveFreq, 2,
                   FastNoiseLite::FractalType_FBm,
                   FastNoiseLite::NoiseType_OpenSimplex2S);
        setupNoise(m_caveNoise2, m_params.seed + 650,
                   m_params.caveFreq, 2,
                   FastNoiseLite::FractalType_FBm,
                   FastNoiseLite::NoiseType_OpenSimplex2S);
        setupNoise(m_oreNoise, m_params.seed + 700,
                   m_params.oreFreq, 1,
                   FastNoiseLite::FractalType_None,
                   FastNoiseLite::NoiseType_Cellular);
    }

    static void setupNoise(FastNoiseLite& n, int seed, float freq, int octaves,
                           FastNoiseLite::FractalType fractal,
                           FastNoiseLite::NoiseType noiseType = FastNoiseLite::NoiseType_OpenSimplex2) {
        n.SetSeed(seed);
        n.SetNoiseType(noiseType);
        n.SetFractalType(fractal);
        n.SetFractalOctaves(octaves);
        n.SetFrequency(freq);
    }

    float baseHeight(float cn, float er) const {
        struct Point { float cn, h; };
        static const Point pts[] = {
            {0.00f,  3.f},
            {0.15f,  5.f},
            {0.28f, 17.f},
            {0.35f, 24.f},
            {0.42f, 32.f},
            {0.55f, 42.f},
            {0.70f, 55.f},
            {0.85f, 72.f},
            {1.00f, 85.f},
        };
        constexpr int n = sizeof(pts) / sizeof(pts[0]);

        if (cn <= pts[0].cn) return pts[0].h;
        if (cn >= pts[n - 1].cn) return pts[n - 1].h;

        for (int i = 0; i < n - 1; ++i) {
            if (cn >= pts[i].cn && cn <= pts[i + 1].cn) {
                float st = smoothstep(pts[i].cn, pts[i + 1].cn, cn);
                float h = pts[i].h + st * (pts[i + 1].h - pts[i].h);
                h *= (1.0f - 0.08f * er);
                return h;
            }
        }
        return pts[n - 1].h;
    }

    float hillScale(float cn, float er) const {
        float cnHill = smoothstep(0.28f, 0.65f, cn);
        float erEffect = 1.0f - 0.55f * er;
        return cnHill * erEffect * 55.0f;
    }

    float continentalness(float x, float z) const {
        return m_continentalNoise.GetNoise(x, z) * 0.5f + 0.5f;
    }
    float erosion(float x, float z) const {
        return m_erosionNoise.GetNoise(x, z) * 0.5f + 0.5f;
    }
    float peaks(float x, float z) const {
        return m_peaksNoise.GetNoise(x, z) * 0.5f + 0.5f;
    }
    float temperature(float x, float z) const {
        return m_temperatureNoise.GetNoise(x, z) * 0.5f + 0.5f;
    }
    float moisture(float x, float z) const {
        return m_moistureNoise.GetNoise(x, z) * 0.5f + 0.5f;
    }
    float detail(float x, float z) const {
        return m_detailNoise.GetNoise(x, z);
    }

    float caveNoise(float wx, float wy, float wz) const {
        float n1 = m_caveNoise.GetNoise(wx, wy, wz);
        float n2 = m_caveNoise2.GetNoise(wx * 1.3f + 200, wy * 1.3f + 200, wz * 1.3f + 200);
        float n3 = m_caveNoise.GetNoise(wx * 0.7f - 50, wy * 0.7f - 50, wz * 0.7f - 50);
        return (n1 * 0.5f + n2 * 0.3f + n3 * 0.2f);
    }

    BlockType deepBlock(float wx, float wy, float wz) const {
        float ore = m_oreNoise.GetNoise(wx, wy, wz);

        if (wy < 16) {
            if (ore > 0.72f) return BlockType::DiamondOre;
            if (ore > 0.66f) return BlockType::GoldOre;
            if (ore > 0.58f) return BlockType::IronOre;
            if (ore > 0.52f) return BlockType::CoalOre;
        } else if (wy < 32) {
            if (ore > 0.68f) return BlockType::GoldOre;
            if (ore > 0.60f) return BlockType::IronOre;
            if (ore > 0.54f) return BlockType::CoalOre;
        } else if (wy < 48) {
            if (ore > 0.62f) return BlockType::IronOre;
            if (ore > 0.55f) return BlockType::CoalOre;
        } else {
            if (ore > 0.58f) return BlockType::CoalOre;
        }

        if (ore < -0.60f) return BlockType::Gravel;
        if (ore < -0.75f && wy > 4) return BlockType::Sand;

        return BlockType::Stone;
    }
};
