/*****************************************************************************************
 *                                                                                       *
 * OpenSpace                                                                             *
 *                                                                                       *
 * Copyright (c) 2014-2019                                                               *
 *                                                                                       *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this  *
 * software and associated documentation files (the "Software"), to deal in the Software *
 * without restriction, including without limitation the rights to use, copy, modify,    *
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to    *
 * permit persons to whom the Software is furnished to do so, subject to the following   *
 * conditions:                                                                           *
 *                                                                                       *
 * The above copyright notice and this permission notice shall be included in all copies *
 * or substantial portions of the Software.                                              *
 *                                                                                       *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,   *
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A         *
 * PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT    *
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF  *
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE  *
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                                         *
 ****************************************************************************************/

#include <openspace/rendering/framebufferrenderer.h>

#include <openspace/engine/globals.h>
#include <openspace/engine/windowdelegate.h>
#include <openspace/performance/performancemanager.h>
#include <openspace/performance/performancemeasurement.h>
#include <openspace/rendering/deferredcaster.h>
#include <openspace/rendering/deferredcastermanager.h>
#include <openspace/rendering/raycastermanager.h>
#include <openspace/rendering/renderable.h>
#include <openspace/rendering/renderengine.h>
#include <openspace/rendering/volumeraycaster.h>
#include <openspace/scene/scene.h>
#include <openspace/util/camera.h>
#include <openspace/util/timemanager.h>
#include <openspace/util/updatestructures.h>
#include <ghoul/filesystem/filesystem.h>
#include <ghoul/logging/logmanager.h>
#include <ghoul/opengl/ghoul_gl.h>
#include <ghoul/opengl/programobject.h>
#include <ghoul/opengl/textureunit.h>
#include <fstream>
#include <string>
#include <vector>

namespace {
    constexpr const char* _loggerCat = "FramebufferRenderer";

    constexpr const std::array<const char*, 3> UniformNames = {
        "mainColorTexture", "blackoutFactor", "nAaSamples"
    };

    constexpr const std::array<const char*, 12> HDRUniformNames = {
        "hdrFeedingTexture", "blackoutFactor", "hdrExposure", "gamma", 
        "toneMapOperator", "maxWhite", "Hue", "Saturation", "Value", 
        "Lightness", "colorSpace", "nAaSamples"
    };

    constexpr const char* ExitFragmentShaderPath =
        "${SHADERS}/framebuffer/exitframebuffer.frag";
    constexpr const char* RaycastFragmentShaderPath =
        "${SHADERS}/framebuffer/raycastframebuffer.frag";
    constexpr const char* GetEntryInsidePath = "${SHADERS}/framebuffer/inside.glsl";
    constexpr const char* GetEntryOutsidePath = "${SHADERS}/framebuffer/outside.glsl";
    constexpr const char* RenderFragmentShaderPath =
        "${SHADERS}/framebuffer/renderframebuffer.frag";

    void saveTextureToMemory(GLenum attachment, int width, int height,
                             std::vector<double>& memory)
    {
        memory.clear();
        memory.resize(width * height * 3);

        std::vector<float> tempMemory(width * height * 3);

        if (attachment != GL_DEPTH_ATTACHMENT) {
            glReadBuffer(attachment);
            glReadPixels(0, 0, width, height, GL_RGB, GL_FLOAT, tempMemory.data());

        }
        else {
            glReadPixels(
                0,
                0,
                width,
                height,
                GL_DEPTH_COMPONENT,
                GL_FLOAT,
                tempMemory.data()
            );
        }

        for (int i = 0; i < width * height * 3; ++i) {
            memory[i] = static_cast<double>(tempMemory[i]);
        }
    }

} // namespace

namespace openspace {

void FramebufferRenderer::initialize() {
    LDEBUG("Initializing FramebufferRenderer");

    const GLfloat vertexData[] = {
        // x     y
        -1.f, -1.f,
         1.f,  1.f,
        -1.f,  1.f,
        -1.f, -1.f,
         1.f, -1.f,
         1.f,  1.f,
    };

    glGenVertexArrays(1, &_screenQuad);
    glBindVertexArray(_screenQuad);

    glGenBuffers(1, &_vertexPositionBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, _vertexPositionBuffer);

    glBufferData(GL_ARRAY_BUFFER, sizeof(vertexData), vertexData, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(GLfloat) * 2, nullptr);
    glEnableVertexAttribArray(0);

    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &_defaultFBO);

    // GBuffers
    glGenTextures(1, &_gBuffers._colorTexture);
    glGenTextures(1, &_gBuffers._depthTexture);
    glGenTextures(1, &_gBuffers._positionTexture);
    glGenTextures(1, &_gBuffers._normalTexture);
    glGenFramebuffers(1, &_gBuffers._framebuffer);

    // PingPong Buffers
    // The first pingpong buffer shares the color texture with the renderbuffer:
    _pingPongBuffers.colorTexture[0] = _gBuffers._colorTexture;
    glGenTextures(1, &_pingPongBuffers.colorTexture[1]);
    glGenFramebuffers(1, &_pingPongBuffers.framebuffer);
    
    // Exit framebuffer
    glGenTextures(1, &_exitColorTexture);
    glGenTextures(1, &_exitDepthTexture);
    glGenFramebuffers(1, &_exitFramebuffer);

    // HDR / Filtering Buffers
    glGenFramebuffers(1, &_hdrBuffers._hdrFilteringFramebuffer);
    glGenTextures(1, &_hdrBuffers._hdrFilteringTexture);

    // Allocate Textures/Buffers Memory
    updateResolution();

    updateRendererData();
    updateRaycastData();

