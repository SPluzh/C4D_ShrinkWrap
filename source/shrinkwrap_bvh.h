#ifndef SHRINKWRAP_BVH_H__
#define SHRINKWRAP_BVH_H__

#include "c4d.h"
#include <algorithm>

namespace cinema
{

struct BVHAABB
{
	Vector minBound;
	Vector maxBound;

	BVHAABB() : minBound(MAXRANGE), maxBound(-MAXRANGE) {}
	BVHAABB(const Vector& mn, const Vector& mx) : minBound(mn), maxBound(mx) {}

	void Expand(const Vector& p)
	{
		minBound.x = Min(minBound.x, p.x);
		minBound.y = Min(minBound.y, p.y);
		minBound.z = Min(minBound.z, p.z);
		maxBound.x = Max(maxBound.x, p.x);
		maxBound.y = Max(maxBound.y, p.y);
		maxBound.z = Max(maxBound.z, p.z);
	}

	void Expand(const BVHAABB& b)
	{
		minBound.x = Min(minBound.x, b.minBound.x);
		minBound.y = Min(minBound.y, b.minBound.y);
		minBound.z = Min(minBound.z, b.minBound.z);
		maxBound.x = Max(maxBound.x, b.maxBound.x);
		maxBound.y = Max(maxBound.y, b.maxBound.y);
		maxBound.z = Max(maxBound.z, b.maxBound.z);
	}

	Float SqrDistanceToPoint(const Vector& p) const
	{
		Float sqDist = 0.0;
		if (p.x < minBound.x) { Float d = minBound.x - p.x; sqDist += d * d; }
		else if (p.x > maxBound.x) { Float d = p.x - maxBound.x; sqDist += d * d; }

		if (p.y < minBound.y) { Float d = minBound.y - p.y; sqDist += d * d; }
		else if (p.y > maxBound.y) { Float d = p.y - maxBound.y; sqDist += d * d; }

		if (p.z < minBound.z) { Float d = minBound.z - p.z; sqDist += d * d; }
		else if (p.z > maxBound.z) { Float d = p.z - maxBound.z; sqDist += d * d; }

		return sqDist;
	}

	Bool IntersectRay(const Vector& orig, const Vector& dir, Float& tMinOut, Float& tMaxOut) const
	{
		Float tmin = -MAXRANGE;
		Float tmax = MAXRANGE;

		for (Int32 i = 0; i < 3; ++i)
		{
			Float o = (i == 0) ? orig.x : (i == 1) ? orig.y : orig.z;
			Float d = (i == 0) ? dir.x : (i == 1) ? dir.y : dir.z;
			Float mn = (i == 0) ? minBound.x : (i == 1) ? minBound.y : minBound.z;
			Float mx = (i == 0) ? maxBound.x : (i == 1) ? maxBound.y : maxBound.z;

			if (Abs(d) < 1e-9)
			{
				if (o < mn || o > mx)
					return false;
			}
			else
			{
				Float invD = 1.0 / d;
				Float t1 = (mn - o) * invD;
				Float t2 = (mx - o) * invD;
				if (t1 > t2)
				{
					Float tmp = t1;
					t1 = t2;
					t2 = tmp;
				}
				tmin = Max(tmin, t1);
				tmax = Min(tmax, t2);
				if (tmin > tmax)
					return false;
			}
		}
		tMinOut = tmin;
		tMaxOut = tmax;
		return true;
	}
};

// Compact triangle: only stores vertices (72 bytes)
struct BVHTriangle
{
	Vector v0;
	Vector v1;
	Vector v2;
};

// Temporary structure used ONLY during BVH construction
struct BVHBuildPrim
{
	BVHAABB aabb;
	Vector centroid;
	Int32 triIndex = 0;
};

// Compact node: exactly 64 bytes (1 CPU cache line)
struct BVHNode
{
	BVHAABB bbox;        // 48 bytes
	Int32 leftChild = -1;// 4 bytes: if triCount == 0, index of left child (right child is leftChild + 1)
	Int32 firstTri = -1; // 4 bytes: if triCount > 0, start index into contiguous _triangles
	Int32 triCount = 0;  // 4 bytes: 0 for internal node, > 0 for leaf
	Int32 pad = 0;       // 4 bytes padding
};

class TriangleBVH
{
public:
	TriangleBVH() = default;

