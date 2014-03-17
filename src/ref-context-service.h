#pragma once

#include <vector>
#include <map>
#include <chrono>
#include <stdexcept>

//Reference implementations of context and service
using Buffer  = std::vector< char >;
using Buffers = std::vector< Buffer >;
using BufferMap = std::map< void*, Buffers >;
using TimerMap = std::map< void*, std::chrono::steady_clock::time_point >

#ifdef BINARY_DATA
#define BINARY_OPTION true
#else 
#define BINARY_OPTION false
#endif

//------------------------------------------------------------------------------
/// Context implementation: provides storage space to per-session services.
/// A Context is created once when the libwebsockets context is created
/// and destroyed when the WebSocketService instance is destroyed.
class Context {
public:
    /// Create char buffers
    /// @param p pointer key indexing the per-session buffer arrays; this is
    ///        the parameter received in the libwebsockets handler function
    /// @param n number of buffers to create, default = 1
    /// @param s initial size of buffers, default = 1
    /// @param d initial value of buffer elements
    void CreateBuffers(void* p, int n = 1, std::size_t s = 1, char d = '\0') {
        buffers_[p] = Buffers(n, Buffer(s, d));
    }
    /// Return reference to buffer
    /// @param p pointer key indexing the per-session buffer arrays; this is
    ///        the parameter received in the libwebsockets handler function
    /// @param i buffer index
    Buffer& GetBuffer(void* p, int i) {
        assert(buffers_.find(p) != buffers_.end());
        assert(buffers_[p].size() > i);
        return buffers_[p][i];
    }
    /// Remove all buffers associated to a user session
    /// @param p pointer key indexing the per-session buffer arrays; this is
    ///        the parameter received in the libwebsockets handler function
    void RemoveBuffers(void* p) {
        assert(buffers_.find(p) != buffers_.end());
        buffers_.erase(buffers_.find(p));
    }
    void Clear(void* user) {
        RemoveBuffers(user);
        RemoveTimers(user);
    }
    void RecordWriteTime(void* user) {
        writeTimers_[user] = std::chrono::steady_clock::now();
    }
    std::chrono::duration< double > ElapsedWriteTime(void* user) const {
        if(writeTimers_.find(user) == writeTimers_.end()) {
            throw std::logic_error(
                "Cannot find service instance in write timers");
        }
        const std::chrono::steady_clock::time_point t = 
            writeTimers_.find(user)->second;
        const std::chrono::steady_clock::time_point now = steady_clock::now();
        return std::chrono::duration_cast<
                    std::chrono::duration< double > >(now - t);
    }
    void RemoveTimers(void* user) {
        if(writeTimers_.find(user) == writeTimers_.end()) {
            throw std::logic_error(
                "Cannot find service instance in write timers");
        }
        writeTimers_.erase(writeTimers_.find(user));
    }
    /// Perform websocket protocol initialization
    void InitProtocol(const char*) {}    
private:
    /// per-session buffer map; maps per-session user data pointer
    /// to buffer array
    BufferMap buffers_;    
    TimerMap writeTimers_;
};

/// Per-session (between a socket open and close) echo service instance, this is
/// the class that handles incoming requests and sends back replies.
/// SessionService instances get created when a WebSocket connection is
/// established and deleted when the connection terminates.
class SessionService {
public:
    /// Data frame to be sent to client.
    /// <code>frameBegin == bufferEnd</code> signals the end of writing
    /// operations; <code>frameBegin == bufferBegin</code> signals the
    /// beginning of writing operations.
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
        ///         to send
        DataFrame(const char* bb, const char* be,
                  const char* fb, const char* fe,
                  bool b)
        : bufferBegin(bb), bufferEnd(be),
          frameBegin(fb), frameEnd(fe), binary(b) {}
 }; 
