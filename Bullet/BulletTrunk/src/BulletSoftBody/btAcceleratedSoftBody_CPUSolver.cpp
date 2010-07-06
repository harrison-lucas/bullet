/*
Bullet Continuous Collision Detection and Physics Library
Copyright (c) 2003-2006 Erwin Coumans  http://continuousphysics.com/Bullet/

This software is provided 'as-is', without any express or implied warranty.
In no event will the authors be held liable for any damages arising from the use of this software.
Permission is granted to anyone to use this software for any purpose, 
including commercial applications, and to alter it and redistribute it freely, 
subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not claim that you wrote the original software. If you use this software in a product, an acknowledgment in the product documentation would be appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
*/

#include "BulletCollision/CollisionShapes/btTriangleIndexVertexArray.h"
#include "BulletCollision/CollisionDispatch/btCollisionObject.h"
#include "BulletCollision/CollisionShapes/btCollisionShape.h"
#include "BulletMultiThreaded/vectormath/scalar/cpp/vectormath_aos.h"
#include "BulletMultiThreaded/vectormath/scalar/cpp/mat_aos.h"
#include "BulletMultiThreaded/vectormath/scalar/cpp/vec_aos.h"

#include "BulletSoftBody/btAcceleratedSoftBody_CPUSolver.h"
#include "BulletCollision/CollisionShapes/btCapsuleShape.h"


btCPUSoftBodySolver::btCPUSoftBodySolver()
{
	// Initial we will clearly need to update solver constants
	// For now this is global for the cloths linked with this solver - we should probably make this body specific 
	// for performance in future once we understand more clearly when constants need to be updated
	m_updateSolverConstants = true;
}

btCPUSoftBodySolver::~btCPUSoftBodySolver()
{
}

int btCPUSoftBodySolver::ownCloth( btAcceleratedSoftBodyInterface *cloth )
{
	// Ensure that per-cloth acceleration and velocity are large enough to cope
	int clothIdentifier = m_cloths.size();

	// TODO: Check that it's not already there and ensure it stays ordered
	m_cloths.push_back( cloth );	

	Vectormath::Aos::Vector3 zeroVector( 0.f, 0.f, 0.f );
	if( m_perClothAcceleration.size() <= clothIdentifier )
		m_perClothAcceleration.resize( clothIdentifier + 1, zeroVector );
	if( m_perClothWindVelocity.size() <= clothIdentifier )
		m_perClothWindVelocity.resize( clothIdentifier + 1, zeroVector );
	if( m_perClothDampingFactor.size() <= clothIdentifier )
		m_perClothDampingFactor.resize( clothIdentifier + 1, 0 );
	if( m_perClothVelocityCorrectionCoefficient.size() <= clothIdentifier )
		m_perClothVelocityCorrectionCoefficient.resize( clothIdentifier + 1 );
	if( m_perClothLiftFactor.size() <= clothIdentifier )
		m_perClothLiftFactor.resize( clothIdentifier + 1 );
	if( m_perClothDragFactor.size() <= clothIdentifier )
		m_perClothDragFactor.resize( clothIdentifier + 1 );
	if( m_perClothMediumDensity.size() <= clothIdentifier )
		m_perClothMediumDensity.resize( clothIdentifier + 1 );			
	if( m_perClothCollisionObjects.size() <= clothIdentifier )
		m_perClothCollisionObjects.resize( clothIdentifier + 1 );

	return clothIdentifier;
}

void btCPUSoftBodySolver::removeCloth( btAcceleratedSoftBodyInterface *cloth )
{
}

btSoftBodyLinkData &btCPUSoftBodySolver::getLinkData()
{
	return m_linkData;
}

btSoftBodyVertexData &btCPUSoftBodySolver::getVertexData()
{
	return m_vertexData;
}

btSoftBodyTriangleData &btCPUSoftBodySolver::getTriangleData()
{
	return m_triangleData;
}

void btCPUSoftBodySolver::addVelocity( Vectormath::Aos::Vector3 velocity )
{
	int numVertices = m_vertexData.getNumVertices();
	for( int vertexIndex = 0; vertexIndex < numVertices; ++vertexIndex )
	{
		if( m_vertexData.getInverseMass( vertexIndex ) > 0 )
			m_vertexData.getVelocity( vertexIndex ) += velocity;
	}
}

