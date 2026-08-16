#include "InterlacePhase.h"

#include <cstdint>
#include <limits>

namespace
{
int failures = 0;

void Check(bool condition)
{
    if (!condition)
        ++failures;
}
}

int RunInterlacePhaseTests()
{
    using mistercast::InterlacePhase;

    InterlacePhase phase;
    uint32_t frame = 5;
    uint8_t field = 1;
    phase.Reset(false);
    phase.Align(frame, field, 99, 1);
    Check(frame == 5 && field == 0 && phase.IsValid());

    phase.Reset(true);
    frame = 40;
    field = 1;
    phase.Align(frame, field, 99, 1);
    Check(frame == 40 && field == 0 && !phase.IsValid());
    for (uint32_t next = 41; next <= 44; ++next)
    {
        frame = next;
        phase.Align(frame, field, 99, 1);
        Check(field == ((next - 40) & 1));
    }

    // Init or stale-mode status must not establish the new mode's phase.
    phase.Acknowledge(44, 43);
    Check(!phase.IsValid());
    phase.Acknowledge(44, 44);
    Check(phase.IsValid());

    // FPGA feedback may pull the sender forward after a dropped frame. Field
    // selection follows that authoritative frame/field pair rather than merely
    // toggling the previous local field.
    frame = 41;
    field = 1;
    phase.Align(frame, field, 42, 0);
    Check(frame == 43 && field == 0);
    frame = 44;
    phase.Align(frame, field, 43, 1);
    Check(frame == 44 && field == 1);
    frame = 45;
    phase.Align(frame, field, 43, 1);
    Check(frame == 45 && field == 0);

    // A second switch discards the old phase and starts a fresh local field-zero
    // sequence until a new blit is acknowledged.
    phase.Reset(true);
    frame = 46;
    field = 1;
    phase.Align(frame, field, 100, 1);
    Check(frame == 46 && field == 0 && !phase.IsValid());

    // Serial-number ordering must continue correctly through uint32 wraparound.
    phase.Acknowledge(frame, frame);
    frame = 0;
    field = 1;
    phase.Align(frame, field, std::numeric_limits<uint32_t>::max(), 0);
    Check(frame == 0 && field == 0);

    return failures;
}
