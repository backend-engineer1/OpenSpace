/*****************************************************************************************
 *                                                                                       *
 * OpenSpace                                                                             *
 *                                                                                       *
 * Copyright (c) 2014-2017                                                               *
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

#include <modules/fieldlinessequence/rendering/renderablefieldlinessequence.h>

#include <openspace/engine/openspaceengine.h>
#include <openspace/engine/wrapper/windowwrapper.h>
#include <openspace/rendering/renderengine.h>
#include <openspace/util/updatestructures.h>

#include <ghoul/opengl/programobject.h>
#include <ghoul/opengl/textureunit.h>

namespace {
    std::string _loggerCat = "RenderableFieldlinesSequence";

    const GLuint VaPosition = 0; // MUST CORRESPOND TO THE SHADER PROGRAM
    const GLuint VaColor    = 1; // MUST CORRESPOND TO THE SHADER PROGRAM
    const GLuint VaMasking  = 2; // MUST CORRESPOND TO THE SHADER PROGRAM
} // namespace

namespace openspace {

void RenderableFieldlinesSequence::deinitialize() {
    glDeleteVertexArrays(1, &_vertexArrayObject);
    _vertexArrayObject = 0;

    glDeleteBuffers(1, &_vertexPositionBuffer);
    _vertexPositionBuffer = 0;

    glDeleteBuffers(1, &_vertexColorBuffer);
    _vertexColorBuffer = 0;

    glDeleteBuffers(1, &_vertexMaskingBuffer);
    _vertexMaskingBuffer = 0;

    RenderEngine& renderEngine = OsEng.renderEngine();
    if (_shaderProgram) {
        renderEngine.removeRenderProgram(_shaderProgram);
        _shaderProgram = nullptr;
    }

    // Stall main thread until thread that's loading states is done!
    while (_isLoadingStateFromDisk) {
        LWARNING("TRYING TO DESTROY CLASS WHEN A THREAD USING IT IS STILL ACTIVE");
    }
}

bool RenderableFieldlinesSequence::isReady() const {
    return _isReady;
}

void RenderableFieldlinesSequence::render(const RenderData& data, RendererTasks&) {
    if (_activeTriggerTimeIndex != -1) {
        _shaderProgram->activate();

        // Calculate Model View MatrixProjection
        const glm::dmat4 rotMat = glm::dmat4(data.modelTransform.rotation);
        const glm::dmat4 modelMat =
                glm::translate(glm::dmat4(1.0), data.modelTransform.translation) *
                rotMat *
                glm::dmat4(glm::scale(glm::dmat4(1), glm::dvec3(data.modelTransform.scale)));
        const glm::dmat4 modelViewMat = data.camera.combinedViewMatrix() * modelMat;

        _shaderProgram->setUniform("modelViewProjection",
                data.camera.sgctInternal.projectionMatrix() * glm::mat4(modelViewMat));

        _shaderProgram->setUniform("colorMethod",  _pColorMethod);
        _shaderProgram->setUniform("lineColor",    _pColorUniform);
        _shaderProgram->setUniform("usingDomain",  _pDomainEnabled);
        _shaderProgram->setUniform("usingMasking", _pMaskingEnabled);

        if (_pColorMethod == ColorMethod::ByQuantity) {
                ghoul::opengl::TextureUnit textureUnit;
                textureUnit.activate();
                _transferFunction->bind(); // Calls update internally
                _shaderProgram->setUniform("colorTable", textureUnit);
                _shaderProgram->setUniform("colorTableRange",
                                              _colorTableRanges[_pColorQuantity]);
        }

        if (_pMaskingEnabled) {
            _shaderProgram->setUniform("maskingRange", _maskingRanges[_pMaskingQuantity]);
        }

        _shaderProgram->setUniform("domainLimR", _pDomainR.value() * _scalingFactor);
        _shaderProgram->setUniform("domainLimX", _pDomainX.value() * _scalingFactor);
        _shaderProgram->setUniform("domainLimY", _pDomainY.value() * _scalingFactor);
        _shaderProgram->setUniform("domainLimZ", _pDomainZ.value() * _scalingFactor);

        // Flow/Particles
        _shaderProgram->setUniform("flowColor",       _pFlowColor);
        _shaderProgram->setUniform("usingParticles",  _pFlowEnabled);
        _shaderProgram->setUniform("particleSize",    _pFlowParticleSize);
        _shaderProgram->setUniform("particleSpacing", _pFlowParticleSpacing);
        _shaderProgram->setUniform("particleSpeed",   _pFlowSpeed);
        _shaderProgram->setUniform(
            "time",
            OsEng.windowWrapper().applicationTime() * (_pFlowReversed ? -1 : 1)
        );

        bool additiveBlending = false;
        if (_pColorABlendEnabled) {
            const auto renderer = OsEng.renderEngine().rendererImplementation();
            bool usingFBufferRenderer = renderer ==
                                        RenderEngine::RendererImplementation::Framebuffer;

            bool usingABufferRenderer = renderer ==
                                        RenderEngine::RendererImplementation::ABuffer;

            if (usingABufferRenderer) {
                _shaderProgram->setUniform("usingAdditiveBlending", _pColorABlendEnabled);
            }

            additiveBlending = usingFBufferRenderer;
            if (additiveBlending) {
                glDepthMask(false);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            }
        }

        glBindVertexArray(_vertexArrayObject);
        glMultiDrawArrays(
                GL_LINE_STRIP, //_drawingOutputType,
                _states[_activeStateIndex].lineStart().data(),
                _states[_activeStateIndex].lineCount().data(),
                static_cast<GLsizei>(_states[_activeStateIndex].lineStart().size())
        );

        glBindVertexArray(0);
        _shaderProgram->deactivate();

        if (additiveBlending) {
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(true);
        }
    }
}

void RenderableFieldlinesSequence::update(const UpdateData& data) {
    // This node shouldn't do anything if its been disabled from the gui!
    if (!_enabled) {
        return;
    }

    if (_shaderProgram->isDirty()) {
        _shaderProgram->rebuildFromFile();
    }

    const double currentTime = data.time.j2000Seconds();
    // Check if current time in OpenSpace is within sequence interval
    if (isWithinSequenceInterval(currentTime)) {
        const int nextIdx = _activeTriggerTimeIndex + 1;
        if (_activeTriggerTimeIndex < 0                                       // true => Previous frame was not within the sequence interval
            || currentTime < _startTimes[_activeTriggerTimeIndex]             // true => OpenSpace has stepped back    to a time represented by another state
            || (nextIdx < _nStates && currentTime >= _startTimes[nextIdx])) { // true => OpenSpace has stepped forward to a time represented by another state

            updateActiveTriggerTimeIndex(currentTime);

            if (_loadingStatesDynamically) {
                _mustLoadNewStateFromDisk = true;
            } else {
                _needsUpdate = true;
                _activeStateIndex = _activeTriggerTimeIndex;
            }
        } // else {we're still in same state as previous frame (no changes needed)}
    } else {
        // Not in interval => set everything to false
        _activeTriggerTimeIndex   = -1;
        _mustLoadNewStateFromDisk = false;
        _needsUpdate              = false;
    }

    if (_mustLoadNewStateFromDisk) {
        if (!_isLoadingStateFromDisk && !_newStateIsReady) {
                _isLoadingStateFromDisk    = true;
                _mustLoadNewStateFromDisk  = false;
                const std::string filePath = _sourceFiles[_activeTriggerTimeIndex];
                std::thread readBinaryThread([this, filePath] {
                    this->readNewState(filePath);
                });
                readBinaryThread.detach();
        }
    }

    if (_needsUpdate || _newStateIsReady) {
        if (_loadingStatesDynamically) {
            _states[0] = std::move(*_newState);
        }

        updateVertexPositionBuffer();

        if (_states[_activeStateIndex].nExtraQuantities() > 0) {
            _shouldUpdateColorBuffer   = true;
            _shouldUpdateMaskingBuffer = true;
        }

        // Everything is set and ready for rendering!
        _needsUpdate     = false;
        _newStateIsReady = false;
    }

    if (_shouldUpdateColorBuffer) {
        updateVertexColorBuffer();
        _shouldUpdateColorBuffer = false;
    }

    if (_shouldUpdateMaskingBuffer) {
        updateVertexMaskingBuffer();
        _shouldUpdateMaskingBuffer = false;
    }
}

inline bool RenderableFieldlinesSequence::isWithinSequenceInterval(const double currentTime) const {
    return (currentTime >= _startTimes[0]) && (currentTime < _sequenceEndTime);
}

// Assumes we already know that currentTime is within the sequence interval
void RenderableFieldlinesSequence::updateActiveTriggerTimeIndex(const double currentTime) {
    auto iter = std::upper_bound(_startTimes.begin(), _startTimes.end(), currentTime);
    if (iter != _startTimes.end()) {
        if ( iter != _startTimes.begin()) {
            _activeTriggerTimeIndex =
                    static_cast<int>(std::distance(_startTimes.begin(), iter)) - 1;
        } else {
            _activeTriggerTimeIndex = 0;
        }
    } else {
        _activeTriggerTimeIndex = static_cast<int>(_nStates) - 1;
    }
}

// Reading state from disk. Must be thread safe!
void RenderableFieldlinesSequence::readNewState(const std::string& filePath) {
    _newState = std::make_unique<FieldlinesState>();
    if (_newState->loadStateFromOsfls(filePath)) {
        _newStateIsReady = true;
    }
    _isLoadingStateFromDisk = false;
}

// Unbind buffers and arrays
inline void unbindGL() {
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void RenderableFieldlinesSequence::updateVertexPositionBuffer() {
    glBindVertexArray(_vertexArrayObject);
    glBindBuffer(GL_ARRAY_BUFFER, _vertexPositionBuffer);

    const std::vector<glm::vec3>& vertexPosVec =
            _states[_activeStateIndex].vertexPositions();

    glBufferData(GL_ARRAY_BUFFER, vertexPosVec.size() * sizeof(glm::vec3),
            &vertexPosVec.front(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(VaPosition);
    glVertexAttribPointer(VaPosition, 3, GL_FLOAT, GL_FALSE, 0, 0);

    unbindGL();
}

void RenderableFieldlinesSequence::updateVertexColorBuffer() {
    glBindVertexArray(_vertexArrayObject);
    glBindBuffer(GL_ARRAY_BUFFER, _vertexColorBuffer);

    bool isSuccessful;
    const std::vector<float>& quantityVec =
            _states[_activeStateIndex].extraQuantity(_pColorQuantity, isSuccessful);

    if (isSuccessful) {
        glBufferData(GL_ARRAY_BUFFER, quantityVec.size() * sizeof(float),
                &quantityVec.front(), GL_STATIC_DRAW);

        glEnableVertexAttribArray(VaColor);
        glVertexAttribPointer(VaColor, 1, GL_FLOAT, GL_FALSE, 0, 0);

        unbindGL();
    }
}

void RenderableFieldlinesSequence::updateVertexMaskingBuffer() {
    glBindVertexArray(_vertexArrayObject);
    glBindBuffer(GL_ARRAY_BUFFER, _vertexMaskingBuffer);

    bool isSuccessful;
    const std::vector<float>& quantityVec =
        _states[_activeStateIndex].extraQuantity(_pMaskingQuantity, isSuccessful);

    if (isSuccessful) {
        glBufferData(GL_ARRAY_BUFFER, quantityVec.size() * sizeof(float),
            &quantityVec.front(), GL_STATIC_DRAW);

        glEnableVertexAttribArray(VaMasking);
        glVertexAttribPointer(VaMasking, 1, GL_FLOAT, GL_FALSE, 0, 0);

        unbindGL();
    }
}

} // namespace openspace
