//////////////////////////////////////////////////////////////////////////////////////////
//	AA_BOUNDING_BOX.cpp
//	Functions for axis aligned bounding box. Derives from BOUNDING_VOLUME
//	Downloaded from: www.paulsprojects.net
//	Created:	13th August 2002
//
//	Copyright (c) 2006, Paul Baker
//	Distributed under the New BSD Licence. (See accompanying file License.txt or copy at
//	http://www.paulsprojects.net/NewBSDLicense.txt)
//////////////////////////////////////////////////////////////////////////////////////////	
#include "Maths.h"

void AA_BOUNDING_BOX::SetFromMinsMaxes(const VECTOR3D & newMins, const VECTOR3D & newMaxes)
{
	//Save mins & maxes
	mins=newMins;
	maxes=newMaxes;

	//set the positions of the vertices
	vertices[0].Set(mins.x, mins.y, mins.z);
	vertices[1].Set(mins.x, mins.y, maxes.z);
	vertices[2].Set(mins.x, maxes.y, mins.z);
	vertices[3].Set(mins.x, maxes.y, maxes.z);
	vertices[4].Set(maxes.x, mins.y, mins.z);
	vertices[5].Set(maxes.x, mins.y, maxes.z);
	vertices[6].Set(maxes.x, maxes.y, mins.z);
	vertices[7].Set(maxes.x, maxes.y, maxes.z);
}

void AA_BOUNDING_BOX::SetFromPoints(int numpoints, VECTOR3D *points)
{
	VECTOR3D min, max;

	// Setup
	min = max = points[0];

	for(long i=1;i<numpoints;i++)
	{
		if(points[i].x > max.x) max.x = points[i].x;
		if(points[i].y > max.y) max.y = points[i].y;
		if(points[i].z > max.z) max.z = points[i].z;

		if(points[i].x < min.x) min.x = points[i].x;
		if(points[i].y < min.y) min.y = points[i].y;
		if(points[i].z < min.z) min.z = points[i].z;
	}

	// Make bbox
	SetFromMinsMaxes(min, max);
}



//is a point in the box
bool AA_BOUNDING_BOX::IsPointInside(const VECTOR3D & point) const
{
	if(point.x < mins.x)
		return false;
	if(point.y < mins.y)
		return false;
	if(point.z < mins.z)
		return false;

	if(point.x > maxes.x)
		return false;
	if(point.y > maxes.y)
		return false;
	if(point.z > maxes.z)
		return false;

	return true;
}


float AA_BOUNDING_BOX::DistFromPoint( const VECTOR3D & point){
	
	float X = (point.x < mins.x) ? mins.x : (point.x > maxes.x) ? maxes.x : point.x;
	float Y = (point.y < mins.y) ? mins.y : (point.y > maxes.y) ? maxes.y : point.y;
	float Z = (point.z < mins.z) ? mins.z : (point.z > maxes.z) ? maxes.z : point.z;
	
	VECTOR3D delta = point - VECTOR3D(X,Y,Z);
	
	return sqrt(delta.DotProduct(delta)); // Very costly!!
}

void AA_BOUNDING_BOX::AddBounds(const AA_BOUNDING_BOX & bounds)
{
	// Go through bounds
	for(long i=0;i<8;i++)
	{
		if(bounds.vertices[i].x > maxes.x) maxes.x = bounds.vertices[i].x;
		if(bounds.vertices[i].y > maxes.y) maxes.y = bounds.vertices[i].y;
		if(bounds.vertices[i].z > maxes.z) maxes.z = bounds.vertices[i].z;

		if(bounds.vertices[i].x < mins.x) mins.x = bounds.vertices[i].x;
		if(bounds.vertices[i].y < mins.y) mins.y = bounds.vertices[i].y;
		if(bounds.vertices[i].z < mins.z) mins.z = bounds.vertices[i].z;
	}

	// Make bbox
	SetFromMinsMaxes(mins, maxes);
}

// BVH support methods

bool AA_BOUNDING_BOX::rayIntersect(const VECTOR3D& origin, const VECTOR3D& dir,
                                    float& tMin, float& tMax) const
{
	tMin = 0.0f;
	tMax = 1e30f;  // Large value instead of FLT_MAX for numerical stability

	// X axis slab
	if (fabs(dir.x) < EPSILON) {
		// Ray is parallel to X slab
		if (origin.x < mins.x || origin.x > maxes.x) return false;
	} else {
		float t1 = (mins.x - origin.x) / dir.x;
		float t2 = (maxes.x - origin.x) / dir.x;
		if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
		if (t1 > tMin) tMin = t1;
		if (t2 < tMax) tMax = t2;
		if (tMin > tMax) return false;
	}

	// Y axis slab
	if (fabs(dir.y) < EPSILON) {
		if (origin.y < mins.y || origin.y > maxes.y) return false;
	} else {
		float t1 = (mins.y - origin.y) / dir.y;
		float t2 = (maxes.y - origin.y) / dir.y;
		if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
		if (t1 > tMin) tMin = t1;
		if (t2 < tMax) tMax = t2;
		if (tMin > tMax) return false;
	}

	// Z axis slab
	if (fabs(dir.z) < EPSILON) {
		if (origin.z < mins.z || origin.z > maxes.z) return false;
	} else {
		float t1 = (mins.z - origin.z) / dir.z;
		float t2 = (maxes.z - origin.z) / dir.z;
		if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
		if (t1 > tMin) tMin = t1;
		if (t2 < tMax) tMax = t2;
		if (tMin > tMax) return false;
	}

	return true;
}

bool AA_BOUNDING_BOX::intersects(const AA_BOUNDING_BOX& other) const
{
	// Separating axis test for axis-aligned boxes
	if (maxes.x < other.mins.x || mins.x > other.maxes.x) return false;
	if (maxes.y < other.mins.y || mins.y > other.maxes.y) return false;
	if (maxes.z < other.mins.z || mins.z > other.maxes.z) return false;
	return true;
}

VECTOR3D AA_BOUNDING_BOX::center() const
{
	return VECTOR3D(
		(mins.x + maxes.x) * 0.5f,
		(mins.y + maxes.y) * 0.5f,
		(mins.z + maxes.z) * 0.5f
	);
}

VECTOR3D AA_BOUNDING_BOX::extent() const
{
	return VECTOR3D(
		(maxes.x - mins.x) * 0.5f,
		(maxes.y - mins.y) * 0.5f,
		(maxes.z - mins.z) * 0.5f
	);
}

float AA_BOUNDING_BOX::surfaceArea() const
{
	float dx = maxes.x - mins.x;
	float dy = maxes.y - mins.y;
	float dz = maxes.z - mins.z;
	return 2.0f * (dx * dy + dy * dz + dz * dx);
}

int AA_BOUNDING_BOX::longestAxis() const
{
	float dx = maxes.x - mins.x;
	float dy = maxes.y - mins.y;
	float dz = maxes.z - mins.z;

	if (dx >= dy && dx >= dz) return 0;  // X axis
	if (dy >= dz) return 1;              // Y axis
	return 2;                            // Z axis
}