	void Clear()
	{
		_triangles.Reset();
		_nodes.Reset();
		_nodeCount = 0;
		_isBuilt = false;
	}

	Bool IsBuilt() const { return _isBuilt; }
	Int32 GetTriangleCount() const { return (Int32)_triangles.GetCount(); }
	Int32 GetNodeCount() const { return _nodeCount; }

	// Closest point on triangle (Ericson's algorithm)
	static Vector ClosestPointOnTriangle(const Vector& p, const Vector& a, const Vector& b, const Vector& c, Vector& outNormal)
	{
		Vector ab = b - a;
		Vector ac = c - a;
		Vector ap = p - a;

		Float d1 = Dot(ab, ap);
		Float d2 = Dot(ac, ap);
		if (d1 <= 0.0 && d2 <= 0.0)
		{
			outNormal = Cross(ab, ac);
			return a;
		}

		Vector bp = p - b;
		Float d3 = Dot(ab, bp);
		Float d4 = Dot(ac, bp);
		if (d3 >= 0.0 && d4 <= d3)
		{
			outNormal = Cross(ab, ac);
			return b;
		}

		Float vc = d1 * d4 - d3 * d2;
		if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0)
		{
			Float v = d1 / (d1 - d3);
			outNormal = Cross(ab, ac);
			return a + ab * v;
		}

		Vector cp = p - c;
		Float d5 = Dot(ab, cp);
		Float d6 = Dot(ac, cp);
		if (d6 >= 0.0 && d5 <= d6)
		{
			outNormal = Cross(ab, ac);
			return c;
		}

		Float vb = d5 * d2 - d1 * d6;
		if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0)
		{
			Float w = d2 / (d2 - d6);
			outNormal = Cross(ab, ac);
			return a + ac * w;
		}

