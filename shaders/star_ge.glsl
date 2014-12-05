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

#version __CONTEXT__

const vec2 corners[4] = vec2[4]( 
    vec2(0.0, 1.0), 
	vec2(0.0, 0.0), 
	vec2(1.0, 1.0), 
	vec2(1.0, 0.0) 
);

#include "PowerScaling/powerScalingMath.hglsl"


layout(points) in;
layout(triangle_strip, max_vertices = 4) out;

layout(location = 0) in vec4 psc_position[];
layout(location = 1) in vec3 vs_brightness[];
layout(location = 2) in vec3 vs_velocity[];
layout(location = 3) in float vs_speed[];
layout(location = 4) in vec4 cam_position[];

layout(location = 0) out vec4 vs_position;
layout(location = 1) out vec3 ge_brightness;
layout(location = 2) out vec3 ge_velocity;
layout(location = 3) out float ge_speed;
layout(location = 4) out vec2 texCoord;

uniform mat4 projection; // we do this after distance computation. 

uniform float spriteSize;

void main() {
	ge_brightness = vs_brightness[0];
	ge_velocity = vs_velocity[0];
	ge_speed = vs_speed[0];
	
	/// --- distance modulus --- NOT OPTIMIZED YET.
 	
//	float M  = vs_brightness[0][0];                                 // get ABSOLUTE magnitude (x param)
	float M  = vs_brightness[0].z; // if NOT running test-target.
	vec4 cam = vec4(-cam_position[0].xyz, cam_position[0].w);                  // get negative camera position   
	vec4 pos = psc_position[0];                                    // get OK star position
	
	vec4 result = psc_addition(pos, cam);                          // compute vec from camera to position
	float x, y, z, w;
	x = result[0];
	y = result[1];
	z = result[2];
	w = result[3];

	vec2 pc = vec2(length(result.xyz), result[3]);

	// @Check conversion is also done in the cpp file ---abock	 
	pc[0] *= 0.324077929f;                                          // convert meters -> parsecs
	pc[1] += -18.0f;
	
	float pc_psc   = pc[0] * pow(10, pc[1]);                       // psc scale out
	float apparent = -(M - 5.0f * (1.f - log10(pc_psc)));          // get APPARENT magnitude. 
     
	vec4 P = gl_in[0].gl_Position;
	 
	float weight = 0.00001f; 										    // otherwise this takes over.
	float depth  = pc[0] * pow(10, pc[1]);
	depth       *= pow(apparent,3);

	//float modifiedSpriteSize = spriteSize + (depth*weight); 
	float modifiedSpriteSize = 0.0005 + (depth*weight); 
	
	// EMIT QUAD
	for(int i = 0; i < 4; i++){
		vec4 p1     = P;                 
		p1.xy      += modifiedSpriteSize *(corners[i] - vec2(0.5)); 
		vs_position = p1;
		gl_Position = projection * p1;  
		// gl_Position = z_normalization(projection * p1);
		texCoord    = corners[i];                           
	  EmitVertex();
	}
	EndPrimitive();
}