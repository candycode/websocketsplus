//OpenGL async streaming through websockets, uses jpeg format for storing framebuffer content 
//pixels are stored into multiple PBOs then read through map calls

//Requires GLFW and GLM, to deal with the missing support for matrix stack
//in OpenGL >= 3.3

//g++ -std=c++11  ../src/examples/gl-stream-async-jpg.cpp -I /usr/local/glfw/include -DGL_GLEXT_PROTOTYPES -L /usr/local/glfw/lib -lglfw -I /usr/local/glm/include -lGL -I /opt/libjpeg-turbo/include -L /opt/libjpeg-turbo/lib64  -lturbojpeg -I /usr/local/libwebsockets/include -L /usr/local/libwebsockets/lib -lwebsockets -O3 -pthread
//NOTE: turbo JPEG is > one order of magnitude faster than webp


// clang++ -std=c++11  ../src/examples/gl-stream-async-jpg-multipbo.cpp 
// -DGL_GLEXT_PROTOTYPES -L /opt/local/lib -lglfw -I /opt/local/include 
// -framework OpenGL -lturbojpeg -I /usr/local/libwebsockets/include 
// -L /usr/local/libwebsockets/lib  -I /opt/libjpeg-turbo/include 
// -L /opt/libjpeg-turbo/lib -lwebsockets -O3 -pthread 
// -o glstream-async-jpeg-multipbo -DGLM_FORCE_RADIANS

//CHECK AFTER MAIN FOR ADDITIONAL INFO


#include <cstdlib>
#include <iostream>
#include <vector>
#include <fstream>
#include <thread>
#include <future>
#include <memory>
#include <chrono>
#include <map>

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#endif

#include <GLFW/glfw3.h>

// Include GLM
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <turbojpeg.h>

#include "../WebSocketService.h"
#include "../Context.h"
#include "SessionService.h"

using namespace std;

#ifdef USE_DOUBLE
typedef double real_t;
const GLenum GL_REAL_T = GL_DOUBLE;
#else
typedef float real_t;
const GLenum GL_REAL_T = GL_FLOAT;
#endif

#ifdef LOG_
#define gle std::cout << "[GL] - " \
                      << __LINE__ << ' ' << glGetError() << std::endl;
#else
#define gle 
#endif                      

//------------------------------------------------------------------------------
struct TJDeleter {
    void operator()(char* p) const {
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

bool resizing = false;

Image ReadImage(tjhandle tj, int width, int height,
                GLuint* pbo, int quality = 75, int cs = TJSAMP_444) {
    //cout << width << ' ' << height << endl;
    static int count = 0;
    static int index = 0;
    static int nextIndex = 0;
    static int prevWidth = 0;
    static int prevHeight = 0;
    if(prevWidth != width || prevHeight != height) {
        prevWidth = width;
        prevHeight = height;
        return Image();
    }
    glReadBuffer(GL_BACK);
#ifdef TIME_READPIXEL    
    using namespace std::chrono;
    steady_clock::time_point t = steady_clock::now(); 
#endif
    index = (index + 1) % 2;
    nextIndex = (index + 1) % 2;    
    // if(count == 0) { //first time, init both pbos
    //     glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo[0]);
    //     glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, 0);
    //     glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo[1]);
    //     glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, 0);
    // } else {
        //cycle through PBOs
        //copy puxels into pbo...
    glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo[index]);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, 0);
        //...while reading previous copy from other pbo
    glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo[nextIndex]);
    //}
    char* glout = (char* ) glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
    assert(glout);
   
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
        3 * width,
        height,
        TJPF_RGB,
        (unsigned char **) &out,
        &size,
        cs, //444=best quality,
            //420=fast and still unnoticeable but MIGHT NOT WORK
            //IN SOME BROWSERS
        quality,
        0); 
    glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);   
    return Image(ImagePtr((char*) out, TJDeleter()), size, count++);
}