    //==============================//
    //=====  GBuffers Buffers  =====//
    //==============================//
    glBindFramebuffer(GL_FRAMEBUFFER, _gBuffers._framebuffer);
    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D_MULTISAMPLE,
        _gBuffers._colorTexture,
        0
    );
    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT1,
        GL_TEXTURE_2D_MULTISAMPLE,
        _gBuffers._positionTexture,
        0
    );
    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT2,
        GL_TEXTURE_2D_MULTISAMPLE,
        _gBuffers._normalTexture,
        0
    );
    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_DEPTH_ATTACHMENT,
        GL_TEXTURE_2D_MULTISAMPLE,
        _gBuffers._depthTexture,
        0
    );

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        LERROR("Main framebuffer is not complete");
    }

    //==============================//
    //=====  PingPong Buffers  =====//
    //==============================//
    glBindFramebuffer(GL_FRAMEBUFFER, _pingPongBuffers.framebuffer);
    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D_MULTISAMPLE,
        _pingPongBuffers.colorTexture[0],
        0
    );
    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT1,
        GL_TEXTURE_2D_MULTISAMPLE,
        _pingPongBuffers.colorTexture[1],
        0
    );
    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_DEPTH_ATTACHMENT,
        GL_TEXTURE_2D_MULTISAMPLE,
        _gBuffers._depthTexture,
        0
    );

    status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        LERROR("Ping pong buffer is not complete");
    }

    //======================================//
    //=====  Volume Rendering Buffers  =====//
    //======================================//
    // Builds Exit Framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, _exitFramebuffer);
    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D,
        _exitColorTexture,
        0
    );
    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_DEPTH_ATTACHMENT,
        GL_TEXTURE_2D,
        _exitDepthTexture,
        0
    );

    status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        LERROR("Exit framebuffer is not complete");
    }

    //===================================//
    //=====  HDR/Filtering Buffers  =====//
    //===================================//
    glBindFramebuffer(GL_FRAMEBUFFER, _hdrBuffers._hdrFilteringFramebuffer);
    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D,
        _hdrBuffers._hdrFilteringTexture,
        0
    );

    status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        LERROR("HDR/Filtering framebuffer is not complete");
    }

    // JCC: Moved to here to avoid NVidia: "Program/shader state performance warning"
    // Building programs
    updateHDRAndFiltering();
    updateDeferredcastData();

    _dirtyMsaaSamplingPattern = true;

    // Sets back to default FBO
    glBindFramebuffer(GL_FRAMEBUFFER, _defaultFBO);

    _resolveProgram = ghoul::opengl::ProgramObject::Build(
        "Framebuffer Resolve",
        absPath("${SHADERS}/framebuffer/resolveframebuffer.vert"),
        absPath("${SHADERS}/framebuffer/resolveframebuffer.frag")
    );

    ghoul::opengl::updateUniformLocations(
        *_resolveProgram, 
        _uniformCache, 
        UniformNames
    );
    ghoul::opengl::updateUniformLocations(
        *_hdrFilteringProgram, 
        _hdrUniformCache, 
        HDRUniformNames
    );

    global::raycasterManager.addListener(*this);
    global::deferredcasterManager.addListener(*this);

    // Default GL State for Blending
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

}

void FramebufferRenderer::deinitialize() {
    LINFO("Deinitializing FramebufferRenderer");

    glDeleteFramebuffers(1, &_gBuffers._framebuffer);
    glDeleteFramebuffers(1, &_exitFramebuffer);
    glDeleteFramebuffers(1, &_hdrBuffers._hdrFilteringFramebuffer);
    glDeleteFramebuffers(1, &_pingPongBuffers.framebuffer);

    glDeleteTextures(1, &_gBuffers._colorTexture);
    glDeleteTextures(1, &_gBuffers._depthTexture);

    glDeleteTextures(1, &_hdrBuffers._hdrFilteringTexture);
    glDeleteTextures(1, &_gBuffers._positionTexture);
    glDeleteTextures(1, &_gBuffers._normalTexture);
    
    glDeleteTextures(1, &_pingPongBuffers.colorTexture[1]);

    glDeleteTextures(1, &_exitColorTexture);
    glDeleteTextures(1, &_exitDepthTexture);

    glDeleteBuffers(1, &_vertexPositionBuffer);
    glDeleteVertexArrays(1, &_screenQuad);

    global::raycasterManager.removeListener(*this);
    global::deferredcasterManager.removeListener(*this);
}

void FramebufferRenderer::raycastersChanged(VolumeRaycaster&,
                                            RaycasterListener::IsAttached)
{
    _dirtyRaycastData = true;
}

void FramebufferRenderer::deferredcastersChanged(Deferredcaster&,
                                                 DeferredcasterListener::IsAttached)
{
    _dirtyDeferredcastData = true;
}

void FramebufferRenderer::resolveMSAA(float blackoutFactor) {
    _resolveProgram->activate();

    ghoul::opengl::TextureUnit mainColorTextureUnit;
    mainColorTextureUnit.activate();

    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, _gBuffers._colorTexture);
    _resolveProgram->setUniform(_uniformCache.mainColorTexture, mainColorTextureUnit);
    _resolveProgram->setUniform(_uniformCache.blackoutFactor, blackoutFactor);
    _resolveProgram->setUniform(_uniformCache.nAaSamples, _nAaSamples);
    glBindVertexArray(_screenQuad);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    _resolveProgram->deactivate();
}

