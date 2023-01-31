#version 450

out gl_PerVertex {
	vec4 gl_Position;
};

layout(location = 0) out vec3 outColor;


vec3 colors[3] = vec3[](
	vec3(0.0, 1.0, 0.0),
	vec3(0.0, 0.0, 1.0),
	vec3(1.0, 0.0, 0.0)
);


void main()
{
	float alpha = radians(gl_VertexIndex*120 + gl_InstanceIndex*6);
	gl_Position = vec4(0.5*sin(alpha), -0.5*cos(alpha), 0.0, 1.0);
	outColor = colors[gl_VertexIndex];
}