public:
    /// Deleted default constructor; object must always be created by a
    /// placement new with the Service(Context*) constructor.
    SessionService() = delete;
    /// Constructor taking a reference to a Context instance. This constructor
    /// is invoked when a new connection is established through a call to 
    /// a placement new
    SessionService(Context*) 
        : writeDataFrame_(nullptr, nullptr, nullptr, nullptr, BINARY_OPTION) {

        }
    /// libwebsockets requires the send buffer to be pre and post padded
    /// properly; in case this service returns a properly padded buffer then
    /// this method returns @c true @c false otherwise. In case the buffer is not
    /// pre-formatted WebSocketService takes care of the formatting by
    /// performing an additional copy of the data into another buffer.
    virtual bool PreformattedBuffer() const { return false; }
   
    /// Returns true if data is ready, false otherwise. In this case Data()
    /// returns @c false after each Get() to make sure that data in buffer
    /// is returned only once:
    virtual bool Data() const {
        return dataAvailable_;
    }
    /// Returns a reference to the data to send; called when libwebsockets needs
    /// to send data to clients;
    /// @param binary @true if data is in binary format, @false if it is text
    virtual const DataFrame& Get(int requestedChunkLength) const { 
        if(writeDataFrame_.frameBegin < writeDataFrame_.bufferEnd) {
            requestedChunkLength = std::min(requestedChunkLength,
                                           int(writeDataFrame_.bufferEnd - 
                                               writeDataFrame_.frameBegin));
            writeDataFrame_.frameEnd = writeDataFrame_.frameBegin
                                       + requestedChunkLength; 
        }
        else dataAvailable_ = false;
        return writeDataFrame_;
    }
    /// Called when libwebsockets receives data from clients
    virtual void Put(void* p, size_t len, bool done) {
        dataAvailable_ = done; //signal that data is available as soon as all
                               //data packets are received
        if(p == nullptr || len == 0) return;
        if(prevReadCompleted_) {
            buffer_.resize(0);
            prevReadCompleted_ = false;
        }
        const size_t prev = buffer_.size();
        buffer_.resize(buffer_.size() + len);
        UpdateWriteDataFrame();
        copy((const char*) p, (const char*) p + len, buffer_.begin() + prev);       
        if(done) {
            prevReadCompleted_ = true;
        }
    }
    /// Update write data frame 
    virtual void UpdateOutBuffer(int writtenBytes) {
        writeDataFrame_.frameBegin += writtenBytes;
    }
    /// Set size of suggested send chunk size; WebSocketService might
    /// decide to use a different size if/when needed 
    virtual void SetSuggestedOutChunkSize(int cs) {
        suggestedWriteChunkSize_ = cs;
    }
    /// Get size of send chunk size; WebSocketService might decide to use
    /// a different value when/if needed
    virtual int GetSuggestedOutChunkSize() const {
        return suggestedWriteChunkSize_;
    } 
    /// Return @true if sending is not finished.
    /// Called from within a socket write event handler to decide if a new
    /// write callback needs to be rescheduled for further writing
    virtual bool Sending() const {
        return false;
    }
    /// Destroy service instance by cleaning up all used resources. Ususally
    /// this is implemented by explicitly invoking the destructor since no
    /// delete is never executed on this instance which is always created
    /// through a placement new in the first place
    virtual void Destroy() {
        this->~SessionService();
    }
    /// Minimum delay between consecutive writes in seconds.
    virtual const std::chrono::duration< double >& 
    MinDelayBetweenWrites() const {
        return minWriteDelay_;
    }
private:
    void UpdateWriteDataFrame() {
        writeDataFrame_.bufferBegin = &buffer_[0];
        writeDataFrame_.bufferEnd   = &buffer_[0] + buffer_.size();
        writeDataFrame_.frameBegin  = writeDataFrame_.bufferBegin;
        writeDataFrame_.frameEnd    = writeDataFrame_.frameBegin;
    }
protected:    
    /// virtual destructor, never called through delete since instances of
    /// this class are always created through a placement new call
    virtual ~SessionService() {}    
private:
    /// Data buffer, filled in Put() method
    Buffer buffer_;
    /// @c false : text data only
    bool binary_ = false;
    /// @c true if data ready
    mutable bool dataAvailable_ = false;
    /// Data frame returned by @c Get method holding information such as
    /// boundaries of buffer to send as a frame
    mutable DataFrame writeDataFrame_;
    /// @c true if previous read completed, @c false otherwise. This data member
    /// is updated by the @c Put method when libwesockets has finished reading
    /// data
    bool prevReadCompleted_ = true;
    /// Suggested write chunk size; will be updated in case it is too big.
    int suggestedWriteChunkSize_ = 4096;
    /// Min delay between consecutive writes
    std::chrono::duration< double > minWriteDelay_ = 1.0; //1s
};