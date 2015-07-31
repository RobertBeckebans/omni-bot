////////////////////////////////////////////////////////////////////////////////
//
// $LastChangedBy$
// $LastChangedDate$
// $LastChangedRevision$
//
////////////////////////////////////////////////////////////////////////////////

#include "PathPlannerRecast.h"
#include "IGameManager.h"
#include "IGame.h"
#include "GoalManager.h"
#include "Path.h"
#include "gmUtilityLib.h"
#include "Client.h"
#include "InterfaceFuncs.h"
#include "ProtoBufUtility.h"
#include "RenderBuffer.h"

#include "PathPlannerRecastPathInterface.h"

#include <DebugDraw.h>

#include <Recast.h>
#include <RecastDebugDraw.h>

#include <DetourCommon.h>
#include <DetourTileCache.h>
#include <DetourTileCacheBuilder.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>
#include <DetourNavMeshBuilder.h>
#include <DetourDebugDraw.h>

#include "recast.pb.h"
#include "modeldata.pb.h"

#pragma warning( push )
#pragma warning( disable: 4244 )
#include "google/protobuf/text_format.h"
#include "google/protobuf/io/coded_stream.h"
//#include "google/protobuf/io/gzip_stream.h"
#include "google/protobuf/io/zero_copy_stream_impl.h"
#include "fastlz/fastlz.h"
#include "sqlite3.h"
#pragma warning( pop )

//////////////////////////////////////////////////////////////////////////

unsigned int dtAreaMaskColor( navAreaMask mask )
{
	obColor clr = COLOR::WHITE;

	if ( mask & NAVFLAGS_WALK )
		clr = obColor( 255, 255, 255, 150 );
	if ( mask & NAVFLAGS_CROUCH )
		clr = obColor( 195, 195, 195, 150 );
	if ( mask & NAVFLAGS_PRONE )
		clr = obColor( 140, 140, 140, 150 );

	if ( mask & NAVFLAGS_ALLTEAMS )
	{
		clr.MultRGB( 0.5 );
		clr.set_g( 255 );
	}
	if ( mask & NAVFLAGS_WATER )
	{
		clr.MultRGB( 0.5 );
		clr.set_b( 255 );
	}
	if ( mask & NAVFLAGS_THREATS )
	{
		clr.MultRGB( 0.5 );
		clr.set_r( 255 );
	}
	if ( mask & ( NAVFLAGS_JUMP | NAVFLAGS_LADDER | NAVFLAGS_TELEPORT | NAVFLAGS_DOOR | NAVFLAGS_ROCKETJUMP | NAVFLAGS_MOVER | NAVFLAGS_JUMPPAD ) )
		clr.MultRGB( 0.5 );

	if ( mask & NAVFLAGS_PUSHABLE )
		clr.fade( 64 );

	if ( mask & NAVFLAGS_DISABLED )
		clr = COLOR::BLACK.fade( 100 );

	return clr.rgba();
}

//////////////////////////////////////////////////////////////////////////

static const uint8_t TILE_DATA_VERSION = 3;

static Vector3f Convert( const RecastIO::Vec3 & vec )
{
	return Vector3f( vec.x(), vec.y(), vec.z() );
}

void MarkDirtyTiles( const dtMeshTile * tile, void * userdata )
{
	PathPlannerRecast * sys = static_cast<PathPlannerRecast*>( userdata );
	sys->MarkTileForBuilding( tile );
}

DefaultMemoryAllocator gDefaultAllocator;

Vector3f rcToLocal( const float * vec )
{
	return Vector3f( vec[ 0 ], vec[ 2 ], vec[ 1 ] );
}

Vector3f localToRc( const float * vec )
{
	return Vector3f( vec[ 0 ], vec[ 2 ], vec[ 1 ] );
}

const Vector3f PathPlannerRecast::sExtents = localToRc( Vector3f( 16.f, 16.f, 64.f ) );

struct DebugDraw : public duDebugDraw
{
	DebugDraw() : mVertCount( 0 ), mSizeHint( 1.0f )
	{
	}

	virtual void depthMask( bool state )
	{
		// not supported
	}

	virtual void texture( bool state )
	{
		// not supported
	}

	virtual void begin( duDebugDrawPrimitives prim, float size = 1.0f )
	{
		mActivePrim = prim;
		mSizeHint = size;
		mVertCount = 0;
	}

	virtual void vertex( const float* pos, unsigned int color )
	{
		vertex( pos[ 0 ], pos[ 1 ], pos[ 2 ], color );
	}

	virtual void vertex( const float x, const float y, const float z, unsigned int color )
	{
		mVertCache[ mVertCount++ ] = Vector3f( x, z, y );

		switch ( mActivePrim )
		{
			case DU_DRAW_POINTS:
				if ( mVertCount == 1 )
				{
					int r, g, b, a;
					duRGBASplit( color, r, g, b, a );

					RenderBuffer::AddPoint( mVertCache[ 0 ], obColor( (uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)a ), mSizeHint );
					mVertCount = 0;
				}
				break;
			case DU_DRAW_LINES:
				if ( mVertCount == 2 )
				{
					int r, g, b, a;
					duRGBASplit( color, r, g, b, a );

					RenderBuffer::AddLine( mVertCache[ 0 ], mVertCache[ 1 ], obColor( (uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)a ), mSizeHint );
					mVertCount = 0;
				}
				break;
			case DU_DRAW_TRIS:
				if ( mVertCount == 3 )
				{
					int r, g, b, a;
					duRGBASplit( color, r, g, b, a );

					RenderBuffer::AddTri( mVertCache[ 0 ], mVertCache[ 1 ], mVertCache[ 2 ], obColor( (uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)a ) );
					mVertCount = 0;
				}
				break;
			case DU_DRAW_QUADS:
				if ( mVertCount == 4 )
				{
					int r, g, b, a;
					duRGBASplit( color, r, g, b, a );

					RenderBuffer::AddQuad( mVertCache[ 0 ], mVertCache[ 1 ], mVertCache[ 2 ], mVertCache[ 3 ], obColor( (uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)a ) );
					mVertCount = 0;
				}
				break;
		}
	}

	virtual void vertex( const float* pos, unsigned int color, const float* uv )
	{
		vertex( pos[ 0 ], pos[ 1 ], pos[ 2 ], color );
	}

	virtual void vertex( const float x, const float y, const float z, unsigned int color, const float u, const float v )
	{
		// uvs not supported
		vertex( x, y, z, color );
	}

	virtual void end()
	{
		mVertCount = 0;
	}
private:
	duDebugDrawPrimitives	mActivePrim;
	float					mSizeHint;
	Vector3f				mVertCache[ 4 ];
	int						mVertCount;
} ddraw;

float dtRandom()
{
	return Mathf::UnitRandom();
}

static const int MAX_LAYERS = 32;

//////////////////////////////////////////////////////////////////////////

PathPlannerRecast::RecastSettings::RecastSettings()
{
	AgentHeightStand = 0.f;
	AgentHeightCrouch = 0.f;
	AgentRadius = 0.f;
	AgentMaxClimb = 0.f;

	WalkableSlopeAngle = 45.0f;

	CellSize = 4.0f;
	CellHeight = 4.0f;

	EdgeMaxLen = 1000.0f;
	MaxSimplificationError = 1.f;
	TileSize = 128;
	DetailSampleDist = 6.0f;
	DetailSampleMaxError = 1.0f;
}

//////////////////////////////////////////////////////////////////////////

struct RasterizationContext
{
	RasterizationContext()
		: solid( NULL )
		, triareas( NULL )
		, lset( NULL )
		, chf( NULL )
		, cset( NULL )
		, polymesh( NULL )
		, meshdetail( NULL )
	{
	}

	~RasterizationContext()
	{
		rcFreeHeightField( solid );
		delete [] triareas;
		rcFreeHeightfieldLayerSet( lset );
		rcFreeCompactHeightfield( chf );
		rcFreeContourSet( cset );
		rcFreePolyMesh( polymesh );
		rcFreePolyMeshDetail( meshdetail );
	}

	rcHeightfield			* solid;
	unsigned char			* triareas;
	rcHeightfieldLayerSet	* lset;
	rcCompactHeightfield	* chf;
	rcContourSet			* cset;
	rcPolyMesh				* polymesh;
	rcPolyMeshDetail		* meshdetail;
};

static bool gAsyncBuildRunning = true;

