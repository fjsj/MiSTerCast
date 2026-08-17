#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

enum class Rotation : uint8_t
{
    None,
    CW90,
    CCW90,
    Flip180
};

inline bool UsesInterlacedFieldBuffer(bool interlaced, bool progressiveFramebuffer) noexcept
{
    return interlaced && !progressiveFramebuffer;
}

inline size_t Rgb24FrameSize(
    uint32_t width,
    uint32_t activeHeight,
    bool interlaced,
    bool progressiveFramebuffer = false)
{
    const uint32_t outputHeight = UsesInterlacedFieldBuffer(interlaced, progressiveFramebuffer)
        ? activeHeight / 2
        : activeHeight;
    if (width == 0 || outputHeight == 0 || width > std::numeric_limits<size_t>::max() / outputHeight / 3)
        return 0;

    return static_cast<size_t>(width) * outputHeight * 3;
}

inline bool TransformBgraToRgb24(
    const uint8_t* source,
    uint32_t sourceWidth,
    uint32_t sourceHeight,
    uint32_t sourceStride,
    uint32_t outputWidth,
    uint32_t activeHeight,
    bool interlaced,
    bool progressiveFramebuffer,
    uint8_t field,
    Rotation rotation,
    uint8_t* destination,
    size_t destinationSize)
{
    const bool fieldBuffer = UsesInterlacedFieldBuffer(interlaced, progressiveFramebuffer);
    const size_t requiredSize = Rgb24FrameSize(
        outputWidth, activeHeight, interlaced, progressiveFramebuffer);
    if (source == nullptr || destination == nullptr || sourceWidth == 0 || sourceHeight == 0 ||
        sourceStride < sourceWidth * 4 || requiredSize == 0 || destinationSize < requiredSize)
        return false;

    const uint32_t outputHeight = fieldBuffer ? activeHeight / 2 : activeHeight;
    uint8_t* output = destination;
    for (uint32_t y = 0; y < outputHeight; ++y)
    {
        // Groovy_MiSTer maps protocol field 0 to display field 1 and field 1 to
        // display field 0. Sampling that parity here makes adjacent fields
        // reconstruct the original progressive source frame.
        const uint32_t fullY = fieldBuffer ? y * 2 + !(field & 1) : y;
        const double v = (static_cast<double>(fullY) + 0.5) / activeHeight;

        for (uint32_t x = 0; x < outputWidth; ++x)
        {
            const double u = (static_cast<double>(x) + 0.5) / outputWidth;
            double sourceU = u;
            double sourceV = v;
            switch (rotation)
            {
            case Rotation::CW90:
                sourceU = v;
                sourceV = 1.0 - u;
                break;
            case Rotation::CCW90:
                sourceU = 1.0 - v;
                sourceV = u;
                break;
            case Rotation::Flip180:
                sourceU = 1.0 - u;
                sourceV = 1.0 - v;
                break;
            case Rotation::None:
            default:
                break;
            }

            const uint32_t sourceX = std::min(sourceWidth - 1, static_cast<uint32_t>(sourceU * sourceWidth));
            const uint32_t sourceY = std::min(sourceHeight - 1, static_cast<uint32_t>(sourceV * sourceHeight));
            const uint8_t* pixel = source + static_cast<size_t>(sourceY) * sourceStride + sourceX * 4;
            *output++ = pixel[0];
            *output++ = pixel[1];
            *output++ = pixel[2];
        }
    }

    return true;
}
