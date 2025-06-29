struct Attributes {
    float3 N; //normal
    float3 V; //view direction
};

struct Plane {
    float3 T, B, N; //tangent, bitangent, and normal
};

struct TriangleData {
    uint3 vIndices;
    int nRows; //Number of micro vertices on the base edge of the triangle
    int displacementOffset; //Offset into the displacement buffer from where displacements for this triangle starts
    Plane plane;
    int minMaxOffset;
};

//Vertex that is coming from the C++ code
struct InputVertex {
    float3 position;
};

struct Vertex2D {
    float2 position;
    float height; //How much to displace up along the plane normal
    int2 coordinates; //local grid coordinates
};

struct Ray2D {
    float2 origin;
    float2 direction;

    float2 on(float t) {
        return origin + t * direction;
    }

    //Computes the height from a point on this ray to its corresponding point on the 3D ray
    float heightTo3DRay(float t2d, Plane p, float3 v0) {
        float3 D = WorldRayDirection();

        float3 D_plane = D - dot(D, p.N) * p.N;
        float lenPlane = length(D_plane);
        float t3 = t2d / lenPlane;

        float3 P3D = WorldRayOrigin() + t3 * D;

        float2 hit2D = on(t2d);
        float3 P_plane = v0 + hit2D.x * p.T + hit2D.y * p.B;

        return dot(P3D - P_plane, p.N);
    }
};

struct Edge {
	Vertex2D start;
    Vertex2D end;

    float2 middleCoord() {
        return (start.coordinates + end.coordinates) * 0.5;
    }

    //Returns true if poit p lies on the left side of this edge, false when it is to the right or exactly on it
    bool isLeft(float2 p) {
        return !isRight(p);
    }

    //Returns true if point p lies on the right side of this edge (or is exactly on it)
    bool isRight(float2 p) {
        float2 SE = end.position - start.position;
        float2 SP = p - start.position;
        float cross = SE.x * SP.y - SE.y * SP.x;

        return cross <= 0;
    }
};

struct Triangle2D {
    Vertex2D vertices[3]; //3 vertices of triangle

    //Path into hierarchical subdivision. Entries can only be 0, 1, 2, and 3.
    //0 means that it entered the triangle close to v0
    //1 means that it entered the triangle close to v1
    //2 means it entered the center triangle
    //3 means it entered the triangle close to v2
    int path[5];
    int pathCount;
};

//Utility struct to represent the array of triangles that the ray crossed (in order)
//You can't directly return an array in HLSL from a function, so we wrap it in a struct
struct IntersectedTriangles {
    Triangle2D triangles[4]; //A maximum of 4 micro triangles can be crossed (exceptional when ray hits vertex, usually it's 3 or less)
    int count; //How many triangles actually got crossed
};

struct TriangleWithT {
    Triangle2D tri;
    float minT; //Ray parameter `t` where ray entered the triangle
};

cbuffer meshData : register(b1) {
    uint subDivLvl;
};

StructuredBuffer<InputVertex> vertices : register(t1);
StructuredBuffer<TriangleData> triangleData : register(t2);
StructuredBuffer<float3> positions2D : register(t3); //Contains 2D position in the xy entries, and height (to displace) in the z entry
StructuredBuffer<float2> minMaxDisplacements : register(t4); //Contains (hierarchically) min and max displacements. x component is min displacement, y component is max displacement
StructuredBuffer<float2[3]> prismPlaneCorners : register(t5);

static const float MAX_FLOAT = 3.402823466e+38f;
static const float MAX_T = 100000.0f; //Should coincide (or be higher) with the ray.TMax in ray generation

//Projects a position onto the plane defined by T and B
// @param p: the position to project onto the plane
// @param T: the tangent vector
// @param B: the bitangent vector
// @param v0Pos: the position of vertex0 (will act as the origin of the plane)
// @return: the position onto the plane, relative to the origin of the plane (v0Pos)
float2 projectTo2D(float3 p, float3 T, float3 B, float3 v0Pos) {
	float3 rel = p - v0Pos;

    return float2(dot(rel, T), dot(rel, B));
}

