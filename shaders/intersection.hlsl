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
    float2 coordinates; //local grid coordinates
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

    Triangle2D tempTriangle = {v0New, v1New, v2New};
    float newArea = tempTriangle.signedArea();

    if(baseArea * newArea < 0.0) {
        Vertex2D temp = v1New;
        v1New = v2New;
        v2New = temp;
    }

    Triangle2D t = {v0New, v1New, v2New};
    return t;
}

//Gets the first edge that a ray hits
// @param ray: the ray
// @param edgesD: the edges with displaced vertices
// @param edgesU: the edges with undisplaced vertices
EdgeWithT getFirstIntersectedEdge(Ray2D ray, Edge edgesD[9], Edge edgesU[9]) {
    float t0 = -1, t1 = -1, t2 = -1, t3 = -1, t4 = -1, t5 = -1, t6 = -1, t7 = -1, t8 = -1;

    rayIntersectsEdge(ray, edgesD[0], t0);
    rayIntersectsEdge(ray, edgesD[1], t1);
    rayIntersectsEdge(ray, edgesD[2], t2);
    rayIntersectsEdge(ray, edgesD[3], t3);
    rayIntersectsEdge(ray, edgesD[4], t4);
    rayIntersectsEdge(ray, edgesD[5], t5);
    rayIntersectsEdge(ray, edgesD[6], t6);
    rayIntersectsEdge(ray, edgesD[7], t7);
    rayIntersectsEdge(ray, edgesD[8], t8);

    bool noHit = (t0 == -1.0) && (t1 == -1.0) && (t2 == -1.0) &&
                   (t3 == -1.0) && (t4 == -1.0) && (t5 == -1.0) &&
                   (t6 == -1.0) && (t7 == -1.0) && (t8 == -1.0);

    if(noHit) {
        EdgeWithT ewt = {edgesD[0], t0, edgesU[0]};
        return ewt;
    }

    EdgeWithT allEdgesWithT[9] = {
        {edgesD[0], t0, edgesU[0]},
        {edgesD[1], t1, edgesU[1]},
        {edgesD[2], t2, edgesU[2]},
        {edgesD[3], t3, edgesU[3]},
        {edgesD[4], t4, edgesU[4]},
        {edgesD[5], t5, edgesU[5]},
        {edgesD[6], t6, edgesU[6]},
        {edgesD[7], t7, edgesU[7]},
        {edgesD[8], t8, edgesU[8]}
    };

    IntersectedEdges ies = sort(allEdgesWithT);

    return ies.edges[0]; //Closest one
}