void btCPUSoftBodySolver::setPerClothAcceleration( int clothIdentifier, Vectormath::Aos::Vector3 acceleration )
{
	m_perClothAcceleration[clothIdentifier] = acceleration;
}

void btCPUSoftBodySolver::setPerClothWindVelocity( int clothIdentifier, Vectormath::Aos::Vector3 windVelocity )
{
	m_perClothWindVelocity[clothIdentifier] = windVelocity;
}

void btCPUSoftBodySolver::setPerClothMediumDensity( int clothIdentifier, float mediumDensity )
{
	m_perClothMediumDensity[clothIdentifier] = mediumDensity;
}

void btCPUSoftBodySolver::setPerClothDampingFactor( int clothIdentifier, float dampingFactor )
{
	m_perClothDampingFactor[clothIdentifier] = dampingFactor;
}

void btCPUSoftBodySolver::setPerClothVelocityCorrectionCoefficient( int clothIdentifier, float velocityCorrectionCoefficient )
{
	m_perClothVelocityCorrectionCoefficient[clothIdentifier] = velocityCorrectionCoefficient;
}		

void btCPUSoftBodySolver::setPerClothLiftFactor( int clothIdentifier, float liftFactor )
{
	m_perClothLiftFactor[clothIdentifier] = liftFactor;
}

/** Drag parameter for wind action on cloth. */
void btCPUSoftBodySolver::setPerClothDragFactor( int clothIdentifier, float dragFactor )
{
	m_perClothDragFactor[clothIdentifier] = dragFactor;
}


void btCPUSoftBodySolver::updateSoftBodies()
{
	using namespace Vectormath::Aos;

	int numVertices = m_vertexData.getNumVertices();
	int numTriangles = m_triangleData.getNumTriangles();

	// Initialise normal and vertex counts
	for( int vertexIndex = 0; vertexIndex < numVertices; ++vertexIndex )
	{
		m_vertexData.getArea(vertexIndex) = 0.f;
		m_vertexData.getNormal(vertexIndex) = Vector3(0.f, 0.f, 0.f);
	}

	// Update the areas for the triangles and vertices.
	for( int triangleIndex = 0; triangleIndex < numTriangles; ++triangleIndex )
	{
		float &triangleArea( m_triangleData.getTriangleArea( triangleIndex ) );
		const btSoftBodyTriangleData::TriangleNodeSet &vertices( m_triangleData.getVertexSet(triangleIndex) );

		Point3 &vertexPosition0( m_vertexData.getPosition( vertices.vertex0 ) );
		Point3 &vertexPosition1( m_vertexData.getPosition( vertices.vertex1 ) );
		Point3 &vertexPosition2( m_vertexData.getPosition( vertices.vertex2 ) );

		triangleArea = computeTriangleArea( vertexPosition0, vertexPosition1, vertexPosition2 );

		// Add to areas for vertices and increase the count of the number of triangles affecting the vertex
		m_vertexData.getArea(vertices.vertex0) += triangleArea;
		m_vertexData.getArea(vertices.vertex1) += triangleArea;
		m_vertexData.getArea(vertices.vertex2) += triangleArea;

		Point3 &vertex0( m_vertexData.getPosition(vertices.vertex0) );
		Point3 &vertex1( m_vertexData.getPosition(vertices.vertex1) );
		Point3 &vertex2( m_vertexData.getPosition(vertices.vertex2) );
		
		Vector3 triangleNormal = cross( vertex1-vertex0, vertex2 - vertex0 );

		m_triangleData.getNormal(triangleIndex) = normalize(triangleNormal);

		m_vertexData.getNormal(vertices.vertex0) += triangleNormal;
		m_vertexData.getNormal(vertices.vertex1) += triangleNormal;
		m_vertexData.getNormal(vertices.vertex2) += triangleNormal;

	}

	// Normalise the area and normals
	for( int vertexIndex = 0; vertexIndex < numVertices; ++vertexIndex )
	{
		m_vertexData.getArea(vertexIndex) /= m_vertexData.getTriangleCount(vertexIndex);
		m_vertexData.getNormal(vertexIndex) = normalize( m_vertexData.getNormal(vertexIndex) );
	}


	// Clear the collision shape array for the next frame
	m_collisionObjectDetails.clear();

} // updateSoftBodies


Vectormath::Aos::Vector3 btCPUSoftBodySolver::ProjectOnAxis( const Vectormath::Aos::Vector3 &v, const Vectormath::Aos::Vector3 &a )
{
	return a*Vectormath::Aos::dot(v, a);
}

