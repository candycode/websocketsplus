#pragma once
//author: Ugo Varetto
//WebSocket service: map protocols to user defined services and handle
//client-server communication in synchronous or asynchronous way

#include <string>
#include <memory>
#include <cstring>
#include <map>
#include <vector>
#include <stdexcept>
#include <libwebsockets.h>

namespace wsp {

using Protocols = std::vector< libwebsocket_protocols >;

/*  NOTE:
 *  weaker logging can be selected at configure time using --disable-debug
 *  that gets rid of the overhead of checking while keeping _warn and _err
 *  active
 */

//------------------------------------------------------------------------------
//REQUIRED TYPE INTERFACES
//Context:
//  vector< char >& GetBuffer(void* user, int bufferIndex);
//  void CreateBuffers(void* user, int numberOfBuffer,
//                     size_t bufferSize, char initValue) 
//  void RemoveBuffers(void* user)
//Service
//  Service(Context)
//  const vector<char>& Get(bool& binary)
//  void Put(void* buffer, size_t length)
//  void Destroy() //should call ~Service
//  bool PreformattedBuffer() true if already contains padding
//  size_t BufferSize() size of the data between paddings
//  if PreformattedBuffer() returns true, or size of the internal buffer
//  if PreformattedBuffer() returns false

//-----------------------------------------------------------------------------
/// libwebsockets wrapper: map your service to a protocol and call StartLoop
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
        Deleter(C* c = 0) : d(c) {}
        C* d;
    };    
