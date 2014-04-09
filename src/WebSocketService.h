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
//WebSocket service: map protocols to user defined services and handle
//client-server communication in synchronous or asynchronous way

#include <string>
#include <memory>
#include <cstring>
#include <map>
#include <vector>
#include <stdexcept>
#include <memory>

#include <libwebsockets.h>

namespace wsp {

using Protocols = std::vector< libwebsocket_protocols >;

//------------------------------------------------------------------------------
//TYPE INTERFACES

//Context:
// /// @param p pointer key indexing the per-session buffer arrays; this is
// ///        the parameter received in the libwebsockets handler function
// /// @param i buffer index
// Buffer& GetBuffer(void* p, int i) 
// /// Record current time into write timer map
// void RecordWriteTime(void* user) 
// /// Compute time elapsed between time of call and value stored into
// /// write timer map
// std::chrono::duration< double > ElapsedWriteTime(void* user) const 
// /// Perform websocket protocol initialization
// void InitProtocol(const char*)
// /// Per-session initialization: buffers and timers
// void InitSession(void* p, int n = 1, std::size_t s = 1, char d = '\0')
// /// Set value of write timer to now - passed value; this is usually used
// /// to ensure that the next write operation succeeds by ensuring that the
// /// delay is > that the minimum delay between writes
// void ResetWriteTimer(void* user, const std::chrono::duration< double >& d)
// /// Release per-session resources
// void Clear(void* user) 


//Service:
// /// Deleted default constructor; object must always be created by a
// /// placement new with the Service(Context*) constructor.
// SessionService() = delete;
// /// Constructor taking a reference to a Context instance. This constructor
// /// is invoked when a new connection is established through a call to 
// /// a placement new
// SessionService(Context*) 
// /// libwebsockets requires the send buffer to be pre and post padded
// /// properly; in case this service returns a properly padded buffer then
// /// this method returns @c true @c false otherwise. In case the buffer is not
// /// pre-formatted WebSocketService takes care of the formatting by
// /// performing an additional copy of the data into another buffer.
// virtual bool PreformattedBuffer() const
// /// Returns true if data is ready, false otherwise. In this case Data()
// /// returns @c false after each Get() to make sure that data in buffer
// /// is returned only once:
// virtual bool Data() const 
// /// Returns a reference to the data to send; called when libwebsockets needs
// /// to send data to clients;
// /// @param binary @true if data is in binary format, @false if it is text
// virtual const DataFrame& Get(int requestedChunkLength) const 
// /// Called when libwebsockets receives data from clients
// virtual void Put(void* p, size_t len, bool done)
// /// Update write data frame 
// virtual void UpdateOutBuffer(int writtenBytes)
// /// Set size of suggested send chunk size; WebSocketService might
// /// decide to use a different size if/when needed 
// virtual void SetSuggestedOutChunkSize(int cs)
// /// Get size of send chunk size; WebSocketService might decide to use
// /// a different value when/if needed
// virtual int GetSuggestedOutChunkSize() const
// /// Return @true if sending is not finished.
// /// Called from within a socket write event handler to decide if a new
// /// write callback needs to be rescheduled for further writing
// virtual bool Sending() const
// /// Destroy service instance by cleaning up all used resources. Ususally
// /// this is implemented by explicitly invoking the destructor since no
// /// delete is never executed on this instance which is always created
// /// through a placement new in the first place
// virtual void Destroy()
// /// Minimum delay between consecutive writes in seconds.
// virtual std::chrono::duration< double > MinDelayBetweenWrites() const


//-----------------------------------------------------------------------------
/// libwebsockets wrapper: map your service to a protocol and call StartLoop
/// Use only one single @c WebSocketService instance per process
class WebSocketService {
private:    
    ///Interface for user data deleter, used to delete the context instance
    ///stored into the libwebsockets context created in the Init() method.
    struct UserDataDeleter {
        ///Destroy instance
        virtual void Destroy() = 0;
    };
    ///Implementation of user data deleter
    template < typename C >
    struct Deleter : UserDataDeleter {
        ///Destroy instance with @c delete
        void Destroy() {
            delete d;
        }
        ///Constructor
        /// @param c pointer to allocated memory
        Deleter(C* c = 0, bool erase = true) : d(c), erase_(erase) {}
        C* d;
        bool erase_ = true;
    };    
public:
    ///Communication type:
    /// - REQ_REP: sync request-reply
    /// - ASYNC_REP: async reply
    enum Type {REQ_REP, ASYNC_REP};
    ///Send mode:
    /// - SEND_GREEDY: data is retrieved form service and sent in a loop
    ///                until no more data is available
    /// - SEND_PACKET: a single packet of data is sent at each write
    enum SendMode {SEND_GREEDY, SEND_PACKET};
    ///Service information
    /// @tparam S service type
    /// @tparam T processing type:
    /// - REQ_REP for synchronous,
    /// - ASYNC_REP for asynchronous
    template < typename S,
               Type T,
               SendMode SM = SEND_PACKET >
    struct Entry {
        ///Service type
        using ServiceType = S;
        ///WebSocket protocol name
        std::string name;
         ///Receive buffer size
        int rxBufSize = 0; //use default (should be 4096 bytes)
        ///Processing type REQ_REP, ASYNC_REP
        const static Type type = T;
        ///Send processing mode: 
        /// - SEND_GREEDY: send all the content in a loop
        /// - SEND_ASYNC:  send content one packet at a time, yielding control
        ///                back after each send
        const static SendMode sendMode = SM;
        ///Constructor
        /// @param n protocol name
        Entry(const std::string& n, int rx = 0) : name(n), rxBufSize(rx) {}
    };
public:
    ///Default constructor    
    WebSocketService() {
        memset(&info_, 0, sizeof(info_));
    }
    ///Deleted copy constructor, you want only one instance of a
    ///WebSocketService per process
    WebSocketService(const WebSocketService&) = delete;
    ///Default move constructor
    WebSocketService(WebSocketService&& ) = default;
    ///Removed assignment operator
    WebSocketService& operator=(const WebSocketService&) = delete;
    ///Destructor: clear all memory allocated in the Init method
    ~WebSocketService() {
        Clear();
    }
public:
    ///Create libwebsockets context
    /// @tparam ContextT context type: used to store reusable char buffers
    ///         as well as global and per-session configuration information   
    /// @tparam ArgsT list of Entry types with protocol-service mapping
    ///         information
    /// @param port tcp/ip port
    /// @param certPath ssl certificate path
    /// @param keyPath ssl key path
    /// @param c context instance copied to internal storage
    /// @param entries Entry list with protocol-service mapping information
    /// @return reference to context allocated in libwebsockets memory space;
    ///         reference will be invalid after libwebsockets context is
    ///         destroyed
    template < typename ContextT, typename... ArgsT >
    ContextT& Init(int port,
                   const char* certPath,
                   const char* keyPath,
                   const ContextT& c,
                   const ArgsT&...entries) {
        Clear();
        if(certPath) certPath_ = certPath;
        if(keyPath) keyPath_  = keyPath; 
        AddHandlers< ContextT >(entries...); 
        protocolHandlers_.push_back({0,0,0,0}); //termination marker
        info_.port = port;
        info_.iface = nullptr;
        info_.protocols = &protocolHandlers_[0];
        info_.ssl_cert_filepath = certPath_.size() ? certPath_.c_str() 
                                                   : nullptr;
        info_.ssl_private_key_filepath = keyPath_.size() ? keyPath_.c_str() 
                                                         : nullptr;
        info_.extensions = libwebsocket_get_internal_extensions();
        info_.options = 0;
        ContextT* ctx = new ContextT(c);
        info_.user = ctx;
        context_ = libwebsocket_create_context(&info_);
        if(!context_) 
            throw std::runtime_error("Cannot create WebSocket context");
        userDataDeleter_.reset(new Deleter< ContextT >(
            reinterpret_cast< ContextT* >(info_.user)));
        return *ctx;
    }
    ///Create libwebsockets context
    /// @tparam ContextT context type: used to store reusable char buffers
    ///         as well as global and per-session configuration information   
    /// @tparam ArgsT list of Entry types with protocol-service mapping
    ///         information
    /// @param port tcp/ip port
    /// @param certPath ssl certificate path
    /// @param keyPath ssl key path
    /// @param c shared pointer wrapping a context instance
    /// @param entries Entry list with protocol-service mapping information
    template < typename ContextT, typename... ArgsT >
    void Init(int port,
              const char* certPath,
              const char* keyPath,
              std::shared_ptr< ContextT > c,
              const ArgsT&...entries) {
        Clear();
        if(certPath) certPath_ = certPath;
        if(keyPath) keyPath_  = keyPath; 
        AddHandlers< ContextT >(entries...); 
        protocolHandlers_.push_back({0,0,0,0}); //termination marker
        info_.port = port;
        info_.iface = nullptr;
        info_.protocols = &protocolHandlers_[0];
        info_.ssl_cert_filepath = certPath_.size() ? certPath_.c_str() 
                                                   : nullptr;
        info_.ssl_private_key_filepath = keyPath_.size() ? keyPath_.c_str() 
                                                         : nullptr;
        info_.extensions = libwebsocket_get_internal_extensions();
        info_.options = 0;
        info_.user = c.get();
        context_ = libwebsocket_create_context(&info_);
        if(!context_) 
            throw std::runtime_error("Cannot create WebSocket context");
        //do not delete context, will be deleted by shared_ptr when needed
        userDataDeleter_.reset(new Deleter< ContextT >(
            reinterpret_cast< ContextT* >(info_.user), false));
    }
    ///Next iteration: performs a single loop iteration calling
    ///libwebsocket_service
    /// @param ms min execution time: if no sockets need service it
    /// returns after at least @c ms milliseconds 
    int Next(int ms = 0) {
        return libwebsocket_service(context_, ms);
    }
    ///Start event loop
    /// @tparam C continuation condition type
    /// @param ms minimum interval between consecutive iterations
    /// @param c continuation condition: loops continues until
    ///        c() returns @c true
    template < typename C >
    void StartLoop(int ms, C&& c) {
        while(c()) {
            Next(ms);
        }
    }

