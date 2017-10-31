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

#ifndef __OPENSPACE_CORE___ASSETLOADER___H__
#define __OPENSPACE_CORE___ASSETLOADER___H__

#include <openspace/scene/scenegraphnode.h>

#include <openspace/scripting/lualibrary.h>
#include <openspace/scene/assetsynchronizer.h>

#include <ghoul/misc/dictionary.h>
#include <ghoul/lua/luastate.h>

#include <ghoul/lua/ghoul_lua.h>
#include <ghoul/filesystem/directory.h>

#include <memory>
#include <string>

namespace openspace {

class Asset;

namespace assetloader {
int onInitialize(lua_State* state);
int onDeinitialize(lua_State* state);
int onInitializeDependency(lua_State* state);
int onDeinitializeDependency(lua_State* state);
int addSynchronization(lua_State* state);
int importDependency(lua_State* state);
int resolveLocalResource(lua_State* state);
int resolveSyncedResource(lua_State* state);
int noOperation(lua_State* state);
int exportAsset(lua_State* state);
} // namespace assetloader

class AssetLoader {
public:
    /**
     * Constructor
     */
    AssetLoader(
        ghoul::lua::LuaState& luaState,
        std::string assetRoot,
        std::string syncRoot
    );

    /**
     * Destructor
     */
    ~AssetLoader();

    /**
     * Load an asset:
     * Add the asset as a dependency on the root asset
     * The asset is loaded synchronously
     */
    std::shared_ptr<Asset> loadAsset(const std::string& identifier);

    /**
     * Unload an asset:
     * Remove the asset as a dependency on the root asset
     * The asset is unloaded synchronously
     */
    void unloadAsset(const std::string& identifier);

    /**
    * Unload an asset:
    * Remove the asset as a dependency on the root asset
    * The asset is unloaded synchronously
    */
    void unloadAsset(const Asset* asset);
    
    /**
     * Return true if the specified asset is loaded
     */
    bool hasLoadedAsset(const std::string& identifier);


    /**
     * Returns the asset identified by the identifier, 
     * if the asset is loaded. Otherwise return nullptr.
     */
    std::shared_ptr<Asset> loadedAsset(const std::string& identifier);

    /**
     * Return all assets loaded using the loadAsset method.
     * Non-recursive (does not include imports of the loaded assets)
     */
    std::vector<std::shared_ptr<Asset>> loadedAssets();

    /**
     * Return the lua state
     */
    ghoul::lua::LuaState* luaState();

    /**
     * Return the root asset
     */
    std::shared_ptr<Asset> rootAsset() const;

    /**
     * Return the sync root directory
     */
    const std::string& syncRootDirectory();

    /**
    * Return the asset root directory
    */
    const std::string& assetRootDirectory();

    void callOnInitialize(Asset* asset);

    void callOnDeinitialize(Asset* asset);

    void callOnDependantInitialize(Asset* asset, Asset* dependant);

    void callOnDependantDeinitialize(Asset* asset, Asset* dependant);

    std::string generateAssetPath(const std::string& baseDirectory, const std::string& path) const;

private:
    std::shared_ptr<Asset> importDependency(const std::string& identifier);
    std::shared_ptr<Asset> importAsset(std::string path);
    std::shared_ptr<Asset> getAsset(std::string path);
    ghoul::filesystem::Directory currentDirectory();

    void pushAsset(std::shared_ptr<Asset> asset);
    void popAsset();
    void updateLuaGlobals();
    void addLuaDependencyTable(Asset* dependant, Asset* dependency);

    // Lua functions
    int onInitializeLua(Asset* asset);
    int onDeinitializeLua(Asset* asset);
    int onInitializeDependencyLua(Asset* dependant, Asset* dependency);
    int onDeinitializeDependencyLua(Asset* dependant, Asset* dependency);
    int addSynchronizationLua(Asset* asset);
    int importDependencyLua(Asset* asset);
    int resolveLocalResourceLua(Asset* asset);
    int resolveSyncedResourceLua(Asset* asset);
    int exportAssetLua(Asset* asset);

    // Friend c closures (callable from lua, and maps to lua functions above)
    friend int assetloader::onInitialize(lua_State* state);
    friend int assetloader::onDeinitialize(lua_State* state);
    friend int assetloader::onInitializeDependency(lua_State* state);
    friend int assetloader::onDeinitializeDependency(lua_State* state);
    friend int assetloader::addSynchronization(lua_State* state);
    friend int assetloader::importDependency(lua_State* state);
    friend int assetloader::resolveLocalResource(lua_State* state);
    friend int assetloader::resolveSyncedResource(lua_State* state);
    friend int assetloader::exportAsset(lua_State* state);

    std::shared_ptr<Asset> _rootAsset;
    std::map<std::string, std::shared_ptr<Asset>> _importedAssets;
    std::vector<std::shared_ptr<Asset>> _assetStack;

    AssetSynchronizer* _assetSynchronizer;
    std::string _assetRootDirectory;
    std::string _syncRootDirectory;
    ghoul::lua::LuaState* _luaState;

    // References to lua values
    std::map<Asset*, std::vector<int>> _onInitializationFunctionRefs;
    std::map<Asset*, std::vector<int>> _onDeinitializationFunctionRefs;
    std::map<Asset*, std::map<Asset*, std::vector<int>>> _onDependencyInitializationFunctionRefs;
    std::map<Asset*, std::map<Asset*, std::vector<int>>> _onDependencyDeinitializationFunctionRefs;

    int _assetsTableRef;

};




} // namespace openspace

#endif // __OPENSPACE_CORE___ASSETLOADER___H__
