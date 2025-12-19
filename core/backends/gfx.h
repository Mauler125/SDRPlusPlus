#include "../backends/glad/glad.h"
#include <utils/opengl_include_code.h>

extern bool Gfx_CompileShader(GLenum shaderType, const char* source, GLuint& outHandle, const std::string& shaderName);
extern bool Gfx_LoadAndCompileShader(GLenum shaderType, const std::string& filePath, GLuint& outHandle);
extern bool Gfx_CreateAndLinkProgram(GLuint vertexShader, GLuint fragmentShader, GLuint& outProgram);
extern void Gfx_CreateScreenQuad(GLuint& outVao, GLuint& outVbo);
extern void Gfx_CreateTexture2D(GLuint& outTexture, GLint minFilter, GLint magFilter, GLint wrapS, GLint wrapT);
extern void Gfx_CreateFramebuffer(GLuint& outFbo);

void Gfx_UpdateTexturePart(GLuint textureHandle, int x, int y, int width, int height,
                           GLenum format, GLenum type, const void* data, int rowLength);

void Gfx_SetUniform1i(GLuint program, const char* name, int value);
void Gfx_SetUniform1f(GLuint program, const char* name, float value);

class Gfx_ScopedState {
public:
    Gfx_ScopedState();
    ~Gfx_ScopedState();

private:
    GLint lastViewport[4];
    GLint lastFbo;
    GLboolean lastBlend;
    GLboolean lastDepth;
    GLboolean lastScissor;
    GLboolean lastSRGB;
};
