#pragma once
#include "../dsp/channel/rx_vfo.h"
#include <gui/widgets/waterfall.h>
#include <utils/event.h>

class VFOManager {
public:
    VFOManager();

    class VFO {
    public:
        VFO(const std::string& name, int reference, double offset, double bandwidth, double sampleRate, double minBandwidth, double maxBandwidth, bool bandwidthLocked);
        ~VFO();

        void setOffset(double offset);
        double getOffset();
        void setCenterOffset(double offset);
        void setBandwidth(double bandwidth, bool updateWaterfall = true);
        void setSampleRate(double sampleRate, double bandwidth);
        void setReference(int ref);
        void setSnapInterval(double interval);
        void setBandwidthLimits(double minBandwidth, double maxBandwidth, bool bandwidthLocked);
        bool getBandwidthChanged(bool erase = true);
        double getBandwidth();
        int getReference();
        void setColor(ImU32 color);
        std::string getName();

        dsp::stream<dsp::complex_t>* output;

        friend class VFOManager;

        dsp::channel::RxVFO* dspVFO;
        ImGui::WaterfallVFO* wtfVFO;

    private:
        std::string name;
        double _bandwidth;

    };

    VFOManager::VFO* createVFO(const std::string& name, int reference, double offset, double bandwidth, double sampleRate, double minBandwidth, double maxBandwidth, bool bandwidthLocked);
    void deleteVFO(VFOManager::VFO* vfo);

    void setOffset(const std::string& name, double offset);
    double getOffset(const std::string& name);
    void setCenterOffset(const std::string& name, double offset);
    void setBandwidth(const std::string& name, double bandwidth, bool updateWaterfall = true);
    void setSampleRate(const std::string& name, double sampleRate, double bandwidth);
    void setReference(const std::string& name, int ref);
    void setBandwidthLimits(const std::string& name, double minBandwidth, double maxBandwidth, bool bandwidthLocked);
    bool getBandwidthChanged(const std::string& name, bool erase = true);
    double getBandwidth(const std::string& name);
    void setColor(const std::string& name, ImU32 color);
    std::string getName();
    int getReference(const std::string& name);
    bool vfoExists(const std::string& name);

    void updateFromWaterfall(ImGui::WaterFall* wtf);

    Event<VFOManager::VFO*> onVfoCreated;
    Event<VFOManager::VFO*> onVfoDelete;
    Event<const std::string&> onVfoDeleted;

private:
    std::map<std::string, VFO*> vfos;
};