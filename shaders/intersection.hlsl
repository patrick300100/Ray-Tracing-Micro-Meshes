struct Attributes {
    float3 N; //normal
    float3 V; //view direction
};

struct Plane {
    float3 T, B, N; //tangent, bitangent, and normal
    float3 origin;

    //Projects point p onto the plane
    // @param p: the point to project onto the plane
    // @return: a float3 where the xy component are the plane positions, and z is the height to displace along the plane normal
    float3 projectOnto(float3 p) {
        float3 movedP = p - origin;

        return float3(dot(movedP, T), dot(movedP, B), dot(movedP, N));
    }

    //Unprojects a 2D point back to 3D
    // @param p: the position on the plane
    // @param h: the height to displace along the plane's normal
    float3 unproject(float2 p, float h) {
        return origin + p.x * T + p.y * B + h * N;
    }
};

struct TriangleData {
    uint3 vIndices;
    int nRows; //Number of micro vertices on the base edge of the triangle
    uint subDivLvl;
    int displacementOffset; //Offset into the displacement buffer from where displacements for this triangle starts
    int minMaxOffset;
};

//Vertex that is coming from the C++ code
struct InputVertex {
    float3 position;
    float3 direction;
};

struct Vertex2D {
    float2 position; //position on plane before displacing it
    float3 bc; //Barycentric coordinates
    uint2 coordinates; //local grid coordinates
};

struct Ray2D {
    float2 origin;
    float2 direction;

    float2 on(float t) {
        return origin + t * direction;
    }

    //Computes the height from a point on this ray to its corresponding point on the 3D ray
    float heightTo3DRay(float t2d, Plane p) {
        float3 D = WorldRayDirection();

        float3 D_plane = D - dot(D, p.N) * p.N;
        float lenPlane = length(D_plane);
        float t3 = t2d / lenPlane;

        float3 P3D = WorldRayOrigin() + t3 * D;

        float2 hit2D = on(t2d);
        float3 P_plane = p.origin + hit2D.x * p.T + hit2D.y * p.B;

        return dot(P3D - P_plane, p.N);
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

    float entryT; //Ray parameter `t` where it enters the triangle
    uint hierarchicalIndex;
};

struct StackElement {
    Triangle2D tri;
    int level;
};

StructuredBuffer<InputVertex> vertices : register(t1);
StructuredBuffer<TriangleData> triangleData : register(t2);
StructuredBuffer<float> displacementScales : register(t3); //Contains a scalar of how much to displace along the displacement direction
StructuredBuffer<float2> minMaxDisplacements : register(t4); //Contains (hierarchically) min and max displacements. x component is min displacement, y component is max displacement
StructuredBuffer<float> deltas : register(t5);

static const int MAX_STACK_DEPTH = 256; //Should be enough
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

//Gets the scale by how much to displace a micro-vertex along its direction.
// @param triangular grid coordinates of the vertex
// @param the offset into the buffer
// @return a float that represents by how much to displace a micro-vertex along its direction.
float getDisplacementScale(float2 coords, int dOffset) {
    int sum = (coords.x * (coords.x + 1)) / 2; //Sum from 1 until coords.x (closed formula of summation)
    int index = sum + coords.y;

    return displacementScales[dOffset + index];
}

struct Edge {
	Vertex2D start;
    Vertex2D end;

