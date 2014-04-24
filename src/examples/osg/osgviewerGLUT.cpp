//Streaming OpenGL buffer to remote clients.

//LINUX:
//g++ -std=c++11  ../src/examples/osg/osgviewerGLUT.cpp -I /usr/local/osg/include -L /usr/local/osg/lib64 -lOpenThreads -losgDB -losgGA -losgManipulator -losg -losgUtil -losgViewer -losgText -lglut -lGL -I /opt/libjpeg-turbo/include -L /opt/libjpeg-turbo/lib64  -lturbojpeg -I /usr/local/libwebsockets/include -L /usr/local/libwebsockets/lib -lwebsockets -O3 -pthread

//APPLE:
//clang++ -std=c++11  -stdlib=libc++ ../src/examples/osg/osgviewerGLUT.cpp -framework GLUT -I /usr/local/osg/include -L /usr/local/osg/lib -lOpenThreads -losgDB -losgGA -losgManipulator -losg -losgUtil -losgViewer -losgText  -framework OpenGL -I /opt/libjpeg-turbo/include -L /opt/libjpeg-turbo/lib  -lturbojpeg -I /usr/local/libwebsockets/include -L /usr/local/libwebsockets/lib -lwebsockets -O3 -pthread

#include <cassert>

#include <osg/Config>

#if defined(_MSC_VER) && defined(OSG_DISABLE_MSVC_WARNINGS)
    // disable warning "glutCreateMenu_ATEXIT_HACK' : unreferenced local function has been removed"
    #pragma warning( disable : 4505 )
#endif

#include <iostream>
#ifdef WIN32
#include <windows.h>
#endif

#ifdef __APPLE__
#  include <GLUT/glut.h>
#else
#  include <GL/glut.h>
#endif

#include <osg/GLExtensions>
#include <osgViewer/Viewer>
#include <osgViewer/ViewerEventHandlers>
#include <osgGA/TrackballManipulator>
#include <osgDB/ReadFile>

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
//------------------------------------------------------------------------------
#ifndef __APPLE__
typedef void (*glBindBufferTYPE)(GLenum, GLuint); 
glBindBufferTYPE glBindBuffer;
typedef void* (*glMapBufferTYPE)(GLenum, GLenum); 
glMapBufferTYPE glMapBuffer;
typedef GLboolean (*glUnmapBufferTYPE)(GLenum); 
glUnmapBufferTYPE glUnmapBuffer;
typedef void (*glBufferDataTYPE)(GLenum, GLsizeiptr, const GLvoid *, GLenum);
glBufferDataTYPE glBufferData;
typedef void (*glGenBuffersTYPE)(GLsizei, GLuint*);
glGenBuffersTYPE glGenBuffers;
typedef void (*glDeleteBuffersTYPE)(GLsizei, const GLuint*);
glDeleteBuffersTYPE glDeleteBuffers;
#endif

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

tjhandle tj = tjhandle();
GLuint pbo[2];
int width = 0;
int height = 0;
int cs = TJSAMP_444;
int quality = 50;
shared_ptr< wsp::Context< Image > > context(new wsp::Context< Image >);
bool END = false;
Image ReadImage() {
    //cout << width << ' ' << height << endl;
    static int count = 0;
    static int index = 0;
    static int nextIndex = 0;
    glReadBuffer(GL_BACK);
#ifdef TIME_READPIXEL    
    using namespace std::chrono;
    steady_clock::time_point t = steady_clock::now(); 
#endif
    index = (index + 1) % 2;
    nextIndex = (index + 1) % 2;    

    glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo[index]);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, 0);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo[nextIndex]);
    char* glout = (char* ) glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
    if(!glout) {
       glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
       glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);    
       return Image();
    }
    
#ifdef TIME_READPIXEL    
    const milliseconds E =
                        duration_cast< milliseconds >(steady_clock::now() - t);
    cout << E.count() << ' '; //doesn't flush                   
#endif    
    char* out = nullptr;
    unsigned long size = 0;
    tjCompress2(tj,
        (unsigned char*) glout,
        width,
        tjPixelSize[TJPF_RGB] * width,
        height,
        TJPF_RGB,
        (unsigned char **) &out,
        &size,
        cs, //444=best quality,
                    //420=fast and still unnoticeable but MIGHT NOT WORK
                    //IN SOME BROWSERS
        quality,
        TJXOP_VFLIP ); 
    glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);   
    return Image(ImagePtr((char*) out, TJDeleter()), size, count++);
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
        return std::chrono::duration< double >(0.000);
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

//==============================================================================

osg::ref_ptr<osgViewer::Viewer> viewer;
osg::observer_ptr<osgViewer::GraphicsWindow> window;

//should the msg parsing be done in the websocket thread or main thread ?
//only getting 3 ints in each message: type, x, y
struct Msg {
    Msg(vector< char >&& d) {
        int* p = reinterpret_cast< int* >(&d[0]);
        type = p[0];
        x = p[1];
        y = p[2];
    }
    int type = -1;
    int x = -1;
    int y = -1;
};

