struct Attributes {
    float3 N; //normal
    float3 V; //view direction
};

struct AABB {
    float3 minPos;
    float3 maxPos;
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

    Vertex2D middle() {
        Vertex2D v;
        v.position = (start.position + end.position) * 0.5;
        v.coordinates = (start.coordinates + end.coordinates) * 0.5;

        return v;
    }

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

//Utility struct that stores an edge along with the intersection point
struct EdgeWithT {
    Edge e;
    float t; //Ray paramater
    Edge eU; //Edge from undisplaced vertices
};

struct Triangle2D {
    Vertex2D vertices[3]; //3 vertices of triangle

    float signedArea() {
        float2 p0 = vertices[0].position;
        float2 p1 = vertices[1].position;
        float2 p2 = vertices[2].position;

    	return (p1.x - p0.x) * (p2.y - p0.y) - (p2.x - p0.x) * (p1.y - p0.y);
	}

    //Fixes winding order (clockwise / counter-clockwise) of this triangle to be the same as the winding order of another triangle.
    // @param otherArea: the signed area of the other triangle
    void fixWindingOrder(float otherArea) {
        if(otherArea * signedArea() < 0.0) {
            Vertex2D temp = vertices[1];
            vertices[1] = vertices[2];
            vertices[2] = temp;
        }
    }
};

//A struct that contains 2 triangles: the triangle with displaced vertices and undisplaced vertices (in 2D)
struct TriangleDAUD {
    Triangle2D tDisplaced;
    Triangle2D tUndisplaced;
};

//Utility struct to represent the array of triangles that the ray crossed (in order)
//You can't directly return an array in HLSL from a function, so we wrap it in a struct
struct IntersectedTriangles {
    TriangleDAUD triangles[3]; //A maximum of 3 triangles can be crossed if you have a triangle with its next subdivision level
    int count; //How many triangles actually got crossed (might be less than 3)
};

//Utility struct to represent the array edges that the ray crossed (in order).
struct IntersectedEdges {
    //TODO maybe we can just store Edges instead of EdgeWithT's?
    EdgeWithT edges[4]; //Max 4 edges can be crossed
    int count; //How many edges actually got crossed (might be less than 4)
};

cbuffer meshData : register(b1) {
    uint subDivLvl;
};

StructuredBuffer<InputVertex> vertices : register(t1);
StructuredBuffer<AABB> AABBs : register(t2);
StructuredBuffer<float3> displacements : register(t3);
StructuredBuffer<float3> positions2D : register(t4);

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

//Projects a vertex onto the plane defined by T and B. In essence, this function is the same as
//the general projectTo2D function, but this function specifically returns a Vertex2D.
// @param p: the position of the vertex to project onto the plane
// @param T: the tangent vector
// @param B: the bitangent vector
// @param N: the plane normal
// @param v0Pos: the position of vertex0 (will act as the origin of the plane)
// @param coords: the coordinates that you want to give to the projected vertex for the triangular grid.
// @return: a 2D vertex
Vertex2D projectVertexTo2D(float3 p, float3 T, float3 B, float3 N, float3 v0Pos, float2 coords) {
    float3 rel = p - v0Pos;

    Vertex2D v = {float2(dot(rel, T), dot(rel, B)), dot(rel, N), coords};
    return v;
}

//Projects a direction vector onto the plane defined by T and B
// @param dir: the direction vector
// @param T: the tangent vector
// @param B: the bitangent vector
float2 projectDirTo2D(float3 dir, float3 T, float3 B) {
    return float2(dot(dir, T), dot(dir, B));
}

//Unprojects a 2D point back to 3D
float3 unproject(float2 p, float h, Plane plane, float3 v0Pos) {
    return v0Pos + p.x * plane.T + p.y * plane.B + h * plane.N;
}

//Gets the displacements of a vertex from the displacement buffer.
// @param triangular grid coordinates of the vertex
// @param the displacement-buffer offset
// @return: the corresponding displacement of the vertex
float3 getDisplacement(float2 coords, int dOffset) {
    int sum = (coords.x * (coords.x + 1)) / 2; //Sum from 1 until coords.x (closed formula of summation)
    int index = sum + coords.y;

    return displacements[dOffset + index];
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

//TODO maybe not necessary to return bool? Just return t value
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

//Sort the edges according to their ray parameter t.
//Some edges are not crossed. They have a 'default' t value of -1 and are discarded. So only the edges that actually got crossed are sorted
// @param edges the edges to sort
// @return an array of edges sorted by the order in which the ray crossed them
IntersectedEdges sort(inout EdgeWithT edges[9]) {
    EdgeWithT crossed[4];
    int crossedCount = 0;

    //First filter to crossed edges
    for(int i = 0; i < 9; i++) {
        if(edges[i].t > 0 && crossedCount < 4) crossed[crossedCount++] = edges[i];
    }

    //Sort the crossed edges
    for(int i = 0; i < crossedCount; i++) {
        for(int j = i + 1; j < crossedCount; j++) {
            if(crossed[j].t < crossed[i].t) {
                EdgeWithT tmp = crossed[i];
                crossed[i] = crossed[j];
                crossed[j] = tmp;
            }
        }
    }

    IntersectedEdges ies = {crossed, crossedCount};
    return ies;
}

//We are given 2 edges that are connected with each other. We can create a triangle from that
Triangle2D createTriangleFromEdges(Edge a, Edge b, float baseArea) {
    Vertex2D v0New, v1New, v2New;

    //TODO maybe switch to more robust float comparison
    if(all(a.start.position == b.start.position)) {
        v0New = a.end; v1New = a.start; v2New = b.end;
    } else if(all(a.start.position == b.end.position)) {
        v0New = a.start; v1New = a.end; v2New = b.start;
    } else if(all(a.end.position == b.start.position)) {
        v0New = a.start; v1New = a.end; v2New = b.end;
    } else { //if(a.end == b.end)
        v0New = a.end; v1New = a.start; v2New = b.start;
    }

    Triangle2D t = {v0New, v1New, v2New};
    t.fixWindingOrder(baseArea);

    return t;
}

//Checks in which micro-triangle point p lies (if p lies inside the triangle).
// @param p: the point p to check
// @param verticesD: the 6 (displaced) vertices in order: [v0, v1, v2, uv0, uv1, uv2]
// @param verticesU: the 6 (undisplaced) vertices in the same order as verticesD
// @param triD: the (displaced) triangle that point p lies in (only defined if p actually lies inside the triangle v0-v1-v2)
// @param triU: the same, but undisplaced triangle that point p lies in (only defined if p actually lies inside the triangle v0-v1-v2)
// @param es: the (displaced) edges [uv0-uv1, uv1-uv2, uv2-uv0], in that order
// @return true if p lies inside the triangle, false if not.
bool findSubTriangle(float2 p, Vertex2D verticesD[6], Vertex2D verticesU[6], out Triangle2D triD, out Triangle2D triU, float baseArea, Edge es[9]) {
    //if p is outside the triangle
    //TODO can be done with only 6 edge comparisons I think
	if((es[0].isRight(p) || es[8].isLeft(p) || es[5].isRight(p)) && (es[1].isRight(p) || es[2].isRight(p) || es[6].isLeft(p)) && (es[3].isRight(p) || es[4].isRight(p) || es[7].isLeft(p)) && (es[6].isRight(p) || es[7].isRight(p) || es[8].isRight(p))) return false;

    // Determine which of the 4 sub-triangles contains the point
    if(es[6].isRight(p)) {
        triD.vertices[0] = verticesD[1]; triU.vertices[0] = verticesU[1];
        triD.vertices[1] = verticesD[4]; triU.vertices[1] = verticesU[4];
        triD.vertices[2] = verticesD[3]; triU.vertices[2] = verticesU[3];
    } else if(es[7].isRight(p)) {
        triD.vertices[0] = verticesD[2]; triU.vertices[0] = verticesU[2];
        triD.vertices[1] = verticesD[5]; triU.vertices[1] = verticesU[5];
        triD.vertices[2] = verticesD[4]; triU.vertices[2] = verticesU[4];
    } else if(es[8].isRight(p)) {
        triD.vertices[0] = verticesD[0]; triU.vertices[0] = verticesU[0];
        triD.vertices[1] = verticesD[3]; triU.vertices[1] = verticesU[3];
        triD.vertices[2] = verticesD[5]; triU.vertices[2] = verticesU[5];
    } else {
        triD.vertices[0] = verticesD[3]; triU.vertices[0] = verticesU[3];
        triD.vertices[1] = verticesD[4]; triU.vertices[1] = verticesU[4];
        triD.vertices[2] = verticesD[5]; triU.vertices[2] = verticesU[5];
    }

    //TODO maybe not necessary? Vertices are maybe already in correct order?
    triD.fixWindingOrder(baseArea);
    triU.fixWindingOrder(baseArea);
    return true;
}

//Gives the distance from a point to an edge
// @param p: the point
// @param e: the edge
// @return the (shortest) distance from p to e
float distPointToEdge(float2 p, Edge e) {
    float2 a = e.start.position;
    float2 b = e.end.position;

    float2 ab = b - a;
    float2 ap = p - a;

    float t = saturate(dot(ap, ab) / dot(ab, ab));
    float2 closest = a + t * ab;
    return length(p - closest);
}

//Given an edge, returns the (displaced) position of the micro vertex that is most diverged.
//So if you have an edge, it might contain micro vertices of the next subdivision level. These micro vertices may also be displaced away from the edge.
//We return the micro verte that is the farthest away from the edge (if there is such micro vertex)
// @param e: the edge
// @param dOffset: the offset into the planePositions2D buffer
// @return the most diverged micro vertex from the given edge
Vertex2D getMostOuterVertex(Edge e, int dOffset, out float dist) {
    int2 startCoord = min(e.start.coordinates, e.end.coordinates);
    int2 endCoord = max(e.start.coordinates, e.end.coordinates);

    float2 maxOuterPos;
    float maxDistance = 0.0f;
    if(startCoord.x == endCoord.x) {
        for(int i = startCoord.y + 1; i < endCoord.y; i++) {
            float2 planePos = getPlanePosition(float2(startCoord.x, i), dOffset).xy;
            float dist = distPointToEdge(planePos, e);

            if(e.isRight(planePos) && dist > maxDistance) {
                maxDistance = dist;
                maxOuterPos = planePos;
            }
        }
    } else if(startCoord.y == endCoord.y) {
        for(int i = startCoord.x + 1; i < endCoord.x; i++) {
            float2 planePos = getPlanePosition(float2(i, startCoord.y), dOffset).xy;
            float dist = distPointToEdge(planePos, e);

            if(e.isRight(planePos) && dist > maxDistance) {
                maxDistance = dist;
                maxOuterPos = planePos;
            }
        }
    } else {
        int increment = endCoord.x - startCoord.x;

        for(int i = 1; i < increment; i++) {
            float2 planePos = getPlanePosition(float2(startCoord.x + i, startCoord.y + 1), dOffset).xy;
            float dist = distPointToEdge(planePos, e);

            if(e.isRight(planePos) && dist > maxDistance) {
                maxDistance = dist;
                maxOuterPos = planePos;
            }
        }
    }

    dist = maxDistance;
    Vertex2D v = {maxOuterPos, -1, float2(-1, -1)}; //We're only interested in the position, so fill vertex with dummy values
    return v;
}

//Given a triangle, we subdivide it one level and return the triangles that the ray crossed
// @param tDis: the displaced triangle
// @param tUndis: the undisplaced triangle
// @param ray: the ray in 2D
// @param sa: signed area of the base triangle. We use this to preserve vertex order (counter clockwise)
// @return an array of triangles that the ray crossed in order
IntersectedTriangles getIntersectedTriangles(Triangle2D tDis, Triangle2D tUndis, Ray2D ray, int dOffset, Plane p, float sa) {
    /*
     * We have our triangle defined by vertices v0-v1-v2 and we are going to subdivide like so:
     *       v2
	 *      /   \
	 *     /     \
	 *   uv2-----uv1
	 *   / \    /  \
	 *  /   \  /    \
	 * v0----uv0----v1
     */
	Vertex2D v0Displaced = tDis.vertices[0];
    Vertex2D v1Displaced = tDis.vertices[1];
    Vertex2D v2Displaced = tDis.vertices[2];

    Vertex2D v0Undisplaced = tUndis.vertices[0];
    Vertex2D v1Undisplaced = tUndis.vertices[1];
    Vertex2D v2Undisplaced = tUndis.vertices[2];

    Edge base0 = {v0Undisplaced, v1Undisplaced};
    Edge base1 = {v1Undisplaced, v2Undisplaced};
    Edge base2 = {v2Undisplaced, v0Undisplaced};

    Vertex2D uv0 = base0.middle();
    Vertex2D uv1 = base1.middle();
    Vertex2D uv2 = base2.middle();

    float3 uv0Displacement = getDisplacement(uv0.coordinates, dOffset);
    float3 uv1Displacement = getDisplacement(uv1.coordinates, dOffset);
    float3 uv2Displacement = getDisplacement(uv2.coordinates, dOffset);

    float2 uv0Displacement2D = projectDirTo2D(uv0Displacement, p.T, p.B);
    float2 uv1Displacement2D = projectDirTo2D(uv1Displacement, p.T, p.B);
    float2 uv2Displacement2D = projectDirTo2D(uv2Displacement, p.T, p.B);

    Vertex2D uv0Displaced = {uv0.position + uv0Displacement2D, dot(uv0Displacement, p.N), uv0.coordinates};
    Vertex2D uv1Displaced = {uv1.position + uv1Displacement2D, dot(uv1Displacement, p.N), uv1.coordinates};
    Vertex2D uv2Displaced = {uv2.position + uv2Displacement2D, dot(uv2Displacement, p.N), uv2.coordinates};

    //Perform edge-crossing tests with all 9 edges
    float t0 = -1;
    Edge e0Displaced = {v0Displaced, uv0Displaced};
    bool intersect0 = rayIntersectsEdge(ray, e0Displaced, t0);

    float t1 = -1;
    Edge e1Displaced = {uv0Displaced, v1Displaced};
    bool intersect1 = rayIntersectsEdge(ray, e1Displaced, t1);

    float t2 = -1;
    Edge e2Displaced = {v1Displaced, uv1Displaced};
    bool intersect2 = rayIntersectsEdge(ray, e2Displaced, t2);

    float t3 = -1;
    Edge e3Displaced = {uv1Displaced, v2Displaced};
    bool intersect3 = rayIntersectsEdge(ray, e3Displaced, t3);

    float t4 = -1;
    Edge e4Displaced = {v2Displaced, uv2Displaced};
    bool intersect4 = rayIntersectsEdge(ray, e4Displaced, t4);

    float t5 = -1;
    Edge e5Displaced = {uv2Displaced, v0Displaced};
    bool intersect5 = rayIntersectsEdge(ray, e5Displaced, t5);

    float t6 = -1;
    Edge e6Displaced = {uv0Displaced, uv1Displaced};
    bool intersect6 = rayIntersectsEdge(ray, e6Displaced, t6);

    float t7 = -1;
    Edge e7Displaced = {uv1Displaced, uv2Displaced};
    bool intersect7 = rayIntersectsEdge(ray, e7Displaced, t7);

    float t8 = -1;
    Edge e8Displaced = {uv2Displaced, uv0Displaced};
    bool intersect8 = rayIntersectsEdge(ray, e8Displaced, t8);

    Edge e0Undisplaced = {tUndis.vertices[0], uv0};
    Edge e1Undisplaced = {uv0, tUndis.vertices[1]};
    Edge e2Undisplaced = {tUndis.vertices[1], uv1};
    Edge e3Undisplaced = {uv1, tUndis.vertices[2]};
    Edge e4Undisplaced = {tUndis.vertices[2], uv2};
    Edge e5Undisplaced = {uv2, tUndis.vertices[0]};
    Edge e6Undisplaced = {uv0, uv1};
    Edge e7Undisplaced = {uv1, uv2};
    Edge e8Undisplaced = {uv2, uv0};

    EdgeWithT allEdgesWithT[9] = {
        {e0Displaced, t0, e0Undisplaced},
        {e1Displaced, t1, e1Undisplaced},
        {e2Displaced, t2, e2Undisplaced},
        {e3Displaced, t3, e3Undisplaced},
        {e4Displaced, t4, e4Undisplaced},
        {e5Displaced, t5, e5Undisplaced},
        {e6Displaced, t6, e6Undisplaced},
        {e7Displaced, t7, e7Undisplaced},
        {e8Displaced, t8, e8Undisplaced}
    };

    IntersectedEdges ies = sort(allEdgesWithT);

    if(ies.count == 0) {
        /*
         * In some edge cases it's possible that it misses the displaced triangle in this subdivision level, but it actually hits the displaced triangle in one of the next subdivision levels.
         * So we check whether it possibly hits it in one of the next subdivision levels (without explicitly subdividing into all subsequent levels!).
         */
        Edge outerEdges[6] = {e5Displaced, e0Displaced, e1Displaced, e2Displaced, e3Displaced, e4Displaced};
        [unroll] for(int i = 0; i < 6; i++) {
            Edge ou = outerEdges[i];

            float dist;
            Vertex2D ov = getMostOuterVertex(ou, dOffset, dist);

            Edge divertedEdge1 = {ou.start, ov};
            Edge divertedEdge2 = {ov, ou.end};
            float divertedT = -1;

            if(dist > 0.0 && (rayIntersectsEdge(ray, divertedEdge1, divertedT) || rayIntersectsEdge(ray, divertedEdge2, divertedT))) {
                TriangleDAUD ts[3];
                Triangle2D triD, triU;

                if(i <= 1) {
                    triD.vertices[0] = v0Displaced; triD.vertices[1] = uv0Displaced; triD.vertices[2] = uv2Displaced;
                    triU.vertices[0] = v0Undisplaced; triU.vertices[1] = uv0; triU.vertices[2] = uv2;
                } else if(i >= 4) {
                    triD.vertices[0] = uv1Displaced; triD.vertices[1] = v2Displaced; triD.vertices[2] = uv2Displaced;
                    triU.vertices[0] = uv1; triU.vertices[1] = v2Undisplaced; triU.vertices[2] = uv2;
                } else {
                    triD.vertices[0] = uv0Displaced; triD.vertices[1] = v1Displaced; triD.vertices[2] = uv1Displaced;
                    triU.vertices[0] = uv0; triU.vertices[1] = v1Undisplaced; triU.vertices[2] = uv1;
                }

                ts[0].tDisplaced = triD;
                ts[0].tUndisplaced = triU;

                IntersectedTriangles its = {ts, 1};
                return its;
            }
        }

        /*
         * It really misses it, so return no intersected triangles.
         */
        TriangleDAUD ts[3];
        IntersectedTriangles its = {ts, 0};
        return its;
    } else {
    	TriangleDAUD ts[3];

        Vertex2D allVertsD[6] = {
            v0Displaced, v1Displaced, v2Displaced,
            uv0Displaced, uv1Displaced, uv2Displaced,
        };
        Vertex2D allVertsU[6] = {
            v0Undisplaced, v1Undisplaced, v2Undisplaced,
            uv0, uv1, uv2
        };
        Edge allEdges[9] = {e0Displaced, e1Displaced, e2Displaced, e3Displaced, e4Displaced, e5Displaced, e6Displaced, e7Displaced, e8Displaced};

        float2 startPoint = ray.origin;
        int counter = 0;
        for(int i = 0; i < ies.count; i++) {
            float2 hitPointEdge = ray.origin + ies.edges[i].t * ray.direction;
            float2 middle = 0.5f * (startPoint + hitPointEdge);
            Triangle2D subTriD;
        	Triangle2D subTriU;

            if(findSubTriangle(middle, allVertsD, allVertsU, subTriD, subTriU, sa, allEdges)) {
                ts[counter].tDisplaced = subTriD;
                ts[counter].tUndisplaced = subTriU;
                counter++;
            }

            startPoint = hitPointEdge;
        }

        IntersectedTriangles its = {ts, counter};
        return its;
    }
}

bool rayTraceTriangle(float3 v0, float3 v1, float3 v2) {
    const float epsilon = 1e-6f; //Needed for small floating-point errors

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
void rayTraceMMTriangle(TriangleDAUD rootTri, Ray2D ray, Plane p, float3 v0Pos, int dOffset) {
    struct StackElement {
        TriangleDAUD tri;
        int level;
	};

    //Creating and populating the stack
    const int MAX_STACK_DEPTH = 256; //Should be enough
    StackElement stack[MAX_STACK_DEPTH];
    int stackTop = 0;

    StackElement se = { rootTri, 0 };
    stack[stackTop++] = se;

    float sa = rootTri.tUndisplaced.signedArea();

    while(stackTop > 0) {
        StackElement current = stack[--stackTop];

        if(current.level == subDivLvl) { //Base case. Raytrace micro triangles directly
            Vertex2D v0Displaced = current.tri.tDisplaced.vertices[0];
            Vertex2D v1Displaced = current.tri.tDisplaced.vertices[1];
            Vertex2D v2Displaced = current.tri.tDisplaced.vertices[2];

            InputVertex vs3D[3] = {
                unproject(v0Displaced.position, v0Displaced.height, p, v0Pos),
                unproject(v1Displaced.position, v1Displaced.height, p, v0Pos),
                unproject(v2Displaced.position, v2Displaced.height, p, v0Pos)
            };

            if(rayTraceTriangle(vs3D[0].position, vs3D[1].position, vs3D[2].position)) return; //Ray hits triangle, so we can stop searching
        } else {
            IntersectedTriangles its = getIntersectedTriangles(current.tri.tDisplaced, current.tri.tUndisplaced, ray, dOffset, p, sa);

            //Push intersected triangles in reverse order to main correct processing order (triangles should be processed in the order the ray hits them)
            //TODO switching data structure to a queue would make more sense...
            for(int i = its.count - 1; i >= 0; i--) {
                TriangleDAUD nextTri = its.triangles[i];

                StackElement se = { nextTri, current.level + 1 };
                stack[stackTop++] = se;
            }
        }
    }
}

[shader("intersection")]
void main() {
    AABB aabb = AABBs[PrimitiveIndex()];

    InputVertex v0 = vertices[aabb.vIndices.x];
    InputVertex v1 = vertices[aabb.vIndices.y];
    InputVertex v2 = vertices[aabb.vIndices.z];
    float2 v0GridCoordinate = float2(0,0);
    float2 v1GridCoordinate = float2(aabb.nRows - 1, 0);
    float2 v2GridCoordinate = float2(aabb.nRows - 1, aabb.nRows - 1);

    /*
	 * Creation of 2D triangle where vertices are displaced
	 */
    float3 e1 = v1.position - v0.position;
    float3 e2 = v2.position - v0.position;
    float3 N = normalize(cross(e1, e2)); // plane normal

    float3 T = normalize(e1);
    float3 B = normalize(cross(N, T));

    Plane p = {T, B, N};

    Vertex2D v0Proj = projectVertexTo2D(v0.position + getDisplacement(v0GridCoordinate, aabb.displacementOffset), T, B, N, v0.position, v0GridCoordinate);
    Vertex2D v1Proj = projectVertexTo2D(v1.position + getDisplacement(v1GridCoordinate, aabb.displacementOffset), T, B, N, v0.position, v1GridCoordinate);
    Vertex2D v2Proj = projectVertexTo2D(v2.position + getDisplacement(v2GridCoordinate, aabb.displacementOffset), T, B, N, v0.position, v2GridCoordinate);
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

    /*
     * Creation of 2D triangle where vertices are NOT displaced
 	 */
    Triangle2D tUndisplaced = {
    	projectVertexTo2D(v0.position, T, B, N, v0.position, v0GridCoordinate),
        projectVertexTo2D(v1.position, T, B, N, v0.position, v1GridCoordinate),
        projectVertexTo2D(v2.position, T, B, N, v0.position, v2GridCoordinate)
    };

    TriangleDAUD ts = {t, tUndisplaced};

	rayTraceMMTriangle(ts, ray, p, v0.position, aabb.displacementOffset);
}
