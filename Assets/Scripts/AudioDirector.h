#pragma once

#include "Shipfulness.h"
#include "Engine/Audio/AudioService.h"

#include <cstring>
#include <filesystem>
#include <map>
#include <random>

namespace ship {

inline std::wstring AudioPath(const char* name) {
    std::filesystem::path p = std::filesystem::path(strace::SceneService::ProjectRoot()) / L"Assets" / L"Audio";
    p /= std::wstring(name, name + std::strlen(name)) + L".ogg";
    return p.wstring();
}

struct Pcm {
    std::vector<float> samples;
    int channels = 2;
    int rate = 44100;
};

inline std::shared_ptr<Pcm> LoadPcm(const char* name) {
    auto dec = strace::OpenAudioDecoder(AudioPath(name));
    if (!dec) return {};
    auto pcm = std::make_shared<Pcm>();
    pcm->channels = std::max(1, dec->Channels());
    pcm->rate = dec->SampleRate();
    std::vector<float> buf(4096 * static_cast<size_t>(pcm->channels));
    for (;;) {
        int got = dec->Read(buf.data(), 4096);
        if (got <= 0) break;
        pcm->samples.insert(pcm->samples.end(), buf.begin(), buf.begin() + static_cast<size_t>(got) * pcm->channels);
    }
    if (pcm->samples.empty()) return {};
    return pcm;
}

inline strace::AudioClipPtr EngineClip(const Pcm& pcm, bool silent) {
    std::vector<int16_t> data(pcm.samples.size(), 0);
    if (!silent)
        for (size_t i = 0; i < data.size(); ++i)
            data[i] = static_cast<int16_t>(Clamp(pcm.samples[i], -1.0f, 1.0f) * 32767.0f);
    return strace::MakeClipFromPcm16(data.data(), data.size(), pcm.channels, pcm.rate);
}

class LiveLoop {
public:
    void Start(const std::shared_ptr<Pcm>& source) {
        Stop();
        if (!source) return;
        m_source = source;
        m_clip = EngineClip(*source, true);
        if (!m_clip) return;
        m_voice = strace::AudioService::PlaySfx(m_clip, 0.0f, true);
        m_written = 0;
        m_last = -1;
        m_wraps = 0;
        m_gain = 0.0f;
    }

    void Stop() {
        if (m_voice != strace::kInvalidVoice) strace::AudioService::Stop(m_voice);
        m_voice = strace::kInvalidVoice;
        m_clip.reset();
    }

    void SetGain(float target) {
        if (m_voice == strace::kInvalidVoice || !m_clip) return;
        auto* clip = const_cast<strace::AudioClip*>(m_clip.get());
        int64_t frames = clip->Frames();
        if (frames <= 0) return;
        int64_t raw = strace::AudioService::Mixer().VoiceFrame(m_voice);
        if (raw < 0) return;
        int64_t p = raw % frames;
        if (m_last >= 0 && p < m_last) m_wraps++;
        m_last = p;
        int64_t now = m_wraps * frames + p;
        int64_t from = std::max(m_written, now + 512);
        int64_t to = now + clip->sampleRate / 8;
        if (to <= from) return;
        int ch = clip->channels;
        float span = static_cast<float>(to - from);
        for (int64_t f = from; f < to; ++f) {
            float g = Lerp(m_gain, target, static_cast<float>(f - from) / span);
            size_t idx = static_cast<size_t>(f % frames) * ch;
            for (int c = 0; c < ch; ++c) clip->samples[idx + c] = m_source->samples[idx + c] * g;
        }
        m_gain = target;
        m_written = to;
    }

private:
    std::shared_ptr<Pcm> m_source;
    strace::AudioClipPtr m_clip;
    strace::VoiceId m_voice = strace::kInvalidVoice;
    int64_t m_written = 0;
    int64_t m_last = -1;
    int64_t m_wraps = 0;
    float m_gain = 0.0f;
};

class AudioDirector {
public:
    void Start() {
        if (!strace::AudioService::Running()) strace::AudioService::Start();
        const char* names[] = {"ambient_water", "wake_loop", "engine_loop", "scrape_loop", "horn_long", "horn_short",
                               "bump_1", "bump_2", "bump_3", "crash", "creak_1", "creak_2", "creak_3", "splash",
                               "gulls_1", "gulls_2", "win_jingle", "fail_jingle", "ui_click", "ui_confirm"};
        for (const char* n : names) {
            auto pcm = LoadPcm(n);
            if (!pcm) continue;
            if (std::strstr(n, "_loop") || std::strcmp(n, "ambient_water") == 0) m_loops[n] = pcm;
            else m_clips[n] = EngineClip(*pcm, false);
        }
        ambient.Start(m_loops["ambient_water"]);
        wake.Start(m_loops["wake_loop"]);
        engine.Start(m_loops["engine_loop"]);
        scrape.Start(m_loops["scrape_loop"]);
        MusicOn(true);
        m_gullTimer = 6.0f;
    }

