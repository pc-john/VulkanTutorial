#version 450

// push constants
layout(push_constant) uniform pushConstants {
	layout(offset=0) vec4 juliaCoords;
	layout(offset=16) int viewPlane;
	layout(offset=24) vec2 constantParameter;
};

// input from vertex shader
layout(location = 0) in vec2 inInitialValue;

// output
layout(location = 0) out vec4 outColor;

// constants
const int maxIter = 255;


void main()
{
	// initialize z and c complex numbers
	vec2 z, c;
	if(viewPlane == 0) {
		z = constantParameter;
		c = inInitialValue;
	}
	else {
		z = inInitialValue;
		c = constantParameter;
	}

	// iterate z = z^2 + c
	int i = 0;
	for(; i<maxIter; i++) {
		z = vec2(z.x*z.x - z.y*z.y, 2.*z.x*z.y) + c;
		if(z.x*z.x + z.y*z.y >= 4.0)
			break;
	}

	// assign color
	float l = float(i) / maxIter;
	outColor = vec4(l);
}