void AsyncTileBuild( PathPlannerRecast * nav, int num )
{
	rmt_SetCurrentThreadName( va( "AsyncTileBuild(%d)", num ) );
	while ( gAsyncBuildRunning )
	{
		TileRebuild buildTile;
		if ( nav->AsyncGetTileRebuild( buildTile ) )
		{
			const int tx = buildTile.mX;
			const int ty = buildTile.mY;

			nav->RasterizeTileLayers( tx, ty );
		}
	}
}

//////////////////////////////////////////////////////////////////////////

TileRebuild::TileRebuild( int x, int y ) 
	: mX( x )
	, mY( y )
{
}

TileAddData::TileAddData( int x, int y )
	: mX( x )
	, mY( y )
	, mNavData( NULL )
	, mNavDataSize( 0 )
{
}
//////////////////////////////////////////////////////////////////////////

PathPlannerRecast::PathPlannerRecast()
	: mNavMesh( NULL )
	, mNavMeshQuery( NULL )
	, mCurrentTool( NULL )
	, mDeferredSaveNav( false )
{
	mTileBuildQueue.reserve( 128 );

	//mPlannerFlags.SetFlag( NAV_VIEW );
}

PathPlannerRecast::~PathPlannerRecast()
{
	Shutdown();
}

bool PathPlannerRecast::Init( System & system )
{
	rmt_ScopedCPUSample( RecastInit );

	InitCommands();
	
	// subtract a thread to try and leave the current core free
	int numBuildThreads = boost::thread::hardware_concurrency() - 1;
	Options::GetValue( "Navigation", "TileBuildThreads", numBuildThreads );
	if ( numBuildThreads < 1 )
		numBuildThreads = 1;

	for ( int i = 0; i < numBuildThreads; ++i )
		mThreadGroup.add_thread( new boost::thread( AsyncTileBuild, this, i ) );

	EngineFuncs::ConsoleMessage( va( "Created %d threads to rebuild navigation tiles", mThreadGroup.size() ) );

	return true;
}

void PathPlannerRecast::RegisterScriptFunctions( gmMachine *a_machine )
{
}

void PathPlannerRecast::MarkTileForBuilding( const Vector3f & pos )
{
	boost::lock_guard<boost::recursive_mutex> lock( mGuardBuildQueue );
	mNavMesh->queryTiles( localToRc( pos ), localToRc( pos ), MarkDirtyTiles, this );
}

void PathPlannerRecast::MarkTileForBuilding( const Vector3f & mins, const Vector3f & maxs )
{ 
	boost::lock_guard<boost::recursive_mutex> lock( mGuardBuildQueue );
	mNavMesh->queryTiles( localToRc( mins ), localToRc( maxs ), MarkDirtyTiles, this );
}

void PathPlannerRecast::MarkTileForBuilding( const AxisAlignedBox3f & oldBounds, const AxisAlignedBox3f & newBounds )
{
	boost::lock_guard<boost::recursive_mutex> lock( mGuardBuildQueue );
	mNavMesh->queryTiles( localToRc( oldBounds.Min ), localToRc( oldBounds.Max ), MarkDirtyTiles, this );
	mNavMesh->queryTiles( localToRc( newBounds.Min ), localToRc( newBounds.Max ), MarkDirtyTiles, this );
}

void PathPlannerRecast::MarkTileForBuilding( const int tx, const int ty )
{
	for ( size_t j = 0; j < mTileBuildQueue.size(); ++j )
	{
		if ( mTileBuildQueue[ j ].mX == tx && mTileBuildQueue[ j ].mY == ty )
		{
			return;
		}
	}
	mTileBuildQueue.push_back( TileRebuild( tx, ty ) );
}

void PathPlannerRecast::MarkTileForBuilding( const dtMeshTile * tile )
{
	for ( size_t j = 0; j < mTileBuildQueue.size(); ++j )
	{
		if ( mTileBuildQueue[ j ].mX == tile->header->x &&
			mTileBuildQueue[ j ].mY == tile->header->y )
		{
			return;
		}
	}
	mTileBuildQueue.push_back( TileRebuild( tile->header->x, tile->header->y ) );
}

bool PathPlannerRecast::AsyncGetTileRebuild( TileRebuild & buildTile )
{
	boost::lock_guard<boost::recursive_mutex> lock( mGuardBuildQueue );
	if ( mTileBuildQueue.size() > 0 )
	{
		buildTile = mTileBuildQueue.front();
		mTileBuildQueue.erase( mTileBuildQueue.begin() );

		EngineFuncs::ConsoleMessage( va( "AsyncTileBuild(%d,%d): %d tiles remaining", buildTile.mX, buildTile.mY, mTileBuildQueue.size() ) );
		return true;
	}
	return false;
}

