#pragma once

class WiFiClass
{
  public:
    void begin(const char *, const char *)
    {
    }
    bool isConnected()
    {
        return false;
    }
};

// One instance per translation unit - acceptable for mock purposes
static WiFiClass WiFi;
