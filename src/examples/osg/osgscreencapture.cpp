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

//g++ -std=c++11  ../src/examples/osg/osgscreencapture.cpp -I /usr/local/osg/include -L /usr/local/osg/lib64 -lOpenThreads -losgDB -losgGA -losgManipulator -losg -losgUtil -losgViewer -losgText -lglut -lGL -I /opt/libjpeg-turbo/include -L /opt/libjpeg-turbo/lib64  -lturbojpeg -I /usr/local/libwebsockets/include -L /usr/local/libwebsockets/lib -lwebsockets -O3 -pthread
//==============================================================================
#include <thread>
#include <future>
#include <memory>
#include <chrono>
#include <map>
#include <deque>

#include <turbojpeg.h>

#include "../../WebSocketService.h"
#include "../../Context.h"
#include "../SessionService.h"
using namespace std;

bool END = false;

bool MouseEvent(int e) {
    return e >= 1 && e <= 3;
}

bool KeyEvent(int e) {
    return e == 4;
}

struct Msg {
    Msg(vector< char >&& d) {
        int* p = reinterpret_cast< int* >(&d[0]);
        type = p[0];
        if(MouseEvent(type)) {
            x = p[1];
            y = p[2];
        } else if(KeyEvent(type)) {
            key = p[1];
            if(!(p[2] & 0x100)) key = tolower(key);
        }
    }
    int type = -1;
    int x = -1;
    int y = -1;
    int key = -1;
};

osgViewer::Viewer* v;
void mousebutton( int button, int state, int x, int y )
{
   
    if (state==0) v->getEventQueue()->mouseButtonPress( x, y, button+1 );
    else v->getEventQueue()->mouseButtonRelease( x, y, button+1 );

}

void mousemove( int x, int y )
{
    {
        v->getEventQueue()->mouseMotion( x, y );
    }
}



void keyevent(int k) {
    if(k == 27) {
        END = true;
        return;
    }
    v->getEventQueue()->keyPress((osgGA::GUIEventAdapter::KeySymbol) k);
}


class WindowCaptureCallback : public osg::Camera::DrawCallback
{
    public:
    
        enum Mode
        {
            SINGLE_PBO,
            DOUBLE_PBO,
            TRIPLE_PBO
        };
    
         
        struct ContextData : public osg::Referenced
        {
        
            ContextData(osg::GraphicsContext* gc, Mode mode, GLenum readBuffer, const std::string& name):
                _gc(gc),
                _mode(mode),
                _readBuffer(readBuffer),
                _pixelFormat(GL_BGRA),
                _type(GL_UNSIGNED_BYTE),
                _width(0),
                _height(0),
                _currentPboIndex(0)

            {

                if (gc && gc->getTraits())
                {
                    if (gc->getTraits()->alpha)
                    {
                        osg::notify(osg::NOTICE)<<"Select GL_BGRA read back format"<<std::endl;
                        _pixelFormat = GL_BGRA;
                    }
                    else 
                    {
                        osg::notify(osg::NOTICE)<<"Select GL_BGR read back format"<<std::endl;
                        _pixelFormat = GL_BGR; 
                    }
                }
            
                getSize(gc, _width, _height);

                // double buffer PBO.
                switch(_mode) {
                    case(SINGLE_PBO):
                        _pboBuffer.push_back(0);
                        break;
                    case(DOUBLE_PBO):
                        _pboBuffer.push_back(0);
                        _pboBuffer.push_back(0);
                        break;
                    case(TRIPLE_PBO):
                        _pboBuffer.push_back(0);
                        _pboBuffer.push_back(0);
                        _pboBuffer.push_back(0);
                        break;
                    default:
                        break;
                }
            }
            
            void getSize(osg::GraphicsContext* gc, int& width, int& height)
            {
                width = width_;
                height = height_;
            }
            
