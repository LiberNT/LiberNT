struct V { float4 position : SV_Position; float4 color : COLOR0; };
[outputtopology("triangle")]
[numthreads(1, 1, 1)]
void main(out vertices V verts[3], out indices uint3 tris[1])
{
    SetMeshOutputCounts(3, 1);
    verts[0].position = float4(-1.0, -1.0, 0.0, 1.0);
    verts[1].position = float4(-1.0, 3.0, 0.0, 1.0);
    verts[2].position = float4(3.0, -1.0, 0.0, 1.0);
    verts[0].color = verts[1].color = verts[2].color = float4(0.0, 1.0, 0.0, 1.0);
    tris[0] = uint3(0, 1, 2);
}
