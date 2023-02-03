#version 450

layout(location = 0) in vec2 initialValue;

layout(location = 0) out vec4 outColor;


void main()
{
	float a = initialValue.x;
	if(int(a*10) % 2 == 1)
		a = 0.;
	float b = initialValue.y;
	if(int(b*10) % 2 == 1)
		b = 0.;
	outColor = vec4(a, b, 0.0, 1.0);
}