void btCPUSoftBodySolver::ApplyClampedForce( float solverdt, const Vectormath::Aos::Vector3 &force, const Vectormath::Aos::Vector3 &vertexVelocity, float inverseMass, Vectormath::Aos::Vector3 &vertexForce )
{
	float dtInverseMass = solverdt*inverseMass;
	if( Vectormath::Aos::lengthSqr(force * dtInverseMass) > Vectormath::Aos::lengthSqr(vertexVelocity) )
	{
		vertexForce -= ProjectOnAxis( vertexVelocity, normalize( force ) )/dtInverseMass;
	} else {
		vertexForce += force;
	}
}

bool btCPUSoftBodySolver::checkInitialized()
{
	return true;
}

void btCPUSoftBodySolver::applyForces( float solverdt )
{		
	using namespace Vectormath::Aos;

	int numVertices = m_vertexData.getNumVertices();			
	for( int clothIndex = 0; clothIndex < m_cloths.size(); ++clothIndex )
	{
		btAcceleratedSoftBodyInterface *currentCloth = m_cloths[clothIndex];
		int clothIdentifier = currentCloth->getIdentifier();
		const int startVertex = currentCloth->getFirstVertex();
		const int numVertices = currentCloth->getNumVertices();

		Vector3 velocityChange = m_perClothAcceleration[clothIdentifier]*solverdt;
		for( int vertexIndex = startVertex; vertexIndex < (startVertex + numVertices); ++vertexIndex )
		{
			float inverseMass = m_vertexData.getInverseMass( vertexIndex );
			Vector3 &vertexVelocity( m_vertexData.getVelocity( vertexIndex ) );

			// First apply the global acceleration to all vertices
			if( inverseMass > 0 )
				vertexVelocity += velocityChange;

			// If it's a non-static vertex
			if( m_vertexData.getInverseMass(vertexIndex) > 0 )
			{
				// Wind effects on a wind-per-cloth basis
				float liftFactor = m_perClothLiftFactor[clothIdentifier];
				float dragFactor = m_perClothDragFactor[clothIdentifier];
				if( (liftFactor > 0.f) || (dragFactor > 0.f) )
				{
					Vector3 normal = m_vertexData.getNormal(vertexIndex);
					Vector3 relativeWindVelocity = m_vertexData.getVelocity(vertexIndex) - m_perClothWindVelocity[clothIndex];
					float relativeSpeedSquared = lengthSqr(relativeWindVelocity);
					if( relativeSpeedSquared > FLT_EPSILON )
					{
						normal = normal * (dot(normal, relativeWindVelocity) < 0 ? -1.f : +1.f);
						float dvNormal = dot(normal, relativeWindVelocity);
						if( dvNormal > 0 )
						{
							Vector3 force( 0.f, 0.f, 0.f );
							float c0 = m_vertexData.getArea(vertexIndex) * dvNormal * relativeSpeedSquared / 2;
							float c1 = c0 * m_perClothMediumDensity[clothIndex];
							force += normal * (-c1 * liftFactor);
							force += normalize(relativeWindVelocity)*(-c1 * dragFactor);

							Vectormath::Aos::Vector3 &vertexForce( m_vertexData.getForceAccumulator(vertexIndex) );
							ApplyClampedForce( solverdt, force, vertexVelocity, inverseMass, vertexForce );
						}
					}
				}
			}
		}
	}
} // btCPUSoftBodySolver::applyForces

/**
 * Integrate motion on the solver.
 */
void btCPUSoftBodySolver::integrate( float solverdt )
{
	using namespace Vectormath::Aos;
	int numVertices = m_vertexData.getNumVertices();
	for( int vertexIndex = 0; vertexIndex < numVertices; ++vertexIndex )
	{
		Point3 &position( m_vertexData.getPosition(vertexIndex) );
		Point3 &previousPosition( m_vertexData.getPreviousPosition(vertexIndex) );
		Vector3 &forceAccumulator( m_vertexData.getForceAccumulator(vertexIndex) );
		Vector3 &velocity( m_vertexData.getVelocity(vertexIndex) );
		float inverseMass = m_vertexData.getInverseMass(vertexIndex);

		previousPosition = position;
		velocity += forceAccumulator * inverseMass * solverdt;
		position += velocity * solverdt;
		forceAccumulator = Vector3(0.f, 0.f, 0.f);
	}	
} // btCPUSoftBodySolver::integrate

