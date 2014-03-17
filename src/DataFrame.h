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
