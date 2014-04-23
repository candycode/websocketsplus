/* -*-c++-*- OpenSceneGraph - Copyright (C) 1998-2006 Robert Osfield 
 *
 * This application is open source and may be redistributed and/or modified   
 * freely and without restriction, both in commercial and non commercial applications,
 * as long as this copyright notice is maintained.
 * 
 * This application is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
*/

#include <osgDB/ReadFile>
#include <osgDB/WriteFile>

#include <osgUtil/Optimizer>
#include <osg/CoordinateSystemNode>

#include <osg/Switch>
#include <osgText/Text>

#include <osgViewer/Viewer>
#include <osgViewer/ViewerEventHandlers>

#include <osgGA/TrackballManipulator>
#include <osgGA/FlightManipulator>
#include <osgGA/DriveManipulator>
#include <osgGA/KeySwitchMatrixManipulator>
#include <osgGA/StateSetManipulator>
#include <osgGA/AnimationPathManipulator>
#include <osgGA/TerrainManipulator>
#include <osgGA/SphericalManipulator>

#include <iostream>
#include <sstream>
#include <string.h>

//g++ -std=c++11  ../src/examples/osg/osgscreencapture.cpp -I /usr/local/osg/include -L /usr/local/osg/lib64 -lOpenThreads -losgDB -losgGA -losgManipulator -losg -losgUtil -losgViewer -losgText -lGL -I /opt/libjpeg-turbo/include -L /opt/libjpeg-turbo/lib64  -lturbojpeg -I /usr/local/libwebsockets/include -L /usr/local/libwebsockets/lib -lwebsockets -O3 -pthread osg-stream.exe
//clang++ -std=c++11  -stdlib=libc++ ../src/examples/osg/osgscreencapture.cpp -I /usr/local/osg/include -L /usr/local/osg/lib -lOpenThreads -losgDB -losgGA -losgManipulator -losg -losgUtil -losgViewer -losgText  -framework OpenGL -I /opt/libjpeg-turbo/include -L /opt/libjpeg-turbo/lib  -lturbojpeg -I /usr/local/libwebsockets/include -L /usr/local/libwebsockets/lib -lwebsockets -O3 -pthread -o osg-stream.exe
//==============================================================================
#include <thread>
#include <future>
#include <memory>
#include <chrono>
#include <unordered_map>
#include <deque>

#include <turbojpeg.h>

#include "../../WebSocketService.h"
#include "../../Context.h"
#include "../SessionService.h"
#include "../../http.h"
 #include "../../DataFrame.h"

using namespace std;


template < typename T >
class SyncQueue {
public:    
    void Get(T& v) {
        std::lock_guard< std::mutex > guard(mutex_);
        v = move(q_.front());
        q_.pop_front();
    }
    void Put(T&& d) {
        std::lock_guard< std::mutex > guard(mutex_);
        q_.push_back(move(d));
    }
    bool Empty() const { return q_.empty(); }
private:    
    deque< T > q_;
    mutex mutex_;

};

struct TJDeleter {
    void operator()(char* p) const {
        if(!p) return;
        tjFree((unsigned char*) p);
    }
};

using ImagePtr = shared_ptr< char >;

struct Image {
    int id = 0;
    ImagePtr image;
    size_t size = 0;
    Image() = default;
    Image(ImagePtr i, size_t s, int c) : image(i), size(s), id(c) {}
    Image(const Image&) = default;
    Image(Image&&) = default;
    Image& operator=(const Image&) = default;
    Image& operator=(Image&&) = default;
};