float btCPUSoftBodySolver::computeTriangleArea( 
	const Vectormath::Aos::Point3 &vertex0,
	const Vectormath::Aos::Point3 &vertex1,
	const Vectormath::Aos::Point3 &vertex2 )
{
	Vectormath::Aos::Vector3 a = vertex1 - vertex0;
	Vectormath::Aos::Vector3 b = vertex2 - vertex0;
	Vectormath::Aos::Vector3 crossProduct = cross(a, b);
	float area = length( crossProduct );
	return area;
}

void btCPUSoftBodySolver::updateConstants( float timeStep )
{
	using namespace Vectormath::Aos;

	if( m_updateSolverConstants )
	{
		m_updateSolverConstants = false;

		// Will have to redo this if we change the structure (tear, maybe) or various other possible changes

		// Initialise link constants
		const int numLinks = m_linkData.getNumLinks();
		for( int linkIndex = 0; linkIndex < numLinks; ++linkIndex )
		{
			btSoftBodyLinkData::LinkNodePair &vertices( m_linkData.getVertexPair(linkIndex) );
			m_linkData.getRestLength(linkIndex) = length((m_vertexData.getPosition( vertices.vertex0 ) - m_vertexData.getPosition( vertices.vertex1 )));
			float invMass0 = m_vertexData.getInverseMass(vertices.vertex0);
			float invMass1 = m_vertexData.getInverseMass(vertices.vertex1);
			float linearStiffness = m_linkData.getLinearStiffnessCoefficient(linkIndex);
			float massLSC = (invMass0 + invMass1)/linearStiffness;
			m_linkData.getMassLSC(linkIndex) = massLSC;
			float restLength = m_linkData.getRestLength(linkIndex);
			float restLengthSquared = restLength*restLength;
			m_linkData.getRestLengthSquared(linkIndex) = restLengthSquared;
		}
	}
} // btCPUSoftBodySolver::updateConstants

/**
 * Sort the collision object details array and generate indexing into it for the per-cloth collision object array.
 */
void btCPUSoftBodySolver::prepareCollisionConstraints()
{
	// First do a simple radix sort on the collision objects
	btAlignedObjectArray<int> numObjectsPerClothPrefixSum;
	btAlignedObjectArray<int> numObjectsPerCloth;
	numObjectsPerCloth.resize( m_cloths.size(), 0 );
	numObjectsPerClothPrefixSum.resize( m_cloths.size(), 0 );

	btAlignedObjectArray< CollisionShapeDescription > m_collisionObjectDetailsCopy(m_collisionObjectDetails);
	// Count and prefix sum number of previous cloths
	for( int collisionObject = 0; collisionObject < m_collisionObjectDetailsCopy.size(); ++collisionObject )
	{
		CollisionShapeDescription &shapeDescription( m_collisionObjectDetailsCopy[collisionObject] );
		++numObjectsPerClothPrefixSum[shapeDescription.softBodyIdentifier];	
	}
	int sum = 0;
	for( int cloth = 0; cloth < m_cloths.size(); ++cloth )
	{
		int currentValue = numObjectsPerClothPrefixSum[cloth];
		numObjectsPerClothPrefixSum[cloth] = sum;
		sum += currentValue;
	}
	// Move into the target array
	for( int collisionObject = 0; collisionObject < m_collisionObjectDetailsCopy.size(); ++collisionObject )
	{
		CollisionShapeDescription &shapeDescription( m_collisionObjectDetailsCopy[collisionObject] );
		int clothID = shapeDescription.softBodyIdentifier;
		int newLocation = numObjectsPerClothPrefixSum[clothID] + numObjectsPerCloth[clothID];
		numObjectsPerCloth[shapeDescription.softBodyIdentifier]++;
		m_collisionObjectDetails[newLocation] = shapeDescription;
	}
	for( int collisionObject = 0; collisionObject < m_collisionObjectDetailsCopy.size(); ++collisionObject )
	{
		CollisionShapeDescription &shapeDescription( m_collisionObjectDetails[collisionObject] );
	}

	// Generating indexing for perClothCollisionObjects
	// First clear the previous values
	for( int clothIndex = 0; clothIndex < m_perClothCollisionObjects.size(); ++clothIndex )
	{
		m_perClothCollisionObjects[clothIndex].firstObject = 0;
		m_perClothCollisionObjects[clothIndex].endObject = 0;
	}
	int currentCloth = 0;
	int startIndex = 0;
	for( int collisionObject = 0; collisionObject < m_collisionObjectDetails.size(); ++collisionObject )
	{
		int nextCloth = m_collisionObjectDetails[collisionObject].softBodyIdentifier;
		if( nextCloth != currentCloth )
		{	
			// Changed cloth in the array
			// Set the end index and the range is what we need for currentCloth
			m_perClothCollisionObjects[currentCloth].firstObject = startIndex;
			m_perClothCollisionObjects[currentCloth].endObject = collisionObject;
			currentCloth = nextCloth;
			startIndex = collisionObject;
		}
	}	
	//m_perClothCollisionObjects
} // prepareCollisionConstraints


