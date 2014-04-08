//OpenGL scratch - reference implementation of OpenGL >= 3.3 rendering code  
//Author: Ugo Varetto

//Requires GLFW and GLM, to deal with the missing support for matrix stack
//in OpenGL >= 3.3

//g++ -std=c++11 gl-stream.cpp -I /usr/local/glfw/include -DGL_GLEXT_PROTOTYPES -L /usr/local/glfw/lib -lglfw -I /usr/local/glm/include -lGL -lwebp

#include <cstdlib>
#include <iostream>
#include <vector>
#include <memory>
#include <fstream>

#include <GLFW/glfw3.h>

// Include GLM
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <webp/encode.h>

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

//size_t WebPEncodeRGB(const uint8_t* rgb, int width, int height, int stride,
//float quality_factor, uint8_t** output);

//------------------------------------------------------------------------------
struct WebpDeleter {
    void operator()(char* p) const {
        free(p);
    }
};

using ImagePtr = shared_ptr< char >;

struct Image {
    ImagePtr image;
    size_t size = 0;
    Image() = default;
    Image(ImagePtr i, size_t s) : image(i), size(s) {}
    Image(const Image&) = default;
    Image(Image&&) = default;
    Image& operator=(const Image&) = default;
    Image& operator=(Image&&) = default;
};

Image ReadImage(int width, int height,
                   float quality = 75) {
    std::vector< char > img( 3*width*height);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, &img[0]);
    uint8_t* out;
    const size_t size = 
        WebPEncodeRGB((uint8_t*) &img[0], width, height, 3 * width, 75, &out);
    return Image(ImagePtr((char*) out, WebpDeleter()), size);
}

void SendImage(const Image& img) {
    static bool first = true;
    if(first) {
        ofstream os("out.webp", ios::out | ios::binary);
        if(!os) exit(EXIT_FAILURE);
        os.write(img.image.get(), img.size);
        first = false;
    }
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

//------------------------------------------------------------------------------
void error_callback(int error, const char* description) {
    std::cerr << description << std::endl;
}

//------------------------------------------------------------------------------
void key_callback(GLFWwindow* window, int key,
                         int scancode, int action, int mods) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        glfwSetWindowShouldClose(window, GL_TRUE);
}

//------------------------------------------------------------------------------
const char fragmentShaderSrc[] =
    "#version 330 core\n"
    "smooth in vec2 UV;\n"
    "smooth out vec3 outColor;\n"
    "uniform sampler2D cltexture;\n"
    "void main() {\n"
    "  outColor = texture(cltexture, UV).rrr;\n"
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
     SessionService(c), ctx_(c), frameCounter_(0) {
        InitDataFrame();
    }
    bool Data() const override { return ctx_->GetServiceData().size > 0; }
    //return data frame and update frame end
    const DataFrame& Get(int requestedChunkLength) {
        if(df_.frameEnd < df_.bufferEnd) {
           //frameBegin *MUST* be updated in the UpdateOutBuffer method
           //because in case the consumed data is less than requestedChunkLength
           // 
           //df_.frameBegin = df_.frameEnd;
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
        return std::chrono::duration< double >(0.01);
    }
private:
    void InitDataFrame() {
        df_.bufferBegin = ctx_->GetServiceData().image.get();
        df_.bufferEnd = df_.bufferBegin 
                        + ctx_->GetServiceData().size;
        df_.frameBegin = df_.bufferBegin;
        df_.frameEnd = df_.frameBegin;
        df_.binary = true;
    }    
private:    
    DataFrame df_;
    Context* ctx_ = nullptr;
    unsigned int frameCounter_ = 0;
};


//==============================================================================    

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


    //==========================================================================
    using Context = wsp::Context< Image >;
    using WSS = wsp::WebSocketService;
    WSS imageStreamer;
    WSS::ResetLogLevels(); 
    //init service
    Context& context = 
    imageStreamer.Init(5000, //port
                       nullptr, //SSL certificate path
                       nullptr, //SSL key path
                       Context(), //context instance,
                       //will be copied internally
                       WSS::Entry< ImageService,
                           WSS::ASYNC_REP, WSS::SEND_GREEDY >("image-stream"));
    //==========================================================================



    //WARNING: THIS DOESN'T WORK
    // glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    // glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    // glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(640, 480,
                                          "OpenCL-GL interop", NULL, NULL);
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

    
    //only need texture unit 0
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(textureID, 0);

    //beckground color        
    glClearColor(0.0f, 0.0f, 0.4f, 0.0f);

//RENDER LOOP    
    //rendering & simulation loop
    while (!glfwWindowShouldClose(window)) {     
        glClear(GL_COLOR_BUFFER_BIT);

        //setup OpenGL matrices: no more matrix stack in OpenGL >= 3 core
        //profile, need to compute modelview and projection matrix manually
        // Clear the screen    
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        const float ratio = width / float(height);
        const glm::mat4 orthoProj = glm::ortho(-ratio, ratio,
                                               -1.0f,  1.0f,
                                                1.0f,  -1.0f);
        const glm::mat4 modelView = glm::mat4(1.0f);
        const glm::mat4 MVP        = orthoProj * modelView;
        glViewport(0, 0, width, height);

        glUniformMatrix4fv(mvpID, 1, GL_FALSE, glm::value_ptr(MVP));
      
   
        //standard OpenGL core profile rendering
        glEnableVertexAttribArray(0);

        glBindBuffer(GL_ARRAY_BUFFER, quadvbo);

        glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, 0);

        glEnableVertexAttribArray(1);
        glBindBuffer(GL_ARRAY_BUFFER, texbo);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, 0);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(1);
       
        context.SetServiceData(ReadImage(width, height));
        imageStreamer.Next(); //send to client
        //glfwSwapBuffers(window);
        //glfwPollEvents();
    }

//CLEANUP
    glDeleteBuffers(1, &quadvbo);
    glDeleteBuffers(1, &texbo);
    glDeleteTextures(1, &tex);
    glfwDestroyWindow(window);

    glfwTerminate();
    exit(EXIT_SUCCESS);
    return 0;
}