//------------------------------------------------------------------------------
GLuint create_program(const char* vertexSrc,
                      const char* fragmentSrc) {
    // Create the shaders
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    GLint res = GL_FALSE;
    int logsize = 0;
    // Compile Vertex Shader
    glShaderSource(vs, 1, &vertexSrc, 0);
    glCompileShader(vs);

    // Check Vertex Shader
    glGetShaderiv(vs, GL_COMPILE_STATUS, &res);
    glGetShaderiv(vs, GL_INFO_LOG_LENGTH, &logsize);
 
    if(logsize > 1){
        std::vector<char> errmsg(logsize + 1, 0);
        glGetShaderInfoLog(vs, logsize, 0, &errmsg[0]);
        std::cout << &errmsg[0] << std::endl;
    }
    // Compile Fragment Shader
    glShaderSource(fs, 1, &fragmentSrc, 0);
    glCompileShader(fs);

    // Check Fragment Shader
    glGetShaderiv(fs, GL_COMPILE_STATUS, &res);
    glGetShaderiv(fs, GL_INFO_LOG_LENGTH, &logsize);
    if(logsize > 1){
        std::vector<char> errmsg(logsize + 1, 0);
        glGetShaderInfoLog(fs, logsize, 0, &errmsg[0]);
        std::cout << &errmsg[0] << std::endl;
    }

    // Link the program
    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    // Check the program
    glGetProgramiv(program, GL_LINK_STATUS, &res);
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logsize);
    if(logsize > 1) {
        std::vector<char> errmsg(logsize + 1, 0);
        glGetShaderInfoLog(program, logsize, 0, &errmsg[0]);
        std::cout << &errmsg[0] << std::endl;
    }

    glDeleteShader(vs);
    glDeleteShader(fs);

    return program;
}
bool END = false;
//------------------------------------------------------------------------------
void error_callback(int error, const char* description) {
    std::cerr << description << std::endl;
}

//------------------------------------------------------------------------------
void key_callback(GLFWwindow* window, int key,
                  int scancode, int action, int mods) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GL_TRUE);
        END = true;
    }
}

//------------------------------------------------------------------------------
const char fragmentShaderSrc[] =
    "#version 330 core\n"
    "smooth in vec2 UV;\n"
    "smooth in vec4 p;\n"
    "out vec3 outColor;\n"
    "uniform float frame;\n"
    "void main() {\n"
    "  outColor = frame * 0.5 * (p + vec4(1)).rgb;\n"
    "}";
const char vertexShaderSrc[] =
    "#version 330 core\n"
    "layout(location = 0) in vec4 pos;\n"
    "smooth out vec4 p;\n"
    "uniform mat4 MVP;\n"
    "void main() {\n"
    "  gl_Position = MVP * pos;\n"
    "  p = gl_Position;\n"
    "}";   

//==============================================================================
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
    void Put(void* p, size_t len, bool done) override {}
    std::chrono::duration< double > 
    MinDelayBetweenWrites() const {
        //use 0.0
        return std::chrono::duration< double >(0.001);
    }
private:
    void InitDataFrame() const {
        //check if id == current id, if it is do not send
        //note: not using sync access!
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
};


//==============================================================================    
using ImageContext = wsp::Context< Image >;

struct UserData {
     GLuint vao;
     GLuint quadvbo;
     GLuint mvpID;
     GLuint frameID;
     shared_ptr< ImageContext > context;
     int frame;
     UserData(GLuint v, GLuint q, GLuint m, GLuint f,
              shared_ptr< ImageContext > c,
              int fr)
     : vao(v), quadvbo(q), mvpID(m), frameID(f), context(c), frame(fr) {}
};

void Draw(GLFWwindow* window, UserData& d, int width, int height) {
    glClear(GL_COLOR_BUFFER_BIT);

    //setup OpenGL matrices: no more matrix stack in OpenGL >= 3 core
    //profile, need to compute modelview and projection matrix manually
    // Clear the screen    
    const float ratio = width / float(height);
    const glm::mat4 orthoProj = glm::ortho(-ratio, ratio,
                                           -1.0f,  1.0f,
                                            1.0f,  -1.0f);
    const glm::mat4 modelView = glm::mat4(1.0f);
    const glm::mat4 MVP        = orthoProj * modelView;
    glViewport(0, 0, width, height);

    glUniformMatrix4fv(d.mvpID, 1, GL_FALSE, glm::value_ptr(MVP));
  

    //standard OpenGL core profile rendering
    //select geometry to render
    glBindVertexArray(d.vao); 
    glEnableVertexAttribArray(0);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(0);
    glBindVertexArray(0);
    float f = (d.frame % 101) / 100.0f;
    if(f < 0.f) f = 1.0f + f;
    glUniform1f(d.frameID, f);
   
}