void btCPUSoftBodySolver::solveConstraints( float solverdt )
{
	using Vectormath::Aos::Vector3;
	using Vectormath::Aos::Point3;
	using Vectormath::Aos::lengthSqr;
	using Vectormath::Aos::dot;

	// Prepare links
	int numLinks = m_linkData.getNumLinks();
	int numVertices = m_vertexData.getNumVertices();

	float kst = 1.f;

	for( int linkIndex = 0; linkIndex < numLinks; ++linkIndex )
	{			
		btSoftBodyLinkData::LinkNodePair &nodePair( m_linkData.getVertexPair(linkIndex) );
		Vector3 currentLength = m_vertexData.getPreviousPosition( nodePair.vertex1 ) - m_vertexData.getPreviousPosition( nodePair.vertex0 );
		m_linkData.getCurrentLength(linkIndex) = currentLength;

		// If mass at both ends of links is 0 (both static points) then we don't want this information.
		// In reality this would be a fairly pointless link, but it could have been inserted
		float linkLengthRatio = 0;
		if( m_linkData.getMassLSC(linkIndex) > 0 )
			linkLengthRatio = 1.f/(lengthSqr(currentLength) * m_linkData.getMassLSC(linkIndex));
		m_linkData.getLinkLengthRatio(linkIndex) = linkLengthRatio;

	}

	prepareCollisionConstraints();

	// Solve collision constraints
	// Very simple solver that pushes the vertex out of collision imposters for now
	// to test integration with the broad phase code.
	// May also want to put this into position solver loop every n iterations depending on
	// how it behaves
	for( int clothIndex = 0; clothIndex < m_cloths.size(); ++clothIndex )
	{
		btAcceleratedSoftBodyInterface *currentCloth = m_cloths[clothIndex];
		int clothIdentifier = currentCloth->getIdentifier();
		const int startVertex = currentCloth->getFirstVertex();
		const int numVertices = currentCloth->getNumVertices();
		int endVertex = startVertex + numVertices;

		int startObject = m_perClothCollisionObjects[clothIndex].firstObject;
		int endObject = m_perClothCollisionObjects[clothIndex].endObject;

		for( int collisionObject = startObject; collisionObject < endObject; ++collisionObject )
		{
			CollisionShapeDescription &shapeDescription( m_collisionObjectDetails[collisionObject] );

			if( shapeDescription.collisionShapeType == CAPSULE_SHAPE_PROXYTYPE )
			{
				using namespace Vectormath::Aos;

				float capsuleHalfHeight = shapeDescription.shapeInformation.capsule.halfHeight;
				float capsuleRadius = shapeDescription.shapeInformation.capsule.radius;
				Transform3 worldTransform = shapeDescription.shapeTransform;
				for( int vertexIndex = startVertex; vertexIndex < endVertex; ++vertexIndex )
				{		
					Point3 vertex( m_vertexData.getPosition( vertexIndex ) );
					Point3 c1(0.f, -capsuleHalfHeight, 0.f); 
					Point3 c2(0.f, +capsuleHalfHeight, 0.f);
					Point3 worldC1 = worldTransform * c1;
					Point3 worldC2 = worldTransform * c2;
					Vector3 segment = worldC2 - worldC1;

					// compute distance of tangent to vertex along line segment in capsule
					float distanceAlongSegment = -( dot( worldC1 - vertex, segment ) / lengthSqr(segment) );

					Point3 closestPoint = (worldC1 + segment * distanceAlongSegment);
					float distanceFromLine = length(vertex - closestPoint);
					float distanceFromC1 = length(worldC1 - vertex);
					float distanceFromC2 = length(worldC2 - vertex);
					
					// Final distance from collision, point to push from, direction to push in
					// for impulse force
					float distance;
					Point3 sourcePoint;
					Vector3 pushVector;
					if( distanceAlongSegment < 0 )
					{
						distance = distanceFromC1;
						sourcePoint = worldC1;
						pushVector = normalize(vertex - worldC1);
					} else if( distanceAlongSegment > 1.f ) {
						distance = distanceFromC1;
						sourcePoint = worldC1;
						pushVector = normalize(vertex - worldC1);	
					} else {
						distance = distanceFromLine;
						sourcePoint = closestPoint;
						pushVector = normalize(vertex - closestPoint);
					}

					// For now just update vertex position by moving to radius distance along the push vector
					// Could use this as the basis for simple vector distance constraint for the point later, possibly?
					// That way in the main solver loop all shape types could be the same... though when
					// we need to apply bi-directionally it becomes more complicated
					m_vertexData.getPosition( vertexIndex ) = closestPoint + capsuleRadius * pushVector;
				}
			}
		}
	}

	// Solve drift
	for( int iteration = 0; iteration < m_numberOfPositionIterations ; ++iteration )
	{
		for( int clothIndex = 0; clothIndex < m_cloths.size(); ++clothIndex )
		{
			btAcceleratedSoftBodyInterface *currentCloth = m_cloths[clothIndex];
			int clothIdentifier = currentCloth->getIdentifier();
			const int startLink = currentCloth->getFirstLink();
			const int numLinks = currentCloth->getNumLinks();

			int endLink = startLink + numLinks;
			for(int linkIndex = startLink; linkIndex < endLink; ++linkIndex)
			{			
				int vertexIndex0 = m_linkData.getVertexPair(linkIndex).vertex0;
				int vertexIndex1 = m_linkData.getVertexPair(linkIndex).vertex1;

				float massLSC = m_linkData.getMassLSC(linkIndex);
				if( massLSC > 0.f )
				{
					Point3 &vertexPosition0( m_vertexData.getPosition( vertexIndex0 ) );
					Point3 &vertexPosition1( m_vertexData.getPosition( vertexIndex1 ) );

					Vector3 del = vertexPosition1 - vertexPosition0;
					float len = lengthSqr(del);
					float restLength2 = m_linkData.getRestLengthSquared(linkIndex);
					float k = ((restLength2 - len) / (massLSC * (restLength2 + len) ) )*kst;

					vertexPosition0 -= del*(k*m_vertexData.getInverseMass(vertexIndex0));
					vertexPosition1 += del*(k*m_vertexData.getInverseMass(vertexIndex1));
				}
			}
		}
	}
	for( int clothIndex = 0; clothIndex < m_cloths.size(); ++clothIndex )
	{
		btAcceleratedSoftBodyInterface *currentCloth = m_cloths[clothIndex];
		int clothIdentifier = currentCloth->getIdentifier();
		const int startLink = currentCloth->getFirstLink();
		const int numLinks = currentCloth->getNumLinks();
		const int startVertex = currentCloth->getFirstVertex();
		const int numVertices = currentCloth->getNumVertices();
		const int lastVertex = startVertex + numVertices;
		// Update the velocities based on the change in position
		// TODO: Damping should only be applied to the action of link constraints so the cloth still falls but then moves stiffly once it hits something
		float velocityCoefficient = (1.f - m_perClothDampingFactor[clothIdentifier]);
		float velocityCorrectionCoefficient = m_perClothVelocityCorrectionCoefficient[clothIdentifier];
		float isolverDt = 1.f/solverdt;

		// If we didn't compute the velocity iteratively then we compute it purely based on the position change
		for(int vertexIndex = startVertex; vertexIndex < lastVertex; ++vertexIndex)
		{
			m_vertexData.getVelocity(vertexIndex) = (m_vertexData.getPosition(vertexIndex) - m_vertexData.getPreviousPosition(vertexIndex)) * velocityCoefficient * isolverDt;
			m_vertexData.getForceAccumulator( vertexIndex ) = Vector3(0.f, 0.f, 0.f);
		}
	}
} // btCPUSoftBodySolver::solveConstraints


