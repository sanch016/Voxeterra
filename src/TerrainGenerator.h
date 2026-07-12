#pragma once

#include "Block.h"
#include <FastNoiseLite.h>

struct TerrainParams {
    int   seed            = 12345;
    int   seaLevel        = 20;
    int   bedrockDepth    = 2;
    int   dirtDepth       = 8;
    int   minTerrainHeight = 5;
    int   maxTerrainHeight = 300;

    float continentalFreq = 0.001f;
    int   continentalOctaves = 6;
    float continentalWeight = 0.7f;

    float erosionFreq    = 0.002f;
    int   erosionOctaves = 5;
    float erosionWeight  = 0.5f;

    float peaksFreq      = 0.008f;
    int   peaksOctaves   = 5;
    float peaksWeight    = 0.5f;

    float temperatureFreq = 0.001f;
    int   temperatureOctaves = 4;

    float moistureFreq   = 0.001f;
    int   moistureOctaves = 4;

    float caveFreq       = 0.012f;
    float caveThreshold  = 0.58f;
    bool  cavesEnabled   = true;

    float oreFreq        = 0.08f;

    float snowLine       = 150.0f;
    float desertMaxTemp  = 0.3f;
    float desertMinMoist = 0.5f;

    float beachRange     = 1.5f;
};

class TerrainGenerator {
public:
    explicit TerrainGenerator(const TerrainParams& params = {})
        : m_params(params)
    {
        rebuild();
    }

    void setParams(const TerrainParams& params) {
        m_params = params;
        rebuild();
    }

    const TerrainParams& getParams() const { return m_params; }

    struct ColumnResult {
        int   surfaceHeight;
        float temperature;
        float moisture;
        float continentalness;
        float erosion;
    };

    ColumnResult sampleColumn(float worldX, float worldZ) const {
        float cn = continentalness(worldX, worldZ);
        float er = erosion(worldX, worldZ);
        float pk = peaks(worldX, worldZ);
        float temp = temperature(worldX, worldZ);
        float moist = moisture(worldX, worldZ);

        float normalizedHeight = cn * m_params.continentalWeight
                               + (1.0f - er) * m_params.erosionWeight
                               + pk * m_params.peaksWeight;

        normalizedHeight = normalizedHeight * 0.5f + 0.5f;
        if (normalizedHeight < 0.0f) normalizedHeight = 0.0f;
        if (normalizedHeight > 1.0f) normalizedHeight = 1.0f;

        int height = m_params.minTerrainHeight
                   + static_cast<int>(normalizedHeight
                   * (m_params.maxTerrainHeight - m_params.minTerrainHeight));

        return { height, temp, moist, cn, er };
    }

    BlockType getBlock(float worldX, float worldY, float worldZ) const {
        ColumnResult col = sampleColumn(worldX, worldZ);
        int surface = col.surfaceHeight;

        if (worldY > surface && worldY > m_params.seaLevel) return BlockType::Air;
        if (worldY > surface) return BlockType::Water;

        if (m_params.cavesEnabled && worldY > m_params.bedrockDepth && worldY < surface - 2) {
            float cv = caveNoise3D(worldX, worldY, worldZ);
            if (cv > m_params.caveThreshold) return BlockType::Air;
        }

        if (worldY <= m_params.bedrockDepth) return BlockType::Bedrock;

        float temp = col.temperature;
        float moist = col.moisture;

        if (worldY == surface) {
            return surfaceBlock(surface, temp, moist);
        } else if (worldY > surface - m_params.dirtDepth) {
            return subsurfaceBlock(surface, temp, moist);
        } else {
            return deepBlock(worldX, worldY, worldZ);
        }
    }

private:
    TerrainParams m_params;

    FastNoiseLite m_continentalNoise;
    FastNoiseLite m_erosionNoise;
    FastNoiseLite m_peaksNoise;
    FastNoiseLite m_temperatureNoise;
    FastNoiseLite m_moistureNoise;
    FastNoiseLite m_caveNoise;
    FastNoiseLite m_oreNoise;

    void rebuild() {
        setupNoise(m_continentalNoise, m_params.seed + 100,
                   m_params.continentalFreq, m_params.continentalOctaves,
                   FastNoiseLite::FractalType_FBm);

        setupNoise(m_erosionNoise, m_params.seed + 200,
                   m_params.erosionFreq, m_params.erosionOctaves,
                   FastNoiseLite::FractalType_Ridged);

        setupNoise(m_peaksNoise, m_params.seed + 300,
                   m_params.peaksFreq, m_params.peaksOctaves,
                   FastNoiseLite::FractalType_FBm);

        setupNoise(m_temperatureNoise, m_params.seed + 400,
                   m_params.temperatureFreq, m_params.temperatureOctaves,
                   FastNoiseLite::FractalType_FBm);

        setupNoise(m_moistureNoise, m_params.seed + 500,
                   m_params.moistureFreq, m_params.moistureOctaves,
                   FastNoiseLite::FractalType_FBm);

        setupNoise(m_caveNoise, m_params.seed + 600,
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

    float continentalness(float x, float z) const {
        return m_continentalNoise.GetNoise(x, z);
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

    float caveNoise3D(float x, float y, float z) const {
        float n1 = m_caveNoise.GetNoise(x, y, z);
        float n2 = m_caveNoise.GetNoise(x * 1.5f + 100, y * 1.5f + 100, z * 1.5f + 100);
        return (n1 + n2) * 0.5f;
    }

    float oreNoise3D(float x, float y, float z) const {
        return m_oreNoise.GetNoise(x, y, z);
    }

    BlockType surfaceBlock(int height, float temp, float moist) const {
        if (height <= m_params.seaLevel + static_cast<int>(m_params.beachRange))
            return BlockType::Sand;

        if (height >= static_cast<int>(m_params.snowLine))
            return BlockType::Snow;

        if (temp < m_params.desertMaxTemp && moist < m_params.desertMinMoist)
            return BlockType::Sand;

        if (temp > 0.7f && moist < 0.3f)
            return BlockType::Sand;

        if (height >= static_cast<int>(m_params.snowLine) - 5 && temp > 0.6f)
            return BlockType::Snow;

        return BlockType::Grass;
    }

    BlockType subsurfaceBlock(int surface, float temp, float /*moist*/) const {
        if (surface <= m_params.seaLevel + static_cast<int>(m_params.beachRange))
            return BlockType::Sand;

        if (temp < m_params.desertMaxTemp)
            return BlockType::Sand;

        return BlockType::Dirt;
    }

    BlockType deepBlock(float worldX, float worldY, float worldZ) const {
        float ore = oreNoise3D(worldX, worldY, worldZ);

        if (ore > 0.70f && worldY < 16)       return BlockType::DiamondOre;
        if (ore > 0.65f && worldY < 32)       return BlockType::GoldOre;
        if (ore > 0.60f && worldY < 48)       return BlockType::IronOre;
        if (ore > 0.55f)                       return BlockType::CoalOre;
        if (ore < -0.60f)                      return BlockType::Gravel;

        return BlockType::Stone;
    }
};