//------------------------------------------------------------------------------
int main(int argc, char** argv) {
//USER INPUT
    if(argc < 3) {
      std::cout << "usage: " << argv[0]
                << " <size> <quality> [chrominance = 444 | 440 | 422 | 420]"
                << std::endl; 
      exit(EXIT_FAILURE);          
    }
    map< string, int > cs = {{"444", TJSAMP_444},
                             {"440", TJSAMP_440},
                             {"422", TJSAMP_422},
                             {"420", TJSAMP_420}};
    const int SIZE = stoi(argv[1]);
    const int QUALITY = stoi(argv[2]);
    const int CHROMINANCE_SAMPLING = argc >= 4 ? cs[argv[3]] : TJSAMP_444;

//GRAPHICS SETUP        
    glfwSetErrorCallback(error_callback);

    if(!glfwInit()) {
        std::cerr << "ERROR - glfwInit" << std::endl;
        exit(EXIT_FAILURE);
    }

    tjhandle tj = tjInitCompress();
    //==========================================================================
    using WSS = wsp::WebSocketService;
    WSS imageStreamer;
    //WSS::ResetLogLevels(); 
    //init service
    shared_ptr< ImageContext > context(new ImageContext);

   
    auto is = async(launch::async, [&context](WSS& imageStreamer){
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

    //WARNING: THIS DOESN'T WORK
    // glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    // glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    // glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__    
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif        

    //GLFWwindow* window = glfwCreateWindow((16 * SIZE) / 9, SIZE,
    //                                      "image streaming", NULL, NULL);
    GLFWwindow* window = glfwCreateWindow(640, 480,
                                          "image streaming", NULL, NULL);
    if (!window) {
        std::cerr << "ERROR - glfwCreateWindow" << std::endl;
        glfwTerminate();
        exit(EXIT_FAILURE);
    }

    glfwSetKeyCallback(window, key_callback);

    glfwMakeContextCurrent(window);

    std::cout << "OpenGL version: " << glGetString(GL_VERSION) << std::endl;
    std::cout << "GLSL version: " 
              << glGetString(GL_SHADING_LANGUAGE_VERSION) << std::endl;
    std::cout << "Vendor: " << glGetString(GL_VENDOR) << std::endl;
    std::cout << "Renderer: " << glGetString(GL_RENDERER) << std::endl;

//GEOMETRY
     //geometry: textured quad
    float quad[] = {-1.0f,  1.0f, 0.0f, 1.0f,
                    -1.0f, -1.0f, 0.0f, 1.0f,
                     1.0f, -1.0f, 0.0f, 1.0f,
                     1.0f, -1.0f, 0.0f, 1.0f,
                     1.0f,  1.0f, 0.0f, 1.0f,
                    -1.0f,  1.0f, 0.0f, 1.0f};

    float  texcoord[] = {0.0f, 1.0f,
                         0.0f, 0.0f,
                         1.0f, 0.0f,
                         1.0f, 0.0f,
                         1.0f, 1.0f,
                         0.0f, 1.0f};   
    //OpenGL >= 3.3 core requires a vertex array object containing multiple attribute
    //buffers                      
    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao); 

    //geometry buffer
    GLuint quadvbo;  
    glGenBuffers(1, &quadvbo);
    glBindBuffer(GL_ARRAY_BUFFER, quadvbo);
    glBufferData(GL_ARRAY_BUFFER, 6 * 4 * sizeof(float),
                 &quad[0], GL_STATIC_DRAW);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, 0);

    glBindBuffer(GL_ARRAY_BUFFER, 0);


//OPENGL RENDERING SHADERS
   
    GLuint glprogram = create_program(vertexShaderSrc, fragmentShaderSrc);

    //enable gl program
    glUseProgram(glprogram);

    //extract ids of shader variables
    GLint mvpID = glGetUniformLocation(glprogram, "MVP");

    GLint frameID = glGetUniformLocation(glprogram, "frame");

    //=========================================================================
    GLuint pboId[2];
    glGenBuffers(2, pboId);
  
    //=========================================================================


    //background color        
    glClearColor(0.0f, 0.0f, 0.4f, 0.0f);

//RENDER LOOP    
    //rendering & simulation loop
    UserData data(vao, quadvbo, mvpID, frameID, context, 0);
    glfwSetWindowUserPointer(window, &data); 
    int width = 0;
    int height = 0;
    int size = 0;
    using namespace std::chrono;
    const milliseconds T(20);
    while (!glfwWindowShouldClose(window)) {
        steady_clock::time_point t = steady_clock::now(); 
        glfwGetFramebufferSize(window, &width, &height);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, pboId[0]);
        if(width * height > size) {
            glBufferData(GL_PIXEL_PACK_BUFFER, width * height * 3, 0,
                         GL_DYNAMIC_READ);
            glBindBuffer(GL_PIXEL_PACK_BUFFER, pboId[1]);
            glBufferData(GL_PIXEL_PACK_BUFFER, width * height * 3, 0,
                          GL_DYNAMIC_READ);
            glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        }      
        Draw(window, data, width, height);
        glfwSwapBuffers(window);
        if(width * height == size || !size)
        data.context->SetServiceDataSync(ReadImage(tj, width, height, pboId,
                                                   QUALITY,
                                                   CHROMINANCE_SAMPLING));
       
        size = width * height;
        ++data.frame;
        const milliseconds E =
                        duration_cast< milliseconds >(steady_clock::now() - t);
#ifdef TIME_RENDER_STEP
        cout << E.count() << ' ';
#endif                                
        std::this_thread::sleep_for(
            max(duration_values< milliseconds >::zero(), T - E));
        glfwPollEvents();
    }
    