//------------------------------------------------------------------------------
bool END = false;
SyncQueue< vector< char > > msgQueue;
osgViewer::Viewer* v = nullptr;
int cs = TJSAMP_420;
int quality = 75;
shared_ptr< wsp::Context< Image > > context(new wsp::Context< Image >);
bool moving = false;
//frames are sent to clients if and only if this variable's value is > 0
int sendFrame = 2;
//------------------------------------------------------------------------------
struct Msg {
    Msg(vector< char >&& d) {
        int* p = reinterpret_cast< int* >(&d[0]);
        type = p[0];
        if(MouseEvent(type)) {
            x = p[1];
            y = p[2];
            buttons = p[3];
        } else if(KeyEvent(type)) {
            key = p[1];
            if(!(p[2] & 0x100)) key = tolower(key);
        } else if(WheelEvent(type)) {
            x = p[1];
            y = p[2];
            delta = p[3];
        } else if(ResizeEvent(type)) {
            x = p[1];
            y = p[2];
        } else if(ReadFile(type)) {
              
            const int len = p[1];
            const short unsigned* s = (const short unsigned*) &d[2 * sizeof(int)];
            filename = "";
            for(int i = 0; i != len; ++i) {
               filename.push_back((char) s[i]);
            }
        }
    }
    bool MouseEvent(int e) {
        return e >= 1 && e <= 3;
    }
    bool KeyEvent(int e) {
        return e == 4;
    }
    bool WheelEvent(int e) {
        return e == 5;
    }
    bool ResizeEvent(int e) {
        return e == 6;
    }
    bool ReadFile(int e) {
        return e == 7;
    }
    int type = -1;
    int x = -1;
    int y = -1;
    int buttons = -1;
    int key = -1;
    int delta = 0;
    string filename;
};
//------------------------------------------------------------------------------
int SelectButton(int buttons) {
    if(buttons == 0) return 1;
    if(buttons == 1) return 2;
    if(buttons == 4) return 3;
    return 0;
}

void mousebutton(int buttons, int state, int x, int y ) { 
    if (state==0) v->getEventQueue()
        ->mouseButtonPress(x, y, SelectButton(buttons),
            v->getEventQueue()->getTime());
    else v->getEventQueue()
        ->mouseButtonRelease( x, y, SelectButton(buttons),
            v->getEventQueue()->getTime());
}
void mousemove( int x, int y ) {
    v->getEventQueue()->mouseMotion( x, y, v->getEventQueue()->getTime());
}
void keyevent(int k) {
    if(k == 27) {
        END = true;
        return;
    }
    sendFrame = 3;
    v->getEventQueue()->keyPress((osgGA::GUIEventAdapter::KeySymbol) k);
   
}
void wheelevent(const Msg& m) {
    float delta = m.delta;

    if(delta == 0) return;

    osgGA::GUIEventAdapter* ea = v->getEventQueue()->createEvent();
    ea->setTime(v->getEventQueue()->getTime());
    ea->setX(m.x);
    ea->setY(m.y);
    ea->setEventType(osgGA::GUIEventAdapter::SCROLL);
    if(delta > 0) {
        ea->setScrollingMotionDelta(0.0, delta);
        ea->setScrollingMotion(osgGA::GUIEventAdapter::SCROLL_UP);
    }
    else{
        ea->setScrollingMotionDelta(0.0, -delta);
        ea->setScrollingMotion(osgGA::GUIEventAdapter::SCROLL_DOWN);
    }
    v->getEventQueue()->addEvent(ea);
}


void resizeevent(int w, int h) {
    sendFrame = 10;
    v->getCamera()->getGraphicsContext()->resized(0, 0, w, h);      
    //the following does not resize the context but it is required to
    //make the manipulators work!
    v->getEventQueue()->windowResize(0, 0, w, h); 
}

void loadfiles(const std::string& f) {
    osg::ref_ptr<osg::Node> loadedModel = osgDB::readNodeFile(f);
    if (!loadedModel) {
        std::cout << f << " - no data loaded" << std::endl;
        return; 
    }
    osgUtil::Optimizer optimizer;
    optimizer.optimize(loadedModel.get());
    v->setSceneData(loadedModel.get());
    sendFrame = 2;
}

void HandleMessage() {
    if(!msgQueue.Empty()) {
        std::vector< char > v;
        msgQueue.Get(v);
        Msg msg(move(v));
        switch(msg.type) {
        case 1: {
            mousebutton(msg.buttons, 0, msg.x, msg.y );
        }
        break;
        case 2: {
            mousebutton(msg.buttons, 1, msg.x, msg.y );
        }
        case 3: {
            mousemove(msg.x,msg.y);
        }
        case 4: {
            keyevent(msg.key); 
        }
        break;
        case 5: {
            wheelevent(msg);
        }
        break;
        case 6: {
            resizeevent(msg.x, msg.y);
        }
        break;
        case 7: {
            loadfiles(msg.filename);
        }
        break;
        default: break;          
        }
    }
}



