// Websockets+ : C++11 server-side websocket library based on libwebsockets;
//               supports easy creation of services and built-in throttling
// Copyright (C) 2014  Ugo Varetto
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
#pragma once
#include <algorithm>

namespace wsp {
// data frame information returned by Service::Get() and used
// to send data to clients

/// Data frame to be sent to client.
/// <code>frameBegin == bufferEnd</code> signals the end of writing
/// operations; <code>frameBegin == bufferBegin</code> signals the
/// beginning of writing operations.
/// DataFrame must be an inner type of the Service type used in the
/// WebSocketService class; either implement your own or include this file
/// and add a typedef/using declaration inside the Service type
struct DataFrame {
    /// Start of buffer
    const char* bufferBegin = nullptr;
    /// One element past last element of buffer
    const char* bufferEnd = nullptr;
    /// Start of sub-region of buffer to operate on
    const char* frameBegin = nullptr;
    /// One element past the end of the sub-region of the buffer to
    /// operate on
    const char* frameEnd = nullptr;
    /// @c true if data is binary, @c false if it is text
    bool binary = false;
    /// Default constructor
    DataFrame() = default;
    /// Constructor
    /// @param bb pointer to first element of buffer
    /// @param be pointer to first element after the end of the buffer
    /// @param fb pointer to first element of sub-buffer
    /// @param fe pointer to fitst element after the end of the sub-buffer
    ///        to send
    /// @param b binary flag, true if binary, false if text
    DataFrame(const char* bb, const char* be,
              const char* fb, const char* fe,
              bool b)
    : bufferBegin(bb), bufferEnd(be),
      frameBegin(fb), frameEnd(fe), binary(b) {}
};

inline bool Update(DataFrame& df, size_t chunkLength) {
    if(df.frameBegin < df.bufferEnd) {
        chunkLength = std::min(chunkLength,
                               size_t(df.bufferEnd - df.frameEnd));
        df.frameBegin = df.frameEnd;
        df.frameEnd = df.frameEnd + chunkLength;
        return true;
    } else return false;
}

inline bool Consumed(const DataFrame& df) {
    return df.bufferEnd && df.frameEnd >= df.bufferEnd;
}

inline void Init(DataFrame& df, char* begin, size_t size) {
    df = DataFrame(begin, begin + size, begin, begin, true);
}


}