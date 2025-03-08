#pragma once

#include <math.h>

using u64 = unsigned long long;
using u32 = unsigned int;

struct matrix
{
	float m[4][4];
};

struct float3
{
	float x, y, z;
};

struct float4
{
	float x, y, z, w;
};

inline matrix operator*(const matrix& m1, const matrix& m2)
{
	return {
		m1.m[0][0] * m2.m[0][0] + m1.m[0][1] * m2.m[1][0] + m1.m[0][2] * m2.m[2][0] + m1.m[0][3] * m2.m[3][0],
		m1.m[0][0] * m2.m[0][1] + m1.m[0][1] * m2.m[1][1] + m1.m[0][2] * m2.m[2][1] + m1.m[0][3] * m2.m[3][1],
		m1.m[0][0] * m2.m[0][2] + m1.m[0][1] * m2.m[1][2] + m1.m[0][2] * m2.m[2][2] + m1.m[0][3] * m2.m[3][2],
		m1.m[0][0] * m2.m[0][3] + m1.m[0][1] * m2.m[1][3] + m1.m[0][2] * m2.m[2][3] + m1.m[0][3] * m2.m[3][3],
		m1.m[1][0] * m2.m[0][0] + m1.m[1][1] * m2.m[1][0] + m1.m[1][2] * m2.m[2][0] + m1.m[1][3] * m2.m[3][0],
		m1.m[1][0] * m2.m[0][1] + m1.m[1][1] * m2.m[1][1] + m1.m[1][2] * m2.m[2][1] + m1.m[1][3] * m2.m[3][1],
		m1.m[1][0] * m2.m[0][2] + m1.m[1][1] * m2.m[1][2] + m1.m[1][2] * m2.m[2][2] + m1.m[1][3] * m2.m[3][2],
		m1.m[1][0] * m2.m[0][3] + m1.m[1][1] * m2.m[1][3] + m1.m[1][2] * m2.m[2][3] + m1.m[1][3] * m2.m[3][3],
		m1.m[2][0] * m2.m[0][0] + m1.m[2][1] * m2.m[1][0] + m1.m[2][2] * m2.m[2][0] + m1.m[2][3] * m2.m[3][0],
		m1.m[2][0] * m2.m[0][1] + m1.m[2][1] * m2.m[1][1] + m1.m[2][2] * m2.m[2][1] + m1.m[2][3] * m2.m[3][1],
		m1.m[2][0] * m2.m[0][2] + m1.m[2][1] * m2.m[1][2] + m1.m[2][2] * m2.m[2][2] + m1.m[2][3] * m2.m[3][2],
		m1.m[2][0] * m2.m[0][3] + m1.m[2][1] * m2.m[1][3] + m1.m[2][2] * m2.m[2][3] + m1.m[2][3] * m2.m[3][3],
		m1.m[3][0] * m2.m[0][0] + m1.m[3][1] * m2.m[1][0] + m1.m[3][2] * m2.m[2][0] + m1.m[3][3] * m2.m[3][0],
		m1.m[3][0] * m2.m[0][1] + m1.m[3][1] * m2.m[1][1] + m1.m[3][2] * m2.m[2][1] + m1.m[3][3] * m2.m[3][1],
		m1.m[3][0] * m2.m[0][2] + m1.m[3][1] * m2.m[1][2] + m1.m[3][2] * m2.m[2][2] + m1.m[3][3] * m2.m[3][2],
		m1.m[3][0] * m2.m[0][3] + m1.m[3][1] * m2.m[1][3] + m1.m[3][2] * m2.m[2][3] + m1.m[3][3] * m2.m[3][3],
	};
}

struct Camera
{
    float3 m_position;
    float m_yaw;
    float m_pitch;
    
    void update_movement(bool w, bool a, bool s, bool d, bool boost, float delta_time)
    {
        float speed = 15.0f * delta_time;

		if (boost)
		{
			speed *= 10.0f;
		}
        
        float3 forward = {
            sinf(m_yaw) * cosf(m_pitch),
            -sinf(m_pitch),
            cosf(m_yaw) * cosf(m_pitch)
        };
        
        float3 right = {
            cosf(m_yaw),
            0.0f,
            -sinf(m_yaw)
        };

        if (w) {
            m_position.x += forward.x * speed;
            m_position.y += forward.y * speed;
            m_position.z += forward.z * speed;
        }
        if (s) {
            m_position.x -= forward.x * speed;
            m_position.y -= forward.y * speed;
            m_position.z -= forward.z * speed;
        }
        if (d) {
            m_position.x += right.x * speed;
            m_position.z += right.z * speed;
        }
        if (a) {
            m_position.x -= right.x * speed;
            m_position.z -= right.z * speed;
        }
    }
    
    void update_rotation(float delta_x, float delta_y)
    {
        const float sensitivity = 0.001f;
        
        m_yaw += delta_x * sensitivity;
        m_pitch += -1.0f * delta_y * sensitivity;
        
        if(m_pitch > 1.55f) m_pitch = 1.55f;
        if(m_pitch < -1.55f) m_pitch = -1.55f;
    }
    
    matrix get_view_matrix()
    {
        float cosPitch = cosf(m_pitch);
        float sinPitch = sinf(m_pitch);
        float cosYaw = cosf(m_yaw);
        float sinYaw = sinf(m_yaw);
        
        float3 xaxis = { cosYaw, 0, -sinYaw };
        float3 yaxis = { sinYaw * sinPitch, cosPitch, cosYaw * sinPitch };
        float3 zaxis = { sinYaw * cosPitch, -sinPitch, cosPitch * cosYaw };
        
        return {
            xaxis.x, yaxis.x, zaxis.x, 0,
            xaxis.y, yaxis.y, zaxis.y, 0, 
            xaxis.z, yaxis.z, zaxis.z, 0,
            -(m_position.x * xaxis.x + m_position.y * xaxis.y + m_position.z * xaxis.z),
            -(m_position.x * yaxis.x + m_position.y * yaxis.y + m_position.z * yaxis.z),
            -(m_position.x * zaxis.x + m_position.y * zaxis.y + m_position.z * zaxis.z),
            1
        };
    }
};