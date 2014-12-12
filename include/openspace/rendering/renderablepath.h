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

#ifndef __RenderablePath_H__
#define __RenderablePath_H__

// open space includes
#include <openspace/rendering/renderable.h>

#include <openspace/properties/stringproperty.h>
#include <openspace/util/powerscaledcoordinate.h>

// ghoul includes
#include <ghoul/opengl/programobject.h>
#include <ghoul/opengl/texture.h>
//#include <openspace/util/runtimedata.h>

namespace openspace {
	class RenderablePath : public Renderable{
	public:
		RenderablePath(const ghoul::Dictionary& dictionary);
		~RenderablePath();

		bool initialize() override;
		bool deinitialize() override;

		bool isReady() const override;

		void render(const RenderData& data) override;
		void update(const UpdateData& data) override;
	private:
		ghoul::opengl::ProgramObject* _programObject;
		void loadTexture();
		bool fullYearSweep();

		// modfile reads
		// spice
		std::string _target;
		std::string _observer;
		std::string _frame;
		// color
		glm::vec3 _c;
		double _r, _g, _b;

		// need to write robust method for vbo id selection 
		// (right now galactic grid has to be present) (why though?) solve later...
		GLuint _vaoID;
		GLuint _vBufferID;
		GLuint _iBufferID;

		void nextIndex();

		unsigned int _isize;
		unsigned int _vsize;
		unsigned int _vtotal;
		unsigned int _stride;

		//Vertex* _varray;
		std::vector<float> _varray;
		std::vector<int> _iarray;

		//used for update of trail
		psc _pscpos, _pscvel;
		double _increment;
		double _time = 0;
		double _oldTime = 0;

		int _delta = 0;
		int _dtprogress = 0;
	};
}
#endif