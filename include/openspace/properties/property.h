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

#ifndef __PROPERTY_H__
#define __PROPERTY_H__

#include <openspace/properties/propertydelegate.h>

#include <ghoul/misc/dictionary.h>
#include <boost/any.hpp>
#include <functional>
#include <string>

struct lua_State;

namespace openspace {
namespace properties {

class PropertyOwner;

/**
 * A property encapsulates a value which should be user-changeable. A property almost
 * always belongs to a PropertyOwner who has taken ownership (setPropertyOwner) of the
 * Property. Per PropertyOwner, the <code>identifier</code> needs to be unique and can be
 * used as a URI. This class is an abstract base class and each subclass (most notable
 * TemplateProperty) needs to implement the methods Property::className, Property::get,
 * Property::set, Property::type(), Property::getLua, Property::setLua, and
 * Property::typeLua to make full use of the infrastructure.
 * The most common types can be implemented by creating a specialized instantiation of
 * TemplateProperty, which provides default implementations for these methods.
 *
 * The onChange method can be used by the PropertyOwner to listen to changes that happen
 * to the Property. The parameter is a function object that gets called after new value
 * has been set.
 * The metaData allows the developer to specify additional information about the Property
 * which might be used in GUI representations. One example would be a glm::vec4 property,
 * (Vec4Property) that can either represent a 4-dimensional position, a powerscaled
 * coordinate, a light position, or other things, requiring different GUI representations.
 * \see TemplateProperty
 * \see PropertyOwner
 */
class Property {
public:
	/**
	 * The constructor for the property. The <code>identifier</code> needs to be unique
	 * for each PropertyOwner. The <code>guiName</code> will be stored in the metaData
	 * to be accessed by the GUI elements using the <code>guiName</code> key. The default
	 * visibility settings is <code>true</code>, whereas the default read-only state is
	 * <code>false</code>.
	 * \param identifier A unique identifier for this property. It has to be unique to the
	 * PropertyOwner and cannot contain any <code>.</code>s
	 * \param guiName The human-readable GUI name for this Property
	 */
    Property(std::string identifier, std::string guiName);

	/**
	 * The destructor taking care of deallocating all unused memory. This method will not
	 * remove the Property from the PropertyOwner.
	 */
    virtual ~Property();

	/**
	 * This method returns the class name of the Property. The method is used by the
	 * TemplateFactory to create new instances of Propertys. The returned value is almost
	 * always identical to the C++ class name of the derived class.
	 * \return The class name of the Property
	 */
    virtual std::string className() const = 0;

	/**
	 * This method returns the encapsulated value of the Property to the caller. The type
	 * that is returned is determined by the type function and is up to the developer of
	 * the derived class. The default implementation returns an empty boost::any object.
	 * \return The value that is encapsulated by this Property, or an empty boost::any
	 * object if the method was not overritten.
	 */
    virtual boost::any get() const;

	/**
	 * Sets the value encapsulated by this Property to the <code>value</code> passed to
	 * this function. It is the caller's responsibility to ensure that the type contained
	 * in <code>value</code> is compatible with the concrete subclass of the Property. The
	 * method Property::type will return the desired type for the Property. The default
	 * implementation of this method ignores the input.
	 * \param value The new value that should be stored in this Property
	 */
    virtual void set(boost::any value);

	/**
	 * This method returns the type that is requested by this Property for the set method.
	 * The default implementation returns the type of <code>void</code>.
	 * \return The type that is requested by this Property's Property::set method
	 */
    virtual const std::type_info& type() const;
	
	/**
	 * This method encodes the encapsulated value of this Property at the top of the Lua
	 * stack. The specific details of this serialization is up to the property developer
	 * as long as the rest of the stack is unchanged. The implementation has to be
	 * synchronized with the Property::setLua method. The default implementation is a
	 * no-op.
	 * \param state The Lua state to which the value will be encoded
	 * \return <code>true</code> if the encoding succeeded, <code>false</code> otherwise
	 */
	virtual bool getLua(lua_State* state) const;

	/**
	 * This method sets the value encapsulated by this Property by deserializing the value
	 * on top of the passed Lua stack. The specific details of the deserialization are up
	 * to the Property developer, but they must only depend on the top element of the
	 * stack and must leave all other elements unchanged. The implementation has to be
	 * synchronized with the Property::getLua method. The default implementation is a
	 * no-op.
	 * \param state The Lua state from which the value will be decoded
	 * \return <code>true</code> if the decoding and setting of the value succeeded,
	 * <code>false</code> otherwise
	 */
	virtual bool setLua(lua_State* state);

	/**
	 * Returns the Lua type that will be put onto the stack in the Property::getLua method
	 * and which will be consumed by the Property::setLua method. The returned value
	 * can belong to the set of Lua types: <code>LUA_TNONE</code>, <code>LUA_TNIL</code>,
	 * <code>LUA_TBOOLEAN</code>, <code>LUA_TLIGHTUSERDATA</code>,
	 * <code>LUA_TNUMBER</code>, <code>LUA_TSTRING</code>, <code>LUA_TTABLE</code>,
	 * <code>LUA_TFUNCTION</code>, <code>LUA_TUSERDATA</code>, or
	 * <code>LUA_TTHREAD</code>. The default implementation will return
	 * <code>LUA_TNONE</code>.
	 * \return The Lua type that will be consumed or produced by the Property::getLua and
	 * Property::setLua methods.
	 */
	virtual int typeLua() const;

