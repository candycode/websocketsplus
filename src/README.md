Workflow
========

1. Create web socket service:

```cpp
WebSocketService ws;
```

2. Invoke `WebSocketService::Init` specifying:

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

3. Start service event loop

```cpp
    //start event loop: one iteration every >= 50ms
    ws.StartLoop(50, //ms
                 []{return true;} //continuation condition (exit on false)
                                  //checked at each iteration, loops forever
                                  //in this case
                 );
```

Context Interface
-----------------

`InitSession`: 

* called when connection established
* maps void* received as _user_ parameter from libwebsockets to array of memory buffers

`InitProtocol`:

* called once per protocol to initialize per-protocol resources

    
    
    