void PathPlannerRecast::Update( System & system )
{
	rmt_ScopedCPUSample( RecastUpdate );

	if ( mCurrentTool )
	{
		if ( !mCurrentTool->Update( this ) )
			OB_DELETE( mCurrentTool );
	}

	UpdateDeferredModels();
	UpdateModelState( false );
	
	if ( mDeferredSaveNav )
	{
		boost::lock_guard<boost::recursive_mutex> lock( mGuardBuildQueue );
		if ( mTileBuildQueue.size() == 0 )
		{
			if ( Save( gEngineFuncs->GetMapName() ) )
				EngineFuncs::ConsoleMessage( "Saved Base Nav." );
			else
				EngineFuncs::ConsoleError( "ERROR Saving Base Nav." );

			mDeferredSaveNav = false;
		}
	}

	if ( mFlags.mViewMode > 0 )
	{
		rmt_ScopedCPUSample( NavView );
		
		std::string aimInfo = "nav: ";

		for ( int i = 0; i < mContext.getLogCount(); ++i )
		{
			const char * logTxt = mContext.getLogText( i );
			EngineFuncs::ConsoleMessage( logTxt );
		}
		mContext.resetLog();
		
		for ( size_t i = 0; i < mExclusionZones.size(); ++i )
			RenderBuffer::AddAABB( mExclusionZones[ i ], COLOR::RED );

		RenderBuffer::AddAABB( mNavigationBounds, COLOR::MAGENTA );

		// surface probe
		int contents = 0, surface = 0;
		Vector3f vAimPt, vAimNormal;
		if ( Utils::GetLocalAimPoint( vAimPt, &vAimNormal, TR_MASK_FLOODFILLENT, &contents, &surface ) )
		{
			obColor clr = COLOR::WHITE;

			if ( surface & SURFACE_LADDER )
			{
				clr = COLOR::ORANGE;
			}
			RenderBuffer::AddLine( vAimPt, vAimPt + vAimNormal * 16.f, clr );

			if ( mNavMeshQuery )
			{
				const Vector3f sExtents = localToRc( Vector3f( 16.f, 16.f, 64.f ) );

				dtQueryFilter filter;
				filter.setIncludeFlags( ( navAreaMask )- 1 );
				filter.setExcludeFlags( 0 );

				dtPolyRef aimPoly = 0;
				Vector3f startPos = localToRc( vAimPt );
				if ( dtStatusSucceed( mNavMeshQuery->findNearestPoly( startPos, sExtents, &filter, &aimPoly, startPos ) ) )
				{
					navAreaMask aimAreaMask = 0;
					if ( dtStatusSucceed( mNavMesh->getPolyFlags( aimPoly, &aimAreaMask ) ) )
					{
						std::string str;
						NavAreaFlagsEnum::NameForValueBitfield( (NavAreaFlags)aimAreaMask, str );
						aimInfo += str;
					}
				}
			}
		}

		if ( mNavMesh != NULL )
		{
			Vector3f
				vMins = localToRc( mNavigationBounds.Min ),
				vMaxs = localToRc( mNavigationBounds.Max );

			int gw = 0, gh = 0;
			rcCalcGridSize( vMins, vMaxs, mSettings.CellSize, &gw, &gh );

			const int ts = mSettings.TileSize;
			const int tw = ( gw + ts - 1 ) / ts;
			const int th = ( gh + ts - 1 ) / ts;
			const float s = mSettings.TileSize * mSettings.CellSize;

			duDebugDrawGridXZ( &ddraw,
				vMins[ 0 ],
				vAimPt.Z(),
				vMins[ 2 ],
				tw, th,
				s,
				duRGBA( 0, 0, 0, 64 ),
				1.0f );
		}

		static SurfaceFlags ignoreSurfaces = SURFACE_IGNORE;

		RayResult aimHit;
		if ( GetAimedAtModel( aimHit, ignoreSurfaces ) && aimHit.mHitNode )
		{
			aimHit.mHitNode->RenderWorldBounds();
			aimHit.mHitNode->RenderAxis();

			static const float renderRadius = 1.0f;

			static bool renderMdl = true;
			if ( renderMdl )
				aimHit.mHitNode->RenderInRadius( aimHit.mHitPos, renderRadius, COLOR::LIGHT_GREY.fade( 100 ) );
			aimHit.mHitNode->RenderAxis();
			
			Vector3f v[3];
			ContentFlags activeContentFlags = CONT_NONE;
			SurfaceFlags activeSurfaceFlags = SURFACE_NONE;
			size_t materialIndex = 0;
			aimHit.mHitNode->mModel->GetTriangle( aimHit.mHitTriangle, aimHit.mHitNode->mTransform, v, activeContentFlags, activeSurfaceFlags, materialIndex );

			RenderBuffer::AddTri( v[0], v[1], v[2], ( aimHit.mHitNode->mEnabled ? COLOR::GREEN.fade( 100 ) : COLOR::RED.fade( 64 ) ) );
			
			std::string nodeInfo, modelState, contentStr = "cnt: ", surfaceStr = "srf: ";
			if ( aimHit.mHitNode->mSubModel >= 0 )
				nodeInfo = va( "submdl %d", aimHit.mHitNode->mSubModel );
			else if ( aimHit.mHitNode->mStaticModel >= 0 )
				nodeInfo = va( "staticmdl %d", aimHit.mHitNode->mStaticModel );
			ModelStateEnum::NameForValue( aimHit.mHitNode->mActiveState, modelState );
			ContentFlagsEnum::NameForValueBitfield( activeContentFlags, contentStr );
			SurfaceFlagsEnum::NameForValueBitfield( activeSurfaceFlags, surfaceStr );
			
			const Material & mtl = aimHit.mHitNode->mModel->GetMaterial( materialIndex );

			const Vector3f normalOffset = aimHit.mHitPos + aimHit.mHitNormal * 24.0f;
			RenderBuffer::AddLine( aimHit.mHitPos, aimHit.mHitPos + vAimNormal * 16.0f, COLOR::BLUE );
			RenderBuffer::AddString3d( normalOffset, COLOR::CYAN, va( "name:%s %s\n%s\n%s\n%s\n%s\n%s", 
				aimHit.mHitNode->mEntityName.c_str(),nodeInfo.c_str(), aimInfo.c_str(), mtl.mName.c_str(), modelState.c_str(), contentStr.c_str(), surfaceStr.c_str() ) );

			aimHit.mHitNode->RenderWorldBounds();
		}

		Vector3f eyePos;
		if ( Utils::GetLocalEyePosition( eyePos ) )
		{
			static float renderRadius = 1024.0f;
			if ( mNavMesh != NULL )
				duDebugDrawNavMeshInRadius( &ddraw, *mNavMesh, DU_DRAWNAVMESH_OFFMESHCONS, localToRc( eyePos ), renderRadius );
		}
	}

	if ( mFlags.mViewModels > 0 )
		mCollision.mRootNode->RenderWorldBounds();

	if ( mFlags.mViewModels > 1 )
		mCollision.mRootNode->RenderAxis();

	if ( mFlags.mViewConnections )
	{
		for ( size_t i = 0; i < mOffMeshConnections.size(); ++i )
			mOffMeshConnections[ i ].Render();
	}

	{
		rmt_ScopedCPUSample( AddTileFromQueue );

		// only modifications to the nav mesh at the end of the frame so the rebuilding
		// can happen asynchronously, but the navmesh usage and updating is done in
		// the main thread
		boost::lock_guard<boost::recursive_mutex> lock( mGuardAddTile );
		
		for ( size_t i = 0; i < mAddTileQueue.size(); ++i )
		{
			const TileAddData & tile = mAddTileQueue[ i ];

			mNavMesh->removeTile( mNavMesh->getTileRefAt( tile.mX, tile.mY, 0 ), 0, 0 );

			if ( dtStatusFailed( mNavMesh->addTile( tile.mNavData, tile.mNavDataSize, DT_TILE_FREE_DATA, 0, 0 ) ) )
			{
				dtFree( tile.mNavData );
			}

			SendTileModel( tile.mX, tile.mY );
		}

		mAddTileQueue.resize( 0 );
	}
}

void PathPlannerRecast::SendWorldModel()
{
	rmt_ScopedCPUSample( SendWorldModel );
	try
	{
		if ( System::mInstance->mAnalytics != NULL )
		{
			modeldata::Scene ioScene;
			mCollision.BuildScene( ioScene );

			std::string cachedFileData;
			if ( ioScene.SerializeToString( &cachedFileData ) )
			{
				Analytics::MessageUnion msgUnion;
				msgUnion.set_timestamp( 0 );

				Analytics::SystemModelData* mdlData = msgUnion.mutable_systemmodeldata();
				mdlData->set_compressiontype( Analytics::Compression_None );

				// compress the tile data
				/*const size_t bufferSize = cachedFileData.size() + (size_t)( cachedFileData.size() * 0.1 );
				boost::shared_array<char> compressBuffer( new char[ bufferSize ] );
				const int sizeCompressed = fastlz_compress_level( 2, cachedFileData.c_str(), cachedFileData.size(), compressBuffer.get() );
				*/

				mdlData->set_modelname( "world" );
				mdlData->set_modelbytes( cachedFileData );
				if ( msgUnion.IsInitialized() )
					System::mInstance->mAnalytics->AddEvent( msgUnion );
			}
		}
	}
	catch ( const std::exception& ex )
	{
		EngineFuncs::ConsoleError( va( "%s: %s", __FUNCTION__, ex.what() ) );
	}
}

void PathPlannerRecast::SendTileModel( int tx, int ty )
{
	rmt_ScopedCPUSample( SendTileModel );

	try
	{
		// Send the navmesh tile to the analytics remote
		if ( System::mInstance->mAnalytics != NULL )
		{
			std::vector<Vector3f> vertices;
			std::vector<IceMaths::IndexedTriangle> faces;

			vertices.reserve( 1024 );
			faces.reserve( 1024 );

			static const int MaxTiles = 32;
			const dtMeshTile * tiles[ MaxTiles ];
			const int ntiles = mNavMesh->getTilesAt( tx, ty, tiles, MaxTiles );
			for ( int i = 0; i < ntiles; ++i )
			{
				const dtMeshTile * tile = tiles[ i ];
				for ( int i = 0; i < tile->header->polyCount; ++i )
				{
					const dtPoly* p = &tile->polys[ i ];
					if ( p->getType() == DT_POLYTYPE_OFFMESH_CONNECTION )	// Skip off-mesh links.
						continue;

					const dtPolyDetail* pd = &tile->detailMeshes[ i ];

					for ( int j = 0; j < pd->triCount; ++j )
					{
						IceMaths::IndexedTriangle tri;
						tri.mVRef[ 0 ] = vertices.size() + 0;
						tri.mVRef[ 1 ] = vertices.size() + 1;
						tri.mVRef[ 2 ] = vertices.size() + 2;
						faces.push_back( tri );

						const unsigned char* t = &tile->detailTris[ ( pd->triBase + j ) * 4 ];
						for ( int k = 0; k < 3; ++k )
						{
							if ( t[ k ] < p->vertCount )
								vertices.push_back( Vector3f( &tile->verts[ p->verts[ t[ k ] ] * 3 ] ) );
							else
								vertices.push_back( Vector3f( &tile->detailVerts[ ( pd->vertBase + t[ k ] - p->vertCount ) * 3 ] ) );
						}
					}
				}
			}

			if ( faces.empty() )
				return;

			modeldata::Scene ioScene;
			modeldata::Mesh * mesh = ioScene.add_meshes();
			mesh->set_name( va( "tile_%dx%d", tx, ty ) );

			modeldata::Node * node = ioScene.mutable_rootnode();
			node->set_meshname( mesh->name() );

			// todo:
			modeldata::Material * mtrl = mesh->add_materials();
			mtrl->set_name( "nav" );

			mesh->mutable_vertices()->insert( 0, (const char*)&vertices[ 0 ], sizeof( Vector3f ) * vertices.size() );
			mesh->mutable_faces()->insert( 0, (const char*)&faces[ 0 ], sizeof( IceMaths::IndexedTriangle ) * faces.size() );

			std::string cachedFileData;
			if ( ioScene.SerializeToString( &cachedFileData ) )
			{
				Analytics::MessageUnion msgUnion;
				msgUnion.set_timestamp( 0 );

				Analytics::SystemModelData* mdlData = msgUnion.mutable_systemmodeldata();
				mdlData->set_compressiontype( Analytics::Compression_None );

				// compress the tile data
				/*const size_t bufferSize = cachedFileData.size() + (size_t)( cachedFileData.size() * 0.1 );
				boost::shared_array<char> compressBuffer( new char[ bufferSize ] );
				const int sizeCompressed = fastlz_compress_level( 2, cachedFileData.c_str(), cachedFileData.size(), compressBuffer.get() );
				*/

				mdlData->set_modelname( va( "tile_%dx%d", tx, ty ) );
				mdlData->set_modelbytes( cachedFileData );
				if ( msgUnion.IsInitialized() )
					System::mInstance->mAnalytics->AddEvent( msgUnion );
			}
		}
	}
	catch (const std::exception& ex)
	{
		EngineFuncs::ConsoleError( va( "%s: %s", __FUNCTION__, ex.what() ) );
	}
}