	/**
	 * This method registers a <code>callback</code> function that will be called every
	 * time if either Property:set or Property::setLua was called with a value that is
	 * different from the previously stored value. The callback can be removed my passing
	 * an empty <code>std::function<void()></code> object.
	 * \param callback The callback function that is called when the encapsulated type has
	 * been successfully changed by either the Property::set or Property::setLua methods.
	 */
    virtual void onChange(std::function<void()> callback);

	/**
	 * This method returns the unique identifier of this Property.
	 * \return The unique identifier of this Property
	 */
    const std::string& identifier() const;
	
	/**
	 * Returns the fully qualified name for this Property that uniquely identifies this
	 * Property within OpenSpace. It consists of the <code>identifier</code> preceded by
	 * all levels of PropertyOwner%s separated with <code>.</code>; for example:
	 * <code>owner1.owner2.identifier</code>.
	 * \return The fully qualified identifier for this Property
	 */
	std::string fullyQualifiedIdentifier() const;

	/**
	 * Returns the PropertyOwner of this Property or <code>nullptr</code>, if it does not
	 * have an owner.
	 *\ return The Property of this Property
	 */
    PropertyOwner* owner() const;

	/**
	 * Assigned the Property to a new PropertyOwner. This method does not inform the
	 * PropertyOwner of this action.
	 * \param owner The new PropertyOwner for this Property
	 */
    void setPropertyOwner(PropertyOwner* owner);
	
	/**
	 * Returns the human-readable GUI name for this Property that has been set in the
	 * constructor. This method returns the same value as accessing the metaData object
	 * and requesting the <code>std::string</code> stored for the <code>guiName</code>
	 * key.
	 * \return The human-readable GUI name for this Property
	 */
	std::string guiName() const;

	/**
	 * Sets the identifier of the group that this Property belongs to. Property groups can
	 * be used, for example, by GUI application to visually group different properties,
	 * but it has no impact on the Property itself. The default value for the groupID is
	 * <code>""</code>.
	 * \param groupId The group id that this property should belong to
	 */
    void setGroupIdentifier(std::string groupId);

	/**
	 * Returns the group idenfier that this Property belongs to, or <code>""</code> if it
	 * belongs to no group.
	 * \return The group identifier that this Property belongs to
	 */
    std::string groupIdentifier() const;

	/**
	 * Determines a hint if this Property should be visible, or hidden. Each application
	 * accessing the properties is free to ignore this hint. It is stored in the metaData
	 * Dictionary with the key: <code>isVisible</code>. The default value is
	 * <code>true</code>.
	 * \param state <code>true</code> if the Property should be visible,
	 * <code>false</code> otherwise.
	 */
    void setVisible(bool state);

	/**
	 * This method determines if this Property should be read-only in external
	 * applications. This setting is only a hint and does not need to be followed by GUI
	 * applications and does not have any effect on the Property::set or Property::setLua
	 * methods. The value is stored in the metaData Dictionary with the key:
	 * <code>isReadOnly</code>. The default value is <code>false</code>.
	 * \param state <code>true</code> if the Property should be read only,
	 * <code>false</code> otherwise
	 */
	void setReadOnly(bool state);

	/**
	 * This method allows the developer to give hints to the GUI about different
	 * representations for the GUI. The same Property (for example Vec4Property) can be
	 * used in different ways, each requiring a different input method. These values are
	 * stored in the metaData object using the <code>views.</code> prefix in front of the
	 * <code>option</code> parameter. See Property::ViewOptions for a default list of
	 * possible options. As these are only hints, the GUI is free to ignore any suggestion
	 * by the developer.
	 * \param option The view option that should be modified
	 * \param value Determines if the view option should be active (<code>true</code>) or
	 * deactivated (<code>false</code>)
	 */
    void setViewOption(std::string option, bool value = true);

	/**
	 * Default view options that can be used in the Property::setViewOption method. The
	 * values are: Property::ViewOptions::Color = <code>color</code>,
	 * Property::ViewOptions::LightPosition = <code>lightPosition</code>,
	 * Property::ViewOptions::PowerScaledScalar = <code>powerScaledScalar</code>, and
	 * Property::ViewOptions::PowerScaledCoordinate = <code>powerScaledCoordinate</code>.
	 */
    struct ViewOptions {
        static const std::string Color;
        static const std::string LightPosition;
        static const std::string PowerScaledScalar;
        static const std::string PowerScaledCoordinate;
    };

	/**
	 * Returns the metaData that contains all information for external applications to
	 * correctly display information about the Property. No information that is stored in
	 * this Dictionary is necessary for the programmatic use of the Property.
	 * \return The Dictionary containing all meta data information about this Property
	 */
    const ghoul::Dictionary& metaData() const;

protected:
	/**
	 * This method must be called by all subclasses whenever the encapsulated value has
	 * changed and a potential listener has to be informed.
	 */
	void notifyListener();

	/// The PropetyOwner this Property belongs to, or <code>nullptr</code>
    PropertyOwner* _owner; 

	/// The identifier for this Property
    std::string _identifier;

	/// The Dictionary containing all meta data necessary for external applications
    ghoul::Dictionary _metaData;

	/// The callback function that will be invoked whenever the encapsulated value changes
    std::function<void()> _onChangeCallback;
};

} // namespace properties
} // namespace openspace

#endif // __PROPERTY_H__
