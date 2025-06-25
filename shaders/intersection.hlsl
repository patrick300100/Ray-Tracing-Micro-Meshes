struct Attributes {
    float3 N; //normal
    float3 V; //view direction
};

struct TriangleData {
    uint3 vIndices;
    int nRows; //Number of micro vertices on the base edge of the triangle
    int displacementOffset; //Offset into the displacement buffer from where displacements for this triangle starts
};

struct Plane {
    float3 T, B, N; //tangent, bitangent, and normal
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

static const float MAX_FLOAT = 3.402823466e+38f;

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

//Given an edge, at most 2 triangles are touching it. On the edge, there will be micro vertices in subsequent subdisivion levels (called diverted vertices in this subdivision level).
//These diverted vertices may alter the triangle region in future subdivision levels.
//We check whether a ray hits a triangle in possible future subdivision levels by getting all vertices (of future subdivision levels) on the edge, and check whether the ray hits the neighbouring triangles
// @param e: the edge
// @param dOffset: the offset into the planePositions2D buffer
// @param ray: the ray
// @return two boolean variables, one whether the triangle on the left side of the edge was hit, and one for the right side
void checkNeighbourTriangles(Edge e, int dOffset, Ray2D ray, out bool intersectL, out bool intersectR) {
    int2 startCoord = min(e.start.coordinates, e.end.coordinates);
    int2 endCoord = max(e.start.coordinates, e.end.coordinates);

    if(startCoord.x == endCoord.x) {
        for(int i = startCoord.y + 1; i < endCoord.y; i++) {
            float2 planePos = getPlanePosition(float2(startCoord.x, i), dOffset).xy;
            bool separates = raySeparatesPointAndEdge(e, ray, planePos);

            if(separates && e.isRight(planePos)) intersectR = true;
            else if(separates && e.isLeft(planePos)) intersectL = true;
        }
    } else if(startCoord.y == endCoord.y) {
        for(int i = startCoord.x + 1; i < endCoord.x; i++) {
            float2 planePos = getPlanePosition(float2(i, startCoord.y), dOffset).xy;
            bool separates = raySeparatesPointAndEdge(e, ray, planePos);

            if(separates && e.isRight(planePos)) intersectR = true;
            else if(separates && e.isLeft(planePos)) intersectL = true;
        }
    } else {
        int increment = endCoord.x - startCoord.x;

        for(int i = 1; i < increment; i++) {
            float2 planePos = getPlanePosition(float2(startCoord.x + i, startCoord.y + 1), dOffset).xy;
            bool separates = raySeparatesPointAndEdge(e, ray, planePos);

            if(separates && e.isRight(planePos)) intersectR = true;
            else if(separates && e.isLeft(planePos)) intersectL = true;
        }
    }
}

bool approximatelyEqual(float2 a, float2 b) {
    return all(abs(a - b) < 1e-4f);
}

bool isTriangleAlreadyInList(Triangle2D t, float2 midPoints[4], int count) {
    float2 midPoint = (1.0f/3.0f) * t.vertices[0].position + (1.0f/3.0f) * t.vertices[1].position + (1.0f/3.0f) * t.vertices[2].position;

    for(int i = 0; i < count; i++) {
        if(approximatelyEqual(midPoint, midPoints[i])) return true;
    }

    return false;
}

//Given a triangle, we subdivide it one level and return the triangles that the ray crossed
// @param t: the triangle that should be tested for ray intersection
// @param ray: the ray in 2D
// @param dOffset: the offset into a buffer that gets data for this triangle
// @return an array of triangles that the ray crossed in order
IntersectedTriangles getIntersectedTriangles(Triangle2D t, Ray2D ray, int dOffset) {
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

    //Perform edge-crossing tests with all 9 edges
    float t0 = MAX_FLOAT;
    Edge e0Displaced = {v0Displaced, uv0Displaced};
    bool intersect0 = rayIntersectsEdge(ray, e0Displaced, t0);

    float t1 = MAX_FLOAT;
    Edge e1Displaced = {uv0Displaced, v1Displaced};
    bool intersect1 = rayIntersectsEdge(ray, e1Displaced, t1);

    float t2 = MAX_FLOAT;
    Edge e2Displaced = {v1Displaced, uv1Displaced};
    bool intersect2 = rayIntersectsEdge(ray, e2Displaced, t2);

    float t3 = MAX_FLOAT;
    Edge e3Displaced = {uv1Displaced, v2Displaced};
    bool intersect3 = rayIntersectsEdge(ray, e3Displaced, t3);

    float t4 = MAX_FLOAT;
    Edge e4Displaced = {v2Displaced, uv2Displaced};
    bool intersect4 = rayIntersectsEdge(ray, e4Displaced, t4);

    float t5 = MAX_FLOAT;
    Edge e5Displaced = {uv2Displaced, v0Displaced};
    bool intersect5 = rayIntersectsEdge(ray, e5Displaced, t5);

    float t6 = MAX_FLOAT;
    Edge e6Displaced = {uv0Displaced, uv1Displaced};
    bool intersect6 = rayIntersectsEdge(ray, e6Displaced, t6);

    float t7 = MAX_FLOAT;
    Edge e7Displaced = {uv1Displaced, uv2Displaced};
    bool intersect7 = rayIntersectsEdge(ray, e7Displaced, t7);

    float t8 = MAX_FLOAT;
    Edge e8Displaced = {uv2Displaced, uv0Displaced};
    bool intersect8 = rayIntersectsEdge(ray, e8Displaced, t8);

    TriangleWithT trianglesWithT[4];
    int tWTCounter = 0;

    if(intersect0 || intersect5 || intersect8) {
        Triangle2D tD = {v0Displaced, uv0Displaced, uv2Displaced};

        trianglesWithT[tWTCounter].tri = tD;
        trianglesWithT[tWTCounter].minT = min(t0, min(t5, t8));
        tWTCounter++;
    }
    if(intersect1 || intersect2 || intersect6) {
        Triangle2D tD = {uv0Displaced, v1Displaced, uv1Displaced};

        trianglesWithT[tWTCounter].tri = tD;
        trianglesWithT[tWTCounter].minT = min(t1, min(t2, t6));
        tWTCounter++;
    }
    if(intersect3 || intersect4 || intersect7) {
        Triangle2D tD = {uv1Displaced, v2Displaced, uv2Displaced};

        trianglesWithT[tWTCounter].tri = tD;
        trianglesWithT[tWTCounter].minT = min(t3, min(t4, t7));
        tWTCounter++;
    }
    if(intersect6 || intersect7 || intersect8) {
        Triangle2D tD = {uv0Displaced, uv1Displaced, uv2Displaced};

        trianglesWithT[tWTCounter].tri = tD;
        trianglesWithT[tWTCounter].minT = min(t6, min(t7, t8));
        tWTCounter++;
    }

    IntersectedTriangles its;
    its.count = tWTCounter;

    int sortedIndices[4];
    sort(trianglesWithT, tWTCounter, sortedIndices);

    for(int i = 0; i < tWTCounter; i++) {
        its.triangles[i] = trianglesWithT[sortedIndices[i]].tri;
    }

    /*
     * In some edge cases it's possible that it misses the displaced triangle in this subdivision level, but it actually hits the displaced triangle in one of the next subdivision levels.
     * So we check whether it possibly hits it in one of the next subdivision levels (without explicitly subdividing into all subsequent levels!).
     */
    Edge allEdges[9] = {e0Displaced, e1Displaced, e2Displaced, e3Displaced, e4Displaced, e5Displaced, e6Displaced, e7Displaced, e8Displaced};

    bool iwdeR[9]; //intersect with edges that divert to the right
    bool iwdeL[3]; //intersect with edges that divert to the left (only for inner edges)
    [unroll] for(int i = 0; i < 9; i++) {
        Edge ou = allEdges[i];

		bool intersectL, intersectR;
        checkNeighbourTriangles(ou, dOffset, ray, intersectL, intersectR);

        iwdeR[i] = intersectR;
        if(i >= 6) iwdeL[i-6] = intersectL;
    }

    float2 midPoints[4];
    int mpCount = 0;
    for(mpCount = 0; mpCount < its.count; mpCount++) {
        Triangle2D t = its.triangles[mpCount];
        midPoints[mpCount] = (1.0f/3.0f) * t.vertices[0].position + (1.0f/3.0f) * t.vertices[1].position + (1.0f/3.0f) * t.vertices[2].position;
    }

    if(iwdeR[0] || iwdeR[5] || iwdeL[2]) {
        Triangle2D tD = {v0Displaced, uv0Displaced, uv2Displaced};

        if(!isTriangleAlreadyInList(tD, midPoints, mpCount)) {
            its.triangles[its.count] = tD;
        	its.count++;
        }
    }
    if(iwdeR[1] || iwdeR[2] || iwdeL[0]) {
        Triangle2D tD = {uv0Displaced, v1Displaced, uv1Displaced};

        if(!isTriangleAlreadyInList(tD, midPoints, mpCount)) {
            its.triangles[its.count] = tD;
        	its.count++;
        }
    }
    if(iwdeR[3] || iwdeR[4] || iwdeL[1]) {
        Triangle2D tD = {uv1Displaced, v2Displaced, uv2Displaced};

        if(!isTriangleAlreadyInList(tD, midPoints, mpCount)) {
            its.triangles[its.count] = tD;
        	its.count++;
        }
    }
    if(iwdeR[6] || iwdeR[7] || iwdeR[8]) {
        Triangle2D tD = {uv0Displaced, uv1Displaced, uv2Displaced};

        if(!isTriangleAlreadyInList(tD, midPoints, mpCount)) {
            its.triangles[its.count] = tD;
        	its.count++;
        }
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
    if(abs(det) < 1e-8) return false;

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
void rayTraceMMTriangle(Triangle2D rootTri, Ray2D ray, Plane p, float3 v0Pos, int dOffset) {
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
            IntersectedTriangles its = getIntersectedTriangles(current.tri, ray, dOffset);

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
    float2 v0GridCoordinate = float2(0,0);
    float2 v1GridCoordinate = float2(tData.nRows - 1, 0);
    float2 v2GridCoordinate = float2(tData.nRows - 1, tData.nRows - 1);

    /*
	 * Creation of 2D triangle where vertices are displaced
	 */
    float3 e1 = v1.position - v0.position;
    float3 e2 = v2.position - v0.position;
    float3 N = normalize(cross(e1, e2)); // plane normal

    float3 T = normalize(e1);
    float3 B = normalize(cross(N, T));

    Plane p = {T, B, N};

    Vertex2D v0Proj = createVertexFromPlanePosition(v0GridCoordinate, tData.displacementOffset);
    Vertex2D v1Proj = createVertexFromPlanePosition(v1GridCoordinate, tData.displacementOffset);
    Vertex2D v2Proj = createVertexFromPlanePosition(v2GridCoordinate, tData.displacementOffset);
    Triangle2D t = {v0Proj, v1Proj, v2Proj};

    /*
 	 * Creation of 2D ray
	 */
    float3 O = WorldRayOrigin();
    float3 D = WorldRayDirection();

    float3 O_proj = O - dot(O - v0.position, N) * N;
    float3 D_proj = normalize(D - dot(D, N) * N);

    float2 rayOrigin2D = projectTo2D(O_proj, T, B, v0.position);
    float2 rayDir2D = normalize(projectTo2D(O_proj + D_proj, T, B, v0.position) - rayOrigin2D);
    Ray2D ray = {rayOrigin2D, rayDir2D};

	rayTraceMMTriangle(t, ray, p, v0.position, tData.displacementOffset);
}
