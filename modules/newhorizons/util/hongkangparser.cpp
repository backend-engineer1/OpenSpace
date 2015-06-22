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

#include <modules/newhorizons/util/imagesequencer2.h>
#include <ghoul/logging/logmanager.h>
#include <ghoul/filesystem/filesystem.h>
#include <ghoul/filesystem/directory.h>
#include <openspace/util/time.h>
#include <openspace/util/spicemanager.h>
#include <fstream>
#include <iterator>
#include <iomanip>
#include <limits>
#include <modules/newhorizons/util/hongkangparser.h>
#include <modules/newhorizons/util/instrumentdecoder.h>


namespace {
	const std::string _loggerCat = "HongKangParser";
	const std::string keyTranslation = "DataInputTranslation";

    const std::string PlaybookIdentifierName = "HongKang";
}

namespace openspace {
	HongKangParser::HongKangParser(const std::string& fileName,
		                           std::string spacecraft,
								   ghoul::Dictionary translationDictionary,
		                           std::vector<std::string> potentialTargets) :
							       _defaultCaptureImage(absPath("${OPENSPACE_DATA}/scene/common/textures/placeholder.png"))
{
	_fileName          = fileName;
	_spacecraft        = spacecraft;
	_potentialTargets  = potentialTargets;
	
	//get the different instrument types
	const std::vector<std::string>& decoders = translationDictionary.keys();
	//for each decoder (assuming might have more if hong makes changes)
	for (int i = 0; i < decoders.size(); i++){
		//create dictionary containing all {playbookKeys , spice IDs}
		if (decoders[i] == "Instrument"){
			ghoul::Dictionary typeDictionary;
			translationDictionary.getValue(decoders[i], typeDictionary);
			//for each playbook call -> create a Decoder object
			const std::vector<std::string>& keys = typeDictionary.keys();
			//std::string abort = decoders[i] + "." + keyStopCommand;
			for (int j = 0; j < keys.size(); j++){
				std::string currentKey = decoders[i] + "." + keys[j];
				
				ghoul::Dictionary decoderDictionary;
				translationDictionary.getValue(currentKey, decoderDictionary);

				Decoder *decoder = Decoder::createFromDictionary(decoderDictionary, decoders[i]);
				//insert decoder to map - this will be used in the parser to determine
				//behavioral characteristics of each instrument
				_fileTranslation[keys[j]] = decoder;
			}
		}
		//Hong's playbook needs _only_ instrument translation though.
	}
}

void findPlaybookSpecifiedTarget(std::string line, std::string& target){
	//remembto add this lua later... 
	std::vector<std::string> ptarg = { "PLUTO", "CHARON", "NIX", "HYDRA", "P5", "P4" };
	for (auto p : ptarg){
		// loop over all targets and determine from 4th col which target this instrument points to
		std::transform(line.begin(), line.end(), line.begin(), toupper);
		if (line.find(p) != std::string::npos){
			target = p;
			break;
		}
		else{
		// not found - we set void until we have more info. 
			target = "VOID";
		}
	}
}

bool HongKangParser::create(){
	if (size_t position = _fileName.find_last_of(".") + 1){
		if (position != std::string::npos){
			std::string extension = ghoul::filesystem::File(_fileName).fileExtension();

			if (extension == "txt") {// Hong Kang. pre-parsed playbook
				LINFO("Using Preparsed Playbook V9H");
				std::ifstream file(_fileName , std::ios::binary);
				if (!file.good()) LERROR("Failed to open txt file '" << _fileName << "'");

				std::string line = "";
				double shutter = 0.01;

				std::string previousTarget;

				std::string previousCamera;
				std::string previousScanner;

				TimeRange cameraRange;
				TimeRange scanRange;

				std::vector<std::string> scannerSpiceID;
				std::vector<std::string> cameraSpiceID;

				double capture_start = -1;
				double capture_stop = -1;
				double scan_start = -1;
				double scan_stop = -1;

				std::string cameraTarget = "VOID";
				std::string scannerTarget = "VOID";

				while (std::getline(file, line)) {//only while inte do,  FIX
					//std::getline(file, line);
					
					std::string event = line.substr(0, line.find_first_of(" ")); 

					auto it = _fileTranslation.find(event);
					bool foundEvent = (it != _fileTranslation.end());

					std::string met = line.substr(25, 9);
					double time = getETfromMet(met);

					Image image;
					
					if (foundEvent){
						//store the time, this is used for getNextCaptureTime() 
						_captureProgression.push_back(time);
						
						if (it->second->getDecoderType() == "CAMERA"){
							if (capture_start == -1){
								//encountered new camera sequence- store start time
								capture_start = time;
								previousCamera = it->first;
							}
							//always store individual image for camera
							cameraSpiceID = it->second->getTranslation();
							//rely on playboook mdl column to determine target
							findPlaybookSpecifiedTarget(line, cameraTarget);

							//fill image
							createImage(image, time, time + shutter, cameraSpiceID, cameraTarget, _defaultCaptureImage);
							//IFF spaccraft has decided to switch target, store in target map (used for: 'next observation focus')
							if (previousTarget != image.target){
								previousTarget = image.target;
								std::pair<double, std::string> v_target = std::make_pair(time, image.target);
								_targetTimes.push_back(v_target);
							}

							//store actual image in map. All targets get _only_ their corresp. subset.
							_subsetMap[image.target]._subset.push_back(image);
							//compute and store the range for each subset 
							_subsetMap[image.target]._range.setRange(time);
						}
						if (it->second->getDecoderType() == "SCANNER"){ // SCANNER START 
							scan_start = time;

							InstrumentDecoder* scanner = static_cast<InstrumentDecoder*>(it->second);
							std::string endNominal = scanner->getStopCommand();

							// store current position in file
                            std::streampos len = file.tellg();
							std::string linePeek;
							bool foundstop = false;
							while (!file.eof() && !foundstop){
								//continue grabbing next line until we find what we need
								getline(file, linePeek);
								if (linePeek.find(endNominal) != std::string::npos){
									foundstop = true;

									met = linePeek.substr(25, 9);
									scan_stop = getETfromMet(met);
									findPlaybookSpecifiedTarget(line, scannerTarget);
									scannerSpiceID = it->second->getTranslation();

									scanRange._min = scan_start;
									scanRange._max = scan_stop;
									_instrumentTimes.push_back(std::make_pair(it->first, scanRange));


									//store individual image
									createImage(image, scan_start, scan_stop, scannerSpiceID, scannerTarget, _defaultCaptureImage);

									_subsetMap[image.target]._subset.push_back(image);
									_subsetMap[image.target]._range.setRange(scan_start);
								}
							}
							//go back to stored position in file
							file.seekg(len, std::ios_base::beg);

							/*//scanner works like state-machine -only store start time now
							scan_start = time;
							previousScanner = it->first;
							//store scanning instrument - store image once stopTime is found!
							findPlaybookSpecifiedTarget(line, scannerTarget);
							scannerSpiceID = it->second->getTranslation();*/
						}
					}
					else{ // we have reached the end of a scan or consecutive capture sequence!
						if (capture_start != -1){
							//end of capture sequence for camera, store end time of this sequence
							capture_stop = time;
							cameraRange._min = capture_start;
							cameraRange._max = capture_stop;
							_instrumentTimes.push_back(std::make_pair(previousCamera, cameraRange));

							capture_start = -1;
						}
						/*if (line.find("END_NOM") != std::string::npos){
							assert(scan_start != -1, "SCAN end occured before SCAN call!");
							//end of scan, store end time of this scan + store the scan image
							scan_stop = time;
							scanRange._min = scan_start;
							scanRange._max = scan_stop;
							_instrumentTimes.push_back(std::make_pair(previousScanner, scanRange));
							
							//store individual image
							createImage(image, scan_start, scan_stop, scannerSpiceID, scannerTarget, _defaultCaptureImage);

							_subsetMap[image.target]._subset.push_back(image);
							_subsetMap[image.target]._range.setRange(scan_start);

							scan_start = -1;
						}*/
					}
				}
			}
		}
	}

    sendPlaybookInformation(PlaybookIdentifierName);
    return true;
}

bool HongKangParser::augmentWithSpice(Image& image,
	                                  std::string spacecraft, 
									  std::vector<std::string> payload, 
									  std::vector<std::string> potentialTargets){
	image.target = "VOID";
	// we have (?) to cast to int, unfortunately
    // Why? --abock
	int exposureTime = image.stopTime - image.startTime;
	if (exposureTime == 0)
        exposureTime = 1;

	for (int i = 0; i < potentialTargets.size(); i++){
		bool success = false;
		bool _withinFOV = false;
		for (int j = 0; j < image.activeInstruments.size(); j++){
			double time = image.startTime;
			for (int k = 0; k < exposureTime; k++){
				time += k;
				success = openspace::SpiceManager::ref().targetWithinFieldOfView(
					image.activeInstruments[j],
					potentialTargets[i],
					spacecraft,
					"ELLIPSOID",
					"NONE",
					time,
					_withinFOV);
				if (_withinFOV){
					image.target = potentialTargets[i];
					_withinFOV = false;
				}
			}
		}
	}
	return false;
}

void HongKangParser::createImage(Image& image, double startTime, double stopTime, std::vector<std::string> instr, std::string targ, std::string path) {
	image.startTime = startTime;
	image.stopTime = stopTime;
	image.path = path;
	for (int i = 0; i < instr.size(); i++){
		image.activeInstruments.push_back(instr[i]);
	}
	image.target = targ;
	image.projected = false;
}

double HongKangParser::getETfromMet(std::string line){
	std::string::size_type sz;
	return getETfromMet(std::stod(line, &sz));
}

double HongKangParser::getETfromMet(double met){
	double diff;
	double referenceET;
	openspace::SpiceManager::ref().getETfromDate("2015-07-14T11:50:00.00", referenceET);
    double et = referenceET;

    diff = std::abs(met - _metRef);
	if (met > _metRef){
		et = referenceET + diff;
	} else if (met < _metRef){
		et = referenceET - diff;
	}
	return et;
}


double HongKangParser::getMetFromET(double et){
	double met;
	double referenceET;
	openspace::SpiceManager::ref().getETfromDate("2015-07-14T11:50:00.00", referenceET);

	if (et >= referenceET){
		met = _metRef + (et - referenceET);
	}else{
		met = _metRef - (referenceET - et);
	}

	return met;
}


}