void PathPlannerRecast::UpdateDeferredModels()
{
	rmt_ScopedCPUSample( UpdateDeferredModels );

	for ( size_t i = 0; i < mDeferredModel.size(); ++i )
	{
		EntityInfo entInfo;
		if ( IGame::GetEntityInfo( mDeferredModel[ i ], entInfo ) )
		{
			NodePtr entNode = CreateEntityModel( mDeferredModel[ i ], entInfo );
			if ( entNode )
			{
				mDeferredModel.erase( mDeferredModel.begin() + i );
				--i;
			}
		}
	}
}

void PathPlannerRecast::UpdateModelState( bool forcePositionUpdate )
{
	rmt_ScopedCPUSample( UpdateModelState );

	mCollision.mRootNode->UpdateModelState( this, forcePositionUpdate );
}

void PathPlannerRecast::Shutdown()
{
	gAsyncBuildRunning = false;
	//mThreadGroup.interrupt_all();
	mThreadGroup.join_all();

	Unload();
}

bool PathPlannerRecast::Load( const std::string &_mapname, bool _dl )
{
	rmt_ScopedCPUSample( RecastLoadFromFile );

	Unload();

	const std::string navName = _mapname + _GetNavFileExtension();
	const std::string navPathBinary = std::string( "nav/" ) + navName;
	const std::string navPathText = std::string( "nav/" ) + navName + "_txt";

	bool success = true;

	try
	{
		RecastIO::NavigationMesh ioNavmesh;

		std::string dataIn;

		File fileBin, fileTxt;
		if ( !fileBin.OpenForRead( navPathBinary.c_str(), File::Binary ) ||
			!fileBin.ReadWholeFile( dataIn ) ||
			!ioNavmesh.ParseFromString( dataIn ) )
		{
			dataIn.clear();

			// try to fall back to a text formatted file
			if ( !fileTxt.OpenForRead( navPathText.c_str(), File::Text ) ||
				 !fileTxt.ReadWholeFile( dataIn ) ||
				 !google::protobuf::TextFormat::ParseFromString( dataIn, &ioNavmesh ) )
			{
				throw std::exception( va( "PathPlannerRecast:: Load failed %s", navPathBinary.c_str() ) );
			}
		}

		LoadWorldModel();
		InitNavmesh();
		
		const int fileVersion = ioNavmesh.version();
		fileVersion; // todo: check this?

		mSettings.AgentHeightStand = ioNavmesh.navmeshparams().agentheightstand();
		mSettings.AgentHeightCrouch = ioNavmesh.navmeshparams().agentheightcrouch();
		mSettings.AgentRadius = ioNavmesh.navmeshparams().agentradius();
		mSettings.AgentMaxClimb = ioNavmesh.navmeshparams().agentclimb();
		mSettings.WalkableSlopeAngle = ioNavmesh.navmeshparams().walkslopeangle();
		mSettings.CellSize = ioNavmesh.navmeshparams().cellsize();
		mSettings.CellHeight = ioNavmesh.navmeshparams().cellheight();
		mSettings.EdgeMaxLen = ioNavmesh.navmeshparams().edgemaxlength();
		mSettings.MaxSimplificationError = ioNavmesh.navmeshparams().edgemaxerror();
		mSettings.TileSize = ioNavmesh.navmeshparams().tilesize();
		mSettings.DetailSampleDist = ioNavmesh.navmeshparams().detailsampledist();
		mSettings.DetailSampleMaxError = ioNavmesh.navmeshparams().detailsamplemaxerr();

		for ( int i = 0; i < ioNavmesh.exclusionzone_size(); ++i )
		{
			AxisAlignedBox3f bounds;
			bounds.Min = Convert( ioNavmesh.exclusionzone( i ).mins() );
			bounds.Max = Convert( ioNavmesh.exclusionzone( i ).maxs() );
			mExclusionZones.push_back( bounds );
		}

		for ( int i = 0; i < ioNavmesh.offmeshconnection_size(); ++i )
		{
			const RecastIO::OffMeshConnection & ioConn = ioNavmesh.offmeshconnection( i );

			OffMeshConnection conn;
			conn.mEntry = Convert( ioConn.entrypos() );
			conn.mExit = Convert( ioConn.exitpos() );

			for ( int v = 0; v < ioConn.vertices_size(); ++v )
				conn.mVertices.push_back( Convert( ioConn.vertices( v ) ) );
			
			conn.mRadius = ioConn.radius();
			conn.mAreaFlags = (NavAreaFlags)ioConn.areaflags();
			conn.mBiDirectional = ioConn.bidirectional();
			mOffMeshConnections.push_back( conn );
		}

		for ( int i = 0; i < ioNavmesh.nodestate_size(); ++i )
		{
			const RecastIO::NodeState & nodeState = ioNavmesh.nodestate( i );

			NodePtr node;
			if ( nodeState.has_submodelid() )
				mCollision.mRootNode->FindNodeWithSubModel( nodeState.submodelid(), node );
			else if ( nodeState.has_staticmodelid() )
				mCollision.mRootNode->FindNodeWithStaticModel( nodeState.staticmodelid(), node );
			else if ( nodeState.has_name() )
			{
				node.reset( new Node() );
				mCollision.mRootNode->mChildren.push_back( node );
			}
			else
				EngineFuncs::ConsoleError( "Orphaned Node(no unique identifer), skipping load" );

			if ( node != NULL )
				node->LoadState( nodeState );
		}

		for ( int i = 0; i < ioNavmesh.models_size(); ++i )
		{
			const RecastIO::Model & iomdl = ioNavmesh.models( i );

			CollisionWorld::ModelCache::iterator it = mCollision.mCachedModels.find( iomdl.name() );
			if ( it == mCollision.mCachedModels.end() )
			{
				// if it doesn't exist, allocate a new one
				ModelPtr mdl( new CollisionModel() );
				it = mCollision.mCachedModels.insert( std::make_pair( iomdl.name(), mdl ) ).first;
			}

			it->second->LoadState( iomdl );
		}

		for ( int i = 0; i < ioNavmesh.tiles_size(); ++i )
		{
			const RecastIO::Tile & tile = ioNavmesh.tiles( i );
			const std::string & tileCompressedData = tile.compresseddata();

			unsigned char * decompressedBuffer = new unsigned char[ tile.uncompressedsize() ];
			const int sizeD = fastlz_decompress( tileCompressedData.c_str(), tileCompressedData.size(), decompressedBuffer, tile.uncompressedsize() );
			if ( sizeD == tile.uncompressedsize() )
			{
				dtTileRef tref;
				if ( dtStatusSucceed( mNavMesh->addTile( decompressedBuffer, sizeD, DT_TILE_FREE_DATA, 0, &tref ) ) )
				{
					const dtMeshTile * tile = const_cast<const dtNavMesh *>( mNavMesh )->getTileByRef( tref );
					if ( tile != NULL )
					{
						SendTileModel( tile->header->x, tile->header->y );
					}
				}
			}
			else
			{
				EngineFuncs::ConsoleError( "Invalid Tile Data(compressed size mismatch)" );
			}
		}
	}
	catch ( const std::exception & ex )
	{
		EngineFuncs::ConsoleError( va( "PathPlannerRecast:: Load failed %s", ex.what() ) );
		success = false;

		LoadWorldModel();

		// no matter if we loaded data or not we need to load the world geometry
		// and initialize the navmesh
		InitNavmesh();
	}

	UpdateModelState( true );
	BuildNav( false );

	SendWorldModel();
	
	return success;
}

