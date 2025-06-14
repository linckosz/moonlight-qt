#ifndef VIDEOENHANCEMENT_H
#define VIDEOENHANCEMENT_H

#include <string>

#pragma once

class VideoEnhancement
{

private:

    static VideoEnhancement* instance;

    bool m_Enabled = false;
    bool m_UIvisible = false;
    bool m_ForceCapable = false;
    bool m_VSRcapable = false;
    bool m_HDRcapable = false;
    float m_Ratio;
    std::string m_Algo;

    // Vendors' name (PCI Special Interest Group)
    const int VENDOR_ID_AMD = 0x1002;
    const int VENDOR_ID_INTEL = 0x8086;
    const int VENDOR_ID_NVIDIA = 0x10DE;

    // GPU information
    int m_VendorId = 0;
    int m_AdapterIndex = -1;

    // Disable the constructor from outside to avoid a new instance
    VideoEnhancement();

    // Private copy constructor and assignment operator to prevent duplication
    VideoEnhancement(const VideoEnhancement&);
    VideoEnhancement& operator=(const VideoEnhancement&);

public:
    static VideoEnhancement& getInstance();
    void setVendorID(int vendorId);
    bool isVendorAMD();
    bool isVendorAMD(int vendorId);
    bool isVendorIntel();
    bool isVendorIntel(int vendorId);
    bool isVendorNVIDIA();
    bool isVendorNVIDIA(int vendorId);
    bool isEnhancementCapable();
    void setForceCapable(bool capable);
    void setVSRcapable(bool capable);
    bool isVSRcapable();
    void setHDRcapable(bool capable);
    bool isHDRcapable();
    bool isVideoEnhancementEnabled();
    bool enableVideoEnhancement(bool activate = true);
    void enableUIvisible(bool visible = true);
    void setAdapterIndex(int adapterIndex);
    int getAdapterIndex();
    bool isUIvisible();
    bool isExperimental();
    void setRatio(float ratio);
    float getRatio();
    void setAlgo(std::string algo);
    std::string getAlgo();

};

#endif // VIDEOENHANCEMENT_H