//Unprojects a 2D point back to 3D
float3 unproject(float2 p, float h, Plane plane, float3 v0Pos) {
    return v0Pos + p.x * plane.T + p.y * plane.B + h * plane.N;
}

//Gets the position of a (micro) vertex on the plane, as well as the height needed to displace it.
// @param triangular grid coordinates of the vertex
// @param the offset into the buffer
// @return a float3, where xy are the plane coordinates, and z is the height needed to displace
float3 getPlanePosition(float2 coords, int dOffset) {
    int sum = (coords.x * (coords.x + 1)) / 2; //Sum from 1 until coords.x (closed formula of summation)
    int index = sum + coords.y;

    return positions2D[dOffset + index];
}

//Utility function that creates a vertex from its plane position
// @param coords: the grid coordinates of the vertex
// @param dOffset: the offset into a buffer that gets data for this triangle (and thereby getting data for this (micro) veretx)
Vertex2D createVertexFromPlanePosition(float2 coords, int dOffset) {
    float3 planePosAndHeight = getPlanePosition(coords, dOffset);

    Vertex2D v = {planePosAndHeight.xy, planePosAndHeight.z, coords};
    return v;
}

bool rayIntersectsEdge(Ray2D ray, Edge e, inout float t) {
	float2 val1 = ray.origin - e.start.position;
    float2 val2 = e.end.position - e.start.position;
    float2 val3 = float2(-ray.direction.y, ray.direction.x);

    float denom = dot(val2, val3);

    if(abs(denom) < 1e-6) return false; //ray and edge are parallel; no intersection

    float t1 = determinant(float2x2(val2, val1)) / denom;
    float t2 = dot(val1, val3) / denom;

    bool intersect = (t1 >= 0) && (t2 >= 0) && (t2 <= 1);

    if(intersect) {
        t = t1;
        return true;
    } else return false;
}

//Sort triangles based on the entry point of the ray.
// @param ts: the triangles with the ray parameter t, which is the `t` at which the ray enters the triangle
// @param size: the number of triangles in the ts array
// @return sorted triangles, expressed in its indices.
void sort(TriangleWithT ts[4], int size, out int indices[4]) {
    // Initialize indices to 0..size-1
    for (int i = 0; i < size; i++) {
        indices[i] = i;
    }

    if(size <= 1) return; //Trivially sorted

    for(int i = 0; i < size - 1; i++) {
        for(int j = 0; j < size - 1 - i; j++) {
            if(ts[indices[j]].minT > ts[indices[j + 1]].minT) {
                int temp = indices[j];
                indices[j] = indices[j + 1];
                indices[j + 1] = temp;
            }
        }
    }
}

//Given an edge, a ray, and a point p, computes whether there is a ray passing between p and the edge.
// @return true if the ray passes between the point and the edge, false if not. This function also returns true if the ray exactly hits p
bool raySeparatesPointAndEdge(Edge e, Ray2D ray, float2 p) {
    float2 A = e.start.position;
    float2 B = e.end.position;
    float2 R0 = ray.origin;
    float2 Rd = ray.direction;

    float oP = (Rd.x * (p.y - R0.y) - Rd.y * (p.x - R0.x));
    float oA = (Rd.x * (A.y - R0.y) - Rd.y * (A.x - R0.x));
    float oB = (Rd.x * (B.y - R0.y) - Rd.y * (B.x - R0.x));

    return abs(oP) < 1e-4f || (oP * oA < 0) || (oP * oB < 0);
}

//Checks if a ray intersects a triangle
// @param vertices: the vertex positions of the triangle
// @param ray: the ray in 2D
// @param ts: an array of t's (one for each edge) that will hold the ray parameter t in case the ray hits the edge
// @return true if the triangle was hit, false if not. It will also return the intersection point in terms of the ray parameter `t` for each edge that was hit
bool rayIntersectTriangle(float2 vertices[3], Ray2D ray, inout float ts[3]) {
    Vertex2D v0 = {vertices[0], 0, float2(-1, -1)};
    Vertex2D v1 = {vertices[1], 0, float2(-1, -1)};
    Vertex2D v2 = {vertices[2], 0, float2(-1, -1)};

    Edge v0v1 = {v0, v1};
    Edge v1v2 = {v1, v2};
    Edge v2v0 = {v2, v0};

    bool intersect1 = rayIntersectsEdge(ray, v0v1, ts[0]);
    bool intersect2 = rayIntersectsEdge(ray, v1v2, ts[1]);
    bool intersect3 = rayIntersectsEdge(ray, v2v0, ts[2]);

    return intersect1 || intersect2 || intersect3;
}

