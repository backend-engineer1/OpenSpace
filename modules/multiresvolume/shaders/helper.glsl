#define MULTIRES_PI      3.14159265358979323846  /* pi */
#define MULTIRES_SQRT1_3 0.57735026919           /* 1/sqrt(3) */
#define MULTIRES_OPACITY_THRESHOLD 0.01
vec3 multires_cartesianToSpherical(vec3 _cartesian) {
    // Put cartesian in [-1..1] range first
    vec3 cartesian = vec3(-1.0,-1.0,-1.0) + _cartesian * 2.0f;

    float r = length(cartesian);
    float theta, phi;

    if (r == 0.0) {
        theta = phi = 0.0;
    } else {
        theta = acos(cartesian.z/r) / MULTIRES_PI;
        phi = (MULTIRES_PI + atan(cartesian.y, cartesian.x)) / (2.0*MULTIRES_PI );
    }
    r *= MULTIRES_SQRT1_3;
    return vec3(r, theta, phi);
}

int multires_intCoord(ivec3 vec3Coords, ivec3 spaceDim) {
    return vec3Coords.x + spaceDim.x*vec3Coords.y + spaceDim.x*spaceDim.y*vec3Coords.z;
}

vec3 multires_vec3Coords(uint intCoord, ivec3 spaceDim) {
    vec3 coords = vec3(0.0);
    coords.x = mod(intCoord, spaceDim.x);
    coords.y = mod(intCoord / spaceDim.x, spaceDim.y);
    coords.z = intCoord / spaceDim.x / spaceDim.y;
    return coords;
}
