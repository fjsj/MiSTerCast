#pragma once

#include "AudioProcessing.h"

// REFERENCE_TIME time units per second and per millisecond
#define REFTIMES_PER_SEC  10000000
#define REFTIMES_PER_MILLISEC  10000

std::atomic_int audioSampleRate;
int16_t* audioBuffer = nullptr;
std::vector<int16_t> audioCaptureScratch;
unsigned int AudioWritePos = 0;

// Audio Capture
REFERENCE_TIME hnsRequestedDuration = REFTIMES_PER_SEC;
UINT32 bufferFrameCount;
UINT32 numFramesAvailable;
IMMDeviceEnumerator *pEnumerator = NULL;
IMMDevice *pDevice = NULL;
IAudioClient *pAudioClient = NULL;
IAudioCaptureClient *pCaptureClient = NULL;
WAVEFORMATEX *pwfx = NULL;

bool InitAudioCapture()
{
    HRESULT hr;

    hr = CoInitialize(nullptr);
    EXIT_ON_ERROR(hr, "CoInitialize failed");

    hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator), NULL,
        CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
        (void**)&pEnumerator);
    EXIT_ON_ERROR(hr, "CoCreateInstance of MMDeviceEnumerator failed");

    hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
    EXIT_ON_ERROR(hr, "IMMDeviceEnumerator GetDefaultAudioEndpoint failed");

    hr = pDevice->Activate(
        __uuidof(IAudioClient), CLSCTX_ALL,
        NULL, (void**)&pAudioClient);
    EXIT_ON_ERROR(hr, "IMMDevice Activate failed");

    hr = pAudioClient->GetMixFormat(&pwfx);
    EXIT_ON_ERROR(hr, "IAudioClient GetMixFormat failed");
    audioSampleRate = pwfx->nSamplesPerSec;

    bool floatFormat = pwfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
    if (pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE && pwfx->cbSize >= 22)
    {
        const WAVEFORMATEXTENSIBLE* extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(pwfx);
        floatFormat = IsEqualGUID(extensible->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
    }
    if (!floatFormat || pwfx->wBitsPerSample != 32 || pwfx->nChannels == 0 ||
        pwfx->nBlockAlign < pwfx->nChannels * sizeof(float))
    {
        LogMessage("The default audio endpoint does not expose 32-bit floating-point loopback audio.", true);
        return false;
    }

    hr = pAudioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK,
        hnsRequestedDuration,
        0,
        pwfx,
        NULL);
    EXIT_ON_ERROR(hr, "IAudioClient Initialize failed");

    hr = pAudioClient->GetBufferSize(&bufferFrameCount);
    EXIT_ON_ERROR(hr, "IAudioClient  GetBufferSize failed");

    hr = pAudioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&pCaptureClient);
    EXIT_ON_ERROR(hr, "IAudioClient GetService failed");

    return true;
}

void CleanupAudioCatpure()
{
    CoTaskMemFree(pwfx);
    SAFE_RELEASE(pEnumerator)
    SAFE_RELEASE(pDevice)
    SAFE_RELEASE(pAudioClient)
    SAFE_RELEASE(pCaptureClient)
}

bool StartAudioCapture()
{
    HRESULT hr = pAudioClient->Start();
    EXIT_ON_ERROR(hr, "IAudioClient Start failed");

    return true;
}

bool StopAudioCapture()
{

    HRESULT hr = pAudioClient->Stop();
    EXIT_ON_ERROR(hr, "IAudioCaptureClient Stop failed");

     return true;
}

bool TickAudioCapture(bool writeOutput = true)
{
    audioCaptureScratch.clear();
    AudioWritePos = 0;
    UINT32 packetLength = 0;
    HRESULT hr = pCaptureClient->GetNextPacketSize(&packetLength);
    EXIT_ON_ERROR(hr, "IAudioCaptureClient GetNextPacketSize failed");

    while (packetLength != 0)
    {
        // Get the available data in the shared buffer.
        BYTE *pData;
        DWORD flags;
        hr = pCaptureClient->GetBuffer(
            &pData,
            &numFramesAvailable,
            &flags, NULL, NULL);
        EXIT_ON_ERROR(hr, "IAudioCaptureClient GetBuffer failed");

        const bool silence = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
        // WASAPI is polled once per rendered frame, so forward the accumulated
        // samples immediately instead of adding the Linux PulseAudio prebuffer.
        // If rendering stalled long enough to exceed the protocol's 16-bit byte
        // count, retain the newest audio so latency cannot grow without bound.
        const size_t framesToKeep = std::min<size_t>(numFramesAvailable, mistercast::MaxAudioValuesPerCommand / 2);
        const size_t stereoValues = framesToKeep * 2;
        if (audioCaptureScratch.size() + stereoValues > mistercast::MaxAudioValuesPerCommand)
        {
            const size_t excess = audioCaptureScratch.size() + stereoValues - mistercast::MaxAudioValuesPerCommand;
            std::move(audioCaptureScratch.begin() + excess, audioCaptureScratch.end(), audioCaptureScratch.begin());
            audioCaptureScratch.resize(audioCaptureScratch.size() - excess);
        }

        const size_t writeOffset = audioCaptureScratch.size();
        audioCaptureScratch.resize(writeOffset + stereoValues);
        if (silence)
        {
            std::fill(audioCaptureScratch.begin() + writeOffset, audioCaptureScratch.end(), 0);
        }
        else
        {
            const float* samples = reinterpret_cast<const float*>(pData) +
                static_cast<size_t>(numFramesAvailable - framesToKeep) * pwfx->nChannels;
            if (!mistercast::ConvertFloatFramesToStereo(
                samples,
                framesToKeep,
                pwfx->nChannels,
                audioCaptureScratch.data() + writeOffset,
                stereoValues))
            {
                pCaptureClient->ReleaseBuffer(numFramesAvailable);
                LogMessage("Unable to convert captured audio to stereo PCM.", true);
                return false;
            }
        }

        hr = pCaptureClient->ReleaseBuffer(numFramesAvailable);
        EXIT_ON_ERROR(hr, "IAudioCaptureClient ReleaseBuffer failed");

        hr = pCaptureClient->GetNextPacketSize(&packetLength);
        EXIT_ON_ERROR(hr, "IAudioCaptureClient GetNextPacketSize failed");
    }

    if (writeOutput && audioBuffer != nullptr && !audioCaptureScratch.empty())
    {
        AudioWritePos = static_cast<unsigned int>(audioCaptureScratch.size());
        std::memcpy(audioBuffer, audioCaptureScratch.data(), AudioWritePos * sizeof(int16_t));
    }

    return true;
}
