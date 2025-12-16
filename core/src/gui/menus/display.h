#pragma once

namespace displaymenu {
    void init();
    void shutdown();
    void setViewBandwidthSlider(float bandwidth);
    void checkKeybinds();
    void draw(void* ctx);
}