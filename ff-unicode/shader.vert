#version 450

// push constants
layout(push_constant) uniform pushConstants {
	layout(offset=0) vec4 juliaCoords;
	layout(offset=16) int viewPlane;
	layout(offset=24) vec2 constantParameter;
};

// output variables
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
	outInitialValue.x = juliaCoords[gl_VertexIndex & 0x02];
	outInitialValue.y = juliaCoords[1 + ((gl_VertexIndex & 0x01)<<1)];
}
