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

#ifndef __NUMERICALPROPERTY_H__
#define __NUMERICALPROPERTY_H__

#include <openspace/properties/templateproperty.h>

namespace openspace {
namespace properties {

template <typename T>
class NumericalProperty : public TemplateProperty<T> {
public:
    NumericalProperty(std::string identifier, std::string guiName);
    NumericalProperty(std::string identifier, std::string guiName, T value);
    NumericalProperty(std::string identifier, std::string guiName, T value,
        T minimumValue, T maximumValue);

	bool getLua(lua_State* state) const override;
	bool setLua(lua_State* state) override;
	int typeLua() const override;

	T minValue() const;
	T maxValue() const;

    virtual std::string className() const override;

    using TemplateProperty<T>::operator=;

protected:
    T _minimumValue;
    T _maximumValue;
};

} // namespace properties
} // namespace openspace

#include "openspace/properties/numericalproperty.inl"

#endif // __NUMERICALPROPERTY_H__
