#pragma once

#include <cmath>
#include <cstdint>
#include <algorithm>

namespace bm
{
//==============================================================================
/** Zero-delay-feedback state variable filter (Simper / Zavalishin). */
struct SVF
{
    float a1 = 1, a2 = 0, a3 = 0, k = 1.414f;
    float ic1 = 0, ic2 = 0;
    float lp = 0, bp = 0, hp = 0;

    void set (float freq, float q, float sampleRate)
    {
        freq = std::clamp (freq, 10.0f, 0.45f * sampleRate);
        const float g = std::tan (3.14159265f * freq / sampleRate);
        k  = 1.0f / std::max (q, 0.05f);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    void reset() { ic1 = ic2 = lp = bp = hp = 0; }

    void process (float x)
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        lp = v2;
        bp = v1;
        hp = x - k * v1 - v2;
    }
};

//==============================================================================
/** Linear gain ramp, used for click-free switching. */
struct Ramp
{
    float value = 0, target = 0, step = 0.01f;

    void setTime (float seconds, float sampleRate) { step = 1.0f / std::max (1.0f, seconds * sampleRate); }
    void setTarget (float t) { target = t; }
    void snap (float v) { value = target = v; }

    float next()
    {
        if (value < target)      value = std::min (target, value + step);
        else if (value > target) value = std::max (target, value - step);
        return value;
    }
};

//==============================================================================
/** Transparent up to +-1, then a soft knee towards +-2 (keeps feedback loops from running away). */
inline float softClip (float x)
{
    const float a = std::abs (x);
    if (a <= 1.0f)
        return x;
    const float over = a - 1.0f;
    return std::copysign (1.0f + over / (1.0f + over), x);
}

struct Noise
{
    uint32_t s = 0x1234567u;
    explicit Noise (uint32_t seed = 0x1234567u) : s (seed) {}

    float next()   // white noise, -1..1
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return (float) (int32_t) s * (1.0f / 2147483648.0f);
    }
};

/** 4-point cubic Hermite interpolation. */
inline float hermite (float xm1, float x0, float x1, float x2, float t)
{
    const float c0 = x0;
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + c0;
}

} // namespace bm