    void Stop() {
        ambient.Stop();
        wake.Stop();
        engine.Stop();
        scrape.Stop();
        strace::AudioService::StopAll();
        strace::AudioService::StopTrack();
        m_clips.clear();
        m_loops.clear();
    }

    void SetVolumes(float music, float sfx) {
        strace::AudioService::SetBusVolumeDb(strace::AudioBus::Music, music > 0.001f ? strace::GainToDecibels(music) : -80.0f);
        strace::AudioService::SetBusVolumeDb(strace::AudioBus::Sfx, sfx > 0.001f ? strace::GainToDecibels(sfx) : -80.0f);
    }

    void MusicOn(bool on) {
        m_music = on;
        if (on && !strace::AudioService::TrackPlaying())
            strace::AudioService::PlayTrackFile(AudioPath("music_loop"), -15.0f, true);
        if (!on) strace::AudioService::StopTrack();
    }
    bool Music() const { return m_music; }

    void Play(const std::string& name, float db = 0.0f) {
        auto it = m_clips.find(name);
        if (it != m_clips.end() && it->second) strace::AudioService::PlaySfx(it->second, db);
    }

    void PlayOneOf(const char* prefix, int count, float db) {
        Play(std::string(prefix) + std::to_string(1 + std::uniform_int_distribution<int>(0, count - 1)(m_rng)), db);
    }

    void Update(float dt, bool sailing, float speed, float throttle, float scrapeLevel, float turnStress) {
        ambient.SetGain(0.55f);
        engine.SetGain(sailing ? 0.12f + 0.55f * std::fabs(throttle) : 0.0f);
        wake.SetGain(Clamp(std::fabs(speed) / 6.0f, 0.0f, 1.0f) * 0.75f);
        scrape.SetGain(Clamp(scrapeLevel, 0.0f, 1.0f) * 0.9f);
        m_gullTimer -= dt;
        if (m_gullTimer <= 0.0f) {
            PlayOneOf("gulls_", 2, std::uniform_real_distribution<float>(-14.0f, -7.0f)(m_rng));
            m_gullTimer = std::uniform_real_distribution<float>(11.0f, 24.0f)(m_rng);
        }
        m_creakTimer -= dt;
        if (turnStress > 0.55f && m_creakTimer <= 0.0f) {
            PlayOneOf("creak_", 3, -10.0f + 6.0f * Clamp(turnStress - 0.55f, 0.0f, 1.0f));
            m_creakTimer = std::uniform_real_distribution<float>(2.5f, 5.5f)(m_rng);
        }
    }

    LiveLoop ambient, wake, engine, scrape;

private:
    std::map<std::string, strace::AudioClipPtr> m_clips;
    std::map<std::string, std::shared_ptr<Pcm>> m_loops;
    std::mt19937 m_rng{77};
    float m_gullTimer = 6.0f;
    float m_creakTimer = 3.0f;
    bool m_music = true;
};

}