//Given a triangle, we subdivide it one level and return the triangles that the ray crossed
// @param t: the triangle that should be tested for ray intersection
// @param ray: the ray in 2D
// @param dOffset: the offset into a buffer that gets data for this triangle
// @return an array of triangles that the ray crossed in order
IntersectedTriangles getIntersectedTriangles(Triangle2D t, Ray2D ray, int dOffset, int minMaxOffset, int level) {
    /*
     * We have our triangle t defined by vertices v0-v1-v2 and we are going to subdivide like so:
     *       v2
	 *      /   \
	 *     /     \
	 *   uv2-----uv1
	 *   / \    /  \
	 *  /   \  /    \
	 * v0----uv0----v1
     */
	Vertex2D v0Displaced = t.vertices[0];
    Vertex2D v1Displaced = t.vertices[1];
    Vertex2D v2Displaced = t.vertices[2];

    Edge base0 = {v0Displaced, v1Displaced};
    Edge base1 = {v1Displaced, v2Displaced};
    Edge base2 = {v2Displaced, v0Displaced};

    Vertex2D uv0Displaced = createVertexFromPlanePosition(base0.middleCoord(), dOffset);
    Vertex2D uv1Displaced = createVertexFromPlanePosition(base1.middleCoord(), dOffset);
    Vertex2D uv2Displaced = createVertexFromPlanePosition(base2.middleCoord(), dOffset);

    int fourPower = 1U << (2 * (level + 1)); //This computes 4^(level+1)
    int firstLocalIndexNxtLvl = (fourPower - 1) / 3;

    int finalStartIndex = firstLocalIndexNxtLvl;
    int lvl = level;
    for(int i = 0; i < t.pathCount; i++) {
        int p = t.path[i];
        int unit = 1U << (2 * lvl);

        finalStartIndex += p * unit;

        lvl--;
    }

    int indxV0 = minMaxOffset + finalStartIndex;
    int indxV1 = minMaxOffset + finalStartIndex + 1;
    int indxV2 = minMaxOffset + finalStartIndex + 3;
    int indxCenter = minMaxOffset + finalStartIndex + 2;

    TriangleWithT trianglesWithT[4];
    int tWTCounter = 0;

    float ts[3] = {MAX_FLOAT, MAX_FLOAT, MAX_FLOAT};
    if(rayIntersectTriangle(prismPlaneCorners[indxV0], ray, ts)) {
        Triangle2D tD = {{v0Displaced, uv0Displaced, uv2Displaced}, t.path, t.pathCount + 1};
        tD.path[t.pathCount] = 0;

        trianglesWithT[tWTCounter].tri = tD;
        trianglesWithT[tWTCounter].minT = min(ts[0], min(ts[1], ts[2]));
        tWTCounter++;
    }

    ts[0] = MAX_FLOAT; ts[1] = MAX_FLOAT; ts[2] = MAX_FLOAT;
    if(rayIntersectTriangle(prismPlaneCorners[indxV1], ray, ts)) {
        Triangle2D tD = {{uv0Displaced, v1Displaced, uv1Displaced}, t.path, t.pathCount + 1};
        tD.path[t.pathCount] = 1;

        trianglesWithT[tWTCounter].tri = tD;
        trianglesWithT[tWTCounter].minT = min(ts[0], min(ts[1], ts[2]));
        tWTCounter++;
    }

    ts[0] = MAX_FLOAT; ts[1] = MAX_FLOAT; ts[2] = MAX_FLOAT;
    if(rayIntersectTriangle(prismPlaneCorners[indxV2], ray, ts)) {
        Triangle2D tD = {{uv2Displaced, uv1Displaced, v2Displaced}, t.path, t.pathCount + 1};
        tD.path[t.pathCount] = 3;

        trianglesWithT[tWTCounter].tri = tD;
        trianglesWithT[tWTCounter].minT = min(ts[0], min(ts[1], ts[2]));
        tWTCounter++;
    }

    ts[0] = MAX_FLOAT; ts[1] = MAX_FLOAT; ts[2] = MAX_FLOAT;
    if(rayIntersectTriangle(prismPlaneCorners[indxCenter], ray, ts)) {
        Triangle2D tD = {{uv0Displaced, uv1Displaced, uv2Displaced}, t.path, t.pathCount + 1};
        tD.path[t.pathCount] = 2;

        trianglesWithT[tWTCounter].tri = tD;
        trianglesWithT[tWTCounter].minT = min(ts[0], min(ts[1], ts[2]));
        tWTCounter++;
    }

    IntersectedTriangles its;
    its.count = tWTCounter;

    int sortedIndices[4];
    sort(trianglesWithT, tWTCounter, sortedIndices);

    for(int i = 0; i < tWTCounter; i++) {
        its.triangles[i] = trianglesWithT[sortedIndices[i]].tri;
    }

    return its;
}

