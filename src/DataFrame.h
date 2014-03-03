#pragma once
// author: Ugo Varetto
// data frame information returned by Service::Get() and used
// to send data to clients

///Information on data frame to write
struct DataFrame {
    ///start of global buffer to send
    const char* bufferBegin = nullptr;
    ///one byte past the end of global buffer to send
    const char* bufferEnd = nullptr;
    ///start of data packet to send must be between
    ///@c bufferBegin and @c bufferEnd
    const char* frameBegin = nullptr;
    ///one byte past end of the packet to send, must lie
    ///between @c frameBegin and @c bufferEnd
    const char* frameEnd = nullptr;
    ///@c true if data is binary, @c false otherwise
    bool binary = false;  
    DataFrame(const char* bb,
              const char* be,
              const char* fb,
              const char* fe,
              bool b)
        : bufferBegin(bb), bufferEnd(be), frameBegin(fb),
        frameEnd(fe), binary(b) {}
    DataFrame() = default; 
};
