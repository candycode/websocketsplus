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

#include <vector>
#include <map>
#include <chrono>
#include <stdexcept>

namespace wsp {

//Implementations of WebSocketService-compatible context
using Buffer  = std::vector< char >;
using Buffers = std::vector< Buffer >;
using BufferMap = std::map< void*, Buffers >;
using TimerMap = std::map< void*, std::chrono::steady_clock::time_point >;

struct ServiceDataEmpty {};

//------------------------------------------------------------------------------
/// Context implementation: provides storage space to per-session services.
/// A Context is created once when the libwebsockets context is created
/// and destroyed when the WebSocketService instance is destroyed.
/// @tparam ServiceDataT convenience type to store service specific data
///         without having to create a separate Context class.
///         ServiceDataT instances are used to share data among all the 
///         instances of per-session service instances
template < typename ServiceDataT = ServiceDataEmpty >
class Context {
public:
    using ServiceData = ServiceDataT;
    /// Default constructor
    Context() {} 
    /// Constructor accepting a ServiceData type instance
    Context(const ServiceData& sd) : serviceData_(sd) {}
    /// Return constant reference to ServiceData instance
    const ServiceData& ServiceData() const { return serviceData_; }
public:
    /// Return reference to buffer
    /// @param p pointer key indexing the per-session buffer arrays; this is
    ///        the parameter received in the libwebsockets handler function
    /// @param i buffer index
    Buffer& GetBuffer(void* p, int i) {
        assert(buffers_.find(p) != buffers_.end());
        assert(buffers_[p].size() > i);
        return buffers_[p][i];
    }
    /// Record current time into write timer map
    void RecordWriteTime(void* user) {
        writeTimers_[user] = std::chrono::steady_clock::now();
    }
    /// Compute time elapsed between time of call and value stored into
    /// write timer map
    std::chrono::duration< double > ElapsedWriteTime(void* user) const {
        if(writeTimers_.find(user) == writeTimers_.end()) {
            throw std::logic_error(
                "Cannot find service instance in write timers");
        }
        const std::chrono::steady_clock::time_point t = 
            writeTimers_.find(user)->second;
        const std::chrono::steady_clock::time_point now = 
            std::chrono::steady_clock::now();
        return std::chrono::duration_cast<
                    std::chrono::duration< double > >(now - t);
    }
    /// Perform websocket protocol initialization
    void InitProtocol(const char*) {} 
    /// Per-session initialization: buffers and timers
    void InitSession(void* p, int n = 1, std::size_t s = 1, char d = '\0') {
        CreateBuffers(p, n, s, d);
        CreateTimers(p);
    }
    /// Set value of write timer to now - passed value; this is usually used
    /// to ensure that the next write operation succeeds by ensuring that the
    /// delay is > that the minimum delay between writes
    void ResetWriteTimer(void* user, const std::chrono::duration< double >& d) {
        if(writeTimers_.find(user) == writeTimers_.end()) {
            throw std::logic_error(
                "Cannot find service instance in write timers");
        }
        const std::chrono::steady_clock::time_point now = 
            std::chrono::steady_clock::now();
        using D = std::chrono::steady_clock::duration;    
        writeTimers_[user] = now - std::chrono::duration_cast< D >(d); 
    }
    /// Release per-session resources
    void Clear(void* user) {
        RemoveBuffers(user);
        RemoveTimers(user);
    }
private:
    /// Create char buffers
    /// @param p pointer key indexing the per-session buffer arrays; this is
    ///        the parameter received in the libwebsockets handler function
    /// @param n number of buffers to create, default = 1
    /// @param s initial size of buffers, default = 1
    /// @param d initial value of buffer elements
    void CreateBuffers(void* p, int n = 1, std::size_t s = 1, char d = '\0') {
        buffers_[p] = Buffers(n, Buffer(s, d));
    }
    /// Create write timers
    void CreateTimers(void* user) {
       RecordWriteTime(user);
    }
    /// Remove all buffers associated to a user session
    /// @param p pointer key indexing the per-session buffer arrays; this is
    ///        the parameter received in the libwebsockets handler function
    void RemoveBuffers(void* p) {
        assert(buffers_.find(p) != buffers_.end());
        buffers_.erase(buffers_.find(p));
    }
    /// Delete time entry associated with session
    void RemoveTimers(void* user) {
        if(writeTimers_.find(user) == writeTimers_.end()) {
            throw std::logic_error(
                "Cannot find service instance in write timers");
        }
        writeTimers_.erase(writeTimers_.find(user));
    }    
private:
    /// per-session buffer map; maps per-session user data pointer
    /// to buffer array
    BufferMap buffers_;    
    TimerMap writeTimers_;
    ServiceData serviceData_;
};

} //namespace wsp 