//------------------------------------------------------------------------------
class WindowCaptureCallback : public osg::Camera::DrawCallback {
    public:
        enum Mode { SINGLE_PBO, DOUBLE_PBO, TRIPLE_PBO };       
        struct ContextData : public osg::Referenced {
            ContextData(osg::GraphicsContext* gc, Mode mode, GLenum readBuffer):
                gc_(gc),
                mode_(mode),
                readBuffer_(readBuffer),
                pixelFormat_(GL_BGRA),
                type_(GL_UNSIGNED_BYTE),
                width_(0),
                height_(0),
                currentPboIndex_(0),
                tj_(tjhandle()) {
                if (gc && gc->getTraits()) {
                    if (gc->getTraits()->alpha) {
                        pixelFormat_ = GL_BGRA;
                    }
                    else {
                        pixelFormat_ = GL_BGR; 
                    }
                }
                width_ = 0;//gc->getTraits()->width;
                height_ = 0;// gc->getTraits()->height;
                // double buffer PBO.
                switch(mode_) {
                    case(SINGLE_PBO):
                        pboBuffer_.push_back(0);
                        break;
                    case(DOUBLE_PBO):
                        pboBuffer_.push_back(0);
                        pboBuffer_.push_back(0);
                        break;
                    case(TRIPLE_PBO):
                        pboBuffer_.push_back(0);
                        pboBuffer_.push_back(0);
                        pboBuffer_.push_back(0);
                        break;
                    default:
                        break;
                }
                 tj_ = tjInitCompress();
            }
            void getSize(osg::GraphicsContext* gc, int& width, int& height) {
                if(gc->getTraits()) {
                     width = gc->getTraits()->width;
                     height = gc->getTraits()->height;
                }
            }
            void read() {
                if(!sendFrame) return;
                osg::GLBufferObject::Extensions* ext =
                    osg::GLBufferObject::getExtensions(gc_->getState()
                        ->getContextID(), true);
                if (ext->isPBOSupported() && !pboBuffer_.empty()) {
                    if (pboBuffer_.size()==1) {
                        singlePBO(ext);
                    }
                    else {
                        multiPBO(ext);
                    }
                }
            }
            void singlePBO(osg::GLBufferObject::Extensions* ext);
            void multiPBO(osg::GLBufferObject::Extensions* ext);
            ~ContextData() {
                tjDestroy(tj_);
            }            
            typedef std::vector< GLuint > PBOBuffer;
            osg::GraphicsContext*   gc_;
            Mode                    mode_;
            GLenum                  readBuffer_;
            GLenum                  pixelFormat_;
            GLenum                  type_;
            int                     width_;
            int                     height_;         
            unsigned int            currentPboIndex_;
            PBOBuffer               pboBuffer_;
            tjhandle                tj_;
        };
        
        WindowCaptureCallback(Mode mode, GLenum readBuffer):
            mode_(mode),
            readBuffer_(readBuffer) {}  
        ContextData* createContextData(osg::GraphicsContext* gc) const {
            return new ContextData(gc, mode_, readBuffer_);
        }
        ContextData* getContextData(osg::GraphicsContext* gc) const {
            OpenThreads::ScopedLock<OpenThreads::Mutex> lock(_mutex);
            osg::ref_ptr<ContextData>& data = _contextDataMap[gc];
            if (!data) data = createContextData(gc);
            
            return data.get();
        }
        virtual void operator () (osg::RenderInfo& renderInfo) const {
            glReadBuffer(readBuffer_);
            osg::GraphicsContext* gc = 
                renderInfo.getState()->getGraphicsContext();
            osg::ref_ptr<ContextData> cd = getContextData(gc);
            cd->read();
        }
private:        
        typedef std::map< osg::GraphicsContext*, osg::ref_ptr< ContextData > >
            ContextDataMap;
        Mode                        mode_;     
        GLenum                      readBuffer_;
        mutable OpenThreads::Mutex  _mutex;
        mutable ContextDataMap      _contextDataMap;
};

