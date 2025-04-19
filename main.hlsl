
cbuffer constants : register(b0)
{
	row_major float4x4 view;
	row_major float4x4 projection;
	float3 lightvector;
	float3 lightcolor;
	float ambientStrength;
	float specularStrength;
	float specularPower;
}

struct VSInput
{
	float3 vertexPos : POS;
	float3 normal : NOR;
	float2 texcoord : TEX;
	float4 instancePos : INSTANCE_POS;
	float3 instanceColor : INSTANCE_COLOR;
};

struct VSOutput
{
	float4 position : SV_POSITION;
	float2 texcoord : TEX;
	float4 color : COL;
	float3 worldnormal : NORMAL;
};

Texture2D mytexture : register(t0);
SamplerState mysampler : register(s0);

VSOutput VS_Main(VSInput input)
{
	float4x4 model =
	{
		1, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 1, 0,
		input.instancePos.x, input.instancePos.y, input.instancePos.z, 1
    };
	
	float4 worldPos = mul(float4(input.vertexPos, 1.0f), model);
	float3 worldNormal = mul(input.normal, (float3x3) model);
	
	VSOutput output;
	output.position = mul(worldPos, mul(view, projection));
	output.texcoord = input.texcoord;
	output.worldnormal = worldNormal;
	
	float light = clamp(dot(normalize(worldNormal), normalize(-lightvector)), 0.0f, 1.0f) * 1.2f;
    output.color = float4(input.instanceColor * light, 1.0f);

	return output;
}

float4 PS_Main(VSOutput pixel) : SV_TARGET
{
	float light = clamp(dot(normalize(pixel.worldnormal), normalize(-lightvector)), 0.0f, 1.0f) * 0.8f + 0.6f;
	return mytexture.Sample(mysampler, pixel.texcoord) * pixel.color * light;
}