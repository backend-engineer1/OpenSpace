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

#ifndef __OPENSPACE_MODULE_ATMOSPHERE___ATMOSPHEREDEFERREDCASTER___H__
#define __OPENSPACE_MODULE_ATMOSPHERE___ATMOSPHEREDEFERREDCASTER___H__

#include <ghoul/glm.h>
#include <string>
#include <vector>
#include <openspace/rendering/deferredcaster.h>
#include <ghoul/opengl/textureunit.h>

//#include <modules/atmosphere/rendering/renderableatmosphere.h>

namespace ghoul::opengl {
    class Texture;        
    class ProgramObject;
    
}

namespace openspace {

struct RenderData;
struct DeferredcastData;
struct ShadowConfiguration;

class AtmosphereDeferredcaster : public Deferredcaster {
public:    
    AtmosphereDeferredcaster();
    virtual ~AtmosphereDeferredcaster() = default;

    void initialize();
    void deinitialize();
    void preRaycast(const RenderData& renderData, const DeferredcastData& deferredData,
                    ghoul::opengl::ProgramObject& program) override;
    void postRaycast(const RenderData& renderData, const DeferredcastData& deferredData,
                     ghoul::opengl::ProgramObject& program) override;
    
    std::string deferredcastPath() const override;
    std::string deferredcastVSPath() const override;
    std::string deferredcastFSPath() const override;
    std::string helperPath() const override;

    void preCalculateAtmosphereParam();

    void setModelTransform(const glm::dmat4 &transform);
    void setTime(const double time);
    void setAtmosphereRadius(const float atmRadius);
    void setPlanetRadius(const float planetRadius);
    void setPlanetAverageGroundReflectance(const float averageGReflectance);
    void setPlanetGroundRadianceEmittion(const float groundRadianceEmittion);
    void setRayleighHeightScale(const float rayleighHeightScale);
    void enableOzone(const bool enable);
    void setOzoneHeightScale(const float ozoneHeightScale);
    void setMieHeightScale(const float mieHeightScale);
    void setMiePhaseConstant(const float miePhaseConstant);
    void setSunRadianceIntensity(const float sunRadiance);
    void setRayleighScatteringCoefficients(const glm::vec3 & rayScattCoeff);
    void setOzoneExtinctionCoefficients(const glm::vec3 & ozoneExtCoeff);
    void setMieScatteringCoefficients(const glm::vec3 & mieScattCoeff);
    void setMieExtinctionCoefficients(const glm::vec3 & mieExtCoeff);
    void setEllipsoidRadii(const glm::dvec3 & radii);
    void setShadowConfigArray(const std::vector<ShadowConfiguration>& shadowConfigArray);
    void setHardShadows(const bool enabled);
    void enableSunFollowing(const bool enable);

    void setPrecalculationTextureScale(const float _preCalculatedTexturesScale);
    void enablePrecalculationTexturesSaving();

private:
    void loadComputationPrograms();
    void unloadComputationPrograms();
    void createComputationTextures();
    void deleteComputationTextures();
    void deleteUnusedComputationTextures();
    void executeCalculations(const GLuint quadCalcVAO,
                             const GLenum drawBuffers[1],
                             const GLsizei vertexSize);
    void resetAtmosphereTextures();
    void createRenderQuad(GLuint * vao, GLuint * vbo,
                          const GLfloat size);
    void step3DTexture(std::unique_ptr<ghoul::opengl::ProgramObject> & shaderProg,
                       const int layer, const bool doCalc = true);
    void checkFrameBufferState(const std::string & codePosition) const;
    void loadAtmosphereDataIntoShaderProgram(std::unique_ptr<ghoul::opengl::ProgramObject> & shaderProg);
    void renderQuadForCalc(const GLuint vao, const GLsizei numberOfVertices);
    void saveTextureToPPMFile(const GLenum color_buffer_attachment,
                              const std::string & fileName,
                              const int width, const int height) const;    
    bool isAtmosphereInFrustum(const double * MVMatrix, const glm::dvec3 position, const double radius) const;


