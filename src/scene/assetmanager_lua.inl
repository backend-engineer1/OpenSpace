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

namespace openspace::luascriptfunctions::asset {

int add(lua_State* state) {
    AssetManager *assetManager =
        reinterpret_cast<AssetManager*>(lua_touserdata(state, lua_upvalueindex(1)));

    int nArguments = lua_gettop(state);
    SCRIPT_CHECK_ARGUMENTS("add", state, 1, nArguments);
    std::string assetName = luaL_checkstring(state, -1);
    assetManager->add(assetName);
    return 0;
}

int remove(lua_State* state) {
    AssetManager *assetManager =
        reinterpret_cast<AssetManager*>(lua_touserdata(state, lua_upvalueindex(1)));

    int nArguments = lua_gettop(state);
    SCRIPT_CHECK_ARGUMENTS("remove", state, 1, nArguments);
    std::string assetName = luaL_checkstring(state, -1);
    assetManager->remove(assetName);
    return 0;
}

int removeAll(lua_State* state) {
    AssetManager *assetManager =
        reinterpret_cast<AssetManager*>(lua_touserdata(state, lua_upvalueindex(1)));

    int nArguments = lua_gettop(state);
    SCRIPT_CHECK_ARGUMENTS("removeAll", state, 0, nArguments);
    assetManager->removeAll();
    return 0;
}

} // namespace openspace::luascriptfunctions
