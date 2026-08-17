#include "FrameTransform.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace
{
int failures = 0;

void Check(bool condition, const char* expression, int line)
{
    if (!condition)
    {
        std::cerr << "line " << line << ": check failed: " << expression << '\n';
        ++failures;
    }
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

std::vector<uint8_t> GrayFrame(uint32_t width, uint32_t height, const std::vector<uint8_t>& values)
{
    std::vector<uint8_t> frame(static_cast<size_t>(width) * height * 4);
    for (size_t i = 0; i < values.size(); ++i)
    {
        frame[i * 4] = values[i];
        frame[i * 4 + 1] = values[i];
        frame[i * 4 + 2] = values[i];
        frame[i * 4 + 3] = 255;
    }
    return frame;
}

std::vector<uint8_t> PerPixel(const std::vector<uint8_t>& rgb)
{
    std::vector<uint8_t> pixels;
    for (size_t i = 0; i < rgb.size(); i += 3)
        pixels.push_back(rgb[i]);
    return pixels;
}

void CheckRotation(Rotation rotation, uint32_t outputWidth, uint32_t outputHeight, const std::vector<uint8_t>& expected)
{
    const auto frame = GrayFrame(2, 3, {0, 1, 2, 3, 4, 5});
    std::vector<uint8_t> rgb(Rgb24FrameSize(outputWidth, outputHeight, false));
    CHECK(TransformBgraToRgb24(
        frame.data(), 2, 3, 8, outputWidth, outputHeight, false, false, 0, rotation, rgb.data(), rgb.size()));
    CHECK(PerPixel(rgb) == expected);
}

void RotationTests()
{
    CheckRotation(Rotation::None, 2, 3, {0, 1, 2, 3, 4, 5});
    CheckRotation(Rotation::Flip180, 2, 3, {5, 4, 3, 2, 1, 0});
    CheckRotation(Rotation::CW90, 3, 2, {4, 2, 0, 5, 3, 1});
    CheckRotation(Rotation::CCW90, 3, 2, {1, 3, 5, 0, 2, 4});
    CheckRotation(static_cast<Rotation>(200), 2, 3, {0, 1, 2, 3, 4, 5});
}

void InterlaceTests()
{
    const auto frame = GrayFrame(2, 4, {0, 1, 2, 3, 4, 5, 6, 7});
    std::vector<uint8_t> rgb(Rgb24FrameSize(2, 4, true));

    CHECK(TransformBgraToRgb24(
        frame.data(), 2, 4, 8, 2, 4, true, false, 0, Rotation::None, rgb.data(), rgb.size()));
    CHECK(PerPixel(rgb) == std::vector<uint8_t>({2, 3, 6, 7}));

    CHECK(TransformBgraToRgb24(
        frame.data(), 2, 4, 8, 2, 4, true, false, 1, Rotation::None, rgb.data(), rgb.size()));
    CHECK(PerPixel(rgb) == std::vector<uint8_t>({0, 1, 4, 5}));

    std::vector<uint8_t> progressiveRgb(Rgb24FrameSize(2, 4, true, true));
    CHECK(progressiveRgb.size() == 24);
    CHECK(TransformBgraToRgb24(
        frame.data(), 2, 4, 8, 2, 4, true, true, 1,
        Rotation::None, progressiveRgb.data(), progressiveRgb.size()));
    CHECK(PerPixel(progressiveRgb) == std::vector<uint8_t>({0, 1, 2, 3, 4, 5, 6, 7}));
}

void BoundsAndChannelTests()
{
    const std::vector<uint8_t> frame = {7, 8, 9, 255};
    const size_t required = Rgb24FrameSize(1, 1, false);
    std::vector<uint8_t> guarded(required + 4, 0xcd);
    CHECK(TransformBgraToRgb24(
        frame.data(), 1, 1, 4, 1, 1, false, false, 0, Rotation::None, guarded.data(), required));
    CHECK(guarded[0] == 7 && guarded[1] == 8 && guarded[2] == 9);
    CHECK(guarded[required] == 0xcd && guarded.back() == 0xcd);

    CHECK(!TransformBgraToRgb24(
        frame.data(), 1, 1, 4, 1, 1, false, false, 0, Rotation::None, guarded.data(), required - 1));
    CHECK(!TransformBgraToRgb24(
        frame.data(), 1, 1, 3, 1, 1, false, false, 0, Rotation::None, guarded.data(), required));
    CHECK(Rgb24FrameSize(1, 1, true) == 0);
}
}

int RunAudioProcessingTests();
int RunCaptureResourceTests();
int RunDiagnosticFaultTests();
int RunGroovyProtocolTests();
int RunInterlacePhaseTests();
int RunStreamingTimingTests();
int RunTransportLifecycleTests();
int RunSourceOptionsStateTests();

int main()
{
    RotationTests();
    InterlaceTests();
    BoundsAndChannelTests();
    failures += RunAudioProcessingTests();
    failures += RunCaptureResourceTests();
    failures += RunDiagnosticFaultTests();
    failures += RunGroovyProtocolTests();
    failures += RunInterlacePhaseTests();
    failures += RunStreamingTimingTests();
    failures += RunTransportLifecycleTests();
    failures += RunSourceOptionsStateTests();

    if (failures != 0)
    {
        std::cerr << failures << " core test(s) failed\n";
        return 1;
    }

    std::cout << "All core tests passed\n";
    return 0;
}