void WindowCaptureCallback::ContextData::singlePBO(
                                        osg::GLBufferObject::Extensions* ext) {  
    int width = 0, height = 0;
    getSize(gc_, width, height);
    const int byteSize = width * height * (pixelFormat_ == GL_BGRA ? 4 : 3);   
    GLuint& pbo = pboBuffer_[0];
    if (width!=width_ || height_!=height) {
        width_ = width;
        height_ = height;
         if(pbo != 0)  { ext->glDeleteBuffers (1, &pbo); pbo = 0; }
         if (pbo==0) {
            ext->glGenBuffers(1, &pbo);
            ext->glBindBuffer(GL_PIXEL_PACK_BUFFER_ARB, pbo);
        }
        ext->glBufferData(GL_PIXEL_PACK_BUFFER_ARB, byteSize, 0,
                          GL_STREAM_READ);
    }
   
    if(pbo==0) {
        ext->glGenBuffers(1, &pbo);
        ext->glBindBuffer(GL_PIXEL_PACK_BUFFER_ARB, pbo);
        ext->glBufferData(GL_PIXEL_PACK_BUFFER_ARB, byteSize, 0,
                          GL_STREAM_READ);
    }
    else {
        ext->glBindBuffer(GL_PIXEL_PACK_BUFFER_ARB, pbo);
    }

    glReadPixels(0, 0, width_, height_, pixelFormat_, type_, 0);
    GLubyte* src = (GLubyte*)ext->glMapBuffer(GL_PIXEL_PACK_BUFFER_ARB,
                                              GL_READ_ONLY_ARB);
    if(src) {
        static int count = 0;
        char* out = nullptr;
        unsigned long size = 0;
        tjCompress2(tj_,
            (unsigned char*) src,
            width,
            width * (pixelFormat_ == GL_BGRA ? tjPixelSize[TJPF_RGBA] 
                                             : tjPixelSize[TJPF_RGB]),
            height,
            pixelFormat_ == GL_BGRA ? TJPF_BGRA : TJPF_BGR,
            (unsigned char **) &out,
            &size,
            cs, //444=best quality,
                        //420=fast and still unnoticeable but MIGHT NOT WORK
                        //IN SOME BROWSERS
            quality,
            TJXOP_VFLIP );    
            ext->glUnmapBuffer(GL_PIXEL_PACK_BUFFER_ARB);
            context->SetServiceDataSync(
                    Image(ImagePtr((char*) out, TJDeleter()), size, count++));
        
    }
    ext->glBindBuffer(GL_PIXEL_PACK_BUFFER_ARB, 0);
}

