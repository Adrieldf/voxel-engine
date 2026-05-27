#pragma once

#include <vector>
#include <numeric>
#include <random>
#include <algorithm>
#include <cmath>

class Noise {
public:
    explicit Noise(unsigned int seed = 1337) {
        reseed(seed);
    }

    void reseed(unsigned int seed) {
        p.resize(256);
        std::iota(p.begin(), p.end(), 0);
        
        std::default_random_engine engine(seed);
        std::shuffle(p.begin(), p.end(), engine);
        
        // Duplicate the permutation array
        p.insert(p.end(), p.begin(), p.end());
    }

    // 2D Perlin Noise in range [-1, 1]
    double noise(double x, double y) const {
        int X = static_cast<int>(std::floor(x)) & 255;
        int Y = static_cast<int>(std::floor(y)) & 255;

        x -= std::floor(x);
        y -= std::floor(y);

        double u = fade(x);
        double v = fade(y);

        int A = p[X] + Y;
        int B = p[X + 1] + Y;

        return lerp(v, lerp(u, grad(p[A], x, y), 
                               grad(p[B], x - 1, y)),
                       lerp(u, grad(p[A + 1], x, y - 1), 
                               grad(p[B + 1], x - 1, y - 1)));
    }

    // 2D Fractal Brownian Motion (fBm) multi-octave noise in range [-1, 1]
    double noiseOctaves(double x, double y, int octaves, double persistence = 0.5, double lacunarity = 2.0) const {
        double total = 0.0;
        double frequency = 1.0;
        double amplitude = 1.0;
        double maxValue = 0.0;  // Used to normalize the result

        for (int i = 0; i < octaves; ++i) {
            total += noise(x * frequency, y * frequency) * amplitude;
            maxValue += amplitude;
            amplitude *= persistence;
            frequency *= lacunarity;
        }

        return total / maxValue;
    }

private:
    std::vector<int> p;

    static double fade(double t) {
        return t * t * t * (t * (t * 6 - 15) + 10);
    }

    static double lerp(double t, double a, double b) {
        return a + t * (b - a);
    }

    static double grad(int hash, double x, double y) {
        // Convert low 3 bits of hash code into 8 gradient directions
        int h = hash & 7;
        double u = h < 4 ? x : y;
        double v = h < 4 ? y : x;
        return ((h & 1) ? -u : u) + ((h & 2) ? -2.0 * v : 2.0 * v);
    }
};