bool PathPlannerRecast::Save( const std::string &_mapname )
{
	rmt_ScopedCPUSample( RecastSaveToFile );

	if ( mNavMesh != NULL )
	{
		const std::string navName = _mapname + _GetNavFileExtension();
		const std::string navPathBinary = std::string( "nav/" ) + navName;
		const std::string navPathText = std::string( "nav/" ) + navName + "_txt";

		RecastIO::NavigationMesh ioNavmesh;
		ioNavmesh.set_version( 1 );

		ioNavmesh.mutable_navmeshparams()->set_agentheightstand( mSettings.AgentHeightStand );
		ioNavmesh.mutable_navmeshparams()->set_agentheightcrouch( mSettings.AgentHeightCrouch );
		ioNavmesh.mutable_navmeshparams()->set_agentradius( mSettings.AgentRadius );
		ioNavmesh.mutable_navmeshparams()->set_agentclimb( mSettings.AgentMaxClimb );
		ioNavmesh.mutable_navmeshparams()->set_walkslopeangle( mSettings.WalkableSlopeAngle );
		ioNavmesh.mutable_navmeshparams()->set_cellsize( mSettings.CellSize );
		ioNavmesh.mutable_navmeshparams()->set_cellheight( mSettings.CellHeight );
		ioNavmesh.mutable_navmeshparams()->set_edgemaxlength( mSettings.EdgeMaxLen );
		ioNavmesh.mutable_navmeshparams()->set_edgemaxerror( mSettings.MaxSimplificationError );
		ioNavmesh.mutable_navmeshparams()->set_tilesize( mSettings.TileSize );
		ioNavmesh.mutable_navmeshparams()->set_detailsampledist( mSettings.DetailSampleDist );
		ioNavmesh.mutable_navmeshparams()->set_detailsamplemaxerr( mSettings.DetailSampleMaxError );
		ioNavmesh.mutable_navmeshparams()->set_tilesize( mSettings.TileSize );
				
		for ( size_t i = 0; i < mExclusionZones.size(); ++i )
		{
			RecastIO::AxisAlignedBounds * bnds = ioNavmesh.add_exclusionzone();
			bnds->mutable_mins()->set_x( mExclusionZones[ i ].Min[ 0 ] );
			bnds->mutable_mins()->set_y( mExclusionZones[ i ].Min[ 1 ] );
			bnds->mutable_mins()->set_z( mExclusionZones[ i ].Min[ 2 ] );
			bnds->mutable_maxs()->set_x( mExclusionZones[ i ].Max[ 0 ] );
			bnds->mutable_maxs()->set_y( mExclusionZones[ i ].Max[ 1 ] );
			bnds->mutable_maxs()->set_z( mExclusionZones[ i ].Max[ 2 ] );
		}

		for ( size_t i = 0; i < mOffMeshConnections.size(); ++i )
		{
			RecastIO::OffMeshConnection * conn = ioNavmesh.add_offmeshconnection();
			conn->mutable_entrypos()->set_x( mOffMeshConnections[ i ].mEntry[ 0 ] );
			conn->mutable_entrypos()->set_y( mOffMeshConnections[ i ].mEntry[ 1 ] );
			conn->mutable_entrypos()->set_z( mOffMeshConnections[ i ].mEntry[ 2 ] );
			conn->mutable_exitpos()->set_x( mOffMeshConnections[ i ].mExit[ 0 ] );
			conn->mutable_exitpos()->set_y( mOffMeshConnections[ i ].mExit[ 1 ] );
			conn->mutable_exitpos()->set_z( mOffMeshConnections[ i ].mExit[ 2 ] );
			conn->set_radius( mOffMeshConnections[ i ].mRadius );
			conn->set_areaflags( mOffMeshConnections[ i ].mAreaFlags );

			for ( size_t p = 0; p < mOffMeshConnections[ i ].mVertices.size(); ++p )
			{
				RecastIO::Vec3 * ioPos = conn->add_vertices();
				ioPos->set_x( mOffMeshConnections[ i ].mVertices[ p ].X() );
				ioPos->set_y( mOffMeshConnections[ i ].mVertices[ p ].Y() );
				ioPos->set_z( mOffMeshConnections[ i ].mVertices[ p ].Z() );
			}
			if ( mOffMeshConnections[ i ].mBiDirectional != conn->bidirectional() )
				conn->set_bidirectional( true );
		}
		
		for ( CollisionWorld::ModelCache::const_iterator it = mCollision.mCachedModels.begin();
			it != mCollision.mCachedModels.end();
			++it )
		{
			it->second->SaveState( ioNavmesh );
		}

		for ( int i = 0; i < mNavMesh->getMaxTiles(); ++i )
		{
			const dtMeshTile* tile = ( (const dtNavMesh *)mNavMesh )->getTile( i );
			if ( tile && tile->header && tile->dataSize )
			{
				// compress the tile data
				const size_t bufferSize = tile->dataSize + (size_t)( tile->dataSize * 0.1 );
				boost::shared_array<char> compressBuffer( new char[ bufferSize ] );
				const int sizeCompressed = fastlz_compress_level( 2, tile->data, tile->dataSize, compressBuffer.get() );

				RecastIO::Tile * ioTile = ioNavmesh.add_tiles();
				ioTile->set_uncompressedsize( tile->dataSize );
				ioTile->set_compresseddata( compressBuffer.get(), sizeCompressed );
			}
		}

		typedef std::queue<NodePtr> NodeQueue;
		NodeQueue treeNodes;
		treeNodes.push( mCollision.mRootNode );

		while ( !treeNodes.empty() )
		{
			NodePtr n = treeNodes.front();
			treeNodes.pop();

			if ( n->mSaveable )
			{
				n->SaveState( ioNavmesh );

				// explore the children as well
				for ( size_t i = 0; i < n->mChildren.size(); ++i )
					treeNodes.push( n->mChildren[ i ] );
			}
		}

		//for ( size_t m = 0; m < mModels.size(); ++m )
		//{
		//	if ( mModels[ m ].mSubModel != -1 )
		//	{
		//		RecastIO::SubModel * submdl = ioNavmesh.add_submodelinfo();
		//		submdl->set_submodelid( mModels[ m ].mSubModel );
		//		submdl->set_disabled( mModels[ m ].mDisabled );
		//		submdl->set_mover( mModels[ m ].mMover );
		//		submdl->set_nonsolid( mModels[ m ].mNonSolid );
		//		//ioNavmesh.add_disabledsubmodels( mModels[ m ].mSubModel );
		//	}
		//}

		try
		{
			/*File outBinary;
			if ( outBinary.OpenForWrite( navPathBinary.c_str(), File::Binary, false ) )
			{
				outBinary.Write( fbbl.GetBufferPointer(), fbbl.GetSize() );
				outBinary.Close();
			}*/

			std::string dataOut;

			// binary file
			/*if ( ioNavmesh.SerializeToString( &dataOut ) )
			{
				File outBinary;
				if ( outBinary.OpenForWrite( navPathBinary.c_str(), File::Binary, false ) )
				{
					outBinary.Write( dataOut.c_str(), dataOut.length() );
					outBinary.Close();
				}
			}*/

			if ( google::protobuf::TextFormat::PrintToString( ioNavmesh, &dataOut ) )
			{
				File outText;
				if ( outText.OpenForWrite( navPathText.c_str(), File::Text, false ) )
				{
					outText.Write( dataOut.c_str(), dataOut.length() );
					outText.Close();
				}
			}
		}
		catch ( const std::exception & ex )
		{
			EngineFuncs::ConsoleError( va( "PathPlannerRecast:: Save failed %s", ex.what() ) );
			return false;
		}
		return true;
	}
	return false;
}

bool PathPlannerRecast::IsReady() const
{
	return mNavMesh != NULL;
}

void PathPlannerRecast::Unload()
{
	OB_DELETE( mCurrentTool );

	dtFreeNavMesh( mNavMesh );
	mNavMesh = NULL;

	mExclusionZones.resize( 0 );

	mOffMeshConnections.resize( 0 );

	mCollision.Clear();
}

//////////////////////////////////////////////////////////////////////////

