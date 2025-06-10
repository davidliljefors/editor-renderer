#pragma once

#include <math.h>

#include "Core.h"

struct float2
{
	float x, y;
    float2() = default;

    float2(float x, float y) : x(x), y(y) 
    {}

    friend float2 operator+(float2 lhs, float2 rhs) 
    {
        return float2(lhs.x + rhs.x, lhs.y + rhs.y);
    }

    friend float2 operator-(float2 lhs, float2 rhs) 
    {
        return float2(lhs.x - rhs.x, lhs.y - rhs.y);
    }

    friend float2 operator*(float2 lhs, float2 rhs) 
    {
        return float2(lhs.x * rhs.x, lhs.y * rhs.y);
    }

    friend float2 operator/(float2 lhs, float2 rhs) 
    {
        return float2(lhs.x / rhs.x, lhs.y / rhs.y);
    }

    friend bool operator==(float2 lhs, float2 rhs) 
    {
        constexpr float epsilon = 0.0001f;
        return fabs(lhs.x - rhs.x) < epsilon && fabs(lhs.y - rhs.y) < epsilon;
    }

    friend bool operator!=(float2 lhs, float2 rhs) 
    {
        return !(lhs == rhs);
    }
};

struct float3
{
	float x, y, z;
};


struct alignas(16) float4
{
	float x, y, z, w;

    float3& xyz()
	{
        return *reinterpret_cast<float3*>(this);
    }
};

struct alignas(16) matrix
{
    matrix()
    {
        }
    matrix(float4 row1, float4 row2, float4 row3, float4 row4)
    {
        m.rows[0] = row1;
        m.rows[1] = row2;
        m.rows[2] = row3;
        m.rows[3] = row4;
    };

    matrix(const matrix&) = default;
    matrix(matrix&&) = default;
    matrix& operator=(const matrix&) = default;
    matrix& operator=(matrix&&) = default;

    union
    {
	    float4 rows[4];
        float data[4][4];
    }m;
};

inline matrix operator*(const matrix& m1, const matrix& m2)
{
    matrix result;
    
    for(int i = 0; i < 4; i++)
    {
        float4 col;
        col.x = m2.m.rows[0].x * m1.m.rows[i].x + m2.m.rows[1].x * m1.m.rows[i].y + m2.m.rows[2].x * m1.m.rows[i].z + m2.m.rows[3].x * m1.m.rows[i].w;
        col.y = m2.m.rows[0].y * m1.m.rows[i].x + m2.m.rows[1].y * m1.m.rows[i].y + m2.m.rows[2].y * m1.m.rows[i].z + m2.m.rows[3].y * m1.m.rows[i].w;
        col.z = m2.m.rows[0].z * m1.m.rows[i].x + m2.m.rows[1].z * m1.m.rows[i].y + m2.m.rows[2].z * m1.m.rows[i].z + m2.m.rows[3].z * m1.m.rows[i].w;
        col.w = m2.m.rows[0].w * m1.m.rows[i].x + m2.m.rows[1].w * m1.m.rows[i].y + m2.m.rows[2].w * m1.m.rows[i].z + m2.m.rows[3].w * m1.m.rows[i].w;
        result.m.rows[i] = col;
    }

    return result;
}

inline float clamp(float value, float min_val, float max_val) {
    if (value < min_val) return min_val;
    if (value > max_val) return max_val;
    return value;
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
            float4{xaxis.x, yaxis.x, zaxis.x, 0},
            float4{xaxis.y, yaxis.y, zaxis.y, 0}, 
            float4{xaxis.z, yaxis.z, zaxis.z, 0},
            float4{-(m_position.x * xaxis.x + m_position.y * xaxis.y + m_position.z * xaxis.z),
            -(m_position.x * yaxis.x + m_position.y * yaxis.y + m_position.z * yaxis.z),
            -(m_position.x * zaxis.x + m_position.y * zaxis.y + m_position.z * zaxis.z),
            1}
        };
    }
};