//Given a triangle, we subdivide it one level and return the triangles that the ray crossed
// @param tDis: the displaced triangle
// @param tUndis: the undisplaced triangle
// @param ray: the ray in 2D
// @param sa: signed area of the base triangle. We use this to preserve vertex order (counter clockwise)
// @return an array of triangles that the ray crossed in order
IntersectedTriangles getIntersectedTriangles(Triangle2D tDis, Triangle2D tUndis, Ray2D ray, int dOffset, Plane p, float sa) {
	Vertex2D v0Displaced = tDis.vertices[0];
    Vertex2D v1Displaced = tDis.vertices[1];
    Vertex2D v2Displaced = tDis.vertices[2];

    Edge base0 = {tUndis.vertices[0], tUndis.vertices[1]};
    Edge base1 = {tUndis.vertices[1], tUndis.vertices[2]};
    Edge base2 = {tUndis.vertices[2], tUndis.vertices[0]};

    Vertex2D uv0 = base0.middle();
    Vertex2D uv1 = base1.middle();
    Vertex2D uv2 = base2.middle();

    float3 uv0Displacement = getDisplacement(uv0.coordinates, dOffset);
    float3 uv1Displacement = getDisplacement(uv1.coordinates, dOffset);
    float3 uv2Displacement = getDisplacement(uv2.coordinates, dOffset);

    float2 uv0Displacement2D = projectDirTo2D(uv0Displacement, p.T, p.B);
    float2 uv1Displacement2D = projectDirTo2D(uv1Displacement, p.T, p.B);
    float2 uv2Displacement2D = projectDirTo2D(uv2Displacement, p.T, p.B);

    Vertex2D uv0Displaced = {uv0.position, dot(uv0Displacement, p.N), uv0.coordinates};
    Vertex2D uv1Displaced = {uv1.position, dot(uv1Displacement, p.N), uv1.coordinates};
    Vertex2D uv2Displaced = {uv2.position, dot(uv2Displacement, p.N), uv2.coordinates};

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
        TriangleDAUD ts[3];
        IntersectedTriangles its = {ts, 0};
        return its;
    } else if(ies.count == 4) {
        Triangle2D tsDisplaced[3] = {
            createTriangleFromEdges(ies.edges[0].e, ies.edges[1].e, sa),
            createTriangleFromEdges(ies.edges[1].e, ies.edges[2].e, sa),
            createTriangleFromEdges(ies.edges[2].e, ies.edges[3].e, sa)
        };

        Triangle2D tsUndisplaced[3] = {
            createTriangleFromEdges(ies.edges[0].eU, ies.edges[1].eU, sa),
            createTriangleFromEdges(ies.edges[1].eU, ies.edges[2].eU, sa),
            createTriangleFromEdges(ies.edges[2].eU, ies.edges[3].eU, sa)
        };

        TriangleDAUD tDaud[3] = {
            {tsDisplaced[0], tsUndisplaced[0]},
            {tsDisplaced[1], tsUndisplaced[1]},
            {tsDisplaced[2], tsUndisplaced[2]}
        };

        IntersectedTriangles its = {tDaud, 3};
        return its;
    } else {
        Edge allEdgesD[9] = {e0Displaced, e1Displaced, e2Displaced, e3Displaced, e4Displaced, e5Displaced, e6Displaced, e7Displaced, e8Displaced};
        Edge allEdgesU[9] = {e0Undisplaced, e1Undisplaced, e2Undisplaced, e3Undisplaced, e4Undisplaced, e5Undisplaced, e6Undisplaced, e7Undisplaced, e8Undisplaced};

        Ray2D backwardRay = {ray.origin, -ray.direction};
        EdgeWithT closestBackward = getFirstIntersectedEdge(backwardRay, allEdgesD, allEdgesU); //The first edge that the backward ray hits

        //Backward ray did not hit anything (can happen when ray hits only 2 (base) edges). So we only need to process the current triangle
        if(closestBackward.t == -1) {
            TriangleDAUD tDaud[3];
            tDaud[0].tDisplaced = createTriangleFromEdges(ies.edges[0].e, ies.edges[1].e, sa);
			tDaud[0].tUndisplaced = createTriangleFromEdges(ies.edges[0].eU, ies.edges[1].eU, sa);

            IntersectedTriangles its = {tDaud, 1};
            return its;
        }

        Triangle2D tsD[3];
        Triangle2D tsU[3];
        int tCount = 0;

        for(int i = 0; i < ies.count; i++) {
            EdgeWithT startEdge;

            if(i == 0) startEdge = closestBackward;
            else startEdge = ies.edges[i - 1];

            tsD[tCount] = createTriangleFromEdges(startEdge.e, ies.edges[i].e, sa);
            tsU[tCount] = createTriangleFromEdges(startEdge.eU, ies.edges[i].eU, sa);
            tCount++;
        }

        TriangleDAUD tDaud[3] = {
            {tsD[0], tsU[0]},
            {tsD[1], tsU[1]},
            {tsD[2], tsU[2]}
        };

        IntersectedTriangles its = {tDaud, tCount};
        return its;
    }
}

bool rayTraceTriangle(float3 v0, float3 v1, float3 v2) {
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
    if(u < 0 || u > 1) return false;

    float3 qvec = cross(tvec, edge1);
    float v = dot(dir, qvec) * invDet;
    if(v < 0 || u + v > 1) return false;

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