		Float va = d3 * d6 - d5 * d4;
		if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0)
		{
			Float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
			outNormal = Cross(ab, ac);
			return b + (c - b) * w;
		}

		Float denom = 1.0 / (va + vb + vc);
		Float v = vb * denom;
		Float w = vc * denom;
		outNormal = Cross(ab, ac);
		return a + ab * v + ac * w;
	}

	// Ray-triangle intersection (Möller-Trumbore)
	static Bool RayTriangleIntersect(const Vector& orig, const Vector& dir,
									 const Vector& v0, const Vector& v1, const Vector& v2,
									 Float& outT, Vector& outNormal, Bool bidirectional)
	{
		const Float eps = 1e-7;
		Vector edge1 = v1 - v0;
		Vector edge2 = v2 - v0;
		Vector pvec = Cross(dir, edge2);
		Float det = Dot(edge1, pvec);

		if (Abs(det) < eps)
			return false;

		Float invDet = 1.0 / det;
		Vector tvec = orig - v0;
		Float u = Dot(tvec, pvec) * invDet;
		if (u < 0.0 || u > 1.0)
			return false;

		Vector qvec = Cross(tvec, edge1);
		Float v = Dot(dir, qvec) * invDet;
		if (v < 0.0 || u + v > 1.0)
			return false;

		Float t = Dot(edge2, qvec) * invDet;
		if (!bidirectional && t < eps)
			return false;

		outT = t;
		outNormal = Cross(edge1, edge2);
		if (outNormal.GetSquaredLength() > 1e-12)
			outNormal.Normalize();

		if (Dot(outNormal, dir) > 0.0)
			outNormal = -outNormal;

		return true;
	}

	// High-performance BVH build from multiple polygon objects with matrices transformed into target root's local space
	Bool Build(const maxon::BaseArray<const PolygonObject*>& polyObjs,
			   const maxon::BaseArray<Matrix>& matrices,
			   const Matrix& invTargetMg,
			   BaseThread* thread = nullptr)
	{
		Clear();
		if (polyObjs.GetCount() == 0 || polyObjs.GetCount() != matrices.GetCount())
			return false;

		Int32 totalEstTris = 0;
		for (Int32 k = 0; k < (Int32)polyObjs.GetCount(); ++k)
		{
			const PolygonObject* polyObj = polyObjs[k];
			if (!polyObj)
				continue;
			Int32 polyCount = polyObj->GetPolygonCount();
			const CPolygon* polys = polyObj->GetPolygonR();
			if (!polys || polyCount <= 0)
				continue;

			for (Int32 i = 0; i < polyCount; ++i)
			{
				totalEstTris += (polys[i].c == polys[i].d) ? 1 : 2;
			}
		}

		if (totalEstTris <= 0)
			return false;

		maxon::BaseArray<BVHTriangle> srcTriangles;
		if (srcTriangles.Resize(totalEstTris) == maxon::FAILED)
			return false;

		maxon::BaseArray<BVHBuildPrim> buildPrims;
		if (buildPrims.Resize(totalEstTris) == maxon::FAILED)
			return false;

		Int32 triIdx = 0;
		BVHAABB rootBbox;
		BVHAABB rootCentroidBbox;

		for (Int32 k = 0; k < (Int32)polyObjs.GetCount(); ++k)
		{
			const PolygonObject* polyObj = polyObjs[k];
			if (!polyObj)
				continue;

			const Vector* points = polyObj->GetPointR();
			const CPolygon* polys = polyObj->GetPolygonR();
			Int32 polyCount = polyObj->GetPolygonCount();
			Int32 pointCount = polyObj->GetPointCount();

			if (!points || !polys || polyCount <= 0 || pointCount <= 0)
				continue;

			Matrix toTargetLocal = invTargetMg * matrices[k];

			for (Int32 i = 0; i < polyCount; ++i)
			{
				const CPolygon& p = polys[i];
				if (p.a < 0 || p.a >= pointCount || p.b < 0 || p.b >= pointCount ||
					p.c < 0 || p.c >= pointCount || p.d < 0 || p.d >= pointCount)
					continue;

				Vector v0 = toTargetLocal * points[p.a];
				Vector v1 = toTargetLocal * points[p.b];
				Vector v2 = toTargetLocal * points[p.c];

				// First triangle (a, b, c)
				{
					BVHTriangle& tri = srcTriangles[triIdx];
					tri.v0 = v0;
					tri.v1 = v1;
					tri.v2 = v2;

					BVHBuildPrim& prim = buildPrims[triIdx];
					prim.triIndex = triIdx;
					prim.centroid = (tri.v0 + tri.v1 + tri.v2) * (1.0 / 3.0);
					prim.aabb.minBound = prim.aabb.maxBound = tri.v0;
					prim.aabb.Expand(tri.v1);
					prim.aabb.Expand(tri.v2);

					rootBbox.Expand(prim.aabb);
					rootCentroidBbox.Expand(prim.centroid);

					triIdx++;
				}

				// Second triangle if quad (a, c, d)
				if (p.c != p.d)
				{
					Vector v3 = toTargetLocal * points[p.d];

					BVHTriangle& tri = srcTriangles[triIdx];
					tri.v0 = v0;
					tri.v1 = v2;
					tri.v2 = v3;

					BVHBuildPrim& prim = buildPrims[triIdx];
					prim.triIndex = triIdx;
					prim.centroid = (tri.v0 + tri.v1 + tri.v2) * (1.0 / 3.0);
					prim.aabb.minBound = prim.aabb.maxBound = tri.v0;
					prim.aabb.Expand(tri.v1);
					prim.aabb.Expand(tri.v2);

					rootBbox.Expand(prim.aabb);
					rootCentroidBbox.Expand(prim.centroid);

					triIdx++;
				}
			}
		}

		Int32 totalTris = triIdx;
		if (totalTris <= 0)
			return false;

		srcTriangles.Resize(totalTris) iferr_ignore("shrink srcTris");
		buildPrims.Resize(totalTris) iferr_ignore("shrink buildPrims");

		// Pre-allocate upper bound of nodes in a SINGLE allocation.
		// A binary tree with N leaves has at most 2N - 1 nodes.
		Int32 maxNodes = totalTris * 2 + 1;
		if (_nodes.Resize(maxNodes) == maxon::FAILED)
			return false;
		_nodeCount = 1;

		// Fast recursive subdivision with inherited bounding boxes
		SubdivideNode(0, 0, totalTris, 0, rootBbox, rootCentroidBbox, buildPrims, thread);

		if (thread && thread->TestBreak())
		{
			Clear();
			return false;
		}

		// Shrink _nodes to the exact count used
		_nodes.Resize(_nodeCount) iferr_ignore("shrink nodes");

		// Reorder triangles so each leaf holds a contiguous span of triangles
		if (_triangles.Resize(totalTris) == maxon::FAILED)
			return false;
		for (Int32 i = 0; i < totalTris; ++i)
		{
			_triangles[i] = srcTriangles[buildPrims[i].triIndex];
		}

		_isBuilt = true;
		return true;
	}

	// Single-object convenience build
	Bool Build(const PolygonObject* polyObj, BaseThread* thread = nullptr)
	{
		Clear();
		if (!polyObj)
			return false;

		maxon::BaseArray<const PolygonObject*> polyObjs;
		maxon::BaseArray<Matrix> matrices;
		if (polyObjs.Append(polyObj) == maxon::FAILED || matrices.Append(Matrix()) == maxon::FAILED)
			return false;

		return Build(polyObjs, matrices, Matrix(), thread);
	}

	// Query: Nearest point on surface
	Bool FindClosestPoint(const Vector& queryPoint, Vector& outClosestPoint, Vector& outNormal, Float maxDist = MAXRANGE) const
	{
		if (!_isBuilt || _nodeCount == 0 || _triangles.GetCount() == 0)
			return false;

		Float bestDistSq = (maxDist < MAXRANGE) ? (maxDist * maxDist) : (MAXRANGE * MAXRANGE);
		Vector bestPoint(0.0);
		Vector bestNormal(0.0, 1.0, 0.0);
		Bool found = false;

		Int32 stack[64];
		Int32 stackPtr = 0;
		stack[stackPtr++] = 0;

		while (stackPtr > 0)
		{
			Int32 nodeIdx = stack[--stackPtr];
			const BVHNode& node = _nodes[nodeIdx];

			if (node.bbox.SqrDistanceToPoint(queryPoint) >= bestDistSq)
				continue;

			if (node.triCount > 0)
			{
				for (Int32 i = 0; i < node.triCount; ++i)
				{
					const BVHTriangle& tri = _triangles[node.firstTri + i];
					Vector n;
					Vector pt = ClosestPointOnTriangle(queryPoint, tri.v0, tri.v1, tri.v2, n);
					Float dSq = (queryPoint - pt).GetSquaredLength();
					if (dSq < bestDistSq)
					{
						bestDistSq = dSq;
						bestPoint = pt;
						bestNormal = n;
						found = true;
					}
				}
			}
			else
			{
				Int32 leftIdx = node.leftChild;
				Int32 rightIdx = node.leftChild + 1;

				Float distLeft = _nodes[leftIdx].bbox.SqrDistanceToPoint(queryPoint);
				Float distRight = _nodes[rightIdx].bbox.SqrDistanceToPoint(queryPoint);

				if (distLeft < distRight)
				{
					if (distRight < bestDistSq && stackPtr < 63) stack[stackPtr++] = rightIdx;
					if (distLeft < bestDistSq && stackPtr < 63) stack[stackPtr++] = leftIdx;
				}
				else
				{
					if (distLeft < bestDistSq && stackPtr < 63) stack[stackPtr++] = leftIdx;
					if (distRight < bestDistSq && stackPtr < 63) stack[stackPtr++] = rightIdx;
				}
			}
		}

		if (found)
		{
			outClosestPoint = bestPoint;
			if (bestNormal.GetSquaredLength() > 1e-10)
				bestNormal.Normalize();
			else
				bestNormal = Vector(0.0, 1.0, 0.0);
			outNormal = bestNormal;
			return true;
		}
		return false;
	}

	// Query: Nearest vertex
	Bool FindNearestVertex(const Vector& queryPoint, Vector& outVertex, Float maxDist = MAXRANGE) const
	{
		if (!_isBuilt || _nodeCount == 0 || _triangles.GetCount() == 0)
			return false;

		Float bestDistSq = (maxDist < MAXRANGE) ? (maxDist * maxDist) : (MAXRANGE * MAXRANGE);
		Vector bestVert(0.0);
		Bool found = false;

		Int32 stack[64];
		Int32 stackPtr = 0;
		stack[stackPtr++] = 0;

		while (stackPtr > 0)
		{
			Int32 nodeIdx = stack[--stackPtr];
			const BVHNode& node = _nodes[nodeIdx];

			if (node.bbox.SqrDistanceToPoint(queryPoint) >= bestDistSq)
				continue;

			if (node.triCount > 0)
			{
				for (Int32 i = 0; i < node.triCount; ++i)
				{
					const BVHTriangle& tri = _triangles[node.firstTri + i];

					Float d0 = (queryPoint - tri.v0).GetSquaredLength();
					if (d0 < bestDistSq) { bestDistSq = d0; bestVert = tri.v0; found = true; }

					Float d1 = (queryPoint - tri.v1).GetSquaredLength();
					if (d1 < bestDistSq) { bestDistSq = d1; bestVert = tri.v1; found = true; }

					Float d2 = (queryPoint - tri.v2).GetSquaredLength();
					if (d2 < bestDistSq) { bestDistSq = d2; bestVert = tri.v2; found = true; }
				}
			}
			else
			{
				Int32 leftIdx = node.leftChild;
				Int32 rightIdx = node.leftChild + 1;

				Float distLeft = _nodes[leftIdx].bbox.SqrDistanceToPoint(queryPoint);
				Float distRight = _nodes[rightIdx].bbox.SqrDistanceToPoint(queryPoint);

				if (distLeft < distRight)
				{
					if (distRight < bestDistSq && stackPtr < 63) stack[stackPtr++] = rightIdx;
					if (distLeft < bestDistSq && stackPtr < 63) stack[stackPtr++] = leftIdx;
				}
				else
				{
					if (distLeft < bestDistSq && stackPtr < 63) stack[stackPtr++] = leftIdx;
					if (distRight < bestDistSq && stackPtr < 63) stack[stackPtr++] = rightIdx;
				}
			}
		}

		if (found)
		{
			outVertex = bestVert;
			return true;
		}
		return false;
	}

	// Query: Raycast along direction
	Bool Raycast(const Vector& orig, const Vector& dir, Vector& outHitPoint, Vector& outNormal, Float maxDist = MAXRANGE, Bool bidirectional = false) const
	{
		if (!_isBuilt || _nodeCount == 0 || _triangles.GetCount() == 0)
			return false;

		Float bestDist = (maxDist > 0.0 && maxDist < MAXRANGE) ? maxDist : MAXRANGE;
		Vector bestPoint(0.0);
		Vector bestNormal(0.0, 1.0, 0.0);
		Bool found = false;

		Vector rayDir = dir;
		if (rayDir.GetSquaredLength() < 1e-12)
			return false;
		rayDir.Normalize();

		Int32 stack[64];
		Int32 stackPtr = 0;
		stack[stackPtr++] = 0;

		while (stackPtr > 0)
		{
			Int32 nodeIdx = stack[--stackPtr];
			const BVHNode& node = _nodes[nodeIdx];

			Float tmin, tmax;
			if (!node.bbox.IntersectRay(orig, rayDir, tmin, tmax))
				continue;

			if (!bidirectional && tmax < 0.0)
				continue;

			if (!bidirectional && tmin > bestDist)
				continue;

			if (bidirectional)
			{
				Float boxMinAbsDist = 0.0;
				if (tmin > 0.0) boxMinAbsDist = tmin;
				else if (tmax < 0.0) boxMinAbsDist = -tmax;
				if (boxMinAbsDist > bestDist)
					continue;
			}

			if (node.triCount > 0)
			{
				for (Int32 i = 0; i < node.triCount; ++i)
				{
					const BVHTriangle& tri = _triangles[node.firstTri + i];

					Float hitT;
					Vector triN;
					if (RayTriangleIntersect(orig, rayDir, tri.v0, tri.v1, tri.v2, hitT, triN, bidirectional))
					{
						Float absDist = Abs(hitT);
						if (absDist < bestDist)
						{
							bestDist = absDist;
							bestPoint = orig + rayDir * hitT;
							bestNormal = triN;
							found = true;
						}
					}
				}
			}
			else
			{
				Int32 leftIdx = node.leftChild;
				Int32 rightIdx = node.leftChild + 1;

				Float tminL, tmaxL, tminR, tmaxR;
				Bool hitL = _nodes[leftIdx].bbox.IntersectRay(orig, rayDir, tminL, tmaxL);
				Bool hitR = _nodes[rightIdx].bbox.IntersectRay(orig, rayDir, tminR, tmaxR);

				if (hitL && hitR)
				{
					if (tminL < tminR)
					{
						if (stackPtr < 63) stack[stackPtr++] = rightIdx;
						if (stackPtr < 63) stack[stackPtr++] = leftIdx;
					}
					else
					{
						if (stackPtr < 63) stack[stackPtr++] = leftIdx;
						if (stackPtr < 63) stack[stackPtr++] = rightIdx;
					}
				}
				else if (hitL)
				{
					if (stackPtr < 63) stack[stackPtr++] = leftIdx;
				}
				else if (hitR)
				{
					if (stackPtr < 63) stack[stackPtr++] = rightIdx;
				}
			}
		}

		if (found)
		{
			outHitPoint = bestPoint;
			outNormal = bestNormal;
			return true;
		}
		return false;
	}

