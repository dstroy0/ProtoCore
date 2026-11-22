#pragma once

class WiFiClass
{
  public:
    void begin(const char *, const char *)
    {
    }
    embed_bool isConnected()
    {
        return EMBED_FALSE;
    }
};

// One instance per translation unit - acceptable for mock purposes
static WiFiClass WiFi;
