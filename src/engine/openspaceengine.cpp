/*****************************************************************************************
 *                                                                                       *
 * OpenSpace                                                                             *
 *                                                                                       *
 * Copyright (c) 2014-2015                                                               *
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

#include <openspace/engine/openspaceengine.h>

#include <ghoul/opengl/ghoul_gl.h>

#define SGCT_WINDOWS_INCLUDE
#include <sgct.h>
#include <openspace/version.h>

#include <openspace/engine/configurationmanager.h>
#include <openspace/engine/logfactory.h>
#include <openspace/interaction/interactionhandler.h>
#include <openspace/interaction/keyboardcontroller.h>
#include <openspace/interaction/luaconsole.h>
#include <openspace/interaction/mousecontroller.h>
#include <openspace/network/networkengine.h>
#include <openspace/rendering/renderengine.h>
#include <openspace/scripting/scriptengine.h>
#include <openspace/scene/scene.h>
#include <openspace/util/time.h>
#include <openspace/util/spicemanager.h>
#include <openspace/util/factorymanager.h>
#include <openspace/util/constants.h>
#include <openspace/util/spicemanager.h>
#include <openspace/util/syncbuffer.h>
#include <openspace/engine/moduleengine.h>
#include <openspace/engine/downloadmanager.h>

#include <ghoul/ghoul.h>
#include <ghoul/cmdparser/commandlineparser.h>
#include <ghoul/cmdparser/singlecommand.h>
#include <ghoul/filesystem/filesystem.h>
#include <ghoul/filesystem/cachemanager.h>
#include <ghoul/logging/consolelog.h>
#include <ghoul/lua/ghoul_lua.h>
#include <ghoul/lua/lua_helper.h>
#include <ghoul/systemcapabilities/systemcapabilities.h>

// std
#include <iostream>
#include <fstream>

#ifdef OPENSPACE_MODULE_ONSCREENGUI_ENABLED
#include <modules/onscreengui/include/gui.h>
#endif

#ifdef _MSC_VER
#ifdef OPENSPACE_ENABLE_VLD
#include <vld.h>
#endif
#endif

using namespace openspace::scripting;
using namespace ghoul::filesystem;
using namespace ghoul::logging;
using namespace ghoul::cmdparser;

namespace {
    const std::string _loggerCat = "OpenSpaceEngine";
    const std::string _configurationFile = "openspace.cfg";
    const std::string _sgctDefaultConfigFile = "${SGCT}/single.xml";
	const std::string _defaultCacheLocation = "${BASE_PATH}/cache";
    
    const std::string _sgctConfigArgumentCommand = "-config";

    const int CacheVersion = 1;
    const int DownloadVersion = 1;
    
    struct {
        std::string configurationName;
		std::string sgctConfigurationName;
    } commandlineArgumentPlaceholders;
}

namespace openspace {

OpenSpaceEngine* OpenSpaceEngine::_engine = nullptr;

OpenSpaceEngine::OpenSpaceEngine(std::string programName)
    : _configurationManager(new ConfigurationManager)
    , _interactionHandler(new interaction::InteractionHandler)
    , _renderEngine(new RenderEngine)
    , _scriptEngine(new scripting::ScriptEngine)
    , _networkEngine(new NetworkEngine)
    , _commandlineParser(new ghoul::cmdparser::CommandlineParser(programName, true))
    , _console(new LuaConsole)
    , _moduleEngine(new ModuleEngine)
    , _gui(new gui::GUI)
	, _isMaster(false)
    , _syncBuffer(nullptr)
{
    FactoryManager::initialize();
    SpiceManager::initialize();
    Time::initialize();
    ghoul::systemcapabilities::SystemCapabilities::initialize();
}

OpenSpaceEngine::~OpenSpaceEngine() {
    _gui->deinitializeGL();

    delete _configurationManager;
    delete _interactionHandler;
    delete _renderEngine;
    delete _scriptEngine;
    delete _networkEngine;
    delete _commandlineParser;
    delete _console;
    delete _moduleEngine;
    delete _gui;

    if(_syncBuffer)
		delete _syncBuffer;
	_syncBuffer = nullptr;
}

OpenSpaceEngine& OpenSpaceEngine::ref() {
    ghoul_assert(_engine, "OpenSpaceEngine not created");
    return *_engine;
}

bool OpenSpaceEngine::create(
    int argc, char** argv,
    std::vector<std::string>& sgctArguments)
{
    ghoul::initialize();

    ghoul_assert(!_engine, "OpenSpaceEngine was already created");

	// Initialize the LogManager and add the console log as this will be used every time
	// and we need a fall back if something goes wrong between here and when we add the
	// logs from the configuration file. If the user requested as specific loglevel in the
	// configuration file, we will deinitialize this LogManager and reinitialize it later
	// with the correct LogLevel
	LogManager::initialize(LogManager::LogLevel::Debug, true);
	LogMgr.addLog(new ConsoleLog);

	LDEBUG("Initialize FileSystem");

#ifdef __APPLE__
    ghoul::filesystem::File app(argv[0]);
    std::string dirName = app.directoryName();
    LINFO("Setting starting directory to '" << dirName << "'");
    FileSys.setCurrentDirectory(dirName);
#endif

	// Sanity check of values
	if (argc < 1 || argv == nullptr) {
		LFATAL("No arguments were passed to this function");
		return false;
	}

	// Create other objects
	LDEBUG("Creating OpenSpaceEngine");
	_engine = new OpenSpaceEngine(std::string(argv[0]));

	// Query modules for commandline arguments
	const bool gatherSuccess = _engine->gatherCommandlineArguments();
	if (!gatherSuccess)
		return false;

	// Parse commandline arguments
	_engine->_commandlineParser->setCommandLine(argc, argv, &sgctArguments);
	const bool executeSuccess = _engine->_commandlineParser->execute();
	if (!executeSuccess)
		return false;

	// Find configuration
	std::string configurationFilePath = commandlineArgumentPlaceholders.configurationName;
	if (configurationFilePath.empty()) {
		LDEBUG("Finding configuration");
		const bool findConfigurationSuccess =
			OpenSpaceEngine::findConfiguration(configurationFilePath);
		if (!findConfigurationSuccess) {
			LFATAL("Could not find OpenSpace configuration file!");
			return false;
		}
	}
	configurationFilePath = absPath(configurationFilePath);
	LINFO("Configuration Path: '" << configurationFilePath << "'");

	// Loading configuration from disk
	LDEBUG("Loading configuration from disk");
	const bool configLoadSuccess = _engine->configurationManager()->loadFromFile(
																configurationFilePath);
	if (!configLoadSuccess) {
		LFATAL("Loading of configuration file '" << configurationFilePath << "' failed");
		return false;
	}

	// Initialize the requested logs from the configuration file
	_engine->configureLogging();

    LINFOC("OpenSpace Version", 
        OPENSPACE_VERSION_MAJOR << "." <<
        OPENSPACE_VERSION_MINOR << "." <<
        OPENSPACE_VERSION_PATCH << " (" << OPENSPACE_VERSION_STRING << ")");

	// Create directories that doesn't exist
	auto tokens = FileSys.tokens();
	for (const std::string& token : tokens) {
		if (!FileSys.directoryExists(token)) {
			std::string p = absPath(token);
			LDEBUG("Directory '" << p << "' does not exist, creating.");
			if (!FileSys.createDirectory(p, true))
				LERROR("Directory '" << p << "' could not be created");
		}
	}

    // Register modules
    _engine->_moduleEngine->create();

	// Create the cachemanager
	FileSys.createCacheManager(absPath("${" + ConfigurationManager::KeyCache + "}"), CacheVersion);
	_engine->_console->initialize();

	// Register the provided shader directories
	ghoul::opengl::ShaderObject::addIncludePath("${SHADERS}");

	_engine->_syncBuffer = new SyncBuffer(1024);

	// Determining SGCT configuration file
	LDEBUG("Determining SGCT configuration file");
	std::string sgctConfigurationPath = _sgctDefaultConfigFile;
	_engine->configurationManager()->getValue(
		ConfigurationManager::KeyConfigSgct, sgctConfigurationPath);

	if (!commandlineArgumentPlaceholders.sgctConfigurationName.empty()) {
		LDEBUG("Overwriting SGCT configuration file with commandline argument: " <<
			commandlineArgumentPlaceholders.sgctConfigurationName);
		sgctConfigurationPath = commandlineArgumentPlaceholders.sgctConfigurationName;
	}

	// Prepend the outgoing sgctArguments with the program name
	// as well as the configuration file that sgct is supposed to use
	sgctArguments.insert(sgctArguments.begin(), argv[0]);
	sgctArguments.insert(sgctArguments.begin() + 1, _sgctConfigArgumentCommand);
	sgctArguments.insert(sgctArguments.begin() + 2, absPath(sgctConfigurationPath));

	return true;
}

void OpenSpaceEngine::destroy() {
    _engine->_moduleEngine->deinitialize();
    _engine->_moduleEngine->destroy();
    _engine->_console->deinitialize();

    _engine->_scriptEngine->deinitialize();
	delete _engine;
	ghoul::systemcapabilities::SystemCapabilities::deinitialize();
	FactoryManager::deinitialize();
	Time::deinitialize();
	SpiceManager::deinitialize();

	FileSystem::deinitialize();
	LogManager::deinitialize();

    ghoul::deinitialize();
}

bool OpenSpaceEngine::initialize() {
	// clear the screen so the user don't have to see old buffer contents from the
	// graphics card
	clearAllWindows();

	// Detect and log OpenCL and OpenGL versions and available devices
	SysCap.addComponent(new ghoul::systemcapabilities::GeneralCapabilitiesComponent);
	SysCap.addComponent(new ghoul::systemcapabilities::OpenGLCapabilitiesComponent);
	SysCap.detectCapabilities();

    using Verbosity = ghoul::systemcapabilities::SystemCapabilitiesComponent::Verbosity;
    Verbosity verbosity = Verbosity::Default;
    if (configurationManager()->hasKeyAndValue<std::string>(ConfigurationManager::KeyCapabilitiesVerbosity)) {
        std::map<std::string, Verbosity> verbosityMap = {
            { "Minimal", Verbosity::Minimal },
            { "Default", Verbosity::Default },
            { "Full", Verbosity::Full }
        };

        std::string v = configurationManager()->value<std::string>(ConfigurationManager::KeyCapabilitiesVerbosity);
        if (verbosityMap.find(v) != verbosityMap.end())
            verbosity = verbosityMap[v];
    }
	SysCap.logCapabilities(verbosity);
    
    std::string requestURL = "";
    bool success = configurationManager()->getValue(ConfigurationManager::KeyDownloadRequestURL, requestURL);
    if (success)
        DownloadManager::initialize(requestURL, DownloadVersion);

	// Load SPICE time kernel
	success = loadSpiceKernels();
	if (!success)
		return false;

	// Register Lua script functions
	LDEBUG("Registering Lua libraries");
	_scriptEngine->addLibrary(RenderEngine::luaLibrary());
	_scriptEngine->addLibrary(Scene::luaLibrary());
	_scriptEngine->addLibrary(Time::luaLibrary());
	_scriptEngine->addLibrary(interaction::InteractionHandler::luaLibrary());
	_scriptEngine->addLibrary(LuaConsole::luaLibrary());
	_scriptEngine->addLibrary(gui::GUI::luaLibrary());

	// TODO: Maybe move all scenegraph and renderengine stuff to initializeGL
	scriptEngine()->initialize();

	// If a LuaDocumentationFile was specified, generate it now
	const bool hasType = configurationManager()->hasKey(ConfigurationManager::KeyLuaDocumentationType);
	const bool hasFile = configurationManager()->hasKey(ConfigurationManager::KeyLuaDocumentationFile);
	if (hasType && hasFile) {
		std::string luaDocumentationType;
		configurationManager()->getValue(ConfigurationManager::KeyLuaDocumentationType, luaDocumentationType);
		std::string luaDocumentationFile;
		configurationManager()->getValue(ConfigurationManager::KeyLuaDocumentationFile, luaDocumentationFile);

		luaDocumentationFile = absPath(luaDocumentationFile);
		_scriptEngine->writeDocumentation(luaDocumentationFile, luaDocumentationType);
	}

    bool disableMasterRendering = false;
    configurationManager()->getValue(
        ConfigurationManager::KeyDisableMasterRendering, disableMasterRendering);
    _renderEngine->setDisableRenderingOnMaster(disableMasterRendering);


	// Load scenegraph
	Scene* sceneGraph = new Scene;
	_renderEngine->setSceneGraph(sceneGraph);

	// initialize the RenderEngine
    _renderEngine->initialize();
	sceneGraph->initialize();

	std::string sceneDescriptionPath;
	success = configurationManager()->getValue(
		ConfigurationManager::KeyConfigScene, sceneDescriptionPath);
	if (success)
		sceneGraph->scheduleLoadSceneFile(sceneDescriptionPath);

	_interactionHandler->setKeyboardController(new interaction::KeyboardControllerFixed);
	_interactionHandler->setMouseController(new interaction::OrbitalMouseController);

	// Run start up scripts
	runStartupScripts();

	// Load a light and a monospaced font
	loadFonts();

    LINFO("Initializing GUI");
	_gui->initialize();

    // Initialize modules
    _moduleEngine->initialize();

    LINFO("Finished initializing");
	return true;
}

bool OpenSpaceEngine::isInitialized() {
	return _engine != nullptr;
}

void OpenSpaceEngine::clearAllWindows() {
	size_t n = sgct::Engine::instance()->getNumberOfWindows();
	for (size_t i = 0; i < n; ++i) {
		glClearColor(0, 0, 0, 0);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		GLFWwindow* win = sgct::Engine::instance()->getWindowPtr(i)->getWindowHandle();
		glfwSwapBuffers(win);
	}
}

bool OpenSpaceEngine::gatherCommandlineArguments() {
    // TODO: Get commandline arguments from all modules

	commandlineArgumentPlaceholders.configurationName = "";
    CommandlineCommand* configurationFileCommand = new SingleCommand<std::string>(
          &commandlineArgumentPlaceholders.configurationName, "-config", "-c",
          "Provides the path to the OpenSpace configuration file");
    _commandlineParser->addCommand(configurationFileCommand);

	commandlineArgumentPlaceholders.sgctConfigurationName = "";
	CommandlineCommand* sgctConfigFileCommand = new SingleCommand<std::string>(
		&commandlineArgumentPlaceholders.sgctConfigurationName, "-sgct", "-s",
		"Provides the path to the SGCT configuration file, overriding the value set in"
		"the OpenSpace configuration file");
	_commandlineParser->addCommand(sgctConfigFileCommand);

    return true;
}

bool OpenSpaceEngine::findConfiguration(std::string& filename) {
	using ghoul::filesystem::Directory;

	Directory directory = FileSys.currentDirectory();
	std::string configurationName = _configurationFile;

	while (true) {
		std::string&& fullPath = FileSys.pathByAppendingComponent(directory,
																  configurationName);
		bool exists = FileSys.fileExists(fullPath);
		if (exists) {
			filename = fullPath;
			return true;
		}

		Directory nextDirectory = directory.parentDirectory(true);

		if (directory.path() == nextDirectory.path())
			// We have reached the root of the file system and did not find the file
			return false;
		directory = nextDirectory;
	}
}

bool OpenSpaceEngine::loadSpiceKernels() {
	// Load time kernel
	std::string timeKernel;
	bool success = configurationManager()->getValue(ConfigurationManager::KeySpiceTimeKernel, timeKernel);
    // Move this to configurationmanager::completenesscheck ---abock
	if (!success) {
		LERROR("Configuration file does not contain a '" << ConfigurationManager::KeySpiceTimeKernel << "'");
		return false;
	}
	SpiceManager::KernelIdentifier id =
		SpiceManager::ref().loadKernel(timeKernel);
	if (id == SpiceManager::KernelFailed) {
		LERROR("Error loading time kernel '" << timeKernel << "'");
		return false;
	}

	// Load SPICE leap second kernel
	std::string leapSecondKernel;
	success = configurationManager()->getValue(ConfigurationManager::KeySpiceLeapsecondKernel, leapSecondKernel);
	if (!success) {
        // Move this to configurationmanager::completenesscheck ---abock
		LERROR("Configuration file does not have a '" << ConfigurationManager::KeySpiceLeapsecondKernel << "'");
		return false;
	}
	id = SpiceManager::ref().loadKernel(std::move(leapSecondKernel));
	if (id == SpiceManager::KernelFailed) {
		LERROR("Error loading leap second kernel '" << leapSecondKernel << "'");
		return false;
	}
	return true;
}

void OpenSpaceEngine::runScripts(const ghoul::Dictionary& scripts) {
    for (size_t i = 1; i <= scripts.size(); ++i) {
        std::stringstream stream;
        stream << i;
        const std::string& key = stream.str();
        const bool hasKey = scripts.hasKeyAndValue<std::string>(key);
        if (!hasKey) {
            LERROR("The startup scripts have to be declared in a simple array format."
                " Startup scripts did not contain the key '" << key << "'");
            break;
        }

        std::string scriptPath;
        scripts.getValue(key, scriptPath);
        std::string&& absoluteScriptPath = absPath(scriptPath);
        _engine->scriptEngine()->runScriptFile(absoluteScriptPath);
    }
}


void OpenSpaceEngine::runStartupScripts() {
	ghoul::Dictionary scripts;
	configurationManager()->getValue(
		ConfigurationManager::KeyStartupScript, scripts);
    runScripts(scripts);
}

void OpenSpaceEngine::runSettingsScripts() {
    ghoul::Dictionary scripts;
    configurationManager()->getValue(
        ConfigurationManager::KeySettingsScript, scripts);
    runScripts(scripts);
}

void OpenSpaceEngine::loadFonts() {
	sgct_text::FontManager::FontPath local = sgct_text::FontManager::FontPath::FontPath_Local;

	ghoul::Dictionary fonts;
	configurationManager()->getValue(ConfigurationManager::KeyFonts, fonts);

	for (const std::string& key : fonts.keys()) {
		std::string font;
		fonts.getValue(key, font);
		font = absPath(font);
        if(!FileSys.fileExists(font)) {
            LERROR("Could not find font '" << font << "'");
            continue;
        }

		LINFO("Registering font '" << font << "' with key '" << key << "'");
		sgct_text::FontManager::instance()->addFont(key, font, local);
	}
}

void OpenSpaceEngine::configureLogging() {
	if (configurationManager()->hasKeyAndValue<std::string>(ConfigurationManager::KeyLogLevel)) {
		std::string logLevel;
		configurationManager()->getValue(ConfigurationManager::KeyLogLevel, logLevel);

		bool immediateFlush = false;
		configurationManager()->getValue(ConfigurationManager::KeyLogImmediateFlush, immediateFlush);

		LogManager::LogLevel level = LogManager::levelFromString(logLevel);
		LogManager::deinitialize();
		LogManager::initialize(level, immediateFlush);
		LogMgr.addLog(new ConsoleLog);
	}

	if (configurationManager()->hasKeyAndValue<ghoul::Dictionary>(ConfigurationManager::KeyLogs)) {
		ghoul::Dictionary logs;
		configurationManager()->getValue(ConfigurationManager::KeyLogs, logs);

		for (size_t i = 1; i <= logs.size(); ++i) {
			ghoul::Dictionary logInfo;
			logs.getValue(std::to_string(i), logInfo);

			Log* log = LogFactory::createLog(logInfo);

			if (log)
				LogMgr.addLog(log);
		}
	}
}

ConfigurationManager* OpenSpaceEngine::configurationManager() {
    ghoul_assert(_configurationManager != nullptr, "ConfigurationManager is nullptr");
    return _configurationManager;
}

interaction::InteractionHandler* OpenSpaceEngine::interactionHandler() {
    ghoul_assert(_interactionHandler != nullptr, "InteractionHandler is nullptr");
    return _interactionHandler;
}

RenderEngine* OpenSpaceEngine::renderEngine() {
    ghoul_assert(_renderEngine != nullptr, "RenderEngine is nullptr");
    return _renderEngine;
}

ScriptEngine* OpenSpaceEngine::scriptEngine() {
    ghoul_assert(_scriptEngine != nullptr, "ScriptEngine is nullptr");
    return _scriptEngine;
}

LuaConsole* OpenSpaceEngine::console() {
    ghoul_assert(_console != nullptr, "LuaConsole is nullptr");
	return _console;
}

gui::GUI* OpenSpaceEngine::gui() {
    ghoul_assert(_gui != nullptr, "GUI is nullptr");
	return _gui;
}

bool OpenSpaceEngine::initializeGL() {
    LINFO("Initializing Rendering Engine");
    bool success = _renderEngine->initializeGL();
    LINFO("Initializing OnScreen GUI GL");
	_gui->initializeGL();
    LINFO("Finished initializing OpenGL");
	return success;
}

bool OpenSpaceEngine::isMaster(){
	return _isMaster;
}

void OpenSpaceEngine::setMaster(bool master){
	_isMaster = master;
}

void OpenSpaceEngine::preSynchronization() {
	FileSys.triggerFilesystemEvents();
    if (_isMaster) {
        //const double dt = sgct::Engine::instance()->getDt();
		const double dt = sgct::Engine::instance()->getAvgDt();

		Time::ref().advanceTime(dt);
		Time::ref().preSynchronization();
		
        _interactionHandler->update(dt);
        //_interactionHandler.lockControls();		

		_scriptEngine->preSynchronization();			
		_renderEngine->preSynchronization();
    }
}

void OpenSpaceEngine::postSynchronizationPreDraw() {
	Time::ref().postSynchronizationPreDraw();

	_scriptEngine->postSynchronizationPreDraw();	
    _renderEngine->postSynchronizationPreDraw();
	

    if (_isMaster && _gui->isEnabled()) {
		double posX, posY;
		sgct::Engine::instance()->getMousePos(0, &posX, &posY);

		int x,y;
		sgct::Engine::instance()->getWindowPtr(0)->getFinalFBODimensions(x, y);

		int button0 = sgct::Engine::instance()->getMouseButton(0, 0);
		int button1 = sgct::Engine::instance()->getMouseButton(0, 1);
		bool buttons[2] = { button0 != 0, button1 != 0 };

		double dt = std::max(sgct::Engine::instance()->getDt(), 1.0/60.0);
		_gui->startFrame(static_cast<float>(dt), glm::vec2(glm::ivec2(x,y)), glm::vec2(posX, posY), buttons);
	}
}

void OpenSpaceEngine::render(const glm::mat4 &projectionMatrix, const glm::mat4 &viewMatrix) {
    _renderEngine->render(projectionMatrix, viewMatrix);

	if (_isMaster) {
		// If currently writing a command, render it to screen
		sgct::SGCTWindow* w = sgct::Engine::instance()->getActiveWindowPtr();
		if (_isMaster && !w->isUsingFisheyeRendering() && _console->isVisible()) {
			_console->render();
		}
		
		if (_gui->isEnabled())
			_gui->endFrame();
	}
}

void OpenSpaceEngine::postDraw() {
	if (_isMaster)
        //_interactionHandler.unlockControls();

	_renderEngine->postDraw();
}

void OpenSpaceEngine::keyboardCallback(int key, int action) {
	if (_isMaster) {
		if (_gui->isEnabled()) {
			bool isConsumed = _gui->keyCallback(key, action);
			if (isConsumed)
				return;
		}

		if (static_cast<unsigned int>(key) == _console->commandInputButton() && (action == SGCT_PRESS || action == SGCT_REPEAT))
			_console->toggleVisibility();

		if (!_console->isVisible()) {
			_interactionHandler->keyboardCallback(key, action);
		}
		else {
			_console->keyboardCallback(key, action);
		}
	}
}

void OpenSpaceEngine::charCallback(unsigned int codepoint) {
	if (_isMaster) {
		if (_gui->isEnabled()) {
			bool isConsumed = _gui->charCallback(codepoint);
			if (isConsumed)
				return;
		}

		if (_console->isVisible()) {
			_console->charCallback(codepoint);
		}
	}
}

void OpenSpaceEngine::mouseButtonCallback(int key, int action) {
	if (_isMaster) {
		if (_gui->isEnabled()) {
			bool isConsumed = _gui->mouseButtonCallback(key, action);
			if (isConsumed && action != SGCT_RELEASE)
				return;
		}

		_interactionHandler->mouseButtonCallback(key, action);
	}
}

void OpenSpaceEngine::mousePositionCallback(double x, double y) {
	if (_isMaster) {
	    _interactionHandler->mousePositionCallback(x, y);
	}
}

void OpenSpaceEngine::mouseScrollWheelCallback(double pos) {
	if (_isMaster) {
		if (_gui->isEnabled()) {
			bool isConsumed = _gui->mouseWheelCallback(pos);
			if (isConsumed)
				return;
		}

		_interactionHandler->mouseScrollWheelCallback(pos);
	}
}

void OpenSpaceEngine::encode() {
	if (_syncBuffer) {
		Time::ref().serialize(_syncBuffer);
		_scriptEngine->serialize(_syncBuffer);
		_renderEngine->serialize(_syncBuffer);		
		
		_syncBuffer->write();
	}
    _networkEngine->publishStatusMessage();
    _networkEngine->sendMessages();
}

void OpenSpaceEngine::decode() {
	if (_syncBuffer) {
		_syncBuffer->read();

		Time::ref().deserialize(_syncBuffer);
		_scriptEngine->deserialize(_syncBuffer);
		_renderEngine->deserialize(_syncBuffer);
	}
}

void OpenSpaceEngine::externalControlCallback(const char* receivedChars,
                                              int size, int clientId)
{
	if (size == 0)
		return;

    _networkEngine->handleMessage(std::string(receivedChars, size));
}

void OpenSpaceEngine::enableBarrier() {
    sgct::SGCTWindow::setBarrier(true);
}

void OpenSpaceEngine::disableBarrier() {
    sgct::SGCTWindow::setBarrier(false);
}

NetworkEngine* OpenSpaceEngine::networkEngine() {
    return _networkEngine;
}

ModuleEngine* OpenSpaceEngine::moduleEngine() {
    return _moduleEngine;
}

}  // namespace openspace
