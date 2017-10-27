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

#include <openspace/scene/assetloader.h>

#include <openspace/scene/asset.h>
#include <openspace/scripting/script_helper.h>

#include <ghoul/lua/ghoul_lua.h>
#include <ghoul/lua/luastate.h>
#include <ghoul/lua/lua_helper.h>
#include <ghoul/logging/logmanager.h>
#include <ghoul/misc/onscopeexit.h>
#include <ghoul/filesystem/filesystem.h>

#include "assetloader_lua.inl"

namespace {
    const char* AssetGlobalVariableName = "asset";

    const char* ImportDependencyFunctionName = "import";
    const char* ExportFunctionName = "export";

    const char* SyncedResourceFunctionName = "syncedResource";
    const char* LocalResourceFunctionName = "localResource";
    const char* AddSynchronizationFunctionName = "addSynchronization";

    const char* OnInitializeFunctionName = "onInitialize";
    const char* OnDeinitializeFunctionName = "onDeinitialize";

    const char* ExportsTableName = "_exports";
    const char* AssetTableName = "_asset";
    const char* DependantsTableName = "_dependants";

    const char* _loggerCat = "AssetLoader";

    const char* AssetFileSuffix = "asset";

    bool isRelative(std::string path) {
        if (path.size() > 2) {
            if (path[0] == '.' && path[1] == '/') return true;
        }
        if (path.size() > 3) {
            if (path[0] == '.' && path[1] == '.' && path[2] == '/') return true;
        }
        return false;
    };
}

namespace openspace {

AssetLoader::AssetLoader(
    ghoul::lua::LuaState& luaState,
    std::string assetRootDirectory,
    std::string syncRootDirectory
)
    : _luaState(&luaState)
    , _rootAsset(std::make_shared<Asset>(this))
    , _assetRootDirectory(assetRootDirectory)
    , _syncRootDirectory(std::move(syncRootDirectory))
{
    pushAsset(_rootAsset);

    // Create _assets table.
    lua_newtable(*_luaState);
    _assetsTableRef = luaL_ref(*_luaState, LUA_REGISTRYINDEX);
}

AssetLoader::~AssetLoader() {
}

std::shared_ptr<Asset> AssetLoader::importAsset(std::string path) {
    std::shared_ptr<Asset> asset = std::make_shared<Asset>(this, path);

    pushAsset(asset);
    ghoul::OnScopeExit e([this]() {
        popAsset();
    });

    if (!FileSys.fileExists(path)) {
        throw ghoul::FileNotFoundError(path);
    }

    ghoul::lua::runScriptFile(*_luaState, path);
    _importedAssets.emplace(asset->id(), asset);

    return asset;
}

std::string AssetLoader::generateAssetPath(const std::string& baseDirectory,
                                           const std::string& assetPath) const
{
    ghoul::filesystem::Directory directory = isRelative(assetPath) ?
        baseDirectory :
        _assetRootDirectory;
   
    return ghoul::filesystem::File(directory.path() +
        ghoul::filesystem::FileSystem::PathSeparator +
        assetPath +
        "." +
        AssetFileSuffix);
}

std::shared_ptr<Asset> AssetLoader::getAsset(std::string name) {
    ghoul::filesystem::Directory directory = currentDirectory();
    std::string path = generateAssetPath(directory, name);

    // Check if asset is already loaded.
    const auto it = _importedAssets.find(path);

    return it == _importedAssets.end() ?
        loadAsset(path) :
        it->second;
}

int AssetLoader::onInitializeLua(Asset* asset) {
    int nArguments = lua_gettop(*_luaState);
    SCRIPT_CHECK_ARGUMENTS("onInitialize", *_luaState, 1, nArguments);
    int referenceIndex = luaL_ref(*_luaState, LUA_REGISTRYINDEX);
    _onInitializationFunctionRefs[asset].push_back(referenceIndex);
    return 0;
}

int AssetLoader::onDeinitializeLua(Asset* asset) {
    int nArguments = lua_gettop(*_luaState);
    SCRIPT_CHECK_ARGUMENTS("onDeinitialize", *_luaState, 1, nArguments);
    int referenceIndex = luaL_ref(*_luaState, LUA_REGISTRYINDEX);
    _onDeinitializationFunctionRefs[asset].push_back(referenceIndex);
    return 0;
}

int AssetLoader::onInitializeDependencyLua(Asset* dependant, Asset* dependency) {
    int nArguments = lua_gettop(*_luaState);
    SCRIPT_CHECK_ARGUMENTS("onInitializeDependency", *_luaState, 1, nArguments);
    int referenceIndex = luaL_ref(*_luaState, LUA_REGISTRYINDEX);
    _onDependencyInitializationFunctionRefs[dependant][dependency].push_back(referenceIndex);
    return 0;
}

int AssetLoader::onDeinitializeDependencyLua(Asset* dependant, Asset* dependency) {
    int nArguments = lua_gettop(*_luaState);
    SCRIPT_CHECK_ARGUMENTS("onDeinitializeDependency", *_luaState, 1, nArguments);
    int referenceIndex = luaL_ref(*_luaState, LUA_REGISTRYINDEX);
    _onDependencyDeinitializationFunctionRefs[dependant][dependency].push_back(referenceIndex);
    return 0;
}

int AssetLoader::addSynchronizationLua(Asset* asset) {
    int nArguments = lua_gettop(*_luaState);
    SCRIPT_CHECK_ARGUMENTS("addSynchronization", *_luaState, 1, nArguments);

    ghoul::Dictionary d;
    ghoul::lua::luaDictionaryFromState(*_luaState, d);
    asset->addSynchronization(ResourceSynchronization::createFromDictionary(d));

    return 0;
}

std::shared_ptr<Asset> AssetLoader::importDependency(const std::string& name) {
    std::shared_ptr<Asset> asset = getAsset(name);
    std::shared_ptr<Asset> dependant = _assetStack.back();
    dependant->addDependency(asset);
    return asset;
}

ghoul::filesystem::Directory AssetLoader::currentDirectory() {
    if (_assetStack.back()->hasAssetFile()) {
        return _assetStack.back()->assetDirectory();
    } else {
        return _assetRootDirectory;
    }
}

std::shared_ptr<Asset> AssetLoader::loadAsset(const std::string & identifier) {
    ghoul_assert(_assetStack.size() == 1, "Can only load an asset from the root asset");
    return importDependency(identifier);
}


void AssetLoader::unloadAsset(const std::string & identifier) {
    ghoul_assert(_assetStack.size() == 1, "Can only unload an asset from the root asset");   
    // TODO: Implement this
    //_rootAsset->removeDependency(id);
}

bool AssetLoader::hasLoadedAsset(const std::string & identifier) {
    const auto it = _importedAssets.find(identifier);
    if (it == _importedAsset.end()) {
        return false;
    }
    return _rootAsset->hasDependency(it->second.get());
}

std::vector<std::shared_ptr<Asset>> AssetLoader::loadedAssets() {
    return _rootAsset->dependencies();
}

ghoul::lua::LuaState* AssetLoader::luaState() {
    return _luaState;
}

std::shared_ptr<Asset> AssetLoader::rootAsset() const {
    return _rootAsset;
}

const std::string& AssetLoader::syncRootDirectory() {
    return _syncRootDirectory;
}

const std::string & AssetLoader::assetRootDirectory()
{
    return _assetRootDirectory;
}

void AssetLoader::callOnInitialize(Asset* asset) {
    for (int init : _onInitializationFunctionRefs[asset]) {
        lua_rawgeti(*_luaState, LUA_REGISTRYINDEX, init);
        if (lua_pcall(*_luaState, 0, 0, 0) != LUA_OK) {
            throw ghoul::lua::LuaRuntimeException(
                "When initializing " + asset->assetFilePath() + ": " + luaL_checkstring(*_luaState, -1)
            );
        }
    }
}

void AssetLoader::callOnDeinitialize(Asset * asset) {
    std::vector<int>& funs = _onDeinitializationFunctionRefs[asset];
    for (auto it = funs.rbegin(); it != funs.rend(); it++) {
        lua_rawgeti(*_luaState, LUA_REGISTRYINDEX, *it);
        if (lua_pcall(*_luaState, 0, 0, 0) != LUA_OK) {
            throw ghoul::lua::LuaRuntimeException(
                "When deinitializing " + asset->assetFilePath() + ": " + luaL_checkstring(*_luaState, -1)
            );
        }
    }
}

void AssetLoader::callOnDependantInitialize(Asset* asset, Asset* dependant) {
    for (int init : _onDependencyInitializationFunctionRefs[dependant][asset]) {
        lua_rawgeti(*_luaState, LUA_REGISTRYINDEX, init);
        if (lua_pcall(*_luaState, 0, 0, 0) != LUA_OK) {
            throw ghoul::lua::LuaRuntimeException(
                "When initializing dependency " + dependant->assetFilePath() + " -> " + 
                asset->assetFilePath() + ": " + luaL_checkstring(*_luaState, -1)
            );
        }
    }
}

void AssetLoader::callOnDependantDeinitialize(Asset* asset, Asset* dependant) {
    std::vector<int>& funs = _onDependencyDeinitializationFunctionRefs[dependant][asset];
    for (auto it = funs.rbegin(); it != funs.rend(); it++) {
        lua_rawgeti(*_luaState, LUA_REGISTRYINDEX, *it);
        if (lua_pcall(*_luaState, 0, 0, 0) != LUA_OK) {
            throw ghoul::lua::LuaRuntimeException(
                "When deinitializing dependency " + dependant->assetFilePath() + " -> " +
                asset->assetFilePath() + ": " + luaL_checkstring(*_luaState, -1)
            );
        }
    }
}

int AssetLoader::resolveLocalResourceLua(Asset* asset) {
    int nArguments = lua_gettop(*_luaState);
    if (nArguments != 1) {
        return luaL_error(*_luaState, "Expected %i arguments, got %i", 1, nArguments);
    }

    std::string resourceName = luaL_checkstring(*_luaState, -1);
    std::string resolved = asset->resolveLocalResource(resourceName);

    lua_pushstring(*_luaState, resolved.c_str());
    return 1;
}

int AssetLoader::resolveSyncedResourceLua(Asset* asset) {
    int nArguments = lua_gettop(*_luaState);
    if (nArguments != 1) {
        return luaL_error(*_luaState, "Expected %i arguments, got %i", 1, nArguments);
    }

    std::string resourceName = luaL_checkstring(*_luaState, -1);
    std::string resolved = asset->resolveSyncedResource(resourceName);

    lua_pushstring(*_luaState, resolved.c_str());
    return 1;
}

void AssetLoader::pushAsset(std::shared_ptr<Asset> asset) {
    if (std::find(_assetStack.begin(), _assetStack.end(), asset) != _assetStack.end()) {
        throw ghoul::lua::LuaRuntimeException("Circular inclusion of assets.");
    }

    _assetStack.push_back(asset);

    if (asset == _rootAsset) {
        return;
    }
    
    // Push the global assets table to the lua stack.
    lua_rawgeti(*_luaState, LUA_REGISTRYINDEX, _assetsTableRef);
    int globalTableIndex = lua_gettop(*_luaState);

    /*
    Set up lua table:
       AssetMeta
       |- Exports (table<name, exported data>)
       |- Asset
       |  |- localResource
       |  |- syncedResource
       |  |- import
       |  |- export
       |  |- onInitialize
       |  |- onDeinitialize
       |  |- addSynchronization
       |- Dependants (table<dependant, Dependency dep>)
     
     where Dependency is a table:
       Dependency
       |- localResource
       |- syncedResource
       |- onInitialize
       |- onDeinitialize
    */ 

    // Create meta table for the current asset.
    lua_newtable(*_luaState);
    int assetMetaTableIndex = lua_gettop(*_luaState);
    
    // Register empty exports table on current asset.
    // (string => exported object)
    lua_newtable(*_luaState);
    lua_setfield(*_luaState, assetMetaTableIndex, ExportsTableName);

    // Create asset table
    lua_newtable(*_luaState);  
    int assetTableIndex = lua_gettop(*_luaState);

    // Register local resource function
    // string localResource(string path)
    lua_pushlightuserdata(*_luaState, asset.get());
    lua_pushcclosure(*_luaState, &assetloader::resolveLocalResource, 1);
    lua_setfield(*_luaState, assetTableIndex, LocalResourceFunctionName);

    // Register synced resource function
    // string syncedResource(string path)
    lua_pushlightuserdata(*_luaState, asset.get());
    lua_pushcclosure(*_luaState, &assetloader::resolveSyncedResource, 1);
    lua_setfield(*_luaState, assetTableIndex, SyncedResourceFunctionName);

    // Register import-dependency function
    // Asset, Dependency import(string path)
    lua_pushlightuserdata(*_luaState, asset.get());
    lua_pushcclosure(*_luaState, &assetloader::importDependency, 1);
    lua_setfield(*_luaState, assetTableIndex, ImportDependencyFunctionName);

    // Register export-dependency function
    // export(string key, any value)
    lua_pushlightuserdata(*_luaState, asset.get());
    lua_pushcclosure(*_luaState, &assetloader::exportAsset, 1);
    lua_setfield(*_luaState, assetTableIndex, ExportFunctionName);

    // Register onInitialize function
    // void onInitialize(function<void()> initializationFunction)
    lua_pushlightuserdata(*_luaState, asset.get());
    lua_pushcclosure(*_luaState, &assetloader::onInitialize, 1);
    lua_setfield(*_luaState, assetTableIndex, OnInitializeFunctionName);

    // Register onDeinitialize function
    // void onDeinitialize(function<void()> deinitializationFunction)
    lua_pushlightuserdata(*_luaState, asset.get());
    lua_pushcclosure(*_luaState, &assetloader::onDeinitialize, 1);
    lua_setfield(*_luaState, assetTableIndex, OnDeinitializeFunctionName);

    // Register addSynchronization function
    // void addSynchronization(table synchronization)
    lua_pushlightuserdata(*_luaState, asset.get());
    lua_pushcclosure(*_luaState, &assetloader::addSynchronization, 1);
    lua_setfield(*_luaState, assetTableIndex, AddSynchronizationFunctionName);

    // Attach asset table to asset meta table
    lua_setfield(*_luaState, assetMetaTableIndex, AssetTableName);

    // Register empty dependant table on asset metatable.
    // (importer => dependant object)
    lua_newtable(*_luaState);
    lua_setfield(*_luaState, assetMetaTableIndex, DependantsTableName);

    // Extend global asset table (pushed to the lua stack earlier) with this asset meta table 
    lua_setfield(*_luaState, globalTableIndex, asset->id().c_str());

    // Update lua globals
    updateLuaGlobals();
}

void AssetLoader::popAsset() {
    _assetStack.pop_back();
    updateLuaGlobals();
}

void AssetLoader::updateLuaGlobals() {
    // Set `asset` lua global to point to the current asset table
    std::shared_ptr<Asset> asset = _assetStack.back();

    if (asset == _rootAsset) {
        lua_pushnil(*_luaState);
        lua_setglobal(*_luaState, AssetGlobalVariableName);
        return;
    }

    lua_rawgeti(*_luaState, LUA_REGISTRYINDEX, _assetsTableRef);
    lua_getfield(*_luaState, -1, asset->id().c_str());
    lua_getfield(*_luaState, -1, AssetTableName);
    lua_setglobal(*_luaState, AssetGlobalVariableName);
}

int AssetLoader::importDependencyLua(Asset* dependant) {
    int nArguments = lua_gettop(*_luaState);
    SCRIPT_CHECK_ARGUMENTS("import", *_luaState, 1, nArguments);

    std::string assetName = luaL_checkstring(*_luaState, 1);

    std::shared_ptr<Asset> dependency = importDependency(assetName);

    if (!dependency) {
        return luaL_error(*_luaState, "Asset '%s' not found", assetName.c_str());
    }

    addLuaDependencyTable(dependant, dependency.get());

    // Get the exports table
    lua_rawgeti(*_luaState, LUA_REGISTRYINDEX, _assetsTableRef);
    lua_getfield(*_luaState, -1, dependency->id().c_str());
    lua_getfield(*_luaState, -1, ExportsTableName);
    int exportsTableIndex = lua_gettop(*_luaState);

    // Get the dependency table
    lua_rawgeti(*_luaState, LUA_REGISTRYINDEX, _assetsTableRef);
    lua_getfield(*_luaState, -1, dependency->id().c_str());
    lua_getfield(*_luaState, -1, DependantsTableName);
    lua_getfield(*_luaState, -1, dependant->id().c_str());
    int dependencyTableIndex = lua_gettop(*_luaState);

    lua_pushvalue(*_luaState, exportsTableIndex);
    lua_pushvalue(*_luaState, dependencyTableIndex);
    return 2;
}

int AssetLoader::exportAssetLua(Asset* asset) {
    int nArguments = lua_gettop(*_luaState);
    SCRIPT_CHECK_ARGUMENTS("exportAsset", *_luaState, 2, nArguments);

    std::string exportName = luaL_checkstring(*_luaState, 1);

    lua_rawgeti(*_luaState, LUA_REGISTRYINDEX, _assetsTableRef);
    lua_getfield(*_luaState, -1, asset->id().c_str());
    lua_getfield(*_luaState, -1, ExportsTableName);
    int exportsTableIndex = lua_gettop(*_luaState);

    // push the second argument
    lua_pushvalue(*_luaState, 2);
    lua_setfield(*_luaState, exportsTableIndex, exportName.c_str());
    return 0;
}

void AssetLoader::addLuaDependencyTable(Asset* dependant, Asset* dependency) {
    const std::string dependantId = dependant->id();
    const std::string dependencyId = dependency->id();

    lua_rawgeti(*_luaState, LUA_REGISTRYINDEX, _assetsTableRef);
    lua_getfield(*_luaState, -1, dependencyId.c_str());
    const int dependencyIndex = lua_gettop(*_luaState);

    // Extract the imported asset's dependants table
    lua_getfield(*_luaState, -1, DependantsTableName);
    const int dependantsTableIndex = lua_gettop(*_luaState);

    // Set up Dependency object
    lua_newtable(*_luaState);
    const int currentDependantTableIndex = lua_gettop(*_luaState);

    // Register onDependencyInitialize function
    // void onInitialize(function<void()> initializationFunction)
    lua_pushlightuserdata(*_luaState, dependant);
    lua_pushlightuserdata(*_luaState, dependency);
    lua_pushcclosure(*_luaState, &assetloader::onInitializeDependency, 2);
    lua_setfield(*_luaState, currentDependantTableIndex, OnInitializeFunctionName);

    // Register onDependencyDeinitialize function
    // void onDeinitialize(function<void()> deinitializationFunction)
    lua_pushlightuserdata(*_luaState, dependant);
    lua_pushlightuserdata(*_luaState, dependency);
    lua_pushcclosure(*_luaState, &assetloader::onDeinitializeDependency, 2);
    lua_setfield(*_luaState, currentDependantTableIndex, OnDeinitializeFunctionName);

    // Register local resource function
    // string localResource(string path)
    lua_pushlightuserdata(*_luaState, dependency);
    lua_pushcclosure(*_luaState, &assetloader::resolveLocalResource, 1);
    lua_setfield(*_luaState, currentDependantTableIndex, LocalResourceFunctionName);

    // Register synced resource function
    // string syncedResource(string path)
    lua_pushlightuserdata(*_luaState, dependency);
    lua_pushcclosure(*_luaState, &assetloader::resolveSyncedResource, 1);
    lua_setfield(*_luaState, currentDependantTableIndex, SyncedResourceFunctionName);

    // duplicate the table reference on the stack, so it remains after assignment.
    lua_pushvalue(*_luaState, -1);

    // Register the dependant table on the imported asset's dependants table.
    lua_setfield(*_luaState, dependantsTableIndex, dependantId.c_str());
}

}