void btCPUSoftBodySolver::outputToVertexBuffers()
{
	// Currently only support CPU output buffers
	// TODO: check for DX11 buffers. Take all offsets into the same DX11 buffer
	// and use them together on a single kernel call if possible by setting up a
	// per-cloth target buffer array for the copy kernel.
	for( int clothIndex = 0; clothIndex < m_cloths.size(); ++clothIndex )
	{
		btAcceleratedSoftBodyInterface *currentCloth = m_cloths[clothIndex];

		if( btVertexBufferDescriptor *vertexBufferTarget = currentCloth->getVertexBufferTarget() )
		{
			if( vertexBufferTarget->getBufferType() == btVertexBufferDescriptor::CPU_BUFFER )
			{
				const int firstVertex = currentCloth->getFirstVertex();
				const int lastVertex = firstVertex + currentCloth->getNumVertices();
				const btCPUVertexBufferDescriptor *cpuVertexBuffer = static_cast< btCPUVertexBufferDescriptor* >(vertexBufferTarget);						
				float *basePointer = cpuVertexBuffer->getBasePointer();						

				if( vertexBufferTarget->hasVertexPositions() )
				{
					const int vertexOffset = cpuVertexBuffer->getVertexOffset();
					const int vertexStride = cpuVertexBuffer->getVertexStride();
					float *vertexPointer = basePointer + vertexOffset;

					for( int vertexIndex = firstVertex; vertexIndex < lastVertex; ++vertexIndex )
					{
						Vectormath::Aos::Point3 position = m_vertexData.getPosition(vertexIndex);
						*(vertexPointer + 0) = position.getX();
						*(vertexPointer + 1) = position.getY();
						*(vertexPointer + 2) = position.getZ();
						vertexPointer += vertexStride;
					}
				}
				if( vertexBufferTarget->hasNormals() )
				{
					const int normalOffset = cpuVertexBuffer->getNormalOffset();
					const int normalStride = cpuVertexBuffer->getNormalStride();
					float *normalPointer = basePointer + normalOffset;

					for( int vertexIndex = firstVertex; vertexIndex < lastVertex; ++vertexIndex )
					{
						Vectormath::Aos::Vector3 normal = m_vertexData.getNormal(vertexIndex);
						*(normalPointer + 0) = normal.getX();
						*(normalPointer + 1) = normal.getY();
						*(normalPointer + 2) = normal.getZ();
						normalPointer += normalStride;
					}
				}
			}
		}
	}
} // btCPUSoftBodySolver::outputToVertexBuffers