    const double DISTANCE_CULLING = 1e10;
    
    std::unique_ptr<ghoul::opengl::ProgramObject> _transmittanceProgramObject;
    std::unique_ptr<ghoul::opengl::ProgramObject> _irradianceProgramObject;
    std::unique_ptr<ghoul::opengl::ProgramObject> _irradianceSupTermsProgramObject;
    std::unique_ptr<ghoul::opengl::ProgramObject> _irradianceFinalProgramObject;
    std::unique_ptr<ghoul::opengl::ProgramObject> _inScatteringProgramObject;
    std::unique_ptr<ghoul::opengl::ProgramObject> _inScatteringSupTermsProgramObject;
    std::unique_ptr<ghoul::opengl::ProgramObject> _deltaEProgramObject;
    std::unique_ptr<ghoul::opengl::ProgramObject> _deltaSProgramObject;
    std::unique_ptr<ghoul::opengl::ProgramObject> _deltaSSupTermsProgramObject;
    std::unique_ptr<ghoul::opengl::ProgramObject> _deltaJProgramObject;
    std::unique_ptr<ghoul::opengl::ProgramObject> _atmosphereProgramObject;
    std::unique_ptr<ghoul::opengl::ProgramObject> _deferredAtmosphereProgramObject;

    GLuint _transmittanceTableTexture;
    GLuint _irradianceTableTexture;
    GLuint _inScatteringTableTexture;
    GLuint _deltaETableTexture;
    GLuint _deltaSRayleighTableTexture;
    GLuint _deltaSMieTableTexture;
    GLuint _deltaJTableTexture;
    GLuint _dummyTexture;
    GLuint _atmosphereTexture;
    GLuint _atmosphereDepthTexture;

    ghoul::opengl::TextureUnit _transmittanceTableTextureUnit;
    ghoul::opengl::TextureUnit _irradianceTableTextureUnit;
    ghoul::opengl::TextureUnit _inScatteringTableTextureUnit;

    // Atmosphere Data
    bool _atmosphereCalculated;
    bool _atmosphereEnabled;
    bool _ozoneEnabled;
    bool _sunFollowingCameraEnabled;
    float _atmosphereRadius;
    float _atmospherePlanetRadius;
    float _planetAverageGroundReflectance;
    float _planetGroundRadianceEmittion;
    float _rayleighHeightScale;
    float _ozoneHeightScale;
    float _mieHeightScale;
    float _miePhaseConstant;
    float _sunRadianceIntensity;
    
    glm::vec3 _rayleighScatteringCoeff;
    glm::vec3 _ozoneExtinctionCoeff;
    glm::vec3 _mieScatteringCoeff;
    glm::vec3 _mieExtinctionCoeff;
    glm::dvec3 _ellipsoidRadii;

    // Atmosphere Texture Data Geometry
    GLuint _atmosphereFBO;
    GLuint _atmosphereRenderVAO;
    GLuint _atmosphereRenderVBO;

    // Atmosphere Textures Dimmensions
    int _transmittance_table_width;
    int _transmittance_table_height;
    int _irradiance_table_width;
    int _irradiance_table_height;
    int _delta_e_table_width;
    int _delta_e_table_height;
    int _r_samples;
    int _mu_samples;
    int _mu_s_samples;
    int _nu_samples;


    glm::dmat4 _modelTransform;
    float _stepSize;
    double _time;

    // Eclipse Shadows
    std::vector<ShadowConfiguration> _shadowConfArray;
    bool _hardShadowsEnabled;

    // Atmosphere Debugging
    float _calculationTextureScale;
    bool _saveCalculationTextures;   
};

} // openspace

#endif // __OPENSPACE_MODULE_ATMOSPHERE___ATMOSPHEREDEFERREDCASTER___H__