void PathPlannerRecast::InitNavmesh()
{
	Vector3f
		vMins = localToRc( mNavigationBounds.Min ),
		vMaxs = localToRc( mNavigationBounds.Max );

	dtFreeNavMesh( mNavMesh );
	mNavMesh = dtAllocNavMesh();

	dtNavMeshParams parms;
	rcVcopy( parms.orig, vMins );
	parms.tileWidth = mSettings.TileSize * mSettings.CellSize;
	parms.tileHeight = mSettings.TileSize * mSettings.CellSize;
	parms.maxTiles = (int)( parms.tileWidth * parms.tileHeight * 2.0 );
	parms.maxPolys = 16384;
	
	mNavMesh->init( &parms );

	dtFreeNavMeshQuery( mNavMeshQuery );
	mNavMeshQuery = dtAllocNavMeshQuery();
	mNavMeshQuery->init( mNavMesh, MaxQueryNodes );
	
	for ( size_t i = 0; i < RecastPathInterface::sInterfaces.size(); ++i )
		RecastPathInterface::sInterfaces[ i ]->ReInit();
}

//////////////////////////////////////////////////////////////////////////

void PathPlannerRecast::BuildNav( bool saveToFile )
{
	rcConfig cfg;
	BuildConfig( cfg );

	int gw = 0, gh = 0;
	rcCalcGridSize( cfg.bmin, cfg.bmax, mSettings.CellSize, &gw, &gh );
	const int ts = mSettings.TileSize;
	const int tw = ( gw + ts - 1 ) / ts;
	const int th = ( gh + ts - 1 ) / ts;

	// prevent the worker threads from changing anything
	boost::lock_guard<boost::recursive_mutex> lock( mGuardBuildQueue );

	// clear any pending tile rebuilds
	mTileBuildQueue.resize( 0 );

	mDeferredSaveNav = saveToFile;

	for ( int y = 0; y < th; ++y )
	{
		for ( int x = 0; x < tw; ++x )
		{
			MarkTileForBuilding( x, y );
		}
	}
}

//////////////////////////////////////////////////////////////////////////

void PathPlannerRecast::BuildConfig( rcConfig & cfg )
{
	NavParms gameParms;
	System::mInstance->mGame->GetNavParms( gameParms );
		
	mSettings.AgentHeightStand = gameParms.AgentHeightStand;
	mSettings.AgentHeightCrouch = gameParms.AgentHeightCrouch;
	mSettings.AgentRadius = gameParms.AgentRadius;
	mSettings.AgentMaxClimb = gameParms.AgentMaxClimb;
	mSettings.WalkableSlopeAngle = gameParms.WalkableSlopeAngle;
	
	memset( &cfg, 0, sizeof( cfg ) );
	cfg.cs = mSettings.CellSize;
	cfg.ch = mSettings.CellHeight;
	cfg.walkableSlopeAngle = mSettings.WalkableSlopeAngle;
	cfg.walkableHeightStand = (int)ceilf( mSettings.AgentHeightStand / cfg.ch );
	cfg.walkableHeightCrouch = (int)ceilf( mSettings.AgentHeightCrouch / cfg.ch );
	cfg.walkableClimb = (int)floorf( mSettings.AgentMaxClimb / cfg.ch );
	cfg.walkableRadius = (int)ceilf( mSettings.AgentRadius / cfg.cs );
	cfg.maxEdgeLen = (int)( mSettings.EdgeMaxLen / mSettings.CellSize );
	cfg.maxSimplificationError = mSettings.MaxSimplificationError;
	cfg.minRegionArea = rcSqr<int>( 6 ); // Note: area = size*size
	cfg.mergeRegionArea = rcSqr<int>( 20 );	// Note: area = size*size
	cfg.maxVertsPerPoly = DT_VERTS_PER_POLYGON;
	cfg.tileSize = mSettings.TileSize;
	cfg.borderSize = cfg.walkableRadius + 3; // Reserve enough padding.
	cfg.width = cfg.tileSize + cfg.borderSize * 2;
	cfg.height = cfg.tileSize + cfg.borderSize * 2;
	cfg.detailSampleDist = mSettings.DetailSampleDist < 0.9f ? 0 : mSettings.CellSize * mSettings.DetailSampleDist;
	cfg.detailSampleMaxError = cfg.ch * mSettings.DetailSampleMaxError;

	Vector3f
		vMins = localToRc( mNavigationBounds.Min ),
		vMaxs = localToRc( mNavigationBounds.Max );

	rcVcopy( cfg.bmin, vMins );
	rcVcopy( cfg.bmax, vMaxs );
}

//////////////////////////////////////////////////////////////////////////

void PathPlannerRecast::BuildNavTile()
{
	if ( mNavMesh == NULL )
		InitNavmesh();

	Vector3f aimPos;
	if ( Utils::GetLocalAimPoint( aimPos ) )
	{
		aimPos = localToRc( aimPos );

		int tileX = 0, tileY = 0;
		mNavMesh->calcTileLoc( aimPos, &tileX, &tileY );

		MarkTileForBuilding( tileX, tileY );
	}
}

//////////////////////////////////////////////////////////////////////////

