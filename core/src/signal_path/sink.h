#pragma once
#include <map>
#include <string>
#include <dsp/stream.h>
#include <dsp/types.h>
#include "../dsp/routing/splitter.h"
#include "../dsp/audio/volume.h"
#include "../dsp/sink/null_sink.h"
#include <mutex>
#include <utils/event.h>
#include <vector>

class SinkManager {
public:
    SinkManager();

    class Sink {
    public:
        virtual ~Sink() {}
        virtual void start() = 0;
        virtual void stop() = 0;
        virtual void menuHandler() = 0;
    };

    class Stream {
    public:
        Stream() {}
        Stream(dsp::stream<dsp::stereo_t>* in, EventHandler<float>* srChangeHandler, float sampleRate);

        void init(dsp::stream<dsp::stereo_t>* in, EventHandler<float>* srChangeHandler, float sampleRate);
        void shutdown();

        void start();
        void stop();

        void setVolume(float volume);
        float getVolume();

        void setSampleRate(float sampleRate);
        float getSampleRate();

        void setInput(dsp::stream<dsp::stereo_t>* in);

        dsp::stream<dsp::stereo_t>* bindStream();
        void unbindStream(dsp::stream<dsp::stereo_t>* stream);

        friend SinkManager;
        friend SinkManager::Sink;

        dsp::stream<dsp::stereo_t>* sinkOut;

        Event<float> srChange;

    private:
        dsp::stream<dsp::stereo_t>* _in;
        dsp::routing::Splitter<dsp::stereo_t> splitter;
        SinkManager::Sink* sink;
        dsp::stream<dsp::stereo_t> volumeInput;
        dsp::audio::Volume volumeAjust;
        std::mutex ctrlMtx;
        float _sampleRate;
        int providerId = 0;
        std::string providerName = "";
        bool running = false;

        float guiVolume = 1.0f;
    };

    struct SinkProvider {
        SinkManager::Sink* (*create)(SinkManager::Stream* stream, std::string streamName, void* ctx);
        void* ctx;
    };

    class NullSink : SinkManager::Sink {
    public:
        NullSink(SinkManager::Stream* stream) {
            ns.init(stream->sinkOut);
        }
        virtual ~NullSink() {
            ns.shutdown();
        }
        void start() { ns.start(); }
        void stop() { ns.stop(); }
        void menuHandler() {}

        static SinkManager::Sink* create(SinkManager::Stream* stream, std::string streamName, void* ctx) {
            stream->setSampleRate(48000);
            return new SinkManager::NullSink(stream);
        }

    private:
        dsp::sink::Null<dsp::stereo_t> ns;
    };

    void registerSinkProvider(const std::string& name, const SinkProvider& provider);
    void unregisterSinkProvider(const std::string& name);

    void registerStream(const std::string& name, Stream* stream);
    void unregisterStream(const std::string& name);

    void startStream(const std::string& name);
    void stopStream(const std::string& name);

    float getStreamSampleRate(const std::string& name);

    void setStreamSink(const std::string& name, const std::string& providerName);

    void showVolumeSlider(const std::string& name, const std::string& prefix, float width, float btnHeight = -1.0f, float btnBorder = 0, bool sameLine = false);

    dsp::stream<dsp::stereo_t>* bindStream(const std::string& name);
    void unbindStream(const std::string& name, dsp::stream<dsp::stereo_t>* stream);

    void loadSinksFromConfig();
    void showMenu();

    void shutdown();

    std::vector<std::string> getStreamNames();

    Event<std::string> onSinkProviderRegistered;
    Event<std::string> onSinkProviderUnregister;
    Event<std::string> onSinkProviderUnregistered;

    Event<std::string> onStreamRegistered;
    Event<std::string> onStreamUnregister;
    Event<std::string> onStreamUnregistered;

private:
    void loadStreamConfig(const std::string& name);
    void saveStreamConfig(const std::string& name);
    void refreshProviders();

    std::map<std::string, SinkProvider> providers;
    std::map<std::string, Stream*> streams;
    std::vector<std::string> providerNames;
    std::string providerNamesTxt;
    std::vector<std::string> streamNames;
};