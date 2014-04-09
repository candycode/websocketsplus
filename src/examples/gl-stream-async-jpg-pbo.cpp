//OpenGL scratch - reference implementation of OpenGL >= 3.3 rendering code  
//Author: Ugo Varetto

//Requires GLFW and GLM, to deal with the missing support for matrix stack
//in OpenGL >= 3.3

//g++ -std=c++11  ../src/examples/gl-stream-async-jpg.cpp -I /usr/local/glfw/include -DGL_GLEXT_PROTOTYPES -L /usr/local/glfw/lib -lglfw -I /usr/local/glm/include -lGL -I /opt/libjpeg-turbo/include -L /opt/libjpeg-turbo/lib64  -lturbojpeg -I /usr/local/libwebsockets/include -L /usr/local/libwebsockets/lib -lwebsockets -O3 -pthread
//NOTE: turbo JPEG is > one order of magnitude faster than webp

//CHECK AFTER MAIN FOR ADDITIONAL INFO


#include <cstdlib>
#include <iostream>
#include <vector>
#include <memory>
#include <fstream>
#include <thread>
#include <future>
#include <memory>
#include <chrono>
#include <thread>

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

Image ReadImage(tjhandle tj, int width, int height, GLuint pbo, int quality = 75) {
    static std::vector< char > img;
    static int count = 0;
    //img.resize(3 * width * height);
    glReadBuffer(GL_BACK);
#ifdef TIME_READPIXEL    
    using namespace std::chrono;
    steady_clock::time_point t = steady_clock::now(); 
#endif    
    glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, 0);
    char* glout = (char* ) glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);

    
#ifdef TIME_READPIXEL    
    const milliseconds E =
                        duration_cast< milliseconds >(steady_clock::now() - t);
    cout << E.count() << ' '; //doesn't flush, prints after closing window                    
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
        TJSAMP_444,
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
    "smooth out vec3 outColor;\n"
    "uniform sampler2D cltexture;\n"
    "uniform float frame;\n"
    "void main() {\n"
    "  outColor = vec3(frame);//texture(cltexture, UV).rrr;\n"
    "}";
const char vertexShaderSrc[] =
    "#version 330 core\n"
    "layout(location = 0) in vec4 pos;\n"
    "layout(location = 1) in vec2 tex;\n"
    "smooth out vec2 UV;\n"
    "uniform mat4 MVP;\n"
    "void main() {\n"
    "  gl_Position = MVP * pos;\n"
    "  UV = tex;\n"
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
};


//==============================================================================    
using ImageContext = wsp::Context< Image >;

struct UserData {
     GLuint quadvbo;
     GLuint texbo;
     GLuint mvpID;
     GLuint frameID;
     shared_ptr< ImageContext > context;
     int frame;
     UserData(GLuint q, GLuint t, GLuint m, GLuint f,
              shared_ptr< ImageContext > c,
              int fr)
     : quadvbo(q), texbo(t), mvpID(m), frameID(f), context(c), frame(fr) {}
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
    glEnableVertexAttribArray(0);

    glBindBuffer(GL_ARRAY_BUFFER, d.quadvbo);

    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, 0);

    glEnableVertexAttribArray(1);
    glBindBuffer(GL_ARRAY_BUFFER, d.texbo);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    float f = (d.frame % 101) / 100.0f;
    if(f < 0.f) f = 1.0f + f;
    glUniform1f(d.frameID, f);  
}

//------------------------------------------------------------------------------
int main(int argc, char** argv) {
//USER INPUT
    if(argc < 2) {
      std::cout << "usage: " << argv[0]
                << " <size>"
                << std::endl; 
      exit(EXIT_FAILURE);          
    }
    const int SIZE = atoi(argv[1]);
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

    GLFWwindow* window = glfwCreateWindow(2560, 1440,
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
    //geometry: textured quad; the texture color is computed by
    //OpenCL
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
    GLuint quadvbo;  
    glGenBuffers(1, &quadvbo);
    glBindBuffer(GL_ARRAY_BUFFER, quadvbo);
    glBufferData(GL_ARRAY_BUFFER, 6 * 4 * sizeof(float),
                 &quad[0], GL_STATIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, 0);

    GLuint texbo;  
    glGenBuffers(1, &texbo);
    glBindBuffer(GL_ARRAY_BUFFER, texbo);
    glBufferData(GL_ARRAY_BUFFER, 12 * sizeof(real_t),
                 &texcoord[0], GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0); 


    // create texture 
    std::vector< float > tc(SIZE * SIZE, 0.5f);
    GLuint tex;
    glGenTextures(1, &tex);

    glBindTexture(GL_TEXTURE_2D, tex);
   
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_RED,
                 SIZE,
                 SIZE,
                 0,
                 GL_RED,
                 GL_FLOAT,
                 &tc[0]);
   
    //optional
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    //required
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); 

    glBindTexture(GL_TEXTURE_2D, 0);


//OPENGL RENDERING SHADERS
   
    GLuint glprogram = create_program(vertexShaderSrc, fragmentShaderSrc);

    //enable gl program
    glUseProgram(glprogram);

    //extract ids of shader variables
    GLint mvpID = glGetUniformLocation(glprogram, "MVP");

    GLint textureID = glGetUniformLocation(glprogram, "cltexture");

    GLint frameID = glGetUniformLocation(glprogram, "frame");

    
    // //only need texture unit 0
    // glActiveTexture(GL_TEXTURE0);
    // glBindTexture(GL_TEXTURE_2D, tex);
    // glUniform1i(textureID, 0);

    //=========================================================================
    GLuint pboId;
    glGenBuffers(1, &pboId);
  
    //=========================================================================



    //beckground color        
    glClearColor(0.0f, 0.0f, 0.4f, 0.0f);

//RENDER LOOP    
    //rendering & simulation loop
    UserData data(quadvbo, texbo, mvpID, frameID, context, 0);
    glfwSetWindowUserPointer(window, &data); 
    int width = 0;
    int height = 0;
    using namespace std::chrono;
    const milliseconds T(20);
    while (!glfwWindowShouldClose(window)) {
        steady_clock::time_point t = steady_clock::now(); 
        glfwGetFramebufferSize(window, &width, &height);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, pboId);
        glBufferData(GL_PIXEL_PACK_BUFFER, width * height * 3, 0,
                     GL_DYNAMIC_READ);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);      
        Draw(window, data, width, height);
        //glfwSwapBuffers(window);
        data.context->SetServiceDataSync(ReadImage(tj, width, height, pboId,
                                                   70));
        ++data.frame;
        const milliseconds E =
                        duration_cast< milliseconds >(steady_clock::now() - t);
#ifdef TIME_RENDER_STEP
        cout << E.count() << ' ';
#endif                                
        std::this_thread::sleep_for(
            max(duration_values<milliseconds>::zero(), T - E));
        glfwPollEvents();
    }
    
//CLEANUP
    glDeleteBuffers(1, &quadvbo);
    glDeleteBuffers(1, &texbo);
    glDeleteTextures(1, &tex);
    glDeleteBuffers(1, &pboId);
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