void FramebufferRenderer::applyTMO(float blackoutFactor) {
    const bool doPerformanceMeasurements = global::performanceManager.isEnabled();
    std::unique_ptr<performance::PerformanceMeasurement> perfInternal;
    
    if (doPerformanceMeasurements) {
        perfInternal = std::make_unique<performance::PerformanceMeasurement>(
            "FramebufferRenderer::render::TMO"
            );
    }
    _hdrFilteringProgram->activate();

    ghoul::opengl::TextureUnit hdrFeedingTextureUnit;
    hdrFeedingTextureUnit.activate();
    glBindTexture(
        GL_TEXTURE_2D_MULTISAMPLE,
        _pingPongBuffers.colorTexture[_pingPongIndex]
    );
    
    _hdrFilteringProgram->setUniform(
        _hdrUniformCache.hdrFeedingTexture,
        hdrFeedingTextureUnit
    );


    _hdrFilteringProgram->setUniform(_hdrUniformCache.blackoutFactor, blackoutFactor);
    _hdrFilteringProgram->setUniform(_hdrUniformCache.hdrExposure, _hdrExposure);
    _hdrFilteringProgram->setUniform(_hdrUniformCache.gamma, _gamma);
    _hdrFilteringProgram->setUniform(_hdrUniformCache.toneMapOperator, _toneMapOperator);
    _hdrFilteringProgram->setUniform(_hdrUniformCache.maxWhite, _maxWhite);
    _hdrFilteringProgram->setUniform(_hdrUniformCache.Hue, _hue);
    _hdrFilteringProgram->setUniform(_hdrUniformCache.Saturation, _saturation);
    _hdrFilteringProgram->setUniform(_hdrUniformCache.Value, _value);
    _hdrFilteringProgram->setUniform(_hdrUniformCache.Lightness, _lightness);
    _hdrFilteringProgram->setUniform(_hdrUniformCache.colorSpace, _colorSpace);
    _hdrFilteringProgram->setUniform(_hdrUniformCache.nAaSamples, _nAaSamples);


    glBindVertexArray(_screenQuad);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    _hdrFilteringProgram->deactivate();
}

void FramebufferRenderer::update() {
    if (_dirtyMsaaSamplingPattern) {
        updateMSAASamplingPattern();
    }

    if (_dirtyResolution) {
        updateResolution();
        updateMSAASamplingPattern();
    }

    if (_dirtyRaycastData) {
        updateRaycastData();
    }

    if (_dirtyDeferredcastData) {
        updateDeferredcastData();
    }

    if (_resolveProgram->isDirty()) {
        _resolveProgram->rebuildFromFile();
        ghoul::opengl::updateUniformLocations(
            *_resolveProgram,
            _uniformCache,
            UniformNames
        );
    }
    
    if (_hdrFilteringProgram->isDirty()) {
        _hdrFilteringProgram->rebuildFromFile();

        ghoul::opengl::updateUniformLocations(
            *_hdrFilteringProgram,
            _hdrUniformCache,
            HDRUniformNames
        );
    }

    using K = VolumeRaycaster*;
    using V = std::unique_ptr<ghoul::opengl::ProgramObject>;
    for (const std::pair<const K, V>& program : _exitPrograms) {
        if (program.second->isDirty()) {
            try {
                program.second->rebuildFromFile();
            }
            catch (const ghoul::RuntimeError& e) {
                LERRORC(e.component, e.message);
            }
        }
    }

    for (const std::pair<const K, V>& program : _raycastPrograms) {
        if (program.second->isDirty()) {
            try {
                program.second->rebuildFromFile();
            }
            catch (const ghoul::RuntimeError& e) {
                LERRORC(e.component, e.message);
            }
        }
    }

    for (const std::pair<const K, V>& program : _insideRaycastPrograms) {
        if (program.second->isDirty()) {
            try {
                program.second->rebuildFromFile();
            }
            catch (const ghoul::RuntimeError& e) {
                LERRORC(e.component, e.message);
            }
        }
    }

    for (const std::pair<
            Deferredcaster* const,
            std::unique_ptr<ghoul::opengl::ProgramObject>
        >& program : _deferredcastPrograms)
    {
        if (program.second && program.second->isDirty()) {
            try {
                program.second->rebuildFromFile();
            }
            catch (const ghoul::RuntimeError& e) {
                LERRORC(e.component, e.message);
            }
        }
    }
}

void FramebufferRenderer::updateResolution() {
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, _gBuffers._colorTexture);
    glTexImage2DMultisample(
        GL_TEXTURE_2D_MULTISAMPLE,
        _nAaSamples,
        GL_RGBA32F,
        _resolution.x,
        _resolution.y,
        GL_TRUE
    );

    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, _gBuffers._positionTexture);
    glTexImage2DMultisample(
        GL_TEXTURE_2D_MULTISAMPLE,
        _nAaSamples,
        GL_RGBA32F,
        _resolution.x,
        _resolution.y,
        GL_TRUE
    );

    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, _gBuffers._normalTexture);

    glTexImage2DMultisample(
        GL_TEXTURE_2D_MULTISAMPLE,
        _nAaSamples,
        GL_RGBA32F,
        _resolution.x,
        _resolution.y,
        GL_TRUE
    );

    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, _gBuffers._depthTexture);
    glTexImage2DMultisample(
        GL_TEXTURE_2D_MULTISAMPLE,
        _nAaSamples,
        GL_DEPTH_COMPONENT32F,
        _resolution.x,
        _resolution.y,
        GL_TRUE
    );

    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, _pingPongBuffers.colorTexture[1]);
    glTexImage2DMultisample(
        GL_TEXTURE_2D_MULTISAMPLE,
        _nAaSamples,
        GL_RGBA32F,
        _resolution.x,
        _resolution.y,
        GL_TRUE
    );


    // HDR / Filtering
    glBindTexture(GL_TEXTURE_2D, _hdrBuffers._hdrFilteringTexture);

    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA32F,
        _resolution.x,
        _resolution.y,
        0,
        GL_RGBA,
        GL_FLOAT,
        nullptr
    );

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    // Volume Rendering Textures
    glBindTexture(GL_TEXTURE_2D, _exitColorTexture);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA16F,
        _resolution.x,
        _resolution.y,
        0,
        GL_RGBA,
        GL_UNSIGNED_SHORT,
        nullptr
    );

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    glBindTexture(GL_TEXTURE_2D, _exitDepthTexture);

    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_DEPTH_COMPONENT32F,
        _resolution.x,
        _resolution.y,
        0,
        GL_DEPTH_COMPONENT,
        GL_FLOAT,
        nullptr
    );

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    _dirtyResolution = false;
}

