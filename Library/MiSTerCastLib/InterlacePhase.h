#pragma once

#include "GroovyProtocol.h"

#include <cstdint>

namespace mistercast
{
class InterlacePhase
{
public:
    void Reset(bool interlaced) noexcept
    {
        m_interlaced = interlaced;
        m_phaseValid = false;
        m_fallbackSet = false;
        m_fallbackFrame = 0;
    }

    void Acknowledge(uint32_t sentFrame, uint32_t echoedFrame) noexcept
    {
        // Only a blit sent after the latest mode switch can establish phase.
        // The init ACK and late ACKs from the previous mode are not authoritative.
        if (m_interlaced && sentFrame == echoedFrame)
            m_phaseValid = true;
    }

    void Align(
        uint32_t& frame,
        uint8_t& field,
        uint32_t fpgaFrame,
        uint8_t fpgaField) noexcept
    {
        if (!m_interlaced)
        {
            field = 0;
            return;
        }

        if (m_phaseValid)
        {
            if (protocol::FrameAfter(fpgaFrame, frame))
                frame = fpgaFrame + 1;
            field = static_cast<uint8_t>((!fpgaField) ^ ((frame - fpgaFrame) & 1));
            return;
        }

        // A mode reset starts from field zero. Keep alternating locally until
        // the first matching post-switch ACK provides authoritative FPGA phase.
        if (!m_fallbackSet)
        {
            m_fallbackFrame = frame;
            m_fallbackSet = true;
        }
        field = static_cast<uint8_t>((frame - m_fallbackFrame) & 1);
    }

    bool IsValid() const noexcept
    {
        return !m_interlaced || m_phaseValid;
    }

private:
    bool m_interlaced = false;
    bool m_phaseValid = false;
    bool m_fallbackSet = false;
    uint32_t m_fallbackFrame = 0;
};
}