void btCPUSoftBodySolver::optimize()
{
	updateConstants(0.f);
}

/** Return the softbody object represented by softBodyIndex */
btAcceleratedSoftBodyInterface *btCPUSoftBodySolver::getSoftBody( int softBodyIndex )
{
	return m_cloths[softBodyIndex];
}

static Vectormath::Aos::Vector3 toVector3( const btVector3 &vec )
{
	Vectormath::Aos::Vector3 outVec( vec.getX(), vec.getY(), vec.getZ() );
	return outVec;
}

static Vectormath::Aos::Transform3 toTransform3( const btTransform &transform )
{
	Vectormath::Aos::Transform3 outTransform;
	outTransform.setCol(0, toVector3(transform.getBasis().getColumn(0)));
	outTransform.setCol(1, toVector3(transform.getBasis().getColumn(1)));
	outTransform.setCol(2, toVector3(transform.getBasis().getColumn(2)));
	outTransform.setCol(3, toVector3(transform.getOrigin()));
	return outTransform;	
}

void btCPUSoftBodySolver::addCollisionObjectForSoftBody( int clothIdentifier, btCollisionObject *collisionObject )
{
	btCollisionShape *collisionShape = collisionObject->getCollisionShape();
	int shapeType = collisionShape->getShapeType();
	if( shapeType == CAPSULE_SHAPE_PROXYTYPE )
	{
		// Add to the list of expected collision objects
		CollisionShapeDescription newCollisionShapeDescription;
		newCollisionShapeDescription.softBodyIdentifier = clothIdentifier;
		newCollisionShapeDescription.collisionShapeType = shapeType;
		newCollisionShapeDescription.shapeTransform = toTransform3(collisionObject->getWorldTransform());
		btCapsuleShape *capsule = static_cast<btCapsuleShape*>( collisionShape );
		newCollisionShapeDescription.shapeInformation.capsule.radius = capsule->getRadius();
		newCollisionShapeDescription.shapeInformation.capsule.halfHeight = capsule->getHalfHeight();
		m_collisionObjectDetails.push_back( newCollisionShapeDescription );

		// TODO: In the collision function, sort the above array on the clothIdentifier and generate the start and end indices
	} else {
		btAssert("Unsupported collision shape type\n");
	}
}