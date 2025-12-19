#include <fstream>
#include "utils/flog.h"
#include "gfx.h"

bool Gfx_CompileShader(GLenum shaderType, const char* source, GLuint& outHandle, const std::string& shaderName) {
    outHandle = glCreateShader(shaderType);

    glShaderSource(outHandle, 1, &source, nullptr);
    glCompileShader(outHandle);

    GLint success = 0;
    glGetShaderiv(outHandle, GL_COMPILE_STATUS, &success);

    if (!success) {
        GLint logLen = 0;
        glGetShaderiv(outHandle, GL_INFO_LOG_LENGTH, &logLen);

        std::string infoLog(logLen, '\0');
        glGetShaderInfoLog(outHandle, logLen, nullptr, infoLog.data());

        flog::error("Failed to compile shader \"{0}\": {1}", shaderName, infoLog.data());

        glDeleteShader(outHandle);
        outHandle = 0;

        return false;
    }

    return true;
}

static bool Gfx_LoadFileRaw(const std::string& path, std::vector<char>& outBuffer) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        flog::error("Failed to open shader file \"{0}\"!", path);
        return false;
    }

    const std::streamsize size = file.tellg();
    if (size <= 0) {
        flog::error("Shader file \"{0}\" is empty!", path);
        return false;
    }

    file.seekg(0, std::ios::beg);
    outBuffer.resize(static_cast<size_t>(size) + 1); // +1 for null terminator
    file.read(outBuffer.data(), size);
    const std::streamsize read = file.gcount();

    if (read < size) {
        flog::error("Short read on shader file \"{0}\"; read({1}) < size({2})", path, read, size);
        return false;
    }

    outBuffer[size] = '\0';
    return true;
}

bool Gfx_LoadAndCompileShader(GLenum shaderType, const std::string& filePath, GLuint& outHandle) {
    std::vector<char> source;

    if (!Gfx_LoadFileRaw(filePath, source))
        return false;

    if (!Gfx_CompileShader(shaderType, source.data(), outHandle, filePath)) {
        return false;
    }

    return true;
}

bool Gfx_CreateAndLinkProgram(GLuint vertexShader, GLuint fragmentShader, GLuint& outProgram) {
    outProgram = glCreateProgram();
    glAttachShader(outProgram, vertexShader);
    glAttachShader(outProgram, fragmentShader);
    glLinkProgram(outProgram);

    GLint success;
    glGetProgramiv(outProgram, GL_LINK_STATUS, &success);

    if (!success) {
        GLint logLen = 0;
        glGetProgramiv(outProgram, GL_INFO_LOG_LENGTH, &logLen);

        std::string infoLog(logLen, '\0');
        glGetProgramInfoLog(outProgram, logLen, nullptr, infoLog.data());

        flog::error("Shader Link failed: {0}", infoLog.data());

        glDeleteProgram(outProgram);
        outProgram = 0;
        return false;
    }

    return true;
}

void Gfx_CreateScreenQuad(GLuint& outVao, GLuint& outVbo) {
    // pos (x, y)  // coords (u, v)
    float quadVertices[] = {
        -1.0f, 1.0f, 0.0f, 0.0f,  // Tl
        -1.0f, -1.0f, 0.0f, 1.0f, // Bl
        1.0f, -1.0f, 1.0f, 1.0f,  // Br

        -1.0f, 1.0f, 0.0f, 0.0f, // Tl
        1.0f, -1.0f, 1.0f, 1.0f, // Br
        1.0f, 1.0f, 1.0f, 0.0f   // Tr
    };

    glGenVertexArrays(1, &outVao);
    glGenBuffers(1, &outVbo);

    glBindVertexArray(outVao);
    glBindBuffer(GL_ARRAY_BUFFER, outVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void Gfx_CreateTexture2D(GLuint& outTexture, GLint minFilter, GLint magFilter, GLint wrapS, GLint wrapT) {
    glGenTextures(1, &outTexture);
    glBindTexture(GL_TEXTURE_2D, outTexture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapS);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapT);

    glBindTexture(GL_TEXTURE_2D, 0);
}

void Gfx_CreateFramebuffer(GLuint& outFbo) {
    glGenFramebuffers(1, &outFbo);
}

void Gfx_UpdateTexturePart(GLuint textureHandle, int x, int y, int width, int height,
                           GLenum format, GLenum type, const void* data, int rowLength) {
    glBindTexture(GL_TEXTURE_2D, textureHandle);

    // Set stride
    glPixelStorei(GL_UNPACK_ROW_LENGTH, rowLength);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, width, height, format, type, data);

    // Reset
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Gfx_SetUniform1i(GLuint program, const char* name, int value) {
    GLint loc = glGetUniformLocation(program, name);
    if (loc != -1) { glUniform1i(loc, value); }
}

void Gfx_SetUniform1f(GLuint program, const char* name, float value) {
    GLint loc = glGetUniformLocation(program, name);
    if (loc != -1) { glUniform1f(loc, value); }
}

Gfx_ScopedState::Gfx_ScopedState() {
    glGetIntegerv(GL_VIEWPORT, lastViewport);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &lastFbo);
    lastBlend = glIsEnabled(GL_BLEND);
    lastDepth = glIsEnabled(GL_DEPTH_TEST);
    lastScissor = glIsEnabled(GL_SCISSOR_TEST);
    lastSRGB = glIsEnabled(GL_FRAMEBUFFER_SRGB);
}

Gfx_ScopedState::~Gfx_ScopedState() {
    glBindFramebuffer(GL_FRAMEBUFFER, lastFbo);
    glViewport(lastViewport[0], lastViewport[1], lastViewport[2], lastViewport[3]);
    if (lastBlend) { glEnable(GL_BLEND); }
    else { glDisable(GL_BLEND); }
    if (lastDepth) { glEnable(GL_DEPTH_TEST); }
    else { glDisable(GL_DEPTH_TEST); }
    if (lastScissor) { glEnable(GL_SCISSOR_TEST); }
    else { glDisable(GL_SCISSOR_TEST); }
    if (lastSRGB) { glEnable(GL_FRAMEBUFFER_SRGB); }
    else { glDisable(GL_FRAMEBUFFER_SRGB); }
}
