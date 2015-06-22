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

// open space includes
#include <modules/newhorizons/util/imagesequencer2.h>
#include <ghoul/logging/logmanager.h>
#include <ghoul/filesystem/filesystem.h>
#include <ghoul/filesystem/directory.h>
#include <openspace/util/time.h>
#include <ghoul/filesystem/cachemanager.h>
#include <modules/newhorizons/util/decoder.h>

#include <openspace/util/spicemanager.h>
#include <fstream>
#include <iterator>
#include <iomanip>
#include <limits>

namespace {
const std::string _loggerCat = "ImageSequencer2";
}

namespace openspace {

ImageSequencer2* ImageSequencer2::_instance = nullptr;

ImageSequencer2::ImageSequencer2()
    : _latestImage()
    , _hasData(false)
{}

ImageSequencer2& ImageSequencer2::ref() {
	assert(_instance != nullptr);
	return *_instance;
}
void ImageSequencer2::initialize() {
	assert(_instance == nullptr);
	_instance = new ImageSequencer2;
    _instance->_defaultCaptureImage = absPath("${OPENSPACE_DATA}/scene/common/textures/placeholder_blank.png");
}

void ImageSequencer2::deinitialize() {
	delete _instance;
	_instance = nullptr;
}

bool ImageSequencer2::isReady(){
	return _hasData;
}

void ImageSequencer2::updateSequencer(double time){
	if (Time::ref().timeJumped() && Time::ref().deltaTime() == 0){
		Time::ref().setDeltaTime(0.1);
	} // Time is not properly updated when time jump with dt = 0 

	if (_currentTime != time){
		_previousTime = _currentTime;
		_currentTime = time;
	}
}

std::pair<double, std::string> ImageSequencer2::getNextTarget(){
	auto compareTime = [](const std::pair<double, std::string> &a,
	                 	  const std::pair<double, std::string> &b)->bool{
		return a.first < b.first;
	};
	std::pair<double, std::string> findEqualToThis;
	findEqualToThis.first = _currentTime;
	auto it = std::lower_bound(_targetTimes.begin(), _targetTimes.end(), findEqualToThis, compareTime);

	if (it != _targetTimes.end() && it != _targetTimes.begin())
		return (*it);
    else
        return std::make_pair(0.0, "");
}

std::pair<double, std::string> ImageSequencer2::getCurrentTarget(){
	auto compareTime = [](const std::pair<double, std::string> &a,
		                  const std::pair<double, std::string> &b)->bool{
		return a.first < b.first;
	};
	std::pair<double, std::string> findEqualToThis;
	findEqualToThis.first = _currentTime;
	auto it = std::lower_bound(_targetTimes.begin(), _targetTimes.end(), findEqualToThis, compareTime);

	if (it != _targetTimes.end() && it != _targetTimes.begin()){
		return *std::prev(it);
	}
    else
        return std::make_pair(0.0, "No Target");
}

std::pair<double, std::vector<std::string>> ImageSequencer2::getIncidentTargetList(int range){
	std::pair<double, std::vector<std::string>> incidentTargets;

	auto compareTime = [](const std::pair<double, std::string> &a,
		                 const std::pair<double, std::string> &b)->bool{
		 return a.first < b.first;
	};
	// what to look for
	std::pair<double, std::string> findEqualToThis;
	findEqualToThis.first = _currentTime;
	auto it = std::lower_bound(_targetTimes.begin(), _targetTimes.end(), findEqualToThis, compareTime);
	
	if (it != _targetTimes.end() && it != _targetTimes.begin()){
		// move the iterator to the first element of the range
		std::advance(it, -(range + 1));

		// now extract incident range 
		for (int i = 0; i < 2 * range + 1; i++){
			incidentTargets.first = it->first;
			incidentTargets.second.push_back(it->second);
			it++;
			if (it == _targetTimes.end())
				break;
		}
	}

	return incidentTargets;
}

double ImageSequencer2::getIntervalLength(){
	double upcoming = getNextCaptureTime();
	if (_nextCapture != upcoming){
		_nextCapture = upcoming;
		_intervalLength = upcoming - _currentTime;
	}
	return _intervalLength;
}

double ImageSequencer2::getNextCaptureTime(){
	auto compareTime = [](const double &a, const double &b)->bool{
		return a < b;
	};
	double nextCaptureTime = 0;
	auto it = std::lower_bound(_captureProgression.begin(), _captureProgression.end(), _currentTime, compareTime);
	if (it != _captureProgression.end())
		nextCaptureTime = *it;

	return nextCaptureTime;
}
const Image ImageSequencer2::getLatestImageForInstrument(const std::string _instrumentID){
	
	return _latestImage;
}

std::map<std::string, bool> ImageSequencer2::getActiveInstruments(){
	// first set all instruments to off
	for (auto i : _switchingMap)
		_switchingMap[i.first] = false;
	// go over the filetranslation map
	for (auto key : _fileTranslation){
		// for each spice-instrument
		for (auto instrumentID : key.second->getTranslation()){
			// check if the spice-instrument is active 
				if (instrumentActive(instrumentID)){
					// go over switching map
					for (auto instrument : _switchingMap){
						// if instrument is present in switching map
						if (instrumentID == instrument.first){
							// set as active
							_switchingMap[instrumentID] = true;
						}
					}
				}
			}
		}
	// return entire map, seen in GUI.
	return _switchingMap;
}
bool ImageSequencer2::instrumentActive(std::string instrumentID){ 
	for (auto i : _instrumentTimes){
		//check if this instrument is in range
		if (i.second.inRange(_currentTime)){ 
			//if so, then get the corresponding spiceID
			std::vector<std::string> spiceIDs = _fileTranslation[i.first]->getTranslation(); 
			//check which specific subinstrument is firing
			for (auto s : spiceIDs){
				if (s == instrumentID){
					return true;
				}
			}
		}
	}
	return false;
}

float ImageSequencer2::instrumentActiveTime(const std::string& instrumentID) const {
    for (auto i : _instrumentTimes){
        //check if this instrument is in range
        if (i.second.inRange(_currentTime)){
            //if so, then get the corresponding spiceID
            std::vector<std::string> spiceIDs = _fileTranslation.at(i.first)->getTranslation();
            //check which specific subinstrument is firing
            for (auto s : spiceIDs){
                if (s == instrumentID) {
                    return static_cast<float>((_currentTime - i.second._min) / (i.second._max - i.second._min));
                }
            }
        }
    }
    return -1.f;
}

bool ImageSequencer2::getImagePaths(std::vector<Image>& captures,  
	                                std::string projectee, 
									std::string instrumentID){

	if (!instrumentActive(instrumentID) && !Time::ref().timeJumped()) return false;
	// dev. note: this is only due to LORRI being the only instrument implemented so far.
	return getImagePaths(captures, projectee);
}

bool ImageSequencer2::getImagePaths(std::vector<Image>& captures, 
	                                std::string projectee){

	// check if this instance is either in range or 
	// a valid candidate to recieve data 

	//if (!Time::ref().timeJumped() && projectee == getCurrentTarget().second)
	if (_subsetMap[projectee]._range.inRange(_currentTime) ||
		_subsetMap[projectee]._range.inRange(_previousTime)){
		auto compareTime = [](const Image &a,
			                  const Image &b)->bool{
			return a.startTime < b.startTime;
		};
		
		// for readability we store the iterators
		auto begin = _subsetMap[projectee]._subset.begin(); 
		auto end   = _subsetMap[projectee]._subset.end();
		
		// create temporary storage
		std::vector<Image> captureTimes;
		// what to look for 
		Image findPrevious, findCurrent;
		findPrevious.startTime = _previousTime;
		findCurrent.startTime  = _currentTime;

		// find the two iterators that correspond to the latest time jump
		auto curr = std::lower_bound(begin, end, findCurrent, compareTime);
		auto prev = std::lower_bound(begin, end, findPrevious, compareTime);

		if (curr != begin && curr != end  && prev != begin && prev != end && prev < curr){
            if (curr->startTime >= prev->startTime){
			    std::transform(prev, curr, std::back_inserter(captureTimes),
				    [](const Image& i) {
				    return i;
			    });
			    std::reverse(captureTimes.begin(), captureTimes.end());
			    captures = captureTimes;
                if (!captures.empty())
					_latestImage = captures.back();

			    return true;
            }
		}
	}
	return false;
}
void ImageSequencer2::sortData(){
	auto targetComparer = [](const std::pair<double, std::string> &a,
		                     const std::pair<double, std::string> &b)->bool{
		return a.first < b.first;
	};
	auto imageComparer = [](const Image &a, const Image &b)->bool{
		return a.startTime < b.startTime;
	};

	std::sort(_targetTimes.begin(), _targetTimes.end(), targetComparer);
	std::stable_sort(_captureProgression.begin(), _captureProgression.end());

	for (auto sub : _subsetMap){
		std::sort(_subsetMap[sub.first]._subset.begin(),
			      _subsetMap[sub.first]._subset.end(), imageComparer);
	}
}

void ImageSequencer2::runSequenceParser(SequenceParser* parser){
	bool success = parser->create();
    if (!success)
        return;
	// get new data 
	std::map<std::string, Decoder*> translations = parser->getTranslation();
	std::map<std::string, ImageSubset> imageData = parser->getSubsetMap();
	std::vector<std::pair<std::string, TimeRange>> instrumentTimes = parser->getIstrumentTimes();
	std::vector<std::pair<double, std::string>> targetTimes = parser->getTargetTimes();
	std::vector<double> captureProgression = parser->getCaptureProgression();
	
	// check for sanity
    if (translations.empty() || imageData.empty() || instrumentTimes.empty() || targetTimes.empty() || captureProgression.empty())
        return;

	_fileTranslation.insert(translations.begin(), translations.end());
	_subsetMap.insert(imageData.begin(), imageData.end());
	_instrumentTimes.insert(_instrumentTimes.end(), instrumentTimes.begin(), instrumentTimes.end());
	_targetTimes.insert(_targetTimes.end(), targetTimes.begin(), targetTimes.end());
	_captureProgression.insert(_captureProgression.end(), captureProgression.begin(), captureProgression.end());

	// sorting of data _not_ optional
	sortData();

	// extract payload from _fileTranslation 
	for (auto t : _fileTranslation) {
		if (t.second->getDecoderType() == "CAMERA" || 
			t.second->getDecoderType() == "SCANNER" ){
			std::vector<std::string> spiceIDs = t.second->getTranslation();
			for (auto id : spiceIDs){
				_switchingMap[id] = false;
			}
		}
	}
	_hasData = true;
}

}  // namespace openspace