void WindowCaptureCallback::ContextData::multiPBO(
                                        osg::GLBufferObject::Extensions* ext) {
    unsigned int nextPboIndex = (currentPboIndex_+1) % pboBuffer_.size();
    int width=0, height=0;
    getSize(gc_, width, height);
    if(width < 1 || height < 1) return;
    const int byteSize = width * height * (pixelFormat_ == GL_BGRA ? 4 : 3);   
    if (width!=width_ || height_!=height) {
        width_ = width;
        height_ = height;
        for(auto& i: pboBuffer_) {
            if(i) ext->glDeleteBuffers (1, &i);
            ext->glGenBuffers(1, &i);
            ext->glBindBuffer(GL_PIXEL_PACK_BUFFER_ARB, i);
            ext->glBufferData(GL_PIXEL_PACK_BUFFER_ARB, byteSize, 0,
                              GL_STREAM_READ);

        }
    
    }
    GLuint& copy_pbo = pboBuffer_[currentPboIndex_];
    GLuint& read_pbo = pboBuffer_[nextPboIndex]; 
    ext->glBindBuffer(GL_PIXEL_PACK_BUFFER_ARB, read_pbo); 
    glReadPixels(0, 0, width, height, pixelFormat_, type_, 0);
    ext->glBindBuffer(GL_PIXEL_PACK_BUFFER_ARB, copy_pbo);
    static GLubyte* src = nullptr;
    src = (GLubyte*)ext->glMapBuffer(GL_PIXEL_PACK_BUFFER_ARB,
                                     GL_READ_ONLY_ARB);
#ifdef JPG_OWN_THREAD //creating one thread every time, use Executor instead 
    static future< void > jpg;
    static bool first = true;
    static vector< unsigned char > b;
#endif    
    if(src) {
        static Image img;
        static int count = 0;
        unsigned long size = 0;
        char* out = nullptr;
        size = tjBufSize(width, height, cs);
        if(img.size < size) {      
            out = (char*) tjAlloc(size);
            img.image.reset(out);
        }

#ifdef  JPG_OWN_THREAD      
        if(first) first = false;
        else jpg.wait();
        b.resize(byteSize);
        b.assign(src, src + byteSize);        
        jpg = async(launch::async, [this, ext, &out, width, height]() {
            unsigned long size = 0;
            tjCompress2(tj_,
            (unsigned char*) &b[0],
                width,
                width  * (pixelFormat_ == GL_BGRA ? tjPixelSize[TJPF_RGBA]
                                                  : tjPixelSize[TJPF_RGB]),
                height,
                pixelFormat_ == GL_BGRA ? TJPF_BGRA : TJPF_BGR,
                (unsigned char **) &out,
                &size,
                cs, //444=best quality,
                    //420=fast and still unnoticeable but MIGHT NOT WORK
                    //IN SOME BROWSERS
                quality,
                TJXOP_VFLIP);           
           
            context->SetServiceDataSync(
                          Image(ImagePtr((char*) out, TJDeleter()), size,
                                         count++));
            
        });
#else
        tjCompress2(tj_,
            (unsigned char*) src,
            width,
            width * (pixelFormat_ == GL_BGRA ? tjPixelSize[TJPF_RGBA] 
                                             : tjPixelSize[TJPF_RGB]),
            height,
            pixelFormat_ == GL_BGRA ? TJPF_BGRA : TJPF_BGR,
            (unsigned char **) &out,
            &size,
            cs, //444=best quality,
                        //420=fast and still unnoticeable but MIGHT NOT WORK
                        //IN SOME BROWSERS
            quality,
            TJXOP_VFLIP | TJFLAG_FASTDCT);    
#endif        
        ext->glUnmapBuffer(GL_PIXEL_PACK_BUFFER_ARB);
        img.size = size;
        img.id = count++;
        context->SetServiceDataSyncSwap(img);
    }
    ext->glBindBuffer(GL_PIXEL_PACK_BUFFER_ARB, 0);
    currentPboIndex_ = nextPboIndex;
}

//------------------------------------------------------------------------------
/// Image service: streams a sequence of images
class ImageService : public SessionService< wsp::Context< Image > > {

    using Context = wsp::Context< Image >;
public:

    using DataFrame = SessionService::DataFrame;
    ImageService(Context* c) :
     SessionService(c), ctx_(c) {
        InitDataFrame();
    }
    bool Data() const override { 
        if(img_.size > 0) return true;
        else {
            InitDataFrame();
            return false;
        }
    }
    //return data frame and update frame end
    const DataFrame& Get(int requestedChunkLength) {
        if(df_.frameEnd < df_.bufferEnd) {
           //frameBegin *MUST* be updated in the UpdateOutBuffer method
           //because in case the consumed data is less than requestedChunkLength
           df_.frameEnd += min((ptrdiff_t) requestedChunkLength, 
                               df_.bufferEnd - df_.frameEnd);
        } else {
           InitDataFrame();
        }
        return df_;  
    }
    //update frame begin/end
    void UpdateOutBuffer(int bytesConsumed) {
        df_.frameBegin += bytesConsumed;
        df_.frameEnd = df_.frameBegin;
    }
    //streaming: always in send mode, no receive
    bool Sending() const override { return true; }
    void Put(void* p, size_t len, bool done) override {
        in_.insert(in_.end(), (char*) p, (char*) p + len);
        if(done) {
            msgQueue.Put(move(in_));
            in_.resize(0);
        }
    }
    std::chrono::duration< double > 
    MinDelayBetweenWrites() const {
        //use 0.0
        return std::chrono::duration< double >(0.0);
    }
private:
    void InitDataFrame() const {
        if(ctx_->GetServiceData().id == img_.id) {
            if(dontSendIfEqual_) {
                //do not ever try to delete the memory in the smart pointer:
                //it is allocated in the main thread and deallocated when
                //the smart pointer counter reaches zero
                img_.size = 0;
                return;
            }
        }
        ctx_->GetServiceDataSync(img_);
        df_.bufferBegin = img_.image.get();
        df_.bufferEnd = df_.bufferBegin + img_.size;
        df_.frameBegin = df_.bufferBegin;
        df_.frameEnd = df_.frameBegin;
        df_.binary = true;
    }    
private:    
    mutable DataFrame df_;
    mutable Context* ctx_ = nullptr;
    mutable Image img_;
    bool dontSendIfEqual_ = true;
    vector< char > in_;
};