void FramebufferRenderer::updateRaycastData() {
    _raycastData.clear();
    _exitPrograms.clear();
    _raycastPrograms.clear();
    _insideRaycastPrograms.clear();

    const std::vector<VolumeRaycaster*>& raycasters =
        global::raycasterManager.raycasters();

    int nextId = 0;
    for (VolumeRaycaster* raycaster : raycasters) {
        RaycastData data = { nextId++, "Helper" };

        const std::string& vsPath = raycaster->boundsVertexShaderPath();
        std::string fsPath = raycaster->boundsFragmentShaderPath();

        ghoul::Dictionary dict;
        dict.setValue("rendererData", _rendererData);
        dict.setValue("fragmentPath", std::move(fsPath));
        dict.setValue("id", data.id);

        std::string helperPath = raycaster->helperPath();
        ghoul::Dictionary helpersDict;
        if (!helperPath.empty()) {
            helpersDict.setValue("0", std::move(helperPath));
        }
        dict.setValue("helperPaths", std::move(helpersDict));
        dict.setValue("raycastPath", raycaster->raycasterPath());

        _raycastData[raycaster] = data;

        try {
            _exitPrograms[raycaster] = ghoul::opengl::ProgramObject::Build(
                "Volume " + std::to_string(data.id) + " exit",
                absPath(vsPath),
                absPath(ExitFragmentShaderPath),
                dict
            );
        } catch (const ghoul::RuntimeError& e) {
            LERROR(e.message);
        }

        try {
            ghoul::Dictionary outsideDict = dict;
            outsideDict.setValue("getEntryPath", GetEntryOutsidePath);
            _raycastPrograms[raycaster] = ghoul::opengl::ProgramObject::Build(
                "Volume " + std::to_string(data.id) + " raycast",
                absPath(vsPath),
                absPath(RaycastFragmentShaderPath),
                outsideDict
            );
        } catch (const ghoul::RuntimeError& e) {
            LERROR(e.message);
        }

        try {
            ghoul::Dictionary insideDict = dict;
            insideDict.setValue("getEntryPath", GetEntryInsidePath);
            _insideRaycastPrograms[raycaster] = ghoul::opengl::ProgramObject::Build(
                "Volume " + std::to_string(data.id) + " inside raycast",
                absPath("${SHADERS}/framebuffer/resolveframebuffer.vert"),
                absPath(RaycastFragmentShaderPath),
                insideDict
            );
        }
        catch (const ghoul::RuntimeError& e) {
            LERRORC(e.component, e.message);
        }
    }
    _dirtyRaycastData = false;
}

void FramebufferRenderer::updateDeferredcastData() {
    _deferredcastData.clear();
    _deferredcastPrograms.clear();

    const std::vector<Deferredcaster*>& deferredcasters =
        global::deferredcasterManager.deferredcasters();
    int nextId = 0;
    for (Deferredcaster* caster : deferredcasters) {
        DeferredcastData data = { nextId++, "HELPER" };

        std::string vsPath = caster->deferredcastVSPath();
        std::string fsPath = caster->deferredcastFSPath();
        std::string deferredShaderPath = caster->deferredcastPath();

        ghoul::Dictionary dict;
        dict.setValue("rendererData", _rendererData);
        //dict.setValue("fragmentPath", fsPath);
        dict.setValue("id", data.id);
        std::string helperPath = caster->helperPath();
        ghoul::Dictionary helpersDict;
        if (!helperPath.empty()) {
            helpersDict.setValue("0", helperPath);
        }
        dict.setValue("helperPaths", helpersDict);

        _deferredcastData[caster] = data;

        try {
            _deferredcastPrograms[caster] = ghoul::opengl::ProgramObject::Build(
                "Deferred " + std::to_string(data.id) + " raycast",
                absPath(vsPath),
                absPath(deferredShaderPath),
                dict
            );

            _deferredcastPrograms[caster]->setIgnoreSubroutineUniformLocationError(
                ghoul::opengl::ProgramObject::IgnoreError::Yes
            );
            _deferredcastPrograms[caster]->setIgnoreUniformLocationError(
                ghoul::opengl::ProgramObject::IgnoreError::Yes
            );

            caster->initializeCachedVariables(*_deferredcastPrograms[caster]);
        }
        catch (ghoul::RuntimeError& e) {
            LERRORC(e.component, e.message);
        }
    }
    _dirtyDeferredcastData = false;
}


void FramebufferRenderer::updateHDRAndFiltering() {
    _hdrFilteringProgram = ghoul::opengl::ProgramObject::Build(
        "HDR and Filtering Program",
        absPath("${SHADERS}/framebuffer/hdrAndFiltering.vert"),
        absPath("${SHADERS}/framebuffer/hdrAndfiltering.frag")
    );
    using IgnoreError = ghoul::opengl::ProgramObject::IgnoreError;
    //_hdrFilteringProgram->setIgnoreSubroutineUniformLocationError(IgnoreError::Yes);
    //_hdrFilteringProgram->setIgnoreUniformLocationError(IgnoreError::Yes);
}