void PathPlannerRecast::RasterizeTileLayers( int tx, int ty )
{
	rmt_ScopedCPUSample( RasterizeTileLayer );

	RasterizationContext rc;

	rcConfig cfg;
	BuildConfig( cfg );

	// Tile bounds.
	const float tcs = cfg.tileSize * cfg.cs;

	rcConfig tcfg;
	memcpy( &tcfg, &cfg, sizeof( tcfg ) );

	tcfg.bmin[ 0 ] = cfg.bmin[ 0 ] + tx*tcs;
	tcfg.bmin[ 1 ] = cfg.bmin[ 1 ];
	tcfg.bmin[ 2 ] = cfg.bmin[ 2 ] + ty*tcs;
	tcfg.bmax[ 0 ] = cfg.bmin[ 0 ] + ( tx + 1 )*tcs;
	tcfg.bmax[ 1 ] = cfg.bmax[ 1 ];
	tcfg.bmax[ 2 ] = cfg.bmin[ 2 ] + ( ty + 1 )*tcs;

	// border size
	tcfg.bmin[ 0 ] -= tcfg.borderSize * tcfg.cs;
	tcfg.bmin[ 2 ] -= tcfg.borderSize * tcfg.cs;
	tcfg.bmax[ 0 ] += tcfg.borderSize * tcfg.cs;
	tcfg.bmax[ 2 ] += tcfg.borderSize * tcfg.cs;

	const AxisAlignedBox3f tileAABB( rcToLocal( tcfg.bmin ), rcToLocal( tcfg.bmax ) );
	
	// Allocate voxel heightfield where we rasterize our input data to.
	rc.solid = rcAllocHeightfield();
	if ( !rc.solid )
	{
		mContext.log( RC_LOG_ERROR, "buildNavigation: Out of memory 'solid'." );
		return;
	}

	{
		rmt_ScopedCPUSample( rcCreateHeightfield );
		if ( !rcCreateHeightfield( &mContext, *rc.solid, tcfg.width, tcfg.height, tcfg.bmin, tcfg.bmax, tcfg.cs, tcfg.ch ) )
		{
			mContext.log( RC_LOG_ERROR, "buildNavigation: Could not create solid heightfield." );
			return;
		}
	}

	std::vector<CollisionTriangle> tileGeometry;
	std::vector<AxisAlignedBox3f> tileExclusions;
	OffMeshConnections tileLinks;

	boost::crc_32_type tileCrc;
	tileCrc.process_byte( TILE_DATA_VERSION );

	tileCrc.process_bytes( &tcfg, sizeof( tcfg ) );

	{
		rmt_ScopedCPUSample( GatherTileData );
		tileGeometry.reserve( 512 );

		GatherParms filter;
		filter.mIgnoreSurfaces = SURFACE_NONSOLID | SURFACE_IGNORE | SURFACE_SKY;

		mCollision.mRootNode->GatherTriangles( filter, Box3f( tileAABB.Min, tileAABB.Max ), tileGeometry );

		if ( tileGeometry.empty() )
			return;

		tileCrc.process_bytes( &tileGeometry[ 0 ], tileGeometry.size() * sizeof( tileGeometry[0] ) );

		for ( size_t i = 0; i < mOffMeshConnections.size(); ++i )
		{
			if ( tileAABB.Contains( mOffMeshConnections[ i ].mEntry ) || tileAABB.Contains( mOffMeshConnections[ i ].mEntry ) )
				tileLinks.push_back( mOffMeshConnections[ i ] );
		}

		for ( size_t i = 0; i < mExclusionZones.size(); ++i )
		{
			if ( tileAABB.TestIntersection( mExclusionZones[ i ] ) )
				tileExclusions.push_back( mExclusionZones[ i ] );
		}

		if ( !tileExclusions.empty() )
			tileCrc.process_bytes( &tileExclusions[ 0 ], tileExclusions.size() * sizeof( tileExclusions[ 0 ] ) );
	}

	const int newTileCrc = tileCrc.checksum();

	int oldCrc = 0;

	{
		// get the old crc from the current tile in the mesh
		boost::lock_guard<boost::recursive_mutex> lock( mGuardAddTile );
		const dtMeshTile * existingTile = mNavMesh->getTileAt( tx, ty, 0 );
		oldCrc = existingTile ? existingTile->header->userId : 0;
	}
	
	// input matches, we can bail
	if ( oldCrc == newTileCrc )
		return;

	size_t tileTriCount = 0;

	{
		rmt_ScopedCPUSample( RasterizeSolids );

		for ( size_t i = 0; i < tileGeometry.size(); ++i )
		{
			const CollisionTriangle& tri = tileGeometry[ i ];
			
			// water is region-ified in the 2nd pass
			if ( tri.mContents & CONT_WATER )
				continue;
			if ( !(tri.mNavFlags & NAVFLAGS_WALK) )
				continue;

			Vector3f verts[ 3 ];
			verts[ 0 ] = localToRc( tri.mTri.V[ 0 ] );
			verts[ 1 ] = localToRc( tri.mTri.V[ 1 ] );
			verts[ 2 ] = localToRc( tri.mTri.V[ 2 ] );

			navAreaMask areaFlags = NAVFLAGS_NONE;
			const int tris[ 3 ] = { 0, 1, 2 };
			rcMarkWalkableTriangles( &mContext,
				tcfg.walkableSlopeAngle,
				(float*)verts, 3,
				tris, 1,
				&areaFlags );

			if ( areaFlags == NAVFLAGS_WALK )
				areaFlags = tri.mNavFlags;

			rcRasterizeTriangle( &mContext, verts[ 0 ], verts[ 1 ], verts[ 2 ], areaFlags, *rc.solid );
			++tileTriCount;
		}
	}

	// must have at least standing height
	rcHeightThreshold heights[ 2 ];
	heights[ 0 ].height = tcfg.walkableHeightStand;
	heights[ 0 ].flag = NAVFLAGS_WALK;

	int numHeights = 1;

	if ( tcfg.walkableHeightCrouch > 0 )
	{
		heights[ numHeights ].height = tcfg.walkableHeightCrouch;
		heights[ numHeights ].flag = NAVFLAGS_CROUCH;
		++numHeights;
	};

	{
		rmt_ScopedCPUSample( Filters );

		// Once all geometry is rasterized, we do initial pass of filtering to
		// remove unwanted overhangs caused by the conservative rasterization
		// as well as filter spans where the character cannot possibly stand.
		rcFilterLowHangingWalkableObstacles( &mContext, tcfg.walkableClimb, *rc.solid );
		//rcFilterLedgeSpans( &mContext, tcfg.walkableHeight, tcfg.walkableClimb, *rc.solid );
		//rcFilterWalkableLowHeightSpans( &mContext, tcfg.walkableHeight, *rc.solid );
	
		rcFilterHeightThresholds( &mContext, heights, numHeights, *rc.solid );
	}

	rc.chf = rcAllocCompactHeightfield();
	if ( !rc.chf )
	{
		mContext.log( RC_LOG_ERROR, "buildNavigation: Out of memory 'chf'." );
		return;
	}

	{
		rmt_ScopedCPUSample( rcBuildCompactHeightfield );

		if ( !rcBuildCompactHeightfield( &mContext, heights[ numHeights - 1 ].height, cfg.walkableClimb, *rc.solid, *rc.chf ) )
		{
			mContext.log( RC_LOG_ERROR, "buildNavigation: Could not build compact data." );
			return;
		}
	}

	// Erode the walkable area by agent radius.
	{
		rmt_ScopedCPUSample( rcErodeWalkableArea );
		if ( !rcErodeWalkableArea( &mContext, tcfg.walkableRadius, *rc.chf ) )
		{
			mContext.log( RC_LOG_ERROR, "buildNavigation: Could not erode." );
			return;
		}
	}

	// use any non solid geometry for marking additional regions
	{
		rmt_ScopedCPUSample( MarkSpecialRegions );

		for ( size_t i = 0; i < tileGeometry.size(); ++i )
		{
			const CollisionTriangle& tri = tileGeometry[ i ];
			
			// only proceed with special areas
			if ( tri.mNavFlags & NAVFLAGS_WALK )
				continue;

			Vector3f verts[ 3 ];
			verts[ 0 ] = localToRc( tri.mTri.V[ 0 ] );
			verts[ 1 ] = localToRc( tri.mTri.V[ 1 ] );
			verts[ 2 ] = localToRc( tri.mTri.V[ 2 ] );

			float hmin = FLT_MAX;
			float hmax = -FLT_MAX;

			for ( int v = 0; v < 3; ++v )
			{
				hmin = rcMin( hmin, verts[ v ].Y() );
				hmax = rcMax( hmax, verts[ v ].Y() );
			}

			static float maxAdj = 4.0f;

			hmin -= mSettings.AgentHeightStand;
			hmax += maxAdj;

			rcMarkConvexPolyArea( &mContext, (float*)verts, 3, hmin, hmax, tri.mNavFlags, *rc.chf );
		}
	}
	
	{
		rmt_ScopedCPUSample( MarkExclusionZones );

		for ( size_t x = 0; x < tileExclusions.size(); ++x )
		{
			const AxisAlignedBox3f & aabb = tileExclusions[ x ];

			const Vector3f verts[ 4 ] =
			{
				localToRc( Vector3f( aabb.Min[ 0 ], aabb.Min[ 1 ], aabb.Min[ 2 ] ) ),
				localToRc( Vector3f( aabb.Max[ 0 ], aabb.Min[ 1 ], aabb.Min[ 2 ] ) ),
				localToRc( Vector3f( aabb.Max[ 0 ], aabb.Max[ 1 ], aabb.Min[ 2 ] ) ),
				localToRc( Vector3f( aabb.Min[ 0 ], aabb.Max[ 1 ], aabb.Min[ 2 ] ) ),
			};

			rcMarkConvexPolyArea( &mContext, (float*)&verts[ 0 ], 4, aabb.Min.Z(), aabb.Max.Z(), RC_NULL_AREA, *rc.chf );
		}
	}

	enum PartitionType
	{
		PARTITION_WATERSHED,
		PARTITION_MONOTONE,
		PARTITION_LAYERS,
	};

	static PartitionType usePartitionType = PARTITION_MONOTONE;

	if ( usePartitionType == PARTITION_WATERSHED )
	{
		// Prepare for region partitioning, by calculating distance field along the walkable surface.
		{
			rmt_ScopedCPUSample( rcBuildDistanceField );

			if ( !rcBuildDistanceField( &mContext, *rc.chf ) )
			{
				mContext.log( RC_LOG_ERROR, "buildNavigation: Could not build distance field." );
				return;
			}
		}

		// Partition the walkable surface into simple regions without holes.
		{
			rmt_ScopedCPUSample( rcBuildRegions );
			if ( !rcBuildRegions( &mContext, *rc.chf, tcfg.borderSize, tcfg.minRegionArea, tcfg.mergeRegionArea ) )
			{
				mContext.log( RC_LOG_ERROR, "buildNavigation: Could not build watershed regions." );
				return;
			}
		}
	}
	else if ( usePartitionType == PARTITION_MONOTONE )
	{
		// Partition the walkable surface into simple regions without holes.
		// Monotone partitioning does not need distance field.
		rmt_ScopedCPUSample( rcBuildRegionsMonotone );
		if ( !rcBuildRegionsMonotone( &mContext, *rc.chf, tcfg.borderSize, tcfg.minRegionArea, tcfg.mergeRegionArea ) )
		{
			mContext.log( RC_LOG_ERROR, "buildNavigation: Could not build monotone regions." );
			return;
		}
	}
	else // PARTITION_LAYERS
	{
		// Partition the walkable surface into simple regions without holes.
		/*rmt_ScopedCPUSample( rcBuildLayerRegions );
		if ( !rcBuildLayerRegions( &mContext, *rc.chf, tcfg.borderSize, tcfg.minRegionArea ) )
		{
			mContext.log( RC_LOG_ERROR, "buildNavigation: Could not build layer regions." );
			return;
		}*/
	}
	
	// Create contours.
	rc.cset = rcAllocContourSet();
	if ( !rc.cset )
	{
		mContext.log( RC_LOG_ERROR, "buildNavigation: Out of memory 'cset'." );
		return;
	}

	{
		rmt_ScopedCPUSample( rcBuildContours );
		if ( !rcBuildContours( &mContext, *rc.chf, tcfg.maxSimplificationError, tcfg.maxEdgeLen, *rc.cset ) )
		{
			mContext.log( RC_LOG_ERROR, "buildNavigation: Could not create contours." );
			return;
		}
	}

	if ( rc.cset->nconts == 0 )
	{
		return;
	}

	// Build polygon navmesh from the contours.
	rc.polymesh = rcAllocPolyMesh();
	if ( !rc.polymesh )
	{
		mContext.log( RC_LOG_ERROR, "buildNavigation: Out of memory 'pmesh'." );
		return;
	}

	{
		rmt_ScopedCPUSample( rcBuildPolyMesh );

		if ( !rcBuildPolyMesh( &mContext, *rc.cset, tcfg.maxVertsPerPoly, *rc.polymesh ) )
		{
			mContext.log( RC_LOG_ERROR, "buildNavigation: Could not triangulate contours." );
			return;
		}
	}

	// Build detail mesh.
	rc.meshdetail = rcAllocPolyMeshDetail();
	if ( !rc.meshdetail )
	{
		mContext.log( RC_LOG_ERROR, "buildNavigation: Out of memory 'dmesh'." );
		return;
	}

	{
		rmt_ScopedCPUSample( rcBuildPolyMeshDetail );
		
		if ( !rcBuildPolyMeshDetail( &mContext, *rc.polymesh, *rc.chf,
			tcfg.detailSampleDist, tcfg.detailSampleMaxError,
			*rc.meshdetail ) )
		{
			mContext.log( RC_LOG_ERROR, "buildNavigation: Could build polymesh detail." );
			return;
		}

	}

	rcFreeCompactHeightfield( rc.chf );
	rc.chf = 0;
	rcFreeContourSet( rc.cset );
	rc.cset = 0;

	if ( tcfg.maxVertsPerPoly <= DT_VERTS_PER_POLYGON )
	{
		struct ConnPts
		{
			Vector3f	mStart;
			Vector3f	mEnd;
		};

		std::vector<ConnPts>			offMeshPoints( tileLinks.size() );
		std::vector<float>				offMeshRadius( tileLinks.size() );
		std::vector<unsigned char>		offMeshConDir( tileLinks.size() );
		std::vector<navAreaMask>		offMeshConFlags( tileLinks.size() );
		std::vector<unsigned int>		offMeshUserIds( tileLinks.size() );

		for ( size_t c = 0; c < tileLinks.size(); ++c )
		{
			const OffMeshConnection & conn = tileLinks[ c ];
			offMeshPoints[ c ].mStart = localToRc( conn.mEntry );
			offMeshPoints[ c ].mEnd = localToRc( conn.mExit );
			offMeshRadius[ c ] = conn.mRadius;
			offMeshConDir[ c ] = conn.mBiDirectional ? DT_OFFMESH_CON_BIDIR : 0;
			offMeshConFlags[ c ] = conn.mAreaFlags;
			offMeshUserIds[ c ] = c;
		}

		dtNavMeshCreateParams params;
		memset( &params, 0, sizeof( params ) );
		params.verts = rc.polymesh->verts;
		params.vertCount = rc.polymesh->nverts;
		params.polys = rc.polymesh->polys;
		params.areaMasks = rc.polymesh->areaMasks;
		params.polyCount = rc.polymesh->npolys;
		params.nvp = rc.polymesh->nvp;
		params.detailMeshes = rc.meshdetail->meshes;
		params.detailVerts = rc.meshdetail->verts;
		params.detailVertsCount = rc.meshdetail->nverts;
		params.detailTris = rc.meshdetail->tris;
		params.detailTriCount = rc.meshdetail->ntris;
		if ( tileLinks.size() > 0 )
		{
			params.offMeshConVerts = offMeshPoints[ 0 ].mStart;
			params.offMeshConRad = &offMeshRadius[ 0 ];
			params.offMeshConDir = &offMeshConDir[ 0 ];
			params.offMeshConAreaFlags = &offMeshConFlags[ 0 ];
			params.offMeshConUserID = &offMeshUserIds[ 0 ];
			params.offMeshConCount = tileLinks.size();
		}
		params.walkableHeight = mSettings.AgentHeightStand;
		params.walkableRadius = mSettings.AgentRadius;
		params.walkableClimb = mSettings.AgentMaxClimb;
		params.tileX = tx;
		params.tileY = ty;
		params.tileLayer = 0;
		rcVcopy( params.bmin, rc.polymesh->bmin );
		rcVcopy( params.bmax, rc.polymesh->bmax );
		params.cs = tcfg.cs;
		params.ch = tcfg.ch;
		params.buildBvTree = true;
		params.userId = tileCrc.checksum();

		TileAddData add;
		add.mX = tx;
		add.mY = ty;

		{
			rmt_ScopedCPUSample( dtCreateNavMeshData );

			if ( !dtCreateNavMeshData( &params, &add.mNavData, &add.mNavDataSize ) )
			{
				mContext.log( RC_LOG_ERROR, "Could not build Detour navmesh." );
				return;
			}
		}

		{
			rmt_ScopedCPUSample( AddToTileQueue );

			boost::lock_guard<boost::recursive_mutex> lock( mGuardAddTile );
			
			mAddTileQueue.push_back( add );
		}
	}
}

