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

#include <modules/base/ephemeris/dynamicephemeris.h>

namespace {
    const std::string KeyPosition = "Position";
}

namespace openspace {

DynamicEphemeris::DynamicEphemeris(const ghoul::Dictionary& dictionary)
    : _position(0.f, 0.f, 0.f, 0.f)
{
    const bool hasPosition = dictionary.hasKeyAndValue<glm::vec4>(KeyPosition);
    if (hasPosition) {
        glm::vec4 tmp;
        dictionary.getValue(KeyPosition, tmp);
        _position = tmp;
    }
}

DynamicEphemeris::~DynamicEphemeris() {}

const psc& DynamicEphemeris::position() const {
    return _position;
}

void DynamicEphemeris::setPosition(psc pos) {
	_position = pos;
}

void DynamicEphemeris::update(const UpdateData&) {}

} // namespace openspace