void FramebufferRenderer::updateMSAASamplingPattern() {
    // JCC: All code below can be replaced by
    // void GetMultisamplefv( enum pname, uint index, float *val );
    
    LDEBUG("Updating MSAA Sampling Pattern");

    constexpr const int GridSize = 32;
    GLfloat step = 2.f / static_cast<GLfloat>(GridSize);
    GLfloat sizeX = -1.f;
    GLfloat sizeY = 1.0;

    constexpr const int NVertex = 4 * 6;
    // openPixelSizeVertexData
    GLfloat vertexData[GridSize * GridSize * NVertex];

    // @CLEANUP(abock): Is this necessary?  I was mucking about with the shader and it
    //                  didn't make any visual difference. If it is necessary, the z and w
    //                  components can be removed for sure since they are always 0, 1 and
    //                  not used in the shader either
    for (int y = 0; y < GridSize; ++y) {
        for (int x = 0; x < GridSize; ++x) {
            vertexData[y * GridSize * NVertex + x * NVertex] = sizeX;
            vertexData[y * GridSize * NVertex + x * NVertex + 1] = sizeY - step;
            vertexData[y * GridSize * NVertex + x * NVertex + 2] = 0.f;
            vertexData[y * GridSize * NVertex + x * NVertex + 3] = 1.f;

            vertexData[y * GridSize * NVertex + x * NVertex + 4] = sizeX + step;
            vertexData[y * GridSize * NVertex + x * NVertex + 5] = sizeY;
            vertexData[y * GridSize * NVertex + x * NVertex + 6] = 0.f;
            vertexData[y * GridSize * NVertex + x * NVertex + 7] = 1.f;

            vertexData[y * GridSize * NVertex + x * NVertex + 8] = sizeX;
            vertexData[y * GridSize * NVertex + x * NVertex + 9] = sizeY;
            vertexData[y * GridSize * NVertex + x * NVertex + 10] = 0.f;
            vertexData[y * GridSize * NVertex + x * NVertex + 11] = 1.f;

            vertexData[y * GridSize * NVertex + x * NVertex + 12] = sizeX;
            vertexData[y * GridSize * NVertex + x * NVertex + 13] = sizeY - step;
            vertexData[y * GridSize * NVertex + x * NVertex + 14] = 0.f;
            vertexData[y * GridSize * NVertex + x * NVertex + 15] = 1.f;

            vertexData[y * GridSize * NVertex + x * NVertex + 16] = sizeX + step;
            vertexData[y * GridSize * NVertex + x * NVertex + 17] = sizeY - step;
            vertexData[y * GridSize * NVertex + x * NVertex + 18] = 0.f;
            vertexData[y * GridSize * NVertex + x * NVertex + 19] = 1.f;

            vertexData[y * GridSize * NVertex + x * NVertex + 20] = sizeX + step;
            vertexData[y * GridSize * NVertex + x * NVertex + 21] = sizeY;
            vertexData[y * GridSize * NVertex + x * NVertex + 22] = 0.f;
            vertexData[y * GridSize * NVertex + x * NVertex + 23] = 1.f;

            sizeX += step;
        }
        sizeX = -1.f;
        sizeY -= step;
    }

    GLuint pixelSizeQuadVAO = 0;
    glGenVertexArrays(1, &pixelSizeQuadVAO);
    glBindVertexArray(pixelSizeQuadVAO);

    GLuint pixelSizeQuadVBO = 0;
    glGenBuffers(1, &pixelSizeQuadVBO);
    glBindBuffer(GL_ARRAY_BUFFER, pixelSizeQuadVBO);

    glBufferData(
        GL_ARRAY_BUFFER,
        sizeof(GLfloat) * GridSize * GridSize * NVertex,
        vertexData,
        GL_STATIC_DRAW
    );

    // Position
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(0);

    // Saves current state
    GLint defaultFbo;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &defaultFbo);
    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);

    // Main framebuffer
    GLuint pixelSizeTexture = 0;
    GLuint pixelSizeFramebuffer = 0;

    glGenTextures(1, &pixelSizeTexture);
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, pixelSizeTexture);

    constexpr const GLsizei OnePixel = 1;
    glTexImage2DMultisample(
        GL_TEXTURE_2D_MULTISAMPLE,
        _nAaSamples,
        GL_RGBA32F,
        OnePixel,
        OnePixel,
        true
    );

    glViewport(0, 0, OnePixel, OnePixel);

    glGenFramebuffers(1, &pixelSizeFramebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, pixelSizeFramebuffer);
    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D_MULTISAMPLE,
        pixelSizeTexture,
        0
    );

    GLenum textureBuffers[1] = { GL_COLOR_ATTACHMENT0 };
    glDrawBuffers(1, textureBuffers);

    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        LERROR("MSAA Sampling pattern framebuffer is not complete");
        return;
    }

    std::unique_ptr<ghoul::opengl::ProgramObject> pixelSizeProgram =
        ghoul::opengl::ProgramObject::Build(
            "OnePixel MSAA",
            absPath("${SHADERS}/framebuffer/pixelSizeMSAA.vert"),
            absPath("${SHADERS}/framebuffer/pixelSizeMSAA.frag")
        );

    pixelSizeProgram->activate();

    // Draw sub-pixel grid
    glEnable(GL_SAMPLE_SHADING);
    glBindVertexArray(pixelSizeQuadVAO);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(false);
    glDrawArrays(GL_TRIANGLES, 0, GridSize * GridSize * 6);
    glBindVertexArray(0);
    glDepthMask(true);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_SAMPLE_SHADING);

    pixelSizeProgram->deactivate();

    // Now we render the Nx1 quad strip
    GLuint nOneStripFramebuffer = 0;
    GLuint nOneStripVAO = 0;
    GLuint nOneStripVBO = 0;
    GLuint nOneStripTexture = 0;

    sizeX = -1.f;
    step = 2.f / static_cast<GLfloat>(_nAaSamples);

    std::vector<GLfloat>nOneStripVertexData(_nAaSamples * (NVertex + 12));

    for (int x = 0; x < _nAaSamples; ++x) {
        nOneStripVertexData[x * (NVertex + 12)] = sizeX;
        nOneStripVertexData[x * (NVertex + 12) + 1] = -1.f;
        nOneStripVertexData[x * (NVertex + 12) + 2] = 0.f;
        nOneStripVertexData[x * (NVertex + 12) + 3] = 1.f;
        nOneStripVertexData[x * (NVertex + 12) + 4] = 0.f;
        nOneStripVertexData[x * (NVertex + 12) + 5] = 0.f;

        nOneStripVertexData[x * (NVertex + 12) + 6] = sizeX + step;
        nOneStripVertexData[x * (NVertex + 12) + 7] = 1.f;
        nOneStripVertexData[x * (NVertex + 12) + 8] = 0.f;
        nOneStripVertexData[x * (NVertex + 12) + 9] = 1.f;
        nOneStripVertexData[x * (NVertex + 12) + 10] = 1.f;
        nOneStripVertexData[x * (NVertex + 12) + 11] = 1.f;

        nOneStripVertexData[x * (NVertex + 12) + 12] = sizeX;
        nOneStripVertexData[x * (NVertex + 12) + 13] = 1.f;
        nOneStripVertexData[x * (NVertex + 12) + 14] = 0.f;
        nOneStripVertexData[x * (NVertex + 12) + 15] = 1.f;
        nOneStripVertexData[x * (NVertex + 12) + 16] = 1.f;
        nOneStripVertexData[x * (NVertex + 12) + 17] = 0.f;

        nOneStripVertexData[x * (NVertex + 12) + 18] = sizeX;
        nOneStripVertexData[x * (NVertex + 12) + 19] = -1.f;
        nOneStripVertexData[x * (NVertex + 12) + 20] = 0.f;
        nOneStripVertexData[x * (NVertex + 12) + 21] = 1.f;
        nOneStripVertexData[x * (NVertex + 12) + 22] = 0.f;
        nOneStripVertexData[x * (NVertex + 12) + 23] = 0.f;

        nOneStripVertexData[x * (NVertex + 12) + 24] = sizeX + step;
        nOneStripVertexData[x * (NVertex + 12) + 25] = -1.f;
        nOneStripVertexData[x * (NVertex + 12) + 26] = 0.f;
        nOneStripVertexData[x * (NVertex + 12) + 27] = 1.f;
        nOneStripVertexData[x * (NVertex + 12) + 28] = 0.f;
        nOneStripVertexData[x * (NVertex + 12) + 29] = 1.f;

        nOneStripVertexData[x * (NVertex + 12) + 30] = sizeX + step;
        nOneStripVertexData[x * (NVertex + 12) + 31] = 1.f;
        nOneStripVertexData[x * (NVertex + 12) + 32] = 0.f;
        nOneStripVertexData[x * (NVertex + 12) + 33] = 1.f;
        nOneStripVertexData[x * (NVertex + 12) + 34] = 1.f;
        nOneStripVertexData[x * (NVertex + 12) + 35] = 1.f;

        sizeX += step;
    }

    glGenVertexArrays(1, &nOneStripVAO);
    glBindVertexArray(nOneStripVAO);
    glGenBuffers(1, &nOneStripVBO);
    glBindBuffer(GL_ARRAY_BUFFER, nOneStripVBO);
    glBufferData(
        GL_ARRAY_BUFFER,
        sizeof(GLfloat) * _nAaSamples * (NVertex + 12),
        nOneStripVertexData.data(),
        GL_STATIC_DRAW
    );

    // position
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(GLfloat) * 6, nullptr);
    glEnableVertexAttribArray(0);

    // texture coords
    glVertexAttribPointer(
        1,
        2,
        GL_FLOAT,
        GL_FALSE,
        sizeof(GLfloat) * 6,
        reinterpret_cast<GLvoid*>(sizeof(GLfloat) * 4)
    );
    glEnableVertexAttribArray(1);

    // fbo texture buffer
    glGenTextures(1, &nOneStripTexture);
    glBindTexture(GL_TEXTURE_2D, nOneStripTexture);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA32F,
        _nAaSamples,
        OnePixel,
        0,
        GL_RGBA,
        GL_FLOAT,
        nullptr
    );

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    glGenFramebuffers(1, &nOneStripFramebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, nOneStripFramebuffer);
    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D,
        nOneStripTexture,
        0
    );

    status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        LERROR("nOneStrip framebuffer is not complete");
    }

    glViewport(0, 0, _nAaSamples, OnePixel);

    std::unique_ptr<ghoul::opengl::ProgramObject> nOneStripProgram =
        ghoul::opengl::ProgramObject::Build(
            "OneStrip MSAA",
            absPath("${SHADERS}/framebuffer/nOneStripMSAA.vert"),
            absPath("${SHADERS}/framebuffer/nOneStripMSAA.frag")
        );

    nOneStripProgram->activate();

    ghoul::opengl::TextureUnit pixelSizeTextureUnit;
    pixelSizeTextureUnit.activate();
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, pixelSizeTexture);
    nOneStripProgram->setUniform("pixelSizeTexture", pixelSizeTextureUnit);

    // render strip
    glDrawBuffers(1, textureBuffers);

    glClearColor(0.f, 1.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindVertexArray(nOneStripVAO);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(false);

    for (int sample = 0; sample < _nAaSamples; ++sample) {
        nOneStripProgram->setUniform("currentSample", sample);
        glDrawArrays(GL_TRIANGLES, sample * 6, 6);
    }
    glDepthMask(true);
    glEnable(GL_DEPTH_TEST);
    glBindVertexArray(0);

    saveTextureToMemory(GL_COLOR_ATTACHMENT0, _nAaSamples, 1, _mSAAPattern);
    // Convert back to [-1, 1] range and then scale for the current viewport size:
    for (int d = 0; d < _nAaSamples; ++d) {
        _mSAAPattern[d * 3] = (2.0 * _mSAAPattern[d * 3] - 1.0) /
            static_cast<double>(viewport[1]);
        _mSAAPattern[(d * 3) + 1] = (2.0 * _mSAAPattern[(d * 3) + 1] - 1.0) /
            static_cast<double>(viewport[3]);
        _mSAAPattern[(d * 3) + 2] = 0.0;
    }

    nOneStripProgram->deactivate();

    // Restores default state
    glBindFramebuffer(GL_FRAMEBUFFER, defaultFbo);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);

    // Deletes unused buffers
    glDeleteFramebuffers(1, &pixelSizeFramebuffer);
    glDeleteTextures(1, &pixelSizeTexture);
    glDeleteBuffers(1, &pixelSizeQuadVBO);
    glDeleteVertexArrays(1, &pixelSizeQuadVAO);

    glDeleteFramebuffers(1, &nOneStripFramebuffer);
    glDeleteTextures(1, &nOneStripTexture);
    glDeleteBuffers(1, &nOneStripVBO);
    glDeleteVertexArrays(1, &nOneStripVAO);

    _dirtyMsaaSamplingPattern = false;
}