//CLEANUP
    glDeleteBuffers(1, &quadvbo);
    glDeleteBuffers(2, pboId);
    glfwDestroyWindow(window);
   
    glfwTerminate();
    is.wait();
    tjDestroy(tj);
    exit(EXIT_SUCCESS);
    return 0;
}

//glReadPixels lasts 10 to 20ms, use PBO or FBO instead
//
// Pixel Buffer Object (PBO)
//
// glGenBuffers(1, &pbo_id);
// glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo_id);
// glBufferData(GL_PIXEL_PACK_BUFFER, pbo_size, 0, GL_DYNAMIC_READ);
// glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
//
// According to the reference of glReadPixels:
//
// If a non-zero named buffer object is bound to the GL_PIXEL_PACK_BUFFER target
// (see glBindBuffer) while a block of pixels is requested, data is treated as a
// byte offset into the buffer object’s data store rather than a pointer to
//client memory.
//
// glReadBuffer(GL_BACK);
// glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo_id);
// glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, 0);
// GLubyte *ptr = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, pbo_size,
//                                 GL_MAP_READ_BIT);
// memcpy(pixels, ptr, pbo_size);
// glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
// glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
// In a real project, we may consider using double or triple PBOs to improve the
// performance.

//further optimization:
//Create renderbuffer + FBO and perform all the rendering to 
//GL_COLOR_ATTACHMENT0`then
// glReadBuffer(GL_COLOR_ATTACHMENT0)
//...

//Also check http://www.songho.ca/opengl/gl_pbo.html#pack
// "index" is used to read pixels from framebuffer to a PBO
// "nextIndex" is used to update pixels in the other PBO
// index = (index + 1) % 2;
// nextIndex = (index + 1) % 2;

// // set the target framebuffer to read
// glReadBuffer(GL_FRONT);

// // read pixels from framebuffer to PBO
// // glReadPixels() should return immediately.
// glBindBufferARB(GL_PIXEL_PACK_BUFFER_ARB, pboIds[index]);
// glReadPixels(0, 0, WIDTH, HEIGHT, GL_BGRA, GL_UNSIGNED_BYTE, 0);

// // map the PBO to process its data by CPU
// glBindBufferARB(GL_PIXEL_PACK_BUFFER_ARB, pboIds[nextIndex]);
// GLubyte* ptr = (GLubyte*)glMapBufferARB(GL_PIXEL_PACK_BUFFER_ARB,
//                                         GL_READ_ONLY_ARB);
// if(ptr)
// {
//     processPixels(ptr, ...);
//     glUnmapBufferARB(GL_PIXEL_PACK_BUFFER_ARB);
// }

// // back to conventional pixel operation
// glBindBufferARB(GL_PIXEL_PACK_BUFFER_ARB, 0);