void mousebutton( int button, int state, int x, int y );
void mousemove(int x, int y );
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
        }
        case 3: {
            mousemove(msg.x,msg.y);
        }
        break;
        default: break;          
        }
    }
    //
}

void idle() {
   HandleMessage();
}

void display(void)
{   
   
    // update and render the scene graph
    if (viewer.valid()) viewer->frame();
    context->SetServiceDataSync(ReadImage());
    glutPostRedisplay();
    // Swap Buffers
    //comment for faster speed
   // glutSwapBuffers();
    //glutPostRedisplay();

} 

void reshape( int w, int h )
{
    width = w;
    height = h;
    glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo[0]);
    glBufferData(GL_PIXEL_PACK_BUFFER, width * height * 3, 0,
                 GL_DYNAMIC_READ);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo[1]);
    glBufferData(GL_PIXEL_PACK_BUFFER, width * height * 3, 0,
                 GL_DYNAMIC_READ);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);      
    // update the window dimensions, in case the window has been resized.
    if (window.valid()) 
    {
        window->resized(window->getTraits()->x, window->getTraits()->y, w, h);
        window->getEventQueue()->windowResize(window->getTraits()->x, window->getTraits()->y, w, h );
    }
}

void mousebutton( int button, int state, int x, int y )
{
    if (window.valid())
    {

        if (state==0) window->getEventQueue()->mouseButtonPress( x, y, button+1 );
        else window->getEventQueue()->mouseButtonRelease( x, y, button+1 );
    }
}

void mousemove( int x, int y )
{
    if (window.valid())
    {
        window->getEventQueue()->mouseMotion( x, y );
    }
}

void keyboard( unsigned char key, int /*x*/, int /*y*/ )
{
    switch( key )
    {
        case 27:
            // clean up the viewer 
            if (viewer.valid()) viewer = 0;
            glutDestroyWindow(glutGetWindow());
            END = true;
            break;
        default:
            if (window.valid())
            {
                window->getEventQueue()->keyPress( (osgGA::GUIEventAdapter::KeySymbol) key );
                window->getEventQueue()->keyRelease( (osgGA::GUIEventAdapter::KeySymbol) key );
            }
            break;
    }
}

int main( int argc, char **argv )
{
    glutInit(&argc, argv);

    if (argc<2)
    {
        std::cout << argv[0] <<": requires filename argument." << std::endl;
        return 1;
    }

    // load the scene.
    osg::ref_ptr<osg::Node> loadedModel = osgDB::readNodeFile(argv[1]);
    if (!loadedModel)
    {
        std::cout << argv[0] <<": No data loaded." << std::endl;
        return 1;
    }
#ifndef __APPLE__
    glBindBuffer = (glBindBufferTYPE) osg::getGLExtensionFuncPtr("glBindBuffer");
    glMapBuffer = (glMapBufferTYPE) osg::getGLExtensionFuncPtr("glMapBuffer");
    glUnmapBuffer = (glUnmapBufferTYPE) osg::getGLExtensionFuncPtr("glUnmapBuffer");
    glGenBuffers = (glGenBuffersTYPE) osg::getGLExtensionFuncPtr("glGenBuffers");
    glBufferData = (glBufferDataTYPE) osg::getGLExtensionFuncPtr("glBufferData");
    glDeleteBuffers = (glDeleteBuffersTYPE) osg::getGLExtensionFuncPtr("glDeleteBuffers");
    assert(glBindBuffer && glMapBuffer && glUnmapBuffer && glGenBuffers
           && glBufferData && glDeleteBuffers );
#endif    

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
        imageStreamer.StartLoop(5, //ms
                 [](){return !END;} //termination condition (exit on false)
                                  //checked at each iteration, loops forever
                                  //in this case
                 );

    }, std::ref(imageStreamer));
    
    //==========================================================================


    glutInitDisplayMode( GLUT_DOUBLE | GLUT_RGBA | GLUT_DEPTH | GLUT_ALPHA );
    glutInitWindowPosition( 100, 100 );
    glutInitWindowSize( 1280, 720 );
    glutCreateWindow( argv[0] );
    glutDisplayFunc( display );
    glutReshapeFunc( reshape );
    glutMouseFunc( mousebutton );
    glutMotionFunc( mousemove );
    glutKeyboardFunc( keyboard );
    glutIdleFunc(idle);

    viewer = new osgViewer::Viewer;
    window = viewer->setUpViewerAsEmbeddedInWindow(0,0,1280,720);
    viewer->setSceneData(loadedModel.get());
    viewer->setCameraManipulator(new osgGA::TrackballManipulator);
    viewer->addEventHandler(new osgViewer::StatsHandler);
    viewer->realize();
/////////////////
    glGenBuffers(2, pbo);
/////////////////    
    // create the view of the scene.
    glutMainLoop();
    is.wait();
    glDeleteBuffers(2, pbo);
    return 0;
}

/*EOF*/
