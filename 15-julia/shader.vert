#version 450

out gl_PerVertex {
	vec4 gl_Position;
};

layout(location = 0) out vec2 outInitialValue;


vec2 coords[4] = vec2[](
	vec2(-1.0,-1.0),
	vec2(-1.0, 1.0),
	vec2( 1.0,-1.0),
	vec2( 1.0, 1.0)
);


void main()
{
	gl_Position = vec4(coords[gl_VertexIndex], 0.0, 1.0);
	outInitialValue = coords[gl_VertexIndex].yx * 2.0;
}