    Vertex2D middle(int dOffset, out bool present) {
        float2 newCoords = (start.coordinates + end.coordinates) * 0.5;
        Vertex2D v = {(start.position + end.position) * 0.5, (start.bc + end.bc) * 0.5, newCoords};

        if(getDisplacementScale(newCoords, dOffset) == -1) present = false;
        else present = true;

        return v;
    }
};

//Computes the intersection point of 2 lines
//https://en.wikipedia.org/wiki/Line-line_intersection#Given_two_points_on_each_line
float2 intersect(float2 p1, float2 p2, float2 p3, float2 p4) {
    float val1 = p1.x * p2.y - p1.y * p2.x;
    float val2 = p3.x * p4.y - p3.y * p4.x;
    float denom = (p1.x - p2.x) * (p3.y - p4.y) - (p1.y - p2.y) * (p3.x - p4.x);

    float px = (val1 * (p3.x - p4.x) - (p1.x - p2.x) * val2) / denom;
    float py = (val1 * (p3.y - p4.y) - (p1.y - p2.y) * val2) / denom;

    return float2(px, py);
}

//Expands a triangle by moving all edges a distance outwards. The intersection points of the expanded edges are the vertices of the expanded triangle.
// @param verts: the vertex positions of the triangle
// @param s: the scale by how much to expand the edges. This scalar should be applied to the interpolated direction vector
// @return the vertex positions of the expanded triangle
float3x2 expandTriangle(float3x2 verts, float s) {
    float3x2 newVerts;
    uint2 indices[3] = {uint2(0, 1), uint2(1, 2), uint2(2, 0)};

    float2 ods[3]; //Outward directions
    [unroll] for(int i = 0; i < 3; i++) {
        float2 start = verts[indices[i].x]; //Start point of edge
        float2 end = verts[indices[i].y]; //End point of edge

        float dx = end.x - start.x;
        float dy = end.y - start.y;

        float2 outwardDirection = normalize(float2(dy, -dx));
        ods[i] = s * outwardDirection;
    }

    newVerts[0] = intersect(verts[0] + ods[0], verts[1] + ods[0], verts[2] + ods[2], verts[0] + ods[2]);
    newVerts[1] = intersect(verts[0] + ods[0], verts[1] + ods[0], verts[1] + ods[1], verts[2] + ods[1]);
    newVerts[2] = intersect(verts[1] + ods[1], verts[2] + ods[1], verts[2] + ods[2], verts[0] + ods[2]);

    return newVerts;
}

//Computes the displacement vector of a micro-vertex.
// @param v: the micro-vertex
// @param directions: the directions of the base vertices
// @param dOffset: the displacement offset into the displacement buffer
// @return the displacement vector of the micro-vertex.
float3 computeDisplacement(Vertex2D v, float3 directions[3], int dOffset) {
    float3 interpolDir = v.bc.x * directions[0] + v.bc.y * directions[1] + v.bc.z * directions[2];
    float disScale = getDisplacementScale(v.coordinates, dOffset);

    return disScale * interpolDir;
}

//Creates a displaced triangle by moving the undisplaced vertex positions on the plane.
//This is equivalent to unprojecting the vertices to 3D space, applying displacements, and projecting them back to the plane.
// @param triVerts: the vertices of the triangle
// @param directions: the displacement directions of the base triangle
// @param dOffset: the displacement offset into the displacement buffer
// @param p: the triangle's plane
// @return the displaced vertex positions on the plane
float3x2 createDisplacedTriangle(Vertex2D triVerts[3], float3 directions[3], int dOffset, Plane p) {
    float3x2 displacedVerts;

    [unroll] for(int i = 0; i < 3; i++) {
        float3 displacement = computeDisplacement(triVerts[i], directions, dOffset);
        displacedVerts[i] = triVerts[i].position + float2(dot(displacement, p.T), dot(displacement, p.B));
    }

    return displacedVerts;
}

bool rayIntersectsEdge(Ray2D ray, float2 start, float2 end, inout float t) {
	float2 val1 = ray.origin - start;
    float2 val2 = end - start;
    float2 val3 = float2(-ray.direction.y, ray.direction.x);

    float denom = dot(val2, val3);

    if(abs(denom) < 1e-6f) return false; //ray and edge are parallel; no intersection

    float t1 = determinant(float2x2(val2, val1)) / denom;
    float t2 = dot(val1, val3) / denom;

    bool intersect = (t1 >= 0) && (t2 >= 0) && (t2 <= 1);

    if(intersect) {
        t = t1;
        return true;
    } else return false;
}

//Sorts part of the stack in decreasing order. Since a stack is FIFO, we pop the triangles with the smallest `entryT` first.
// @param stack: the stack
// @param startIndex: the start index from where to sort
// @param count: how many elements to sort, starting from startIndex
void sort(inout StackElement stack[MAX_STACK_DEPTH], int startIndex, int count) {
    if (count <= 1) return; // Trivially sorted

    for(int i = 0; i < count - 1; i++) {
        for(int j = startIndex; j < startIndex + count - 1 - i; j++) {
            StackElement sej = stack[j];
            StackElement sejPlusOne = stack[j + 1];

            if(sej.tri.entryT < sejPlusOne.tri.entryT) {
                stack[j] = sejPlusOne;
                stack[j + 1] = sej;
            }
        }
    }
}

//Checks if a ray intersects a triangle
// @param vertices: the vertex positions of the triangle
// @param ray: the ray in 2D
// @param ts: an 'array' of t's (one for each edge) that will hold the ray parameter t in case the ray hits the edge
// @return true if the triangle was hit, false if not. It will also return the intersection point in terms of the ray parameter `t` for each edge that was hit
bool rayIntersectTriangle(float3x2 vertices, Ray2D ray, inout float3 ts) {
    bool intersect1 = rayIntersectsEdge(ray, vertices[0], vertices[1], ts[0]);
    bool intersect2 = rayIntersectsEdge(ray, vertices[1], vertices[2], ts[1]);
    bool intersect3 = rayIntersectsEdge(ray, vertices[2], vertices[0], ts[2]);

    return intersect1 || intersect2 || intersect3;
}

bool isOutsideDisplacementRegion(float3 ts, Plane p, Ray2D ray, float2 minMaxDispl) {
    float entryT = min(ts[0] < 0 ? MAX_T : ts[0], min(ts[1] < 0 ? MAX_T : ts[1], ts[2] < 0 ? MAX_T : ts[2]));
    float exitT = max(ts[0], max(ts[1], ts[2]));

    //If we have only 1 intersection point we can not reliably determine if the 3D ray crosses the displacement region.
    //So we return that it crosses it, even if it might not be the case.
    if(abs(entryT - exitT) < 0.0001f) return false;

    float heightEntry = ray.heightTo3DRay(entryT, p);
    float heightExit = ray.heightTo3DRay(exitT, p);

    return (heightEntry < minMaxDispl.x && heightExit < minMaxDispl.x) || (heightEntry > minMaxDispl.y && heightExit > minMaxDispl.y);
}

//Given a triangle, we subdivide it one level and return the triangles that the ray crossed
// @param t: the triangle that should be tested for ray intersection
// @param ray: the ray in 2D
// @param dOffset: the offset into a buffer that gets data for this triangle
// @param subDivLvl: the subdivision level of the triangle
// @return an array of triangles that the ray crossed in order
void addIntersectedTriangles(Triangle2D t, Ray2D ray, int dOffset, int minMaxOffset, int level, Plane p, inout StackElement stack[MAX_STACK_DEPTH], inout int stackTop, float3 directions[3], int subDivLvl) {
    /*
     * We have our triangle t defined by vertices v0-v1-v2 and we are going to subdivide like so:
     *       v0
	 *      /   \
	 *     /     \
	 *   uv0-----uv2
	 *   / \    /  \
	 *  /   \  /    \
	 * v1----uv1----v2
     */
	Vertex2D v0 = t.vertices[0];
    Vertex2D v1 = t.vertices[1];
    Vertex2D v2 = t.vertices[2];

    Edge base0 = {v0, v1};
    Edge base1 = {v1, v2};
    Edge base2 = {v2, v0};

    bool uv0Present, uv1Present, uv2Present;
    Vertex2D uv0 = base0.middle(dOffset, uv0Present);
    Vertex2D uv1 = base1.middle(dOffset, uv1Present);
    Vertex2D uv2 = base2.middle(dOffset, uv2Present);

    /*
     * Compute indices for the buffer which holds the bounding triangles
     */
    int fourPower = 1U << (2 * (level + 1)); //This computes 4^(level+1)
    int firstLocalIndexNxtLvl = (fourPower - 1) / 3;

    int finalStartIndex = firstLocalIndexNxtLvl;
    int lvl = level;
    for(int i = 0; i < t.pathCount; i++) {
        int path = t.path[i];
        int unit = 1U << (2 * lvl);

        finalStartIndex += path * unit;

        lvl--;
    }

    int indxV0 = minMaxOffset + finalStartIndex;
    int indxV1 = minMaxOffset + finalStartIndex + 1;
    int indxV2 = minMaxOffset + finalStartIndex + 3;
    int indxCenter = minMaxOffset + finalStartIndex + 2;

    int oldStackTop = stackTop;

    /*
     * We're now going to check the 4 sub-triangles for ray intersection
     */
    int boundingTriIndices[4] = {indxV0, indxV1, indxV2, indxCenter};
    Vertex2D subTriV0[4] = {v0, uv0, uv2, uv0};
    Vertex2D subTriV1[4] = {uv0, v1, uv1, uv1};
    Vertex2D subTriV2[4] = {uv2, uv1, v2, uv2};
    int pathVals[4] = {0, 1, 3, 2};
    int subTriCount = uv0Present + uv1Present + uv2Present + 1;

    if(level + 1 == subDivLvl && subTriCount != 4) {
        if(uv0Present && !uv1Present && !uv2Present) {
            subTriV2[0] = v2;
            subTriV2[1] = v2;
        } else if(!uv0Present && uv1Present && !uv2Present) {
            subTriV1[0] = v1;
            subTriV2[0] = uv1;
            subTriV0[1] = v0;
            subTriV1[1] = uv1;
            subTriV2[1] = v2;
        } else if(!uv0Present && !uv1Present && uv2Present) {
            subTriV1[0] = v1;
            subTriV0[1] = v1;
            subTriV1[1] = v2;
            subTriV2[1] = uv2;
        } else if(uv0Present && !uv1Present && uv2Present) {
            subTriV2[1] = uv2;
            subTriV0[2] = v1;
            subTriV1[2] = v2;
            subTriV2[2] = uv2;
        } else if(uv0Present && uv1Present && !uv2Present) {
            subTriV2[0] = v2;
            subTriV0[2] = uv0;
        } else if(!uv0Present && uv1Present && uv2Present) {
            subTriV1[0] = v1;
            subTriV0[1] = v1;
            subTriV1[1] = uv1;
            subTriV2[1] = uv2;
        }
    }

    for(int i = 0; i < subTriCount; i++) {
        float3 ts = {-1, -1, -1};

        Vertex2D triVerts[3] = {subTriV0[i], subTriV1[i], subTriV2[i]};
        float3x2 boundingTriVerts;
        float3x2 vPositions = createDisplacedTriangle(triVerts, directions, dOffset, p);
        float2 minMaxDispl = float2(MAX_FLOAT, -MAX_FLOAT);
        if(level + 1 == subDivLvl) {
            boundingTriVerts = vPositions;

            [unroll] for(int j = 0; j < 3; j++) {
                float3 displacement = computeDisplacement(triVerts[j], directions, dOffset);
                float height = dot(displacement, p.N);

                minMaxDispl.x = min(minMaxDispl.x, height);
                minMaxDispl.y = max(minMaxDispl.y, height);
            }
        } else {
    		boundingTriVerts = expandTriangle(vPositions, deltas[boundingTriIndices[i]]);
            minMaxDispl = minMaxDisplacements[boundingTriIndices[i]];
        }

        if(rayIntersectTriangle(boundingTriVerts, ray, ts) && !isOutsideDisplacementRegion(ts, p, ray, minMaxDispl)) {
            float entryT = min(ts[0] < 0 ? MAX_T : ts[0], min(ts[1] < 0 ? MAX_T : ts[1], ts[2] < 0 ? MAX_T : ts[2]));

            Triangle2D newT = {{subTriV0[i], subTriV1[i], subTriV2[i]}, t.path, t.pathCount + 1, entryT, boundingTriIndices[i]};
            newT.path[t.pathCount] = pathVals[i];

            StackElement se = {newT, level + 1};
            stack[stackTop++] = se;
        }
    }

    sort(stack, oldStackTop, stackTop - oldStackTop);
}

bool rayTraceTriangle(float3 v0, float3 v1, float3 v2) {
    const float epsilon = 1e-3f; //Needed for small floating-point errors

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
// @param rootTri: the base triangle in 2D
// @param ray: the ray in 2D
// @param p: the plane of the triangle
// @param dOffset: offset into the displacement buffer for this triangle
// @param minMaxOffset: offset into the minmax buffer for this triangle
// @param subDivLvl: the subdivision level of the triangle
// @param directions: the directions of the 3 base vertices
void rayTraceMMTriangle(Triangle2D rootTri, Ray2D ray, Plane p, int dOffset, int minMaxOffset, int subDivLvl, float3 directions[3]) {
    //Creating and populating the stack
    StackElement stack[MAX_STACK_DEPTH];
    int stackTop = 0;

    StackElement se = {rootTri, 0};
    stack[stackTop++] = se;

    while(stackTop > 0) {
        StackElement current = stack[--stackTop];

        if(current.level == subDivLvl) { //Base case. Raytrace micro triangles directly
            float3 vs3D[3] = {
                p.unproject(current.tri.vertices[0].position, 0) + computeDisplacement(current.tri.vertices[0], directions, dOffset),
                p.unproject(current.tri.vertices[1].position, 0) + computeDisplacement(current.tri.vertices[1], directions, dOffset),
                p.unproject(current.tri.vertices[2].position, 0) + computeDisplacement(current.tri.vertices[2], directions, dOffset)
            };

            if(rayTraceTriangle(vs3D[0], vs3D[1], vs3D[2])) return; //Ray hits triangle, so we can stop searching
        } else {
            addIntersectedTriangles(current.tri, ray, dOffset, minMaxOffset, current.level, p, stack, stackTop, directions, subDivLvl);
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

    /*
     * Creation of plane
     */
    float3 e1 = v1.position - v0.position;
    float3 e2 = v2.position - v0.position;
    float3 N = normalize(cross(e1, e2)); // plane normal

    float3 T = normalize(e1);
    float3 B = normalize(cross(N, T));

    Plane p = {T, B, N, v0.position};

    /*
	 * Creation of 2D triangle
	 */
    Vertex2D v0Proj = {p.projectOnto(v0.position).xy, float3(1, 0, 0), v0GridCoordinate};
    Vertex2D v1Proj = {p.projectOnto(v1.position).xy, float3(0, 1, 0), v1GridCoordinate};
    Vertex2D v2Proj = {p.projectOnto(v2.position).xy, float3(0, 0, 1), v2GridCoordinate};

    int path[5];
    Triangle2D t = {{v0Proj, v1Proj, v2Proj}, path, 0, -1, tData.minMaxOffset};

    /*
     * Compute bounding triangle of base triangle
     */
    float3 directions[3] = {v0.direction, v1.direction, v2.direction};

    float3x2 vPositions = createDisplacedTriangle(t.vertices, directions, tData.displacementOffset, p);
    float3x2 boundingTriVerts = expandTriangle(vPositions, deltas[tData.minMaxOffset]);

    /*
 	 * Creation of 2D ray
	 */
    float3 O = WorldRayOrigin();
    float3 D = WorldRayDirection();

    float3 O_proj = O - dot(O - v0.position, p.N) * p.N;
    float3 D_proj = normalize(D - dot(D, p.N) * p.N);

    float2 rayOrigin2D = projectTo2D(O_proj, p.T, p.B, v0.position);
    float2 rayDir2D = normalize(float2(dot(D_proj, p.T), dot(D_proj, p.B)));
    Ray2D ray = {rayOrigin2D, rayDir2D};

    /*
     * Early opt-out
     */
    struct EdgeHitInfo {
        float2 start, end;
        float rayT;
        bool intersect;
    };

    EdgeHitInfo baseEdges[3] = {
        {boundingTriVerts[0], boundingTriVerts[1], -1, false},
        {boundingTriVerts[1], boundingTriVerts[2], -1, false},
        {boundingTriVerts[2], boundingTriVerts[0], -1, false}
    };

    [unroll] for(int i = 0; i < 3; i++) {
        EdgeHitInfo be = baseEdges[i];

        baseEdges[i].intersect = rayIntersectsEdge(ray, be.start, be.end, baseEdges[i].rayT);
    }

    if(!baseEdges[0].intersect && !baseEdges[1].intersect && !baseEdges[2].intersect) return;

    if(isOutsideDisplacementRegion(float3(baseEdges[0].rayT, baseEdges[1].rayT, baseEdges[2].rayT), p, ray, minMaxDisplacements[tData.minMaxOffset])) return;

	rayTraceMMTriangle(t, ray, p, tData.displacementOffset, tData.minMaxOffset, tData.subDivLvl, directions);
}
