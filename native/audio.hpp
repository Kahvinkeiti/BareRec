#pragma once
// BareRec WASAPI audio: capture speaker loopback and microphone as 48 kHz, stereo PCM16.
// The Windows audio engine performs format conversion in shared mode when supported.
// The video worker polls the streams and writes interleaved AAC samples to the same MP4.
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <ksmedia.h>
#include <deque>
#include <vector>
#include <cstdint>

#pragma comment(lib, "uuid.lib")

constexpr DWORD BareRecAudioRate = 48000;
constexpr int BareRecAudioChannels = 2;
constexpr int BareRecAudioChunkFrames = 960; // 20 ms

struct AudioEndpoint {
    Microsoft::WRL::ComPtr<IAudioClient> client;
    Microsoft::WRL::ComPtr<IAudioCaptureClient> capture;
    std::deque<int16_t> pending;
    bool enabled = false;

    void Open(IMMDeviceEnumerator* enumerator, EDataFlow flow, ERole role, bool loopback) {
        Microsoft::WRL::ComPtr<IMMDevice> device;
        Check(enumerator->GetDefaultAudioEndpoint(flow, role, &device), "Find audio device");
        Check(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
                               nullptr, reinterpret_cast<void**>(client.GetAddressOf())),
              "Activate audio device");

        WAVEFORMATEX format{};
        format.wFormatTag = WAVE_FORMAT_PCM;
        format.nChannels = BareRecAudioChannels;
        format.nSamplesPerSec = BareRecAudioRate;
        format.wBitsPerSample = 16;
        format.nBlockAlign = BareRecAudioChannels * sizeof(int16_t);
        format.nAvgBytesPerSec = format.nBlockAlign * BareRecAudioRate;
        DWORD flags = AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
        if (loopback) flags |= AUDCLNT_STREAMFLAGS_LOOPBACK;
        HRESULT hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 0, 0, &format, nullptr);
        Check(hr, loopback ? "Start system audio capture" : "Start microphone capture");
        Check(client->GetService(__uuidof(IAudioCaptureClient),
                                 reinterpret_cast<void**>(capture.GetAddressOf())),
              "Create audio capture service");
        Check(client->Start(), "Start audio client");
        enabled = true;
    }

    void Poll() {
        if (!enabled) return;
        UINT32 count = 0;
        Check(capture->GetNextPacketSize(&count), "Query audio packet");
        while (count > 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            HRESULT hr = capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
            Check(hr, "Read audio packet");
            if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) || !data) {
                pending.insert(pending.end(), static_cast<size_t>(frames) * BareRecAudioChannels, 0);
            } else {
                auto* samples = reinterpret_cast<const int16_t*>(data);
                pending.insert(pending.end(), samples,
                               samples + static_cast<size_t>(frames) * BareRecAudioChannels);
            }
            Check(capture->ReleaseBuffer(frames), "Release audio packet");
            Check(capture->GetNextPacketSize(&count), "Query next audio packet");
        }
        // Limit buffered samples to 2 seconds in case of video-encoding stalls.
        constexpr size_t maxSamples = BareRecAudioRate * BareRecAudioChannels * 2;
        while (pending.size() > maxSamples) pending.pop_front();
    }

    void Stop() {
        if (client && enabled) client->Stop();
        enabled = false;
    }
};

struct BareRecAudio {
    AudioEndpoint system;
    AudioEndpoint mic;
    bool active = false;
    DWORD streamIndex = 0;
    LONGLONG framesWritten = 0;