            void read()
            {
                osg::GLBufferObject::Extensions* ext = osg::GLBufferObject::getExtensions(_gc->getState()->getContextID(),true);

                if (ext->isPBOSupported() && !_pboBuffer.empty())
                {
                    if (_pboBuffer.size()==1)
                    {
                        singlePBO(ext);
                    }
                    else
                    {
                        multiPBO(ext);
                    }
                }
              
            }
            
            ~ContextData() {
                //osg::GLBufferObject::Extensions* ext = osg::GLBufferObject::getExtensions(_gc->getState()->getContextID(),true);
                //for(auto& i: _pboBuffer) if(i) ext->glDeleteBuffers(1, &i);
            }

            void singlePBO(osg::GLBufferObject::Extensions* ext);

            void multiPBO(osg::GLBufferObject::Extensions* ext);
        
            typedef std::vector< GLuint > PBOBuffer;
        
            osg::GraphicsContext*   _gc;
            Mode                    _mode;
            GLenum                  _readBuffer;
            
            GLenum                  _pixelFormat;
            GLenum                  _type;
            int                     _width;
            int                     _height;
                      
            unsigned int            _currentPboIndex;
            PBOBuffer               _pboBuffer;

            int width_;
            int height_;
        };
    
        WindowCaptureCallback(Mode mode, GLenum readBuffer):
            _mode(mode),
            _readBuffer(readBuffer)
        {
        }

        ContextData* createContextData(osg::GraphicsContext* gc) const
        {
            return new ContextData(gc, _mode, _readBuffer, "");
        }
        
        ContextData* getContextData(osg::GraphicsContext* gc) const
        {
            OpenThreads::ScopedLock<OpenThreads::Mutex> lock(_mutex);
            osg::ref_ptr<ContextData>& data = _contextDataMap[gc];
            if (!data) data = createContextData(gc);
            
            return data.get();
        }

        virtual void operator () (osg::RenderInfo& renderInfo) const
        {
            glReadBuffer(_readBuffer);

            osg::GraphicsContext* gc = renderInfo.getState()->getGraphicsContext();
            osg::ref_ptr<ContextData> cd = getContextData(gc);
            cd->read();
            cd->width_ = renderInfo.getCurrentCamera()->getViewport()->width();
            cd->height_ = renderInfo.getCurrentCamera()->getViewport()->height();
                    
        }
        
        typedef std::map<osg::GraphicsContext*, osg::ref_ptr<ContextData> > ContextDataMap;

        Mode                        _mode;     
        GLenum                      _readBuffer;
        mutable OpenThreads::Mutex  _mutex;
        mutable ContextDataMap      _contextDataMap;
        
};

//==============================================================================
struct TJDeleter {
    void operator()(char* p) const {
        tjFree((unsigned char*) p);
    }
};

using ImagePtr = shared_ptr< char >;

