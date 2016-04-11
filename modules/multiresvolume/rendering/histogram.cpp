/*****************************************************************************************
 *                                                                                       *
 * OpenSpace                                                                             *
 *                                                                                       *
 * Copyright (c) 2015                                                                    *
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

#include <modules/multiresvolume/rendering/histogram.h>

#include <ghoul/logging/logmanager.h>

#include <cmath>
#include <cassert>
namespace {
    const std::string _loggerCat = "Histogram";
}

namespace openspace {

Histogram::Histogram()
    : _minBin(0)
    , _maxBin(0)
    , _numBins(-1)
    , _data(nullptr) {}

Histogram::Histogram(float minBin, float maxBin, int numBins)
    : _minBin(minBin)
    , _maxBin(maxBin)
    , _numBins(numBins)
    , _data(nullptr) {

    _data = new float[numBins];
    for (int i = 0; i < numBins; ++i) {
        _data[i] = 0.0;
    }
}

Histogram::Histogram(float minBin, float maxBin, int numBins, float *data)
    : _minBin(minBin)
    , _maxBin(maxBin)
    , _numBins(numBins)
    , _data(data) {}

Histogram::Histogram(Histogram&& other) {
    _minBin = other._minBin;
    _maxBin = other._maxBin;
    _numBins = other._numBins;
    _data = other._data;
    other._data = nullptr;
}

Histogram& Histogram::operator=(Histogram&& other) {
    _minBin = other._minBin;
    _maxBin = other._maxBin;
    _numBins = other._numBins;
    _data = other._data;
    other._data = nullptr;
	return *this;
}


Histogram::~Histogram() {
    if (_data) {
        delete[] _data;
    }
}


int Histogram::numBins() const {
    return _numBins;
}

float Histogram::minBin() const {
    return _minBin;
}

float Histogram::maxBin() const {
    return _maxBin;
}

bool Histogram::isValid() const {
    return _numBins != -1;
}


bool Histogram::add(float bin, float value) {
    if (bin < _minBin || bin > _maxBin) {
        // Out of range
        return false;
    }

    float normalizedBin = (bin - _minBin) / (_maxBin - _minBin);    // [0.0, 1.0]
    int binIndex = floor(normalizedBin * _numBins);                 // [0, _numBins]
    if (binIndex == _numBins) binIndex--;                           // [0, _numBins[

    _data[binIndex] += value;
    return true;
}

bool Histogram::add(const Histogram& histogram) {
    if (_minBin == histogram.minBin() && _maxBin == histogram.maxBin() && _numBins == histogram.numBins()) {

        const float* data = histogram.data();
        for (int i = 0; i < _numBins; i++) {

            _data[i] += data[i];

        }
        return true;
    } else {
        LERROR("Dimension mismatch");
        return false;
    }
}

bool Histogram::addRectangle(float lowBin, float highBin, float value) {
    if (lowBin == highBin) return true;
    if (lowBin > highBin) {
        std::swap(lowBin, highBin);
    }
    if (lowBin < _minBin || highBin > _maxBin) {
        // Out of range
        return false;
    }

    float normalizedLowBin = (lowBin - _minBin) / (_maxBin - _minBin);
    float normalizedHighBin = (highBin - _minBin) / (_maxBin - _minBin);

    float lowBinIndex = normalizedLowBin * _numBins;
    float highBinIndex = normalizedHighBin * _numBins;

    int fillLow = floor(lowBinIndex);
    int fillHigh = ceil(highBinIndex);

    for (int i = fillLow; i < fillHigh; i++) {
        _data[i] += value;
    }

    if (lowBinIndex > fillLow) {
        float diff = lowBinIndex - fillLow;
        _data[fillLow] -= diff * value;
    }
    if (highBinIndex < fillHigh) {
        float diff = -highBinIndex + fillHigh;
        _data[fillHigh - 1] -= diff * value;
    }

    return true;
}


float Histogram::interpolate(float bin) const {
    float normalizedBin = (bin - _minBin) / (_maxBin - _minBin);
    float binIndex = normalizedBin * _numBins - 0.5; // Center

    float interpolator = binIndex - floor(binIndex);
    int binLow = floor(binIndex);
    int binHigh = ceil(binIndex);

    // Clamp bins
    if (binLow < 0) binLow = 0;
    if (binHigh >= _numBins) binHigh = _numBins - 1;

    return (1.0 - interpolator) * _data[binLow] + interpolator * _data[binHigh];
}

float Histogram::sample(int binIndex) const {
    assert(binIndex >= 0 && binIndex < _numBins);
    return _data[binIndex];
}


const float* Histogram::data() const {
    return _data;
}

std::vector<std::pair<float,float>> Histogram::getDecimated(int numBins) const {
    // Return a copy of _data decimated as in Ljung 2004
    return std::vector<std::pair<float,float>>();
}


void Histogram::normalize() {
    float sum = 0.0;
    for (int i = 0; i < _numBins; i++) {
        sum += _data[i];
    }
    for (int i = 0; i < _numBins; i++) {
        _data[i] /= sum;
    }
}

void Histogram::print() const {
    std::cout << "number of bins: " << _numBins << std::endl
              << "range: " << _minBin << " - " << _maxBin << std::endl << std::endl;
    for (int i = 0; i < _numBins; i++) {
        float low = _minBin + float(i) / _numBins * (_maxBin - _minBin);
        float high = low + (_maxBin - _minBin) / float(_numBins);
        std::cout << "[" << low << ", " << high << "[" << std::endl
                  << "   " << _data[i] << std::endl;
    }
}

}