private:
	void SubdivideNode(Int32 nodeIdx, Int32 first, Int32 count, Int32 depth,
					   const BVHAABB& bbox, const BVHAABB& centroidBbox,
					   maxon::BaseArray<BVHBuildPrim>& buildPrims,
					   BaseThread* thread)
	{
		if (thread && !(depth & 3) && thread->TestBreak())
			return;

		_nodes[nodeIdx].bbox = bbox;

		if (count <= 4 || depth >= 30 || centroidBbox.minBound == centroidBbox.maxBound)
		{
			_nodes[nodeIdx].firstTri = first;
			_nodes[nodeIdx].triCount = count;
			_nodes[nodeIdx].leftChild = -1;
			return;
		}

		Vector extent = centroidBbox.maxBound - centroidBbox.minBound;
		Int32 axis = 0;
		if (extent.y > extent.x && extent.y > extent.z) axis = 1;
		else if (extent.z > extent.x && extent.z > extent.y) axis = 2;

		Float minC = (axis == 0 ? centroidBbox.minBound.x : (axis == 1 ? centroidBbox.minBound.y : centroidBbox.minBound.z));
		Float maxC = (axis == 0 ? centroidBbox.maxBound.x : (axis == 1 ? centroidBbox.maxBound.y : centroidBbox.maxBound.z));
		Float splitPos = 0.5 * (minC + maxC);

		Int32 i = first;
		Int32 j = first + count - 1;
		while (i <= j)
		{
			Float c = (axis == 0) ? buildPrims[i].centroid.x :
					  ((axis == 1) ? buildPrims[i].centroid.y : buildPrims[i].centroid.z);
			if (c < splitPos)
			{
				i++;
			}
			else
			{
				maxon::Swap(buildPrims[i], buildPrims[j]);
				j--;
			}
		}

		Int32 leftCount = i - first;
		if (leftCount == 0 || leftCount == count)
		{
			leftCount = count / 2;
			std::nth_element(&buildPrims[first],
							 &buildPrims[first + leftCount],
							 &buildPrims[first + count],
							 [axis](const BVHBuildPrim& a, const BVHBuildPrim& b) {
								 Float ca = (axis == 0) ? a.centroid.x : (axis == 1 ? a.centroid.y : a.centroid.z);
								 Float cb = (axis == 0) ? b.centroid.x : (axis == 1 ? b.centroid.y : b.centroid.z);
								 return ca < cb;
							 });
		}
		Int32 rightCount = count - leftCount;

		BVHAABB leftBbox, leftCentroidBbox;
		for (Int32 k = first; k < first + leftCount; ++k)
		{
			leftBbox.Expand(buildPrims[k].aabb);
			leftCentroidBbox.Expand(buildPrims[k].centroid);
		}

		BVHAABB rightBbox, rightCentroidBbox;
		for (Int32 k = first + leftCount; k < first + count; ++k)
		{
			rightBbox.Expand(buildPrims[k].aabb);
			rightCentroidBbox.Expand(buildPrims[k].centroid);
		}

		Int32 leftChildIdx = _nodeCount;
		_nodeCount += 2;

		_nodes[nodeIdx].leftChild = leftChildIdx;
		_nodes[nodeIdx].triCount = 0; // internal node

		SubdivideNode(leftChildIdx, first, leftCount, depth + 1, leftBbox, leftCentroidBbox, buildPrims, thread);
		SubdivideNode(leftChildIdx + 1, first + leftCount, rightCount, depth + 1, rightBbox, rightCentroidBbox, buildPrims, thread);
	}

	maxon::BaseArray<BVHTriangle> _triangles;
	maxon::BaseArray<BVHNode> _nodes;
	Int32 _nodeCount = 0;
	Bool _isBuilt = false;
};

} // namespace cinema

#endif // SHRINKWRAP_BVH_H__
