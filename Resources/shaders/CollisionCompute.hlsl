// CollisionCompute.hlsl

struct VertexData {
    float4 position;
    float2 texcoord;
    float3 normal;
    float4 boneWeights;
    uint4 boneIndices;
};

// CBV
cbuffer CBCollision : register(b0) {
    row_major float4x4 g_WorldA;
    row_major float4x4 g_WorldB;
    uint g_NumTrianglesA;
    uint g_NumTrianglesB;
    uint g_ResultIndex;
    uint pad1;
};

// SRV (Root Descriptors)
StructuredBuffer<VertexData> g_VerticesA : register(t0);
StructuredBuffer<uint>       g_IndicesA  : register(t1);
StructuredBuffer<VertexData> g_VerticesB : register(t2);
StructuredBuffer<uint>       g_IndicesB  : register(t3);

// UAV
RWStructuredBuffer<uint> g_Result : register(u0); // result[0] > 0 means intersection

// AABB intersection (early out)
bool TestAABB(float3 minA, float3 maxA, float3 minB, float3 maxB) {
    if (maxA.x < minB.x || minA.x > maxB.x) return false;
    if (maxA.y < minB.y || minA.y > maxB.y) return false;
    if (maxA.z < minB.z || minA.z > maxB.z) return false;
    return true;
}

// -----------------------------------------------------------------------------
// Triangle-Triangle Intersection 
// Separating Axis Theorem (SAT) based algorithm
// -----------------------------------------------------------------------------
void Project(float3 points[3], float3 axis, out float min_val, out float max_val) {
    min_val = max_val = dot(points[0], axis);
    for (int i = 1; i < 3; ++i) {
        float val = dot(points[i], axis);
        if (val < min_val) min_val = val;
        if (val > max_val) max_val = val;
    }
}

bool TriTriIntersect(float3 V0[3], float3 V1[3]) {
    // 1. Edge vectors
    float3 E0[3] = { V0[1] - V0[0], V0[2] - V0[1], V0[0] - V0[2] };
    float3 E1[3] = { V1[1] - V1[0], V1[2] - V1[1], V1[0] - V1[2] };

    // 2. Face normals
    float3 N0 = normalize(cross(E0[0], E0[1]));
    float3 N1 = normalize(cross(E1[0], E1[1]));

    // Distances from planes
    float d0 = -dot(N0, V0[0]);
    float d1 = -dot(N1, V1[0]);

    // Test Face N0
    float min0, max0, min1, max1;
    Project(V1, N0, min1, max1);
    if (min1 > -d0 || max1 < -d0) return false;

    // Test Face N1
    Project(V0, N1, min0, max0);
    if (min0 > -d1 || max0 < -d1) return false;

    // Test Cross Products of edges (9 axes)
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            float3 axis = cross(E0[i], E1[j]);
            // If parallel, axis is close to 0 vector. We can skip or use small tolerance.
            if (dot(axis, axis) > 1e-6) {
                axis = normalize(axis);
                Project(V0, axis, min0, max0);
                Project(V1, axis, min1, max1);
                if (max0 < min1 || max1 < min0) return false;
            }
        }
    }

    return true;
}

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID) {
    uint triA = dtid.x;
    uint triB = dtid.y;

    if (g_Result[g_ResultIndex] > 0) return; // Already intersecting
    
    if (triA >= g_NumTrianglesA || triB >= g_NumTrianglesB) return;

    // Fetch Mesh A Triangle
    float3 v0A = g_VerticesA[g_IndicesA[triA * 3 + 0]].position.xyz;
    float3 v1A = g_VerticesA[g_IndicesA[triA * 3 + 1]].position.xyz;
    float3 v2A = g_VerticesA[g_IndicesA[triA * 3 + 2]].position.xyz;

    v0A = mul(float4(v0A, 1.0f), g_WorldA).xyz;
    v1A = mul(float4(v1A, 1.0f), g_WorldA).xyz;
    v2A = mul(float4(v2A, 1.0f), g_WorldA).xyz;

    // Fetch Mesh B Triangle
    float3 v0B = g_VerticesB[g_IndicesB[triB * 3 + 0]].position.xyz;
    float3 v1B = g_VerticesB[g_IndicesB[triB * 3 + 1]].position.xyz;
    float3 v2B = g_VerticesB[g_IndicesB[triB * 3 + 2]].position.xyz;

    v0B = mul(float4(v0B, 1.0f), g_WorldB).xyz;
    v1B = mul(float4(v1B, 1.0f), g_WorldB).xyz;
    v2B = mul(float4(v2B, 1.0f), g_WorldB).xyz;

    // Fast triangle AABB check
    float3 minA = min(v0A, min(v1A, v2A));
    float3 maxA = max(v0A, max(v1A, v2A));
    float3 minB = min(v0B, min(v1B, v2B));
    float3 maxB = max(v0B, max(v1B, v2B));

    if (!TestAABB(minA, maxA, minB, maxB)) return;

    float3 triAVerts[3] = { v0A, v1A, v2A };
    float3 triBVerts[3] = { v0B, v1B, v2B };

    if (TriTriIntersect(triAVerts, triBVerts)) {
        InterlockedAdd(g_Result[g_ResultIndex], 1);
    }
}
