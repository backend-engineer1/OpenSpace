/*****************************************************************************************
 *                                                                                       *
 * OpenSpace                                                                             *
 *                                                                                       *
 * Copyright (c) 2014-2019                                                               *
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

#include <modules/autonavigation/autonavigationhandler.h>

#include <modules/autonavigation/transferfunctions.h>
#include <openspace/engine/globals.h>
#include <openspace/engine/windowdelegate.h>
#include <openspace/interaction/navigationhandler.h>
#include <openspace/scene/scenegraphnode.h>
#include <openspace/util/camera.h>
#include <openspace/query/query.h>
#include <ghoul/logging/logmanager.h>
#include <glm/gtx/vector_angle.hpp>
#include <glm/gtx/quaternion.hpp>

namespace {
    constexpr const char* _loggerCat = "AutoNavigationHandler";
} // namespace

namespace openspace::autonavigation {

AutoNavigationHandler::AutoNavigationHandler()
    : properties::PropertyOwner({ "AutoNavigationHandler" })
{
    // Add the properties
    // TODO
}

AutoNavigationHandler::~AutoNavigationHandler() {} // NOLINT

Camera* AutoNavigationHandler::camera() const {
    return global::navigationHandler.camera();
}

const double AutoNavigationHandler::pathDuration() const {
    return _pathDuration;
}

PathSegment& AutoNavigationHandler::currentPathSegment() {
    for (PathSegment& ps : _pathSegments) {
        double endTime = ps.startTime + ps.duration;
        if (endTime > _currentTime) {
            return ps;
        }
    }
}

void AutoNavigationHandler::updateCamera(double deltaTime) {
    ghoul_assert(camera() != nullptr, "Camera must not be nullptr");

    if (!_isPlaying || _pathSegments.empty()) return;

    PathSegment cps = currentPathSegment(); 

    // INTERPOLATE (TODO: make a function, and allow different methods)

    double t = (_currentTime - cps.startTime) / cps.duration;
    t = transferfunctions::cubicEaseInOut(t); // TEST
    t = std::max(0.0, std::min(t, 1.0));

    // TODO: don't set every frame and 
    // Set anchor node in orbitalNavigator, to render visible nodes and 
    // add possibility to navigate when we reach the end.
    CameraState cs = (t < 0.5) ? cps.start : cps.end;
    global::navigationHandler.orbitalNavigator().setAnchorNode(cs.referenceNode);

    // TODO: add different ways to interpolate later
    glm::dvec3 cameraPosition = cps.start.position * (1.0 - t) + cps.end.position * t;
    glm::dquat cameraRotation = 
        glm::slerp(cps.start.rotation, cps.end.rotation, t);

    camera()->setPositionVec3(cameraPosition);
    camera()->setRotation(cameraRotation);

    _currentTime += deltaTime;

    // reached the end of the path => stop playing
    if (_currentTime > _pathDuration) {
        _isPlaying = false;
        // TODO: implement suitable stop behaviour
    }
}

void AutoNavigationHandler::createPath(PathSpecification& spec) {
    clearPath();
    bool success = true;
    for (int i = 0; i < spec.instructions()->size(); i++) {
        const Instruction& ins = spec.instructions()->at(i);
        success = handleInstruction(ins, i);

        if (!success)
            break;
    }

    if (success) {
        LINFO("Succefully generated camera path. Starting.");
        startPath();
    }
    else
        LINFO("Could not create path.");
}

void AutoNavigationHandler::clearPath() {
    _pathSegments.clear();
    _pathDuration = 0.0;
    _currentTime = 0.0;
}

void AutoNavigationHandler::startPath() {
    ghoul_assert(!_pathSegments.empty(), "Cannot start an empty path");
    
    _pathDuration = 0.0;
    for (auto ps : _pathSegments) {
        _pathDuration += ps.duration;
    }
    _currentTime = 0.0;
    _isPlaying = true;
}

CameraState AutoNavigationHandler::getStartState() {
    CameraState cs;
    if (_pathSegments.empty()) {
        cs.position = camera()->positionVec3();
        cs.rotation = camera()->rotationQuaternion();
        cs.referenceNode = global::navigationHandler.anchorNode()->identifier();
    }
    else {
        cs = _pathSegments.back().end;
    }

    return cs;
}

bool AutoNavigationHandler::handleInstruction(const Instruction& instruction, int index)
{
    CameraState startState = getStartState();
    CameraState endState;
    double duration, startTime;
    bool success = true;

    switch (instruction.type)
    {
    case InstructionType::TargetNode:
        success = endFromTargetNodeInstruction(
            endState, startState, instruction, index
        );
        break;

    case InstructionType::NavigationState:
        success = endFromNavigationStateInstruction(
            endState, instruction, index
        );
        break;

    default:
        LERROR(fmt::format("Non-implemented instruction type: {}.", instruction.type));
        success = false;
        break;
    }

    if (!success) return false;

    // compute duration 
    if (instruction.props->duration.has_value()) {
        duration = instruction.props->duration.value();
        if (duration <= 0) {
            LERROR(fmt::format("Failed creating path segment number {}. Duration can not be negative.", index + 1));
            return false;
        }
    }
    else {
        // TODO: compute default duration
        duration = 5.0;
    }

    // compute startTime 
    startTime = 0.0;
    if (!_pathSegments.empty()) {
        PathSegment& last = _pathSegments.back();
        startTime = last.startTime + last.duration;
    }

    // create new path
    _pathSegments.push_back(
        PathSegment{ startState, endState, duration, startTime }
    );

    return true;
}

bool AutoNavigationHandler::endFromTargetNodeInstruction(
    CameraState& endState, CameraState& prevState, const Instruction& instruction, int index)
{
    TargetNodeInstructionProps* props = 
        dynamic_cast<TargetNodeInstructionProps*>(instruction.props.get());

    if (!props) {
        LERROR(fmt::format("Could not handle target node instruction (number {}).", index + 1));
        return false;
    }

    // Compute end state 
    std::string& identifier = props->targetNode;
    const SceneGraphNode* targetNode = sceneGraphNode(identifier);
    if (!targetNode) {
        LERROR(fmt::format("Failed handling instruction number {}. Could not find node '{}' to target", index + 1, identifier));
        return false;
    }

    glm::dvec3 targetPos;
    if (props->position.has_value()) {
        // note that the anchor and reference frame is our targetnode. 
        // The position in instruction is given is relative coordinates.
        targetPos = targetNode->worldPosition() + targetNode->worldRotationMatrix() * props->position.value();
    }
    else {
        bool hasHeight = props->height.has_value();

        // TODO: compute defualt height in a better way
        double defaultHeight = 2 * targetNode->boundingSphere();
        double height = hasHeight? props->height.value() : defaultHeight;

        targetPos = computeTargetPositionAtNode(
            targetNode, 
            prevState.position, 
            height
        );
    }

    glm::dmat4 lookAtMat = glm::lookAt(
        targetPos,
        targetNode->worldPosition(),
        camera()->lookUpVectorWorldSpace()
    );

    glm::dquat targetRot = glm::normalize(glm::inverse(glm::quat_cast(lookAtMat)));

    endState = CameraState{ targetPos, targetRot, identifier };

    return true;
}

bool AutoNavigationHandler::endFromNavigationStateInstruction(
    CameraState& endState, const Instruction& instruction, int index)
{
    NavigationStateInstructionProps* props =
        dynamic_cast<NavigationStateInstructionProps*>(instruction.props.get());

    if (!props) {
        LERROR(fmt::format("Could not handle navigation state instruction (number {}).", index + 1));
        return false;
    }

    interaction::NavigationHandler::NavigationState ns = props->navState;

    // OBS! The following code is exactly the same as used in NavigationHandler::applyNavigationState. Should probably be made into a function.
    const SceneGraphNode* referenceFrame = sceneGraphNode(ns.referenceFrame);
    const SceneGraphNode* anchorNode = sceneGraphNode(ns.anchor); // The anchor is also the target

    if (!anchorNode) {
        LERROR(fmt::format("Failed handling instruction number {}. Could not find node '{}' to target", index + 1, ns.anchor));
        return false;
    }

    const glm::dvec3 anchorWorldPosition = anchorNode->worldPosition();
    const glm::dmat3 referenceFrameTransform = referenceFrame->worldRotationMatrix();

    const glm::dvec3 targetPositionWorld = anchorWorldPosition +
        glm::dvec3(referenceFrameTransform * glm::dvec4(ns.position, 1.0));

    glm::dvec3 up = ns.up.has_value() ?
        glm::normalize(referenceFrameTransform * ns.up.value()) :
        glm::dvec3(0.0, 1.0, 0.0);

    // Construct vectors of a "neutral" view, i.e. when the aim is centered in view.
    glm::dvec3 neutralView =
        glm::normalize(anchorWorldPosition - targetPositionWorld);

    glm::dquat neutralCameraRotation = glm::inverse(glm::quat_cast(glm::lookAt(
        glm::dvec3(0.0),
        neutralView,
        up
    )));

    glm::dquat pitchRotation = glm::angleAxis(ns.pitch, glm::dvec3(1.f, 0.f, 0.f));
    glm::dquat yawRotation = glm::angleAxis(ns.yaw, glm::dvec3(0.f, -1.f, 0.f));

    glm::quat targetRotation = neutralCameraRotation * yawRotation * pitchRotation;

    endState = CameraState{ targetPositionWorld, targetRotation, ns.anchor };
    return true;
}

glm::dvec3 AutoNavigationHandler::computeTargetPositionAtNode(
    const SceneGraphNode* node, glm::dvec3 prevPos, double height)
{
    // TODO: compute actual distance above surface and validate negative values

    glm::dvec3 targetPos = node->worldPosition();
    glm::dvec3 targetToPrevVector = prevPos - targetPos;

    double radius = static_cast<double>(node->boundingSphere());

    // move target position out from surface, along vector to camera
    targetPos += glm::normalize(targetToPrevVector) * (radius + height);

    return targetPos;
}

} // namespace openspace::autonavigation