//------------------------------------------------------------------------------
std::string MapToString(
    const std::unordered_map< std::string, std::string >& m,
    const std::string& pre = "",
    const std::string& post = "<br/>") {
    std::ostringstream oss;
    for(auto& i: m) {
        oss << pre << i.first << ": " << i.second << post;
    }
    return oss.str();
}

std::string GetHomeDir() {
    const std::string h 
#ifdef WIN32
        = std::string(getenv("HOMEDRIVE")
                      + "/" //passing path to libwebsockets no need to convert
                      + std::string(getenv("HOMEPATH"));
#else
        = getenv("HOME");
#endif 
    return h;       
}

class HttpService {
public:
    using HTTP = int; //mark as http service
    using DataFrame = wsp::DataFrame;
    HttpService(wsp::Context<Image>* , const char* req, size_t len,
                const wsp::Request& m) :
    df_(nullptr, nullptr, nullptr, nullptr, false), reqHeader_(m) {
        request_.resize(len + 1);
        request_.assign(req, req + len);
        request_.push_back('\0');
        const string filePathRoot = GetHomeDir();
        std::string uri;
        if(wsp::Has(m, "GET URI")) uri = "GET URI";
        else if(wsp::Has(m, "POST URI")) uri = "POST URI";
        if(!uri.empty()) {
            if(!wsp::FileExtension(wsp::Get(m, uri)).empty()) {
                mimeType_ = wsp::GetMimeType(
                    wsp::FileExtension(wsp::Get(m, uri)));
                filePath_ = filePathRoot + wsp::Get(m, uri);
            } else ComposeResponse(MapToString(m) + "<br/>" + string(BODY)); 
        }
        
    }
    //Constructor(Context, unordered_map<string, string> headers)
    bool Valid() const { return true; }
    //return data frame and update frame end
    const DataFrame& Get(int requestedChunkLength) {
        if(df_.frameEnd < df_.bufferEnd) {
           sending_ = true;
           //frameBegin *MUST* be updated in the UpdateOutBuffer method
           //because in case the consumed data is less than requestedChunkLength
           df_.frameEnd += min((ptrdiff_t) requestedChunkLength, 
                               df_.bufferEnd - df_.frameEnd);
        } else {
           InitDataFrame();
           sending_ = false;
        }
        return df_;  
    }
    bool Sending() const { return sending_; }
    //update frame begin/end
    void UpdateOutBuffer(int bytesConsumed) {
        df_.frameBegin += bytesConsumed;
        df_.frameEnd = df_.frameBegin;
    }
    bool Data() const { return true; }
    int GetSuggestedOutChunkSize() const { return 0x1000; }
    const string& FilePath() const { return filePath_; }
    const string& FileMimeType() const { return mimeType_; }
    void Destroy() {}
    void ReceiveStart(size_t len, void* in) {}
    void Receive(size_t len, void* in) {}
    void ReceiveComplete(int len, void* in) {}
private:
    void InitDataFrame() const {
        df_.bufferBegin = &response_[0];
        df_.bufferEnd = &response_[0] + response_.size();
        df_.frameBegin = df_.bufferBegin;
        df_.frameEnd = df_.frameBegin;
    }
    void ComposeResponse(const std::string& data) {
         const string h = string("HTTP/1.0 200 OK\x0d\x0a"
                         "Server: websockets+\x0d\x0a"
                         "Content-Type: text/html\x0d\x0a" 
                         "Content-Length: ") 
                         + to_string(data.size())
                         + string("\x0d\x0a\x0d\x0a") + data;   

        response_.resize(h.size());
        response_.assign(h.begin(), h.end());
        InitDataFrame();   
    }
private:
    mutable bool sending_ = false; 
    string filePath_;
    string mimeType_;
    vector< char > request_;
    vector< char > response_;
    std::unordered_map< string, string > reqHeader_;
    static const char* BODY;
    mutable DataFrame df_;
};
const char* HttpService::BODY =
        "<!DOCTYPE html><head></head><html><body><p><em>Hello</em></p></body><"
        "/html>"; 

//------------------------------------------------------------------------------
int main(int argc, char** argv)
{
    // use an ArgumentParser object to manage the program arguments.
    osg::ArgumentParser arguments(&argc,argv);

    arguments.getApplicationUsage()->setApplicationName(
                                                arguments.getApplicationName());
    arguments.getApplicationUsage()->setCommandLineUsage(
                      arguments.getApplicationName()+" [options] filename ...");

    osgViewer::Viewer viewer(arguments);
    v = &viewer;
    unsigned int helpType = 0;
    if ((helpType = arguments.readHelpType())) {
        arguments.getApplicationUsage()->write(std::cout, helpType);
        return 1;
    }
    
    // report any errors if they have occurred when parsing the program
    // arguments.
    if (arguments.errors()) {
        arguments.writeErrorMessages(std::cout);
        return 1;
    }
    
    if (arguments.argc()<=1) {
        arguments.getApplicationUsage()->write(
                          std::cout,osg::ApplicationUsage::COMMAND_LINE_OPTION);
        return 1;
    }

    // set up the camera manipulators.
    {
        osg::ref_ptr<osgGA::KeySwitchMatrixManipulator> keyswitchManipulator
            = new osgGA::KeySwitchMatrixManipulator;

        keyswitchManipulator->addMatrixManipulator(
            '1', "Trackball", new osgGA::TrackballManipulator());
        keyswitchManipulator->addMatrixManipulator(
            '2', "Flight", new osgGA::FlightManipulator());
        keyswitchManipulator->addMatrixManipulator(
            '3', "Drive", new osgGA::DriveManipulator());
        keyswitchManipulator->addMatrixManipulator(
            '4', "Terrain", new osgGA::TerrainManipulator());
        keyswitchManipulator->addMatrixManipulator(
            '5', "Orbit", new osgGA::OrbitManipulator() );
        keyswitchManipulator->addMatrixManipulator(
            '6', "FirstPerson", new osgGA::FirstPersonManipulator() );
        keyswitchManipulator->addMatrixManipulator(
            '7', "Spherical", new osgGA::SphericalManipulator() );

        viewer.setCameraManipulator(keyswitchManipulator.get());
    }

    // add the state manipulator
    viewer.addEventHandler(new osgGA::StateSetManipulator(
                                    viewer.getCamera()->getOrCreateStateSet()));

    // add the thread model handler
    viewer.addEventHandler(new osgViewer::ThreadingHandler);

    // add the LOD Scale handler
    viewer.addEventHandler(new osgViewer::LODScaleHandler);

   
    GLenum readBuffer = GL_BACK;
    WindowCaptureCallback::Mode mode = WindowCaptureCallback::DOUBLE_PBO;

    while (arguments.read("--single-pbo")) 
        mode = WindowCaptureCallback::SINGLE_PBO;
    while (arguments.read("--double-pbo")) 
        mode = WindowCaptureCallback::DOUBLE_PBO;
    while (arguments.read("--triple-pbo")) 
        mode = WindowCaptureCallback::TRIPLE_PBO;
        
    unsigned int width=1440;
    unsigned int height=900;
    arguments.read("--view-size", width, height);
    osg::ref_ptr<osg::GraphicsContext> pbuffer;
    osg::ref_ptr<osg::GraphicsContext::Traits> traits 
        = new osg::GraphicsContext::Traits;
    traits->x = 0;
    traits->y = 0;
    traits->width = width;
    traits->height = height;
    traits->red = 8;
    traits->green = 8;
    traits->blue = 8;
    traits->alpha = 8;
    traits->windowDecoration = false;
    traits->pbuffer = true;
    traits->doubleBuffer = true;
    traits->sharedContext = 0;
    pbuffer = osg::GraphicsContext::createGraphicsContext(traits.get());
  
     // add the window size toggle handler
    //viewer.addEventHandler(new osgViewer::WindowSizeHandler);
    if(!pbuffer.valid()) {
        osg::notify(osg::FATAL)
            << "Pixel buffer has not been created successfully." <<std::endl;
        return -1;
    } 
    // load the data
    osg::ref_ptr<osg::Node> loadedModel = osgDB::readNodeFiles(arguments);
    if (!loadedModel) {
        std::cout << arguments.getApplicationName() 
                  <<": No data loaded" << std::endl;
        return 1;
    }
    // any option left unread are converted into errors to write out later.
    arguments.reportRemainingOptionsAsUnrecognized();

    // report any errors if they have occurred when parsing the program
    // arguments.
    if (arguments.errors()) {
        arguments.writeErrorMessages(std::cout);
        return 1;
    }

    // optimize the scene graph, remove redundant nodes and state etc.
    osgUtil::Optimizer optimizer;
    optimizer.optimize(loadedModel.get());

    viewer.setSceneData( loadedModel.get() );

    //==========================================================================
    using WSS = wsp::WebSocketService;
    WSS imageStreamer;
    //HTTP SERVICE MUST ALWAYS BE THE FIRST !!!!
    //WSS::ResetLogLevels(); 
    //init service
    auto is = async(launch::async, [](WSS& imageStreamer){
        imageStreamer.Init(5000, //port
                           nullptr, //SSL certificate path
                           nullptr, //SSL key path
                           context, //context instance,
                           //will be copied internally
                           WSS::Entry< HttpService,
                                       WSS::ASYNC_REP>("http-only"),
                           WSS::Entry< ImageService,
                                       WSS::ASYNC_REP >("image-stream"));
         //start event loop: one iteration every >= 50ms
        imageStreamer.StartLoop(1, //ms
                 [](){return !END;} //termination condition (exit on false)
                                  //checked at each iteration, loops forever
                                  //in this case
                 );

    }, std::ref(imageStreamer));
    //==========================================================================
    osg::ref_ptr<osg::Camera> camera = viewer.getCamera();// new osg::Camera;
    camera->setGraphicsContext(pbuffer.get());
    camera->setViewport(new osg::Viewport(0,0,width, height));
    GLenum buffer = pbuffer->getTraits()->doubleBuffer ? GL_BACK : GL_FRONT;
    camera->setDrawBuffer(buffer);
    camera->setReadBuffer(buffer);
    camera->setFinalDrawCallback(new WindowCaptureCallback(mode, readBuffer));
    camera->setProjectionResizePolicy(osg::Camera::VERTICAL);
    camera->setProjectionMatrixAsPerspective(30.0f, 
        static_cast<double>(width)/static_cast<double>(height), 1.0f, 10000.0f);
    viewer.realize();
   
    //required to make sending events work in offscreen pbo-only mode!
    viewer.getEventQueue()->windowResize(0, 0, width, height);  
    

    class CameraUpdateCallback : public osg::NodeCallback {
    public:
        CameraUpdateCallback(int& q, int& s) : quality_(q), sendFrame_(s) {}
         virtual void operator()(osg::Node* node, osg::NodeVisitor* nv) {
            traverse(node,nv);
            osg::Camera* c = (osg::Camera*) node;
            if(c->getViewMatrix() != d) {
                int& q = quality;
                q = minQuality_;
                int& s = sendFrame_;
                s = 2;
                d = c->getViewMatrix();
            } else {
                int& q = quality;
                q = maxQuality_;
                int& s = sendFrame_;
                if(s > 0) --s;
            }
         }
    private:
         osg::Matrixd d;
         int minQuality_ = 10;
         int maxQuality_ = 80;
         reference_wrapper< int > quality_;
         reference_wrapper< int > sendFrame_;
     };
 

    viewer.getCamera()->setUpdateCallback(
                                    new CameraUpdateCallback(quality,
                                                             sendFrame));

    using namespace chrono;
    const microseconds T(int(1E6/60.0));
    while(!END) {
        steady_clock::time_point t = steady_clock::now(); 
        HandleMessage();
        viewer.frame();
        const microseconds E =
                         duration_cast< microseconds >(steady_clock::now() - t);
#ifdef TIME_RENDER_STEP
        cout << double(E.count()) / 1000 << ' ';
#endif                                
        std::this_thread::sleep_for(
            max(duration_values< microseconds >::zero(), T - E));
    }
    is.wait();
    return 0;
}
