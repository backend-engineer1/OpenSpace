/*****************************************************************************************
 *                                                                                       *
 * OpenSpace                                                                             *
 *                                                                                       *
 * Copyright (c) 2014-2018                                                               *
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

#include "floatoperations.glsl"
#include <#{fragmentPath}>

layout(location = 0) out vec4 _out_color_;
layout(location = 1) out vec4 gPosition;
layout(location = 2) out vec4 gNormal;
layout(location = 3) out vec4 filterBuffer;

void main() {
     Fragment f   = getFragment();
     _out_color_  = f.color;
     gPosition    = f.gPosition;
     gNormal      = f.gNormal;
     
     bool automaticBloom = false;
     if (automaticBloom) {
        // Extract luminance
        float Y = dot(f.color.rgb, vec3(0.299, 0.587, 0.144));

        // Apply Bloom on the bloom threshold range values
        //vec4 bColor = f.color * 4.0 * smoothstep(bloom_thresh_min, bloom_thresh_max, Y);
        //filterBuffer = bColor
        filterBuffer = vec4(f.filterFlag);
     } else {
        if (f.filterFlag == 1)
            filterBuffer = f.color;
        else
            filterBuffer = vec4(0);
     }
     
     gl_FragDepth = normalizeFloat(f.depth);
}