template < typename T >
class SyncQueue {
public:    
    T Get() {
        std::lock_guard< std::mutex > guard(mutex_);
        const T d(q_.front());
        q_.pop_front();
        return d; 
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

SyncQueue< vector< char > > msgQueue;

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
shared_ptr< wsp::Context< Image > > context(new wsp::Context< Image >);
tjhandle tj = tjhandle();
int cs = TJSAMP_444;
int quality = 50;
void HandleMessage() {
    if(!msgQueue.Empty()) {
        Msg msg(msgQueue.Get());
        switch(msg.type) {
        case 1: {
            mousebutton(0, 0, msg.x, msg.y );
        }
        break;
        case 2: {
            mousebutton(0, 1, msg.x, msg.y );
            quality = 75;
        }
        case 3: {
            mousemove(msg.x,msg.y);
            quality = 25;
        }
        case 4: {
            keyevent(msg.key); 
        }
        break;
        default: break;          
        }
    }
    //
}

//==============================================================================

void WindowCaptureCallback::ContextData::singlePBO(osg::GLBufferObject::Extensions* ext) {  
    int width = 0, height = 0;
    getSize(_gc, width, height);
    const int byteSize = width * height * (_pixelFormat == GL_BGRA ? 4 : 3);   
    GLuint& pbo = _pboBuffer[0];
    if (width!=_width || _height!=height) {
        _width = width;
        _height = height;
         if(pbo != 0)  { ext->glDeleteBuffers (1, &pbo); pbo = 0; }
         if (pbo==0) {
            ext->glGenBuffers(1, &pbo);
            ext->glBindBuffer(GL_PIXEL_PACK_BUFFER_ARB, pbo);
        }
        ext->glBufferData(GL_PIXEL_PACK_BUFFER_ARB, byteSize, 0, GL_STREAM_READ);
    }
   
    if(pbo==0) {
        ext->glGenBuffers(1, &pbo);
        ext->glBindBuffer(GL_PIXEL_PACK_BUFFER_ARB, pbo);
        ext->glBufferData(GL_PIXEL_PACK_BUFFER_ARB, byteSize, 0, GL_STREAM_READ);
    }
    else {
        ext->glBindBuffer(GL_PIXEL_PACK_BUFFER_ARB, pbo);
    }

    glReadPixels(0, 0, _width, _height, _pixelFormat, _type, 0);
    GLubyte* src = (GLubyte*)ext->glMapBuffer(GL_PIXEL_PACK_BUFFER_ARB,
                                              GL_READ_ONLY_ARB);
    if(src) {
        static int count = 0;
        char* out = nullptr;
        unsigned long size = 0;
        tjCompress2(tj,
            (unsigned char*) src,
            width,
            width * (_pixelFormat == GL_BGRA ? tjPixelSize[TJPF_RGBA] : tjPixelSize[TJPF_RGB]),
            height,
            TJPF_BGRA,
            (unsigned char **) &out,
            &size,
            cs, //444=best quality,
                        //420=fast and still unnoticeable but MIGHT NOT WORK
                        //IN SOME BROWSERS
            quality,
            TJXOP_VFLIP );    
            ext->glUnmapBuffer(GL_PIXEL_PACK_BUFFER_ARB);
            context->SetServiceDataSync(Image(ImagePtr((char*) out, TJDeleter()), size, count++));
        
    }
    ext->glBindBuffer(GL_PIXEL_PACK_BUFFER_ARB, 0);
}

void WindowCaptureCallback::ContextData::multiPBO(osg::GLBufferObject::Extensions* ext)
{
    unsigned int nextPboIndex = (_currentPboIndex+1)%_pboBuffer.size();
    int width=0, height=0;
    getSize(_gc, width, height);
    GLuint& copy_pbo = _pboBuffer[_currentPboIndex];
    GLuint& read_pbo = _pboBuffer[nextPboIndex];
    const int byteSize = width * height * (_pixelFormat == GL_BGRA ? 4 : 3);   
    if (width!=_width || _height!=height) {
        _width = width;
        _height = height;
        if(read_pbo != 0) ext->glDeleteBuffers (1, &read_pbo);
        ext->glGenBuffers(1, &read_pbo);
        ext->glBindBuffer(GL_PIXEL_PACK_BUFFER_ARB, read_pbo);
        ext->glBufferData(GL_PIXEL_PACK_BUFFER_ARB, byteSize, 0, GL_STREAM_READ);
        if(copy_pbo != 0) ext->glDeleteBuffers (1, &copy_pbo);
        ext->glGenBuffers(1, &copy_pbo);
        ext->glBindBuffer(GL_PIXEL_PACK_BUFFER_ARB, copy_pbo);
        ext->glBufferData(GL_PIXEL_PACK_BUFFER_ARB, byteSize, 0, GL_STREAM_READ);
    }

    if(copy_pbo == 0) {
        ext->glGenBuffers(1, &copy_pbo);
        ext->glBindBuffer(GL_PIXEL_PACK_BUFFER_ARB, copy_pbo);
        ext->glBufferData(GL_PIXEL_PACK_BUFFER_ARB, byteSize, 0, GL_STREAM_READ);
    }
    if(read_pbo == 0) {
        ext->glGenBuffers(1, &read_pbo);
        ext->glBindBuffer(GL_PIXEL_PACK_BUFFER_ARB, read_pbo);
        ext->glBufferData(GL_PIXEL_PACK_BUFFER_ARB, byteSize, 0, GL_STREAM_READ);
    }

    ext->glBindBuffer(GL_PIXEL_PACK_BUFFER_ARB, read_pbo); 
    glReadPixels(0, 0, width, height, _pixelFormat, _type, 0);
    ext->glBindBuffer(GL_PIXEL_PACK_BUFFER_ARB, copy_pbo);
    GLubyte* src = (GLubyte*)ext->glMapBuffer(GL_PIXEL_PACK_BUFFER_ARB,
                                              GL_READ_ONLY_ARB);
    if(src) {
        static int count = 0;
        char* out = nullptr;
        unsigned long size = 0;
        tjCompress2(tj,
        (unsigned char*) src,
            width,
            width  * (_pixelFormat == GL_BGRA ? tjPixelSize[TJPF_RGBA] : tjPixelSize[TJPF_RGB]),
            height,
            TJPF_BGRA,
            (unsigned char **) &out,
            &size,
            cs, //444=best quality,
                //420=fast and still unnoticeable but MIGHT NOT WORK
                //IN SOME BROWSERS
            quality,
            TJXOP_VFLIP );    
        
        ext->glUnmapBuffer(GL_PIXEL_PACK_BUFFER_ARB);
        context->SetServiceDataSync(Image(ImagePtr((char*) out, TJDeleter()), size, count++));
    }
    ext->glBindBuffer(GL_PIXEL_PACK_BUFFER_ARB, 0);
    _currentPboIndex = nextPboIndex;
}


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
        return std::chrono::duration< double >(0.001);
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
        img_ = ctx_->GetServiceDataSync();
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

int main(int argc, char** argv)
{
    // use an ArgumentParser object to manage the program arguments.
    osg::ArgumentParser arguments(&argc,argv);

    arguments.getApplicationUsage()->setApplicationName(arguments.getApplicationName());
    arguments.getApplicationUsage()->setCommandLineUsage(arguments.getApplicationName()+" [options] filename ...");

    osgViewer::Viewer viewer(arguments);
    v = &viewer;
    unsigned int helpType = 0;
    if ((helpType = arguments.readHelpType()))
    {
        arguments.getApplicationUsage()->write(std::cout, helpType);
        return 1;
    }
    
    // report any errors if they have occurred when parsing the program arguments.
    if (arguments.errors())
    {
        arguments.writeErrorMessages(std::cout);
        return 1;
    }
    
    if (arguments.argc()<=1)
    {
        arguments.getApplicationUsage()->write(std::cout,osg::ApplicationUsage::COMMAND_LINE_OPTION);
        return 1;
    }

    // set up the camera manipulators.
    {
        osg::ref_ptr<osgGA::KeySwitchMatrixManipulator> keyswitchManipulator = new osgGA::KeySwitchMatrixManipulator;

        keyswitchManipulator->addMatrixManipulator( '1', "Trackball", new osgGA::TrackballManipulator() );
        keyswitchManipulator->addMatrixManipulator( '2', "Flight", new osgGA::FlightManipulator() );
        keyswitchManipulator->addMatrixManipulator( '3', "Drive", new osgGA::DriveManipulator() );
        keyswitchManipulator->addMatrixManipulator( '4', "Terrain", new osgGA::TerrainManipulator() );
        keyswitchManipulator->addMatrixManipulator( '5', "Orbit", new osgGA::OrbitManipulator() );
        keyswitchManipulator->addMatrixManipulator( '6', "FirstPerson", new osgGA::FirstPersonManipulator() );
        keyswitchManipulator->addMatrixManipulator( '7', "Spherical", new osgGA::SphericalManipulator() );

        std::string pathfile;
        char keyForAnimationPath = '5';
        while (arguments.read("-p",pathfile))
        {
            osgGA::AnimationPathManipulator* apm = new osgGA::AnimationPathManipulator(pathfile);
            if (apm || !apm->valid()) 
            {
                unsigned int num = keyswitchManipulator->getNumMatrixManipulators();
                keyswitchManipulator->addMatrixManipulator( keyForAnimationPath, "Path", apm );
                keyswitchManipulator->selectMatrixManipulator(num);
                ++keyForAnimationPath;
            }
        }

        viewer.setCameraManipulator( keyswitchManipulator.get() );
    }

    // add the state manipulator
    viewer.addEventHandler(new osgGA::StateSetManipulator(viewer.getCamera()->getOrCreateStateSet()));

    // add the thread model handler
    viewer.addEventHandler(new osgViewer::ThreadingHandler);

    // add the window size toggle handler
    viewer.addEventHandler(new osgViewer::WindowSizeHandler);
        
    // add the stats handler
    viewer.addEventHandler(new osgViewer::StatsHandler);

    // add the help handler
    viewer.addEventHandler(new osgViewer::HelpHandler(arguments.getApplicationUsage()));

    // add the record camera path handler
    viewer.addEventHandler(new osgViewer::RecordCameraPathHandler);

    // add the LOD Scale handler
    viewer.addEventHandler(new osgViewer::LODScaleHandler);

   
    GLenum readBuffer = GL_BACK;
    WindowCaptureCallback::Mode mode = WindowCaptureCallback::DOUBLE_PBO;

    while (arguments.read("--single-pbo")) mode = WindowCaptureCallback::SINGLE_PBO;
    while (arguments.read("--double-pbo")) mode = WindowCaptureCallback::DOUBLE_PBO;
    while (arguments.read("--triple-pbo")) mode = WindowCaptureCallback::TRIPLE_PBO;
    
    
    unsigned int width=1440;
    unsigned int height=900;
    arguments.read("--ss", width, height);
    osg::ref_ptr<osg::GraphicsContext> pbuffer;
    osg::ref_ptr<osg::GraphicsContext::Traits> traits = new osg::GraphicsContext::Traits;
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
  
    if(!pbuffer.valid()) {
        osg::notify(osg::FATAL)<<"Pixel buffer has not been created successfully."<<std::endl;
        return -1;
    }

 
    // load the data
    osg::ref_ptr<osg::Node> loadedModel = osgDB::readNodeFiles(arguments);
    if (!loadedModel) 
    {
        std::cout << arguments.getApplicationName() <<": No data loaded" << std::endl;
        return 1;
    }

    // any option left unread are converted into errors to write out later.
    arguments.reportRemainingOptionsAsUnrecognized();

    // report any errors if they have occurred when parsing the program arguments.
    if (arguments.errors())
    {
        arguments.writeErrorMessages(std::cout);
        return 1;
    }


    // optimize the scene graph, remove redundant nodes and state etc.
    osgUtil::Optimizer optimizer;
    optimizer.optimize(loadedModel.get());

    viewer.setSceneData( loadedModel.get() );

    //==========================================================================
    tj = tjInitCompress();
    using WSS = wsp::WebSocketService;
    WSS imageStreamer;
    //WSS::ResetLogLevels(); 
    //init service
    auto is = async(launch::async, [](WSS& imageStreamer){
        imageStreamer.Init(5000, //port
                           nullptr, //SSL certificate path
                           nullptr, //SSL key path
                           context, //context instance,
                           //will be copied internally
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
    camera->setProjectionMatrixAsPerspective(30.0f, static_cast<double>(width)/static_cast<double>(height), 1.0f, 10000.0f);
    viewer.realize();
   
   
    //required to make sending events work in offscreen pbo-only mode!
    viewer.getEventQueue()->windowResize(0, 0, width, height);  
    
    using namespace chrono;
    const microseconds T(int(1E6/60.0));
    while(!END) {
        steady_clock::time_point t = steady_clock::now(); 
        HandleMessage();
        //camera->setProjectionMatrixAsPerspective(30.0f, static_cast<double>(width)/static_cast<double>(height), 1.0f, 10000.0f);
        // viewer.update();
        // viewer.updateTraversal();
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
