/*****************************************************************************************
*                                                                                       *
* OpenSpace                                                                             *
*                                                                                       *
* Copyright (c) 2014                                                                    *
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

// temporary includes (will fix as soon as I figure out how class hierarchy should work, 
//                     ie after I see model on screen)

// open space includes
#include <openspace/rendering/model/renderablemodel.h>
#include <openspace/util/constants.h>
#include <openspace/rendering/model/modelgeometry.h>


#include <ghoul/io/texture/texturereader.h>
#include <ghoul/opengl/textureunit.h>
#include <ghoul/filesystem/filesystem.h>

#include <openspace/util/time.h>
#include <openspace/util/spicemanager.h>

#include <openspace/engine/openspaceengine.h>
#include <sgct.h>

namespace {
const std::string _loggerCat = "RenderableModel";
	const std::string keySource      = "Rotation.Source";
	const std::string keyDestination = "Rotation.Destination";
}

namespace openspace {

RenderableModel::RenderableModel(const ghoul::Dictionary& dictionary)
    : Renderable(dictionary)
	, _colorTexturePath("colorTexture", "Color Texture")
    , _programObject(nullptr)
    , _texture(nullptr)
	, _geometry(nullptr)
{
	std::string name;
	std::string path;
	dictionary.getValue(constants::scenegraphnode::keyName, name);
	dictionary.getValue(constants::scenegraph::keyPathModule, path);

	ghoul::Dictionary geometryDictionary;
	bool success = dictionary.getValue(
		constants::renderablemodel::keyGeometry, geometryDictionary);
	if (success) {
		geometryDictionary.setValue(constants::scenegraphnode::keyName, name);
		geometryDictionary.setValue(constants::scenegraph::keyPathModule, path);
		_geometry = modelgeometry::ModelGeometry::createFromDictionary(geometryDictionary);
	}

	std::string texturePath = "";
	success = dictionary.getValue("Textures.Color", texturePath);
	if (success)
		_colorTexturePath = path + "/" + texturePath;

	addPropertySubOwner(_geometry);

	addProperty(_colorTexturePath);
	_colorTexturePath.onChange(std::bind(&RenderableModel::loadTexture, this));

	dictionary.getValue(keySource, _source);
	dictionary.getValue(keyDestination, _destination);
}


RenderableModel::~RenderableModel(){
}

bool RenderableModel::isReady() const {
	bool ready = true;
	ready &= (_programObject != nullptr);
	ready &= (_texture != nullptr);
	return ready;
}

bool RenderableModel::initialize(){
    bool completeSuccess = true;
    if (_programObject == nullptr)
        completeSuccess
              &= OsEng.ref().configurationManager().getValue("pscShader", _programObject); 

    loadTexture();

    completeSuccess &= (_texture != nullptr);
    completeSuccess &= _geometry->initialize(this); 
	completeSuccess &= !_source.empty();
	completeSuccess &= !_destination.empty();

    return completeSuccess;
}

bool RenderableModel::deinitialize(){
	if (_geometry) {
		_geometry->deinitialize();
		delete _geometry;
	}
	if (_texture)
		delete _texture;

	_geometry = nullptr;
	_texture = nullptr;
	return true;
}

void RenderableModel::render(const RenderData& data)
{
    // activate shader
    _programObject->activate();

    // scale the planet to appropriate size since the planet is a unit sphere
    glm::mat4 transform = glm::mat4(1);

	glm::mat4 tmp = glm::mat4(1);
	for (int i = 0; i < 3; i++){
		for (int j = 0; j < 3; j++){
			tmp[i][j] = static_cast<float>(_stateMatrix[i][j]);
		}
	}
	
	transform *= tmp;
	
	//glm::mat4 modelview = data.camera.viewMatrix()*data.camera.modelMatrix();
	//glm::vec3 camSpaceEye = (-(modelview*data.position.vec4())).xyz;
	// setup the data to the shader
//	_programObject->setUniform("camdir", camSpaceEye);

	_programObject->setUniform("ViewProjection", data.camera.viewProjectionMatrix());
	_programObject->setUniform("ModelTransform", transform);
	setPscUniforms(_programObject, &data.camera, data.position);
	
    // Bind texture
    ghoul::opengl::TextureUnit unit;
    unit.activate();
    _texture->bind();
    _programObject->setUniform("texture1", unit);

	_geometry->render();

    // disable shader
    _programObject->deactivate();
}

void RenderableModel::update(const UpdateData& data){
#ifndef NDEBUG
	if (_source.empty() || _destination.empty())
		return;
#endif
	// set spice-orientation in accordance to timestamp
	openspace::SpiceManager::ref().getPositionTransformMatrix(_source, _destination, data.time, _stateMatrix);
	
}

void RenderableModel::loadTexture()
{
    delete _texture;
    _texture = nullptr;
    if (_colorTexturePath.value() != "") {
        _texture = ghoul::io::TextureReader::ref().loadTexture(absPath(_colorTexturePath));
        if (_texture) {
            LDEBUG("Loaded texture from '" << absPath(_colorTexturePath) << "'");
            _texture->uploadTexture();
        }
    }
}

}  // namespace openspace