    void Initialize(IMFSinkWriter* writer, bool useSystem, bool useMic) {
        if (!useSystem && !useMic) return;
        Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator;
        Check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                               IID_PPV_ARGS(&enumerator)), "Initialize audio devices");
        if (useSystem) system.Open(enumerator.Get(), eRender, eConsole, true);
        if (useMic) mic.Open(enumerator.Get(), eCapture, eCommunications, false);

        Microsoft::WRL::ComPtr<IMFMediaType> output;
        Check(MFCreateMediaType(&output), "Create AAC media type");
        Check(output->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio), "Set AAC major type");
        Check(output->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC), "Set AAC subtype");
        Check(output->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, BareRecAudioRate), "Set AAC rate");
        Check(output->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, BareRecAudioChannels), "Set AAC channels");
        Check(output->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16), "Set AAC depth");
        Check(output->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 24000), "Set AAC 192 kbps bitrate");
        Check(writer->AddStream(output.Get(), &streamIndex), "Add AAC stream");

        Microsoft::WRL::ComPtr<IMFMediaType> input;
        Check(MFCreateMediaType(&input), "Create audio input type");
        Check(input->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio), "Set audio major type");
        Check(input->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM), "Set PCM input");
        Check(input->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, BareRecAudioRate), "Set PCM rate");
        Check(input->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, BareRecAudioChannels), "Set PCM channels");
        Check(input->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16), "Set PCM bits");
        Check(input->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4), "Set PCM block alignment");
        Check(input->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, BareRecAudioRate * 4), "Set PCM bytes/sec");
        Check(writer->SetInputMediaType(streamIndex, input.Get(), nullptr), "Configure AAC encoder");
        active = true;
    }

    void WriteChunk(IMFSinkWriter* writer) {
        std::vector<int16_t> samples(static_cast<size_t>(BareRecAudioChunkFrames) * BareRecAudioChannels);
        for (size_t i = 0; i < samples.size(); ++i) {
            int mixed = 0, sources = 0;
            if (system.enabled) {
                if (!system.pending.empty()) {
                    mixed += system.pending.front();
                    system.pending.pop_front();
                }
                ++sources;
            }
            if (mic.enabled) {
                if (!mic.pending.empty()) {
                    mixed += mic.pending.front();
                    mic.pending.pop_front();
                }
                ++sources;
            }
            // Preserve a healthy level while protecting against summed-source clipping.
            if (sources > 1) mixed = static_cast<int>(mixed * 0.65);
            samples[i] = static_cast<int16_t>(std::clamp(mixed, -32768, 32767));
        }
        DWORD bytes = static_cast<DWORD>(samples.size() * sizeof(int16_t));
        Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
        Check(MFCreateMemoryBuffer(bytes, &buffer), "Allocate audio buffer");
        BYTE* dst = nullptr;
        Check(buffer->Lock(&dst, nullptr, nullptr), "Lock audio buffer");
        std::memcpy(dst, samples.data(), bytes);
        buffer->Unlock();
        Check(buffer->SetCurrentLength(bytes), "Set audio buffer length");
        Microsoft::WRL::ComPtr<IMFSample> sample;
        Check(MFCreateSample(&sample), "Create audio sample");
        Check(sample->AddBuffer(buffer.Get()), "Attach audio buffer");
        const LONGLONG duration = static_cast<LONGLONG>(BareRecAudioChunkFrames) * 10000000LL / BareRecAudioRate;
        Check(sample->SetSampleTime(framesWritten * 10000000LL / BareRecAudioRate), "Set audio timestamp");
        Check(sample->SetSampleDuration(duration), "Set audio duration");
        Check(writer->WriteSample(streamIndex, sample.Get()), "Encode audio sample");
        framesWritten += BareRecAudioChunkFrames;
    }

    void WriteUntil(IMFSinkWriter* writer, LONGLONG targetHundredNs, bool flushing = false) {
        if (!active) return;
        system.Poll();
        mic.Poll();
        constexpr size_t chunkSamples = static_cast<size_t>(BareRecAudioChunkFrames) * BareRecAudioChannels;
        constexpr LONGLONG chunkDuration = 200000LL; // 20 ms in 100-ns units
        constexpr LONGLONG captureJitterAllowance = 1200000LL; // 120 ms
        // Audio packets arrive asynchronously from WASAPI. Do not pad normal packet
        // latency with silence: that produced regular gaps and severe distortion.
        // Keep a small latency budget so video and audio stay aligned in the file.
        while (framesWritten * 10000000LL / BareRecAudioRate + chunkDuration <= targetHundredNs) {
            const bool systemReady = !system.enabled || system.pending.size() >= chunkSamples;
            const bool micReady = !mic.enabled || mic.pending.size() >= chunkSamples;
            const LONGLONG audioCursor = framesWritten * 10000000LL / BareRecAudioRate;
            const bool overdue = flushing || (targetHundredNs - audioCursor > captureJitterAllowance);
            if (!overdue && (!systemReady || !micReady)) break;
            // In true device silence, Windows may emit no loopback packets at all.
            // Only overdue chunks may be padded, rather than every pending chunk.
            WriteChunk(writer);
        }
    }
    void Stop() { system.Stop(); mic.Stop(); }
};
