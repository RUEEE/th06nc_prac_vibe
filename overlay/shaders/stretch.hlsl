struct VertexOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VertexOutput VertexMain(uint vertexId : SV_VertexID)
{
    static const float2 positions[3] = {
        float2(-1.0, -1.0), float2(-1.0, 3.0), float2(3.0, -1.0)
    };
    VertexOutput output;
    output.position = float4(positions[vertexId], 0.0, 1.0);
    output.uv = float2((positions[vertexId].x + 1.0) * 0.5,
                       (1.0 - positions[vertexId].y) * 0.5);
    return output;
}

Texture2D gameTexture : register(t0);
SamplerState linearClamp : register(s0);

float4 PixelMain(VertexOutput input) : SV_Target
{
    // Keep the right edge fixed and display the rightmost 75% of the scene.
    input.uv.x = 0.25 + input.uv.x * 0.75;
    return gameTexture.Sample(linearClamp, input.uv);
}