//////////////////////////////////////////////////////////////////////////

bool PathPlannerRecast::GetNavInfo( const Vector3f &pos, int32_t &_id, std::string &_name )
{
	return false;
}

void PathPlannerRecast::AddEntityConnection( const Event_EntityConnection &_conn )
{
}

void PathPlannerRecast::RemoveEntityConnection( GameEntity _ent )
{
}

PathInterface * PathPlannerRecast::AllocPathInterface( Client * client )
{
	return new RecastPathInterface( client, this );
}
const char *PathPlannerRecast::GetPlannerName() const
{
	return "Recast Path Planner";
}

int PathPlannerRecast::GetPlannerType() const
{
	return NAVID_RECAST;
}

void PathPlannerRecast::LoadWorldModel()
{
	rmt_ScopedCPUSample( LoadWorldModel );

	LOGFUNC;

	mNavigationBounds.Clear();

	GameModelInfo modelInfo;
	gEngineFuncs->GetWorldModel( modelInfo, gDefaultAllocator );

	if ( modelInfo.mDataBuffer == NULL )
		return;

	mCollision.Clear();
	mCollision.LoadModelIntoWorld( GameEntity(), modelInfo, EntityInfo() );
	mCollision.mRootNode->Init( this );
	mNavigationBounds = mCollision.CalcWorldAABB();
	
	gDefaultAllocator.FreeMemory( modelInfo.mDataBuffer );
}

//////////////////////////////////////////////////////////////////////////

void PathPlannerRecast::EntityCreated( const EntityInstance &ei )
{
	if ( ei.mEntInfo.mCategory.CheckFlag( ENT_CAT_OBSTACLE ) )
	{
		NodePtr entityNode;
		mCollision.mRootNode->FindNodeWithEntity( ei.mEntity, entityNode );
		if ( entityNode )
			return;
		
		NodePtr entNode = CreateEntityModel( ei.mEntity, ei.mEntInfo );
		if ( !entNode )
		{
			mDeferredModel.push_back( ei.mEntity );
		}
	}
}

NodePtr PathPlannerRecast::CreateEntityModel( const GameEntity& entity, const EntityInfo & entInfo )
{
	Timer loadTimer;
	loadTimer.Reset();

	GameModelInfo modelInfo;
	gEngineFuncs->GetEntityModel( entity, modelInfo, gDefaultAllocator );

	NodePtr entityNode = mCollision.LoadModelIntoWorld( entity, modelInfo, entInfo );

	gDefaultAllocator.FreeMemory( modelInfo.mDataBuffer );

	return entityNode;
}

void PathPlannerRecast::EntityDeleted( const EntityInstance &ei )
{
	mCollision.DeleteNodeForEntity( ei.mEntity );
}

bool PathPlannerRecast::GetAimedAtModel( RayResult& result, SurfaceFlags ignoreSurfaces )
{
	Vector3f rayStart, rayDir;
	if ( Utils::GetLocalEyePosition( rayStart ) && Utils::GetLocalFacing( rayDir ) )
	{
		const Segment3f segment( rayStart, rayStart + rayDir * 2048.f );
		
		if ( mCollision.mRootNode->CollideSegmentNearest( result, segment, ignoreSurfaces, mFlags.mViewMode == 1 ) )
		{
			return true;
		}
	}
	return false;
}