bool rayTraceTriangle(float3 v0, float3 v1, float3 v2) {
    const float epsilon = 1e-4f; //Needed for small floating-point errors

    float3 origin = WorldRayOrigin();
    float3 dir = WorldRayDirection();

    float3 edge1 = v1 - v0;
    float3 edge2 = v2 - v0;

    float3 pvec = cross(dir, edge2);
    float det = dot(edge1, pvec);
    if(abs(det) < 1e-8f) return false;

    float invDet = 1.0 / det;
    float3 tvec = origin - v0;
    float u = dot(tvec, pvec) * invDet;
    if(u < -epsilon || u > 1.0f + epsilon) return false;

    float3 qvec = cross(tvec, edge1);
    float v = dot(dir, qvec) * invDet;
    if(v < -epsilon || u + v > 1.0f + epsilon) return false;

    float t = dot(edge2, qvec) * invDet;

    //Compute attributes needed for lighting calculation
    Attributes attr;
    attr.N = normalize(cross(edge1, edge2));
    attr.V = -dir;

    return ReportHit(t, 0, attr);
}

// Ray trace a micro mesh triangle (a triangle which can be subdivided).
// The idea was to use recursion, but recursion is now allowed in shaders.
// So we simulate recursion by converting it to a loop-based approach and manually creating a call stack.
// @param rootTri: the displaced and undisplaced triangle
// @param ray: the ray in 2D
// @param p: the plane of the triangle
// @param v0Pos: position of vertex 0
// @param dOffset: offset into the displacement buffer for this triangle
void rayTraceMMTriangle(Triangle2D rootTri, Ray2D ray, Plane p, float3 v0Pos, int dOffset, int minMaxOffset) {
    struct StackElement {
        Triangle2D tri;
        int level;
	};

    //Creating and populating the stack
    const int MAX_STACK_DEPTH = 256; //Should be enough
    StackElement stack[MAX_STACK_DEPTH];
    int stackTop = 0;

    StackElement se = { rootTri, 0 };
    stack[stackTop++] = se;

    while(stackTop > 0) {
        StackElement current = stack[--stackTop];

        if(current.level == subDivLvl) { //Base case. Raytrace micro triangles directly
            Vertex2D v0Displaced = current.tri.vertices[0];
            Vertex2D v1Displaced = current.tri.vertices[1];
            Vertex2D v2Displaced = current.tri.vertices[2];

            InputVertex vs3D[3] = {
                unproject(v0Displaced.position, v0Displaced.height, p, v0Pos),
                unproject(v1Displaced.position, v1Displaced.height, p, v0Pos),
                unproject(v2Displaced.position, v2Displaced.height, p, v0Pos)
            };

            if(rayTraceTriangle(vs3D[0].position, vs3D[1].position, vs3D[2].position)) return; //Ray hits triangle, so we can stop searching
        } else {
            IntersectedTriangles its = getIntersectedTriangles(current.tri, ray, dOffset, minMaxOffset, current.level);

            //Push intersected triangles in reverse order to maintain correct processing order (triangles should be processed in the order the ray hits them)
            //TODO switching data structure to a queue would make more sense...
            for(int i = its.count - 1; i >= 0; i--) {
                Triangle2D nextTri = its.triangles[i];

                StackElement se = { nextTri, current.level + 1 };
                stack[stackTop++] = se;
            }
        }
    }
}