void FramebufferRenderer::render(Scene* scene, Camera* camera, float blackoutFactor) {
    // Set OpenGL default rendering state
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &_defaultFBO);
    glEnablei(GL_BLEND, 0);
    glDisablei(GL_BLEND, 1);
    glDisablei(GL_BLEND, 2);
    
    glClampColor(GL_CLAMP_READ_COLOR, GL_FALSE);

    glEnable(GL_DEPTH_TEST);

    _pingPongIndex = 0;
    
    // Measurements cache variable
    const bool doPerformanceMeasurements = global::performanceManager.isEnabled();

    std::unique_ptr<performance::PerformanceMeasurement> perf;
    if (doPerformanceMeasurements) {
        perf = std::make_unique<performance::PerformanceMeasurement>(
            "FramebufferRenderer::render"
        );
    }

    if (!scene || !camera) {
        return;
    }    

    // deferred g-buffer
    glBindFramebuffer(GL_FRAMEBUFFER, _gBuffers._framebuffer);
    glDrawBuffers(3, ColorAttachment012Array);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    Time time = global::timeManager.time();

    RenderData data = {
        *camera,
        std::move(time),
        doPerformanceMeasurements,
        0,
        {}
    };
    RendererTasks tasks;

    data.renderBinMask = static_cast<int>(Renderable::RenderBin::Background);
    scene->render(data, tasks);
    data.renderBinMask = static_cast<int>(Renderable::RenderBin::Opaque);
    scene->render(data, tasks);
    data.renderBinMask = static_cast<int>(Renderable::RenderBin::Transparent);
    scene->render(data, tasks);

    // Run Volume Tasks
    {
        std::unique_ptr<performance::PerformanceMeasurement> perfInternal;
        if (doPerformanceMeasurements) {
            perfInternal = std::make_unique<performance::PerformanceMeasurement>(
                "FramebufferRenderer::render::raycasterTasks"
            );
        }
        performRaycasterTasks(tasks.raycasterTasks);
    }

    if (!tasks.deferredcasterTasks.empty()) {
        // We use ping pong rendering in order to be able to
        // render to the same final buffer, multiple 
        // deferred tasks at same time (e.g. more than 1 ATM being seen at once)
        glBindFramebuffer(GL_FRAMEBUFFER, _pingPongBuffers.framebuffer);
        glDrawBuffers(1, &ColorAttachment01Array[_pingPongIndex]);

        std::unique_ptr<performance::PerformanceMeasurement> perfInternal;
        if (doPerformanceMeasurements) {
            perfInternal = std::make_unique<performance::PerformanceMeasurement>(
                "FramebufferRenderer::render::deferredTasks"
                );
        }
        performDeferredTasks(tasks.deferredcasterTasks);
    }
    
    glDrawBuffers(1, &ColorAttachment01Array[_pingPongIndex]);
    glEnablei(GL_BLEND, 0);

    data.renderBinMask = static_cast<int>(Renderable::RenderBin::Overlay);
    scene->render(data, tasks);

    glDrawBuffer(GL_COLOR_ATTACHMENT0);

    // Disabling depth test for filtering and hdr
    glDisable(GL_DEPTH_TEST);
    
    // When applying the TMO, the result is saved to the default FBO to be displayed
    // by the Operating System. Also, the resolve procedure is executed in this step.
    glBindFramebuffer(GL_FRAMEBUFFER, _defaultFBO);
    glViewport(0, 0, _resolution.x, _resolution.y);
    
    // Apply the selected TMO on the results and resolve the result for the default FBO
    applyTMO(blackoutFactor);
}

