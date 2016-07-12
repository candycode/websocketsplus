Workflow
========

**I Create web socket service:**


```cpp
WebSocketService ws;
```

**II Invoke `WebSocketService::Init` specifying:**

1. Internet port
2. SSL certificate path (optional)
3. SSL key path (optional)
4. Context instance - template parameter
5. Variadic template list of services wrappend with `WebSocketService::Entry`

```cpp
 using Service = SessionService< Context<> >;
    const int readBufferSize = 4096; //the default anyway
    //init service
    ws.Init(9001, //port
            nullptr, //SSL certificate path
            nullptr, //SSL key path
            Context<>(), //context instance, will be copied internally
            //protocol->service mapping
            //sync request-reply: at each request a reply is immediately sent
            //to the client
            WSS::Entry< Service, WSS::REQ_REP >("myprotocol", readBufferSize),
            //async: requests and replies are handled asynchronously
            WSS::Entry< Service, WSS::ASYNC_REP >("myprotocol-async"),
            //sync request with greedy send: all the data chunks are sent in
            //a loop
            WSS::Entry< Service, WSS::REQ_REP,
                        WSS::SendMode::SEND_GREEDY >("myprotocol-greedy"),
             //async request with greedy send
            WSS::Entry< Service, WSS::ASYNC_REP,
                        WSS::SendMode::SEND_GREEDY >("myprotocol-async-greedy")
    );
```

**III Start service event loop**

```cpp
    //start event loop: one iteration every >= 50ms
    ws.StartLoop(50, //ms
                 []{return true;} //continuation condition (exit on false)
                                  //checked at each iteration, loops forever
                                  //in this case
                 );
```

Memory management
=================


Service object is allocated into memory region allocated by libwebsockets...

See `p.per_session_data_size` in `WebSocketService::AddHandler` method

```cpp
///Add handler: non-http case
    template < typename ContextT, typename ArgT >
    void AddHandler(int pos, const ArgT& entry, const NoHttpService&) {
        lws_protocols p;
        p.name = new char[entry.name.size() + 1];
        p.rx_buffer_size = entry.rxBufSize;
        
        std::strcpy((char*) p.name, entry.name.c_str());        
        
        p.callback = &WebSocketService::WSCallback< ContextT,
                                      typename ArgT::ServiceType,
                                      ArgT::type,
                                      ArgT::sendMode >;
        //VVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVV                              
        p.per_session_data_size = sizeof(typename ArgT::ServiceType);
        //^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
        
        protocolHandlers_.push_back(p);
    }
```
...when a new connection is established, see 
`WebSocketService::WSCallback`

```cpp
template < typename C, typename S, WebSocketService::Type type,
           WebSocketService::SendMode sm >
int WebSocketService::WSCallback(
               lws *wsi,
               lws_callback_reasons reason,
               void *user,
               void *in,
               size_t len) {
        lws_context* context = lws_get_context(wsi);
    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED: {
            C* c = reinterpret_cast< C* >(lws_context_user(context));
            c->InitSession(user);
            // user points to a memory region pre-allocated by
            // libwesockets of size = sizeof(S), see
            
            //vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
            new (user) S(c);
            //^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
            
            const S* s = reinterpret_cast< const S* >(user);
            if(s->Sending()) {
                lws_callback_on_writable(wsi);
            }
        }
        ...
```

Context Interface
-----------------

Context is a template parameter passed to WebSocketService used to
share per-session data among requests and replies.

`InitSession`: 

* called when connection established
* maps void* received as _user_ parameter from libwebsockets to array of memory buffers

`InitSession` internally allocates the buffers used for sending data

`InitProtocol`:

* called once per protocol to initialize per-protocol resources


Service Interface
-----------------

Implementation of the actual service.

`Put` method is called to put data in buffer when data is received

`Get` method is called by run-time when to extract the data to send

`Get` returns a `DataFrame` object which stores the beginning and end
of the subset of data buffer to send.





    
    
    
    
    

