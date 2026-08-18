#pragma once

class WiFiClass
{
  public:
    void begin(const char *, const char *)
    {
    }
    proto_bool isConnected()
    {
        return PROTO_FALSE;
    }
};

// One instance per translation unit - acceptable for mock purposes
static WiFiClass WiFi;
