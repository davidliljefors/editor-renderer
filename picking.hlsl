
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
	float3 position : POS;
    float4 instancePos : INSTANCE_POS;
    uint idHigh : IDHIGH;
    uint idLow : IDLOW;
};

struct VSOutput
{
	float4 position : SV_POSITION;
    uint idHigh : IDHIGH;
    uint idLow : IDLOW;
};

VSOutput VS_Picking(VSInput input)
{
	float4x4 model =
	{
		1, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 1, 0,
		input.instancePos.x, input.instancePos.y, input.instancePos.z, 1
    };
	
    float4 worldPos = mul(float4(input.position, 1.0f), model);
	
	VSOutput output;
	output.position = mul(worldPos, mul(view, projection));
    output.idHigh = input.idHigh;
    output.idLow = input.idLow;
	return output;
}

uint2 PS_Picking(VSOutput psInput) : SV_TARGET
{
    return uint2(psInput.idHigh, psInput.idLow);
}