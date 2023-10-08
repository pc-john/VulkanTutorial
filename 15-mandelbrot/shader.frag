#version 450

layout(location = 0) in vec2 inInitialValue;

layout(location = 0) out vec4 outColor;

const int maxIter = 255;


void main()
{
	// initialize z and c complex numbers
	vec2 z = vec2(0.0, 0.0);
	vec2 c = inInitialValue;

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