    /// @note
    /// weaker logging can be selected at libwebsockets configure time using
    /// --disable-debug that gets rid of the overhead of checking while keeping
    /// _warn and _err active
    
    /// Set log handler for all levels
    template < typename F >
    static void SetLogger(F&& f) {
        logger_ = f;
        lws_set_log_level(LLL_ERR 
                          | LLL_WARN
                          | LLL_NOTICE
                          | LLL_INFO
                          | LLL_DEBUG
                          | LLL_PARSER
                          | LLL_HEADER
                          | LLL_EXT
                          | LLL_CLIENT
                          | LLL_LATENCY, &LogFunction);
    }
    /// Set log handlers for scpecific log levels only.
    /// @tparam F callable object type to invoke
    /// @tparam S string type
    /// 
    template < typename F, typename... S >
    static void SetLogger(F&& f, const S&...ll) {
        logger_ = f;
        lws_set_log_level(ComposeLogLevels(lws_log_levels(0),ll...),
                          &LogFunction);
    }
    ///Clear all log levels: no logging happens after a call to this method.
    ///If this method is not called then the default libwebsockets logging takes
    ///place
    static void ResetLogLevels() {
        lws_set_log_level(LLL_ERR, nullptr);
        lws_set_log_level(LLL_WARN, nullptr);
        lws_set_log_level(LLL_NOTICE, nullptr);
        lws_set_log_level(LLL_INFO, nullptr);
        lws_set_log_level(LLL_DEBUG, nullptr);
        lws_set_log_level(LLL_PARSER, nullptr);
        lws_set_log_level(LLL_HEADER, nullptr);
        lws_set_log_level(LLL_EXT, nullptr);
        lws_set_log_level(LLL_CLIENT, nullptr);
        lws_set_log_level(LLL_LATENCY, nullptr);
    }
    ///Map libwebsockets log level to string
    static const std::string& Level(int ll) {
        static const std::string empty;
        if(levels_.find(lws_log_levels(ll)) == levels_.end()) return empty;
        else return levels_.find(lws_log_levels(ll))->second;
    }
private:
    /// 
    template < typename T, typename... S >
    static lws_log_levels ComposeLogLevels(lws_log_levels prev,
                                    const T& l, const S&... rest) {
        if(levelNames_.find(l) == levelNames_.end()) {
            throw std::logic_error(
                (std::string("Invalid log level name: ") + l).c_str());
        }
        prev = lws_log_levels(int(prev) | int(levelNames_.find(l)->second));
        return ComposeLogLevels(prev, rest...);
    }
    template < typename S >
    static lws_log_levels ComposeLogLevels(lws_log_levels prev, const S& l) {
        if(levelNames_.find(l) == levelNames_.end()) {
            throw std::logic_error(
                (std::string("Invalid log level name: ") + l).c_str());
        }
        prev = lws_log_levels(int(prev) | int(levelNames_.find(l)->second));
        return prev;
    }
    ///Callback function passed to libwebsockets to receive log info
    static void LogFunction(int level, const char* msg) {
        logger_(level, msg);
    }
    ///Create a new protocol->service mapping
    template < typename ContextT, typename ArgT, typename...ArgsT >
    void AddHandlers(const ArgT& entry, const ArgsT&...entries) {
        if(entry.name == std::string("http-only")) 
            throw std::runtime_error("HTTP protocol not supported");
        libwebsocket_protocols p;
        p.name = new char[entry.name.size() + 1];
        p.rx_buffer_size = entry.rxBufSize;
        p.no_buffer_all_partial_tx = 1;//1; //handle partial send
        std::strcpy((char*) p.name, entry.name.c_str());
        p.callback = &WebSocketService::WSCallback< ContextT,
                                  typename ArgT::ServiceType,
                                  ArgT::type,
                                  ArgT::sendMode >;
        p.per_session_data_size = sizeof(typename ArgT::ServiceType);
        protocolHandlers_.push_back(p);
        AddHandlers< ContextT >(entries...);
    }
    ///Termination condition for variadic templates
    template < typename T >
    void AddHandlers() {}
    ///Actual callback function passed to libwebsocket to handle the
    ///WebSocket protocol communication
    /// @tparam ContextT shared context
    /// @tparam T Service type
    /// @tparam t communication type, sync or async
    /// @tparam sm send processing mode: greedy (loop) or async
    /// @tparam rm receive processing mode: greedy (loop) or async
    /// @param context libwebsocket context
    /// @param wsi libwebsocket struct pointer
    /// @param reason one of libsockets' LWS_* values
    /// @param in input buffer
    /// @param len length of input buffer
    /// @return true if all packets sent, false otherwise
    template < typename ContextT, typename T, Type t, SendMode sm >
    static int WSCallback(libwebsocket_context *context,
               libwebsocket *wsi,
               libwebsocket_callback_reasons reason,
               void *user,
               void *in,
               size_t len);
    template < typename C, typename S >
    static bool Send(libwebsocket_context *context,
                     libwebsocket* wsi,
                     void* user,
                     bool greedy) {
        S* s = reinterpret_cast< S* >(user);
        assert(s);
        if(!s->Data()) return true;
        C* c = reinterpret_cast< C* >(libwebsocket_context_user(context));
        assert(c);
        using DF = typename S::DataFrame;  
        bool done = false;
        const int chunkSize = s->GetSuggestedOutChunkSize();   
        while(!done) {
            const DF df = s->Get(chunkSize);    
            const size_t bsize = df.frameEnd - df.frameBegin;
            //std::cout << size_t(df.frameEnd - df.frameBegin) << std::endl;
            done = df.frameEnd == df.bufferEnd;
            const bool begin = df.frameBegin == df.bufferBegin;
            const size_t bytesToWrite = s->PreformattedBuffer() 
                             ? LWS_SEND_BUFFER_PRE_PADDING + bsize +
                               LWS_SEND_BUFFER_POST_PADDING
                             : bsize;
            if(bytesToWrite < 1) return true;                      
            int writeMode = df.binary == true ? LWS_WRITE_BINARY 
                                              : LWS_WRITE_TEXT;
            int bytesWritten = 0;                                   
            if(!begin) writeMode = LWS_WRITE_CONTINUATION;
            if(!done) writeMode |= LWS_WRITE_NO_FIN;
           
            if(s->PreformattedBuffer()) {
                bytesWritten = libwebsocket_write(
                               wsi, 
                               (unsigned char*) df.frameBegin,
                               bytesToWrite, // <= padding + chunkSize
                               libwebsocket_write_protocol(writeMode));
                                
            } else {
                std::vector< char >& buffer = c->GetBuffer(user, 0);
                buffer.resize(LWS_SEND_BUFFER_PRE_PADDING + bsize +
                              LWS_SEND_BUFFER_POST_PADDING);
                std::copy(df.frameBegin, df.frameEnd, buffer.begin()
                                                + LWS_SEND_BUFFER_PRE_PADDING);
                
                bytesWritten = libwebsocket_write(
                          wsi, 
                          (unsigned char*) &buffer[LWS_SEND_BUFFER_PRE_PADDING],
                          bytesToWrite, //<= chunkSize
                          libwebsocket_write_protocol(writeMode));
            }
            if(bytesWritten < 0) 
                throw std::runtime_error("Send error");
            else {
                s->UpdateOutBuffer(bytesWritten);
            }
            if(!greedy) break;
        }
        return done;
    }
    ///Release resources
    void Clear() {
        for(auto& i: protocolHandlers_) {
            delete [] i.name;
        }
        protocolHandlers_.clear();
        if(context_) libwebsocket_context_destroy(context_);
        if(userDataDeleter_.get()) {
            userDataDeleter_->Destroy();
            userDataDeleter_.reset(nullptr);
        }
    }    
private:
    ///libwebosckets' creation info data    
    lws_context_creation_info info_;
    ///libwebsockets context
    libwebsocket_context* context_ = nullptr;
    ///Array of protocol->service mappings
    Protocols protocolHandlers_;
    ///SSL certificate path
    std::string certPath_;
    ///SSL key path
    std::string keyPath_;
    ///User data deleter, used to delete the copy of the context stored into
    ///libwebsockets' context
    std::unique_ptr< UserDataDeleter > userDataDeleter_;
    ///libwebsockets log levels -> logger
    static std::function< void (int, const char*) > logger_;
    ///libwebsockets' log levels -> text map
    const static std::map< lws_log_levels, std::string > levels_;
    ///log level name -> libwebsockets' log level map
    const static std::map< std::string, lws_log_levels > levelNames_;
};

//------------------------------------------------------------------------------
template < typename C, typename S, WebSocketService::Type type,
           WebSocketService::SendMode sm >
int WebSocketService::WSCallback(
               libwebsocket_context *context,
               libwebsocket *wsi,
               libwebsocket_callback_reasons reason,
               void *user,
               void *in,
               size_t len) {
    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED: {
            C* c = reinterpret_cast< C* >(libwebsocket_context_user(context));
            c->InitSession(user);
            new (user) S(c);
            const S* s = reinterpret_cast< const S* >(user);
            if(s->Sending()) {
                libwebsocket_callback_on_writable(context, wsi);
            }
        }
        break;
        case LWS_CALLBACK_PROTOCOL_INIT: {
            //One time protocol initialiation 
            C* c = reinterpret_cast< C* >(libwebsocket_context_user(context));
            if(!wsi) break;
            const libwebsocket_protocols* p =  libwebsockets_get_protocol(wsi);
            if(p) c->InitProtocol(p->name);
        }
        break;
        case LWS_CALLBACK_RECEIVE: {
            S* s = reinterpret_cast< S* >(user);
            const bool done = libwebsockets_remaining_packet_payload(wsi) == 0;
            s->Put(in, len, done);
            if(type == Type::REQ_REP && done) {
                const bool GREEDY_OPTION = sm == SendMode::SEND_GREEDY;
                C* c = 
                  reinterpret_cast< C* >(libwebsocket_context_user(context));
                if(!Send< C, S >(context, wsi, user, GREEDY_OPTION))
                    libwebsocket_callback_on_writable(context, wsi); 
            } else if(type == Type::ASYNC_REP && done) {
                  libwebsocket_callback_on_writable(context, wsi); 
            }
        }
        break;
        case LWS_CALLBACK_SERVER_WRITEABLE: {
            C* c = reinterpret_cast< C* >(libwebsocket_context_user(context));
            S* s = reinterpret_cast< S* >(user);
            if(c->ElapsedWriteTime(user) < s->MinDelayBetweenWrites()) {
                libwebsocket_callback_on_writable(context, wsi);
                break;
            }
            const bool GREEDY_OPTION = sm == SendMode::SEND_GREEDY;
            const bool allSent = Send< C, S >(context, wsi, user,
                                              GREEDY_OPTION);
            //if data still pending reset timer, if not data will have to wait 
            //until next available time frame, the timer is reset to current
            //time - min delay time to ensure that the next write operation is
            //executed 
            if(!allSent) c->ResetWriteTimer(user, s->MinDelayBetweenWrites());
            else c->RecordWriteTime(user);
            if(!allSent 
               || s->Sending()) {

                libwebsocket_callback_on_writable(context, wsi);
            }

        }
        break;
        case LWS_CALLBACK_CLOSED:
            reinterpret_cast< S* >(user)->Destroy();
            reinterpret_cast< C* >(libwebsocket_context_user(context))
                                                          ->Clear(user);

        break;
        default:
            break;
    }

    return 0;
}

} //namespace wsp