public:    
    enum Type {REQ_REP, ASYNC_REP};
    ///Service information
    /// @tparam S service type
    /// @tparam T processing type:
    /// - REQ_REP for synchronous,
    /// - ASYNC_REP for asynchronous
    template < typename S, Type T >
    struct Entry {
        ///Service type
        using ServiceType = S;
        ///WebSocket protocol name
        std::string name;
        ///Processing type REQ_REP, ASYNC_REP
        const static Type type = T;
        ///Constructor
        /// @param n protocol name
        Entry(const std::string& n) : name(n) {}
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
    template < typename ContextT, typename... ArgsT >
    void Init(int port,
              const char* certPath,
              const char* keyPath,
              const ContextT& c,
              const ArgsT&...entries) {
        Clear();
        if(certPath) certPath_ = certPath;
        if(keyPath) keyPath_  = keyPath; 
        AddHandlers< ContextT >(entries...); 
        protocolHandlers_.push_back({0,0,0,0});
        info_.port = port;
        info_.iface = nullptr;
        info_.protocols = &protocolHandlers_[0];
        info_.ssl_cert_filepath = certPath_.size() ? certPath_.c_str() 
                                                   : nullptr;
        info_.ssl_private_key_filepath = keyPath_.size() ? keyPath_.c_str() 
                                                         : nullptr;
        info_.extensions = libwebsocket_get_internal_extensions();
        info_.options = 0;
        info_.user = new ContextT(c);
        context_ = libwebsocket_create_context(&info_);
        if(!context_) 
            throw std::runtime_error("Cannot create WebSocket context");
        userDataDeleter_.reset(new Deleter< ContextT >(
            reinterpret_cast< ContextT* >(info_.user)));
    }
    ///Next iteration: performs a single loop iteration calling
    ///libwebsocket_service
    /// @param ms min execution time: if no sockets need service it
    /// returns after @c ms milliseconds 
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
    /// Set log handler for specific log levels
    /// @tparam F logger type
    /// @param log level name, maps to libwebsockets levels:
    /// - "ERR"
    /// - "WARN"
    /// - "NOTICE"
    /// - "INFO"
    /// - "DEBUG"
    /// - "PARSER"
    /// - "HEADER"
    /// - "EXTENSION"
    /// - "CLIENT"
    /// - "LATENCY"
    /// @param f logger
    template < typename F >
    static void SetLogger(const std::string& l, F&& f) {
        if(levelNames_.find(l) == levelNames_.end())
            throw std::logic_error("Invalid log level '" + l + "'");
        loggers_[levelNames_.find(l)->second] = f;
        lws_set_log_level(levelNames_.find(l)->second, &LogLevel);
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
    ///Callback function passed to libwebsockets to receive log info
    static void LogLevel(int level, const char* msg) {
        loggers_[lws_log_levels(level)](level, msg);
    }
    ///Create a new protocol->service mapping
    template < typename ContextT, typename ArgT, typename...ArgsT >
    void AddHandlers(const ArgT& entry, const ArgsT&...entries) {
        libwebsocket_protocols p;
        p.name = new char[entry.name.size() + 1];
        std::strcpy((char*) p.name, entry.name.c_str());
        p.callback = &WebSocketService::WSCallback< ContextT,
                                  typename ArgT::ServiceType,
                                  ArgT::type >;
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
    /// @param context libwebsocket context
    /// @param wsi libwebsocket struct pointer
    /// @param reason one of libsockets' LWS_* values
    /// @param in input buffer
    /// @param len length of input buffer
    template < typename ContextT, typename T, Type t >
    static int WSCallback(libwebsocket_context *context,
               libwebsocket *wsi,
               libwebsocket_callback_reasons reason,
               void *user,
               void *in,
               size_t len);
    template < typename C, typename S >
    static void Send(libwebsocket_context *context,
                     libwebsocket* wsi,
                     void* user) {
        S* s = reinterpret_cast< S* >(user);
        if(!s->Data()) return;
        assert(s);
        C* c = reinterpret_cast< C* >(libwebsocket_context_user(context));
        assert(c);
        bool binary = false;
        const std::vector< char >& b = s->Get(binary); 
        if(!s->PreformattedBuffer()) {
            if(b.empty()) return;
            std::vector< char >& buffer = c->GetBuffer(user, 0);
            buffer.resize(LWS_SEND_BUFFER_PRE_PADDING + b.size() +
                          LWS_SEND_BUFFER_POST_PADDING);
            std::copy(b.begin(), b.end(), buffer.begin() + LWS_SEND_BUFFER_PRE_PADDING);

            libwebsocket_write(
                             wsi, 
                             (unsigned char*) &buffer[LWS_SEND_BUFFER_PRE_PADDING],
                             b.size(),
                             binary == false ? LWS_WRITE_TEXT 
                                             : LWS_WRITE_BINARY);
        } else {
            libwebsocket_write(
                             wsi, 
                             (unsigned char*) &b[LWS_SEND_BUFFER_PRE_PADDING],
                             s->BufferSize(),
                             binary == false ? LWS_WRITE_TEXT 
                                             : LWS_WRITE_BINARY); 
        }
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
    libwebsocket_context* context_;
    ///Array of protocol->service mappings
    Protocols protocolHandlers_;
    ///SSL certificate path
    std::string certPath_;
    ///SSL key path
    std::string keyPath_;
    ///User data deleter, used to delete the copy of the context stored into
    ///libwebsockets' context
    std::unique_ptr< UserDataDeleter > userDataDeleter_;
    ///libwebsockets log levels -> logger map
    static std::map< lws_log_levels, std::function< void (int, const char*) > >
        loggers_;
    ///libwebsockets' log levels -> text map
    const static std::map< lws_log_levels, std::string > levels_;
    ///log level name -> libwebsockets' log level map
    const static std::map< std::string, lws_log_levels > levelNames_; 
};

template < typename C, typename S, WebSocketService::Type type>
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
            c->CreateBuffers(user);
            new (user) S(c);
            if(type == ASYNC_REP) {
                libwebsocket_callback_on_writable(context, wsi);
            }
        }
        break;
        case LWS_CALLBACK_PROTOCOL_INIT:
            break;
        case LWS_CALLBACK_RECEIVE: { 
            reinterpret_cast< S* >(user)->Put(in, len);
            if(type == REQ_REP) {
                Send< C, S >(context, wsi, user);    
            }
        }
        break;
        case LWS_CALLBACK_SERVER_WRITEABLE: {
            if(type == ASYNC_REP) {
                Send< C, S >(context, wsi, user);
                libwebsocket_callback_on_writable(context, wsi);
            }
        }
        break;
        case LWS_CALLBACK_CLOSED:
            reinterpret_cast< S* >(user)->Destroy();
            reinterpret_cast< C* >(libwebsocket_context_user(context))
                    ->RemoveBuffers(user);
        break;
        default:
            break;
    }

    return 0;
}

} //namespace wsp