void FramebufferRenderer::performRaycasterTasks(const std::vector<RaycasterTask>& tasks) {
    for (const RaycasterTask& raycasterTask : tasks) {
        VolumeRaycaster* raycaster = raycasterTask.raycaster;

        glBindFramebuffer(GL_FRAMEBUFFER, _exitFramebuffer);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        ghoul::opengl::ProgramObject* exitProgram = _exitPrograms[raycaster].get();
        if (exitProgram) {
            exitProgram->activate();
            raycaster->renderExitPoints(raycasterTask.renderData, *exitProgram);
            exitProgram->deactivate();
        }

        glBindFramebuffer(GL_FRAMEBUFFER, _gBuffers._framebuffer);
        glm::vec3 cameraPosition;
        bool isCameraInside = raycaster->isCameraInside(
            raycasterTask.renderData,
            cameraPosition
        );
        ghoul::opengl::ProgramObject* raycastProgram = nullptr;

        if (isCameraInside) {
            raycastProgram = _insideRaycastPrograms[raycaster].get();
            if (raycastProgram) {
                raycastProgram->activate();
                raycastProgram->setUniform("cameraPosInRaycaster", cameraPosition);
            }
            else {
                raycastProgram = _insideRaycastPrograms[raycaster].get();
                raycastProgram->activate();
                raycastProgram->setUniform("cameraPosInRaycaster", cameraPosition);
            }
        }
        else {
            raycastProgram = _raycastPrograms[raycaster].get();
            if (raycastProgram) {
                raycastProgram->activate();
            }
            else {
                raycastProgram = _raycastPrograms[raycaster].get();
                raycastProgram->activate();
            }
        }

        if (raycastProgram) {
            raycaster->preRaycast(_raycastData[raycaster], *raycastProgram);

            ghoul::opengl::TextureUnit exitColorTextureUnit;
            exitColorTextureUnit.activate();
            glBindTexture(GL_TEXTURE_2D, _exitColorTexture);
            raycastProgram->setUniform("exitColorTexture", exitColorTextureUnit);

            ghoul::opengl::TextureUnit exitDepthTextureUnit;
            exitDepthTextureUnit.activate();
            glBindTexture(GL_TEXTURE_2D, _exitDepthTexture);
            raycastProgram->setUniform("exitDepthTexture", exitDepthTextureUnit);

            ghoul::opengl::TextureUnit mainDepthTextureUnit;
            mainDepthTextureUnit.activate();
            glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, _gBuffers._depthTexture);
            raycastProgram->setUniform("mainDepthTexture", mainDepthTextureUnit);

            raycastProgram->setUniform("nAaSamples", _nAaSamples);
            raycastProgram->setUniform("windowSize", static_cast<glm::vec2>(_resolution));

            glDisable(GL_DEPTH_TEST);
            glDepthMask(false);
            if (isCameraInside) {
                glBindVertexArray(_screenQuad);
                glDrawArrays(GL_TRIANGLES, 0, 6);
                glBindVertexArray(0);
            }
            else {
                raycaster->renderEntryPoints(raycasterTask.renderData, *raycastProgram);
            }
            glDepthMask(true);
            glEnable(GL_DEPTH_TEST);

            raycaster->postRaycast(_raycastData[raycaster], *raycastProgram);
            raycastProgram->deactivate();
        }
        else {
            LWARNING("Raycaster is not attached when trying to perform raycaster task");
        }
    }
}