[shader("intersection")]
void main() {
    TriangleData tData = triangleData[PrimitiveIndex()];

    InputVertex v0 = vertices[tData.vIndices.x];
    InputVertex v1 = vertices[tData.vIndices.y];
    InputVertex v2 = vertices[tData.vIndices.z];
    float2 v0GridCoordinate = float2(0, 0);
    float2 v1GridCoordinate = float2(tData.nRows - 1, 0);
    float2 v2GridCoordinate = float2(tData.nRows - 1, tData.nRows - 1);

    float2 prismCorners[3] = prismPlaneCorners[tData.minMaxOffset]; //Prism corners

    Vertex2D prismCornerV0 = {prismCorners[0], 0, v0GridCoordinate};
    Vertex2D prismCornerV1 = {prismCorners[1], 0, v1GridCoordinate};
    Vertex2D prismCornerV2 = {prismCorners[2], 0, v2GridCoordinate};

    /*
	 * Creation of 2D triangle where vertices are displaced
	 */
    Plane p = tData.plane;

    Vertex2D v0Proj = createVertexFromPlanePosition(v0GridCoordinate, tData.displacementOffset);
    Vertex2D v1Proj = createVertexFromPlanePosition(v1GridCoordinate, tData.displacementOffset);
    Vertex2D v2Proj = createVertexFromPlanePosition(v2GridCoordinate, tData.displacementOffset);

    int path[5];
    Triangle2D t = {{v0Proj, v1Proj, v2Proj}, path, 0};

    /*
 	 * Creation of 2D ray
	 */
    float3 O = WorldRayOrigin();
    float3 D = WorldRayDirection();

    float3 O_proj = O - dot(O - v0.position, p.N) * p.N;
    float3 D_proj = normalize(D - dot(D, p.N) * p.N);

    float2 rayOrigin2D = projectTo2D(O_proj, p.T, p.B, v0.position);
    float2 rayDir2D = normalize(projectTo2D(O_proj + D_proj, p.T, p.B, v0.position) - rayOrigin2D);
    Ray2D ray = {rayOrigin2D, rayDir2D};

    /*
     * Early opt-out
     */
    struct EdgeHitInfo {
        Edge e;
        float rayT;
        bool intersect;
    };

    EdgeHitInfo baseEdges[3] = {
        {{prismCornerV0, prismCornerV1}, -1, false},
        {{prismCornerV1, prismCornerV2}, -1, false},
        {{prismCornerV2, prismCornerV0}, -1, false}
    };

    [unroll] for(int i = 0; i < 3; i++) {
        EdgeHitInfo be = baseEdges[i];

        baseEdges[i].intersect = rayIntersectsEdge(ray, be.e, baseEdges[i].rayT);
    }

    if(!baseEdges[0].intersect && !baseEdges[1].intersect && !baseEdges[2].intersect) return;

    float entryT = min(baseEdges[0].rayT < 0 ? MAX_T : baseEdges[0].rayT, min(baseEdges[1].rayT < 0 ? MAX_T : baseEdges[1].rayT, baseEdges[2].rayT < 0 ? MAX_T : baseEdges[2].rayT));
    float exitT = max(baseEdges[0].rayT, max(baseEdges[1].rayT, baseEdges[2].rayT));

    if(abs(entryT - exitT) < 0.0001f) entryT = 0; //If ray origin is inside triangle

    float heightEntry = ray.heightTo3DRay(entryT, p, v0.position);
    float exitEntry = ray.heightTo3DRay(exitT, p, v0.position);

    float2 minMaxDispl = minMaxDisplacements[tData.minMaxOffset];
    if((heightEntry < minMaxDispl.x && exitEntry < minMaxDispl.x) || (heightEntry > minMaxDispl.y && exitEntry > minMaxDispl.y)) return;

	rayTraceMMTriangle(t, ray, p, v0.position, tData.displacementOffset, tData.minMaxOffset);
}
