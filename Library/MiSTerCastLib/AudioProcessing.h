#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace mistercast
{
constexpr size_t MaxAudioValuesPerCommand = 32000;

inline int16_t FloatToPcm16(float sample) noexcept
{
    if (!std::isfinite(sample))
        return 0;
    if (sample <= -1.0f)
        return std::numeric_limits<int16_t>::min();
    if (sample >= 1.0f)
        return std::numeric_limits<int16_t>::max();
    return static_cast<int16_t>(std::lround(sample * std::numeric_limits<int16_t>::max()));
}

inline bool ConvertFloatFramesToStereo(
    const float* source,
    size_t frames,
    uint16_t channels,
    int16_t* destination,
    size_t destinationValues) noexcept
{
    if (source == nullptr || destination == nullptr || channels == 0 ||
        frames > std::numeric_limits<size_t>::max() / 2 || destinationValues < frames * 2)
        return false;

    for (size_t frame = 0; frame < frames; ++frame)
    {
        const float* input = source + frame * channels;
        destination[frame * 2] = FloatToPcm16(input[0]);
        destination[frame * 2 + 1] = FloatToPcm16(input[channels > 1 ? 1 : 0]);
    }
    return true;
}
}