void FramebufferRenderer::performDeferredTasks(
                                             const std::vector<DeferredcasterTask>& tasks
                                              )
{   
    for (const DeferredcasterTask& deferredcasterTask : tasks) {
        Deferredcaster* deferredcaster = deferredcasterTask.deferredcaster;

        ghoul::opengl::ProgramObject* deferredcastProgram = nullptr;

        if (deferredcastProgram != _deferredcastPrograms[deferredcaster].get()
            || deferredcastProgram == nullptr)
        {
            deferredcastProgram = _deferredcastPrograms[deferredcaster].get();
        }

        if (deferredcastProgram) {
            _pingPongIndex = _pingPongIndex == 0 ? 1 : 0;
            int fromIndex = _pingPongIndex == 0 ? 1 : 0;
            glDrawBuffers(1, &ColorAttachment01Array[_pingPongIndex]);
            glDisablei(GL_BLEND, 0);
            glDisablei(GL_BLEND, 1);

            deferredcastProgram->activate();

            // adding G-Buffer
            ghoul::opengl::TextureUnit mainDColorTextureUnit;
            mainDColorTextureUnit.activate();
            //glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, _gBuffers._colorTexture);
            glBindTexture(
                GL_TEXTURE_2D_MULTISAMPLE,
                _pingPongBuffers.colorTexture[fromIndex]
            );
            deferredcastProgram->setUniform(
                "mainColorTexture",
                mainDColorTextureUnit
            );

            ghoul::opengl::TextureUnit mainPositionTextureUnit;
            mainPositionTextureUnit.activate();
            glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, _gBuffers._positionTexture);
            deferredcastProgram->setUniform(
                "mainPositionTexture",
                mainPositionTextureUnit
            );

            ghoul::opengl::TextureUnit mainNormalTextureUnit;
            mainNormalTextureUnit.activate();
            glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, _gBuffers._normalTexture);
            deferredcastProgram->setUniform(
                "mainNormalTexture",
                mainNormalTextureUnit
            );

            deferredcastProgram->setUniform("nAaSamples", _nAaSamples);
            
            // 48 = 16 samples * 3 coords
            deferredcastProgram->setUniform("msaaSamplePatter", &_mSAAPattern[0], 48);

            deferredcaster->preRaycast(
                deferredcasterTask.renderData,
                _deferredcastData[deferredcaster],
                *deferredcastProgram
            );

            glDisable(GL_DEPTH_TEST);
            glDepthMask(false);

            glBindVertexArray(_screenQuad);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glBindVertexArray(0);

            glDepthMask(true);
            glEnable(GL_DEPTH_TEST);

            deferredcaster->postRaycast(
                deferredcasterTask.renderData,
                _deferredcastData[deferredcaster],
                *deferredcastProgram
            );

            deferredcastProgram->deactivate();
        }
        else {
            LWARNING(
                "Deferredcaster is not attached when trying to perform deferred task"
            );
        }
    }
}

void FramebufferRenderer::setResolution(glm::ivec2 res) {
    _resolution = std::move(res);
    _dirtyResolution = true;
}

void FramebufferRenderer::setNAaSamples(int nAaSamples) {
    ghoul_assert(
        nAaSamples >= 1 && nAaSamples <= 8,
        "Number of AA samples has to be between 1 and 8"
    );
    _nAaSamples = nAaSamples;
    if (_nAaSamples == 0) {
        _nAaSamples = 1;
    }
    if (_nAaSamples > 8) {
        LERROR("Framebuffer renderer does not support more than 8 MSAA samples.");
        _nAaSamples = 8;
    }
    _dirtyMsaaSamplingPattern = true;
}

void FramebufferRenderer::setHDRExposure(float hdrExposure) {
    ghoul_assert(hdrExposure > 0.f, "HDR exposure must be greater than zero");
    _hdrExposure = hdrExposure;
    updateRendererData();
}

void FramebufferRenderer::setGamma(float gamma) {
    ghoul_assert(gamma > 0.f, "Gamma value must be greater than zero");
    _gamma = gamma;
}

void FramebufferRenderer::setMaxWhite(float maxWhite) {
    ghoul_assert(maxWhite > 0.f, "Max White value must be greater than zero");
    _maxWhite = maxWhite;
}

void FramebufferRenderer::setToneMapOperator(int tmOp) {
    _toneMapOperator = tmOp;
}

void FramebufferRenderer::setHue(float hue) {
    _hue = hue;
}

void FramebufferRenderer::setValue(float value) {
    _value = value;
}

void FramebufferRenderer::setSaturation(float sat) {
    _saturation = sat;
}

void FramebufferRenderer::setLightness(float lightness) {
    _lightness = lightness;
}

void FramebufferRenderer::setColorSpace(unsigned int colorspace) {
    _colorSpace = colorspace;
}

int FramebufferRenderer::nAaSamples() const {
    return _nAaSamples;
}

const std::vector<double>& FramebufferRenderer::mSSAPattern() const {
    return _mSAAPattern;
}

void FramebufferRenderer::updateRendererData() {
    ghoul::Dictionary dict;
    dict.setValue("fragmentRendererPath", std::string(RenderFragmentShaderPath));
    dict.setValue("hdrExposure", std::to_string(_hdrExposure));
    _rendererData = dict;
    global::renderEngine.setRendererData(dict);
}

} // namespace openspace
