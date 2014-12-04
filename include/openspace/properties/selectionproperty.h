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

#ifndef __SELECTIONPROPERTY_H__
#define __SELECTIONPROPERTY_H__

#include <openspace/properties/scalarproperty.h>

#include <vector>

namespace openspace {
namespace properties {

//REGISTER_TEMPLATEPROPERTY_HEADER(SelectionProperty, std::vector<int>);

class SelectionProperty : public TemplateProperty<std::vector<int>> {
public:
	struct Option {
		int value;
		std::string description;
	};

	SelectionProperty(std::string identifier, std::string guiName);
	
	void addOption(Option option);
	const std::vector<Option>& options() const;

	void setValue(std::vector<int> value) override;

private:
	/// The list of options which have been registered with this OptionProperty
	std::vector<Option> _options;
	std::vector<int> _values;
};

template <>
std::string PropertyDelegate<TemplateProperty<std::vector<int>>>::className();

template <>
template <>
std::vector<int> PropertyDelegate<TemplateProperty<std::vector<int>>>::fromLuaValue(lua_State* state, bool& success);

template <>
template <>
bool PropertyDelegate<TemplateProperty<std::vector<int>>>::toLuaValue(lua_State* state, std::vector<int> value);

template <>
int PropertyDelegate<TemplateProperty<std::vector<int>>>::typeLua();

} // namespace properties
} // namespace openspace

#endif // __SELECTIONPROPERTY_H__