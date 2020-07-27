/*****************************************************************************************
*                                                                                       *
* OpenSpace                                                                             *
*                                                                                       *
* Copyright (c) 2014-2020                                                               *
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

#include <openspace/rendering/renderable.h>

#include <modules/fieldlinessequence/util/fieldlinesstate.h>
#include <openspace/properties/optionproperty.h>
#include <openspace/properties/stringproperty.h>
#include <openspace/properties/triggerproperty.h>
#include <openspace/properties/scalar/intproperty.h>
#include <openspace/properties/vector/vec2property.h>
#include <openspace/properties/vector/vec4property.h>
#include <openspace/rendering/transferfunction.h>
#include <atomic>

#include <modules/base/rendering/renderabletrail.h>

namespace { enum class SourceFileType; }

namespace openspace {

class RenderableStreamNodes : public Renderable {
public:
    RenderableStreamNodes(const ghoul::Dictionary& dictionary);

    void initializeGL() override;
    void deinitializeGL() override;

        bool isReady() const override;

    void render(const RenderData& data, RendererTasks& rendererTask) override;
    void update(const UpdateData& data) override;
    void updateActiveTriggerTimeIndex(double currentTime);

private:
    std::vector<GLsizei> _lineCount;
    std::vector<GLint> _lineStart;
    // ------------------------------------- ENUMS -------------------------------------//
    // Used to determine if lines should be colored UNIFORMLY or by Flux Value
    enum class ColorMethod : int {
        Uniform = 0,
        ByFluxValue = 1
    };
    enum class GoesEnergyBins : int {
        Emin01 = 0,
        Emin03 = 1
    };
    enum class ScalingMethod : int {
        Flux = 0,
        RFlux = 1,
        R2Flux = 2,
        log10RFlux = 3,
        lnRFlux = 4
    };
    enum class NodeSkipMethod : int {
        Uniform = 0,
        Flux = 1,
        Radius = 2,
        Streamnumber = 3
    };
    enum class DistanceMethod : int {
        Eucledian = 0,
        x = 1,
        y = 2,
        z = 3
    };
    enum class EnhanceMethod : int {
        Sizescaling = 0,
        Colortables = 1,
        Outline = 2,
        Lines = 3,
        Sizeandcolor = 4,
        test = 5
    };

    UniformCache(streamColor, nodeSize, nodeSizeLargerFlux, thresholdFlux)
        _uniformCache;

    // ------------------------------------ STRINGS ------------------------------------//
    // Name of the Node
    std::string _identifier;    
    // ------------------------------------- FLAGS -------------------------------------//
    // Used for 'runtime-states'. True when loading a new state from disk on another
    // thread.
    bool _isLoadingStateFromDisk = false;
    // False => states are stored in RAM (using 'in-RAM-states'), True => states are
    // loaded from disk during runtime (using 'runtime-states')
    bool _loadingStatesDynamically = false;
    // Used for 'runtime-states': True if new 'runtime-state' must be loaded from disk.
    // False => the previous frame's state should still be shown
    bool _mustLoadNewStateFromDisk = true;
    // Used for 'in-RAM-states' : True if new 'in-RAM-state'  must be loaded.
    // False => the previous frame's state should still be shown
    bool _needsUpdate = false;

    // Used for changing energybins during runtime, as to prevent loading and update issue
    // in render. 
    bool _isLoadingNewEnergyBin = false;

    //can be used when loading in emin03 files for the first time. 
    bool shouldwritecacheforemin03 = false;
    //Used for reading directly from sync-folder
    bool shouldreadBinariesDirectly = true;

    // --------------------------------- NUMERICALS ----------------------------------- //
    // Active index of _states. If(==-1)=>no state available for current time. Always the
    // same as _activeTriggerTimeIndex if(_loadingStatesDynamically==true), else
    // always = 0
    int _activeStateIndex = -1;
    // Active index of _startTimes
    int _activeTriggerTimeIndex = -1;
    // Number of states in the sequence
    size_t _nStates = 274;
    const int _numberofStreams = 863;
    // In setup it is used to scale JSON coordinates. During runtime it is used to scale
    // domain limits.
    float _scalingFactor = 1.f;
    // Estimated end of sequence.
    double _sequenceEndTime;
    // OpenGL Vertex Array Object
    GLuint _vertexArrayObject = 0;
    // OpenGL Vertex Buffer Object containing the vertex positions
    GLuint _vertexPositionBuffer = 0;
    // OpenGL Vertex Buffer Object containing the Flux values used for coloring
    // the nodes
    GLuint _vertexColorBuffer = 0;
    // OpenGL Vertex Buffer Object containing the positions to filter the nodes
    GLuint _vertexFilteringBuffer = 0;
    // OpenGL Vertex Buffer Object containing the index of nodes
    GLuint _vertexindexBuffer = 0;
    // OpenGL Vertex Buffer Object containing the stream number for every node. 
    GLuint _vertexStreamNumberBuffer = 0;
    // OpenGL Vertex Buffer Object containing the stream number for every node. 
    //GLuint _arrow = 0;


    // ----------------------------------- POINTERS ------------------------------------//
    // The Lua-Modfile-Dictionary used during initialization
    std::unique_ptr<ghoul::Dictionary> _dictionary;
    std::unique_ptr<ghoul::opengl::ProgramObject> _shaderProgram;
    // Transfer function used to color lines when _pColorMethod is set to BY_FLUX_VALUE
    std::unique_ptr<TransferFunction> _transferFunction;
    // Transfer function used to color line near Earth
    std::unique_ptr<TransferFunction> _transferFunctionEarth;
    // Transfer function used to color line flow
    std::unique_ptr<TransferFunction> _transferFunctionFlow;

    // ------------------------------------ VECTORS ----------------------------------- //
    // Paths to color tables. One for each 'ColorFlux'
    std::vector<std::string> _colorTablePaths;
    // Values represents min & max values represented in the color table
    std::vector<glm::vec2> _colorTableRanges;
    // Contains the _triggerTimes for all FieldlineStates in the sequence
    std::vector<double> _startTimes;
    // Contains vertexPositions
    std::vector<glm::vec3> _vertexPositions;
    // Contains vertex flux values for color
    std::vector<float> _vertexColor;
    // Contains radius of vertices
    std::vector<float> _vertexRadius;
    // Stores the states position
    std::vector<std::vector<glm::vec3>> _statesPos;
    // Stores the states color
    std::vector<std::vector<float>> _statesColor;
    // Stores the states radius
    std::vector<std::vector<float>> _statesRadius;
    //used to show vertexes dependent on specific streams
    std::vector<int> _vertexStreamnumber;
    // vector storing "interesting streams", read in by a json file.
    std::vector<int> _interestingStreams;

    // ---------------------------------- Properties ---------------------------------- //   
    // Group to hold properties regarding distance to earth
    properties::PropertyOwner _pEarthdistGroup;

    //Property to show different energybins
    properties::OptionProperty _pGoesEnergyBins;
    // Group to hold the color properties
    properties::PropertyOwner _pColorGroup;
    // Uniform/transfer function
    properties::OptionProperty _pColorMode;
    // Uniform stream Color
    properties::Vec4Property _pStreamColor;
    // Chose different distant measurements:
    properties::OptionProperty _pDistancemethod;
    // Choose different distant to earth enhanchement method. 
    properties::OptionProperty _pEnhancemethod;
    // Color table/transfer function for "By Flux value" coloring
    properties::StringProperty _pColorTablePath;
    // Valid range for the color table
    properties::Vec2Property _pColorTableRange;
    // The value of alpha for the flux color mode
    properties::FloatProperty _pFluxColorAlpha;
    // Group to hold the particle properties
    properties::PropertyOwner _pStreamGroup;
    // Scaling options
    properties::OptionProperty _pScalingmethod;
    // Group for how many nodes to render dependent on radius and flux
    properties::PropertyOwner _pNodesamountGroup;
    // Size of simulated node particles
    properties::FloatProperty _pNodeSize;
    // Size of nodes for larger flux
    properties::FloatProperty _pNodeSizeLargerFlux;
    // Threshold from earth to decide the distance for which the nodeSize gets larger
    properties::FloatProperty _pDistanceThreshold;
    // Minimum size of nodes at a certin distance
    //properties::FloatProperty _pMinNodeDistanceSize;
    // Maximum size of nodes at a certin distance
    properties::FloatProperty _pMaxNodeDistanceSize;
    // Threshold for where to interpolate between the max and min node distance
    properties::FloatProperty _pNodeDistanceThreshold;

    properties::FloatProperty _pMaxNodeSize;
    properties::FloatProperty _pMinNodeSize;
    /// Line width for the line rendering part
    properties::FloatProperty _pLineWidth;
    // Valid range along the Z-axis
    properties::Vec2Property _pDomainZ;
    // Threshold flux value
    properties::FloatProperty _pThresholdFlux;
    // Filtering nodes within a range
    properties::FloatProperty _pFilteringLower;
    // Filtering nodes with a upper range
    properties::FloatProperty _pFilteringUpper;
    // Amount of nodes to show
    properties::IntProperty _pAmountofNodes;
    // Nodeskipping options
    properties::OptionProperty _pNodeskipMethod;
    // amount of nodes to show outside of filterrange
    properties::IntProperty _pDefaultNodeSkip;
    // The Flux threshold to decide the line between
    //_pDefaultNodeSkip and _pAmountofNodes
    properties::FloatProperty _pFluxNodeskipThreshold;
    // The Radius threshold to decide the line between 
    //_pDefaultNodeSkip and _pAmountofNodes
    properties::FloatProperty _pRadiusNodeSkipThreshold;
    //The active stream that we want to look at
    properties::IntProperty _pActiveStreamNumber;

    //Misaligned index for fieldlines vs Streamnodes
    properties::IntProperty _pMisalignedIndex;

    // Flow Properties
    // Simulated particles' color
    properties::Vec4Property _pFlowColor;
    // Toggle flow [ON/OFF]
    properties::BoolProperty _pFlowEnabled;
    // Group to hold the flow/particle properties
    properties::PropertyOwner _pFlowGroup;
    // Size of simulated flow particles
    properties::IntProperty _pFlowParticleSize;
    // Size of simulated flow particles
    properties::IntProperty _pFlowParticleSpacing;
    // Toggle flow direction [FORWARDS/BACKWARDS]
    //properties::BoolProperty _pFlowReversed;
    // Speed of simulated flow
    properties::IntProperty _pFlowSpeed;
    //Either use flowcolortable or FlowColor.
    properties::BoolProperty _pUseFlowColor;

    properties::PropertyOwner _pCameraPerspectiveGroup;
    properties::BoolProperty _pCameraPerspective;
    properties::BoolProperty _pDrawingCircles;
    properties::BoolProperty _pDrawingHollow;
    properties::BoolProperty _pGaussianAlphaFilter;
    properties::BoolProperty _pRadiusPerspective;
    properties::FloatProperty _pPerspectiveDistanceFactor;
    properties::BoolProperty _pUseBlinking;
    properties::BoolProperty _pBlinkAlways;

    // initialization
    std::vector<std::string> _sourceFiles;
    // binary files sourcefolder
    std::string _binarySourceFilePath;

    // --------------------- FUNCTIONS USED DURING INITIALIZATION --------------------- //    
    bool extractMandatoryInfoFromDictionary(SourceFileType& sourceFileType);
    void definePropertyCallbackFunctions();
    bool extractJsonInfoFromDictionary(fls::Model& model);
    std::vector<std::string> LoadJsonfile(std::string filepath);
    void extractTriggerTimesFromFileNames();
    void computeSequenceEndTime();
    void setModelDependentConstants();
    void setupProperties();

    void writeCachedFile(const std::string& file) const;
    bool readCachedFile(const std::string& file, const std::string& energybin);
    bool loadFilesIntoRam();
    void loadNodeData();
    void createStreamnumberVector();
    void ExtractandwriteInterestingStreams(float distanceThreshold);
    void ReadInterestingStreamsFromJson();
    bool loadBinaryfilesDirectly(const std::string& energybin);
    // ------------------------- FUNCTIONS USED DURING RUNTIME ------------------------ //
    void updatePositionBuffer();
    void updateVertexColorBuffer();
    void updateVertexFilteringBuffer();
    void updateVertexStreamNumberBuffer();
    //void updateArrow();

    // ----------------------TEMPORARY VARIABLES ------------------
    //properties::StringProperty _spriteTexturePath;

    //std::unique_ptr<ghoul::opengl::Texture> _spriteTexture;
    //std::unique_ptr<ghoul::filesystem::File> _spriteTextureFile;

    properties::FloatProperty _scaleFactor